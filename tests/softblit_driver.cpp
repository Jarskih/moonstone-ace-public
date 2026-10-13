// Host driver of tests/test_softblit.py (ROADMAP 11.7): runs ms::softblit::run over cases read from stdin and prints the result.
//
// Input, whitespace separated tokens:
//   case := 'C' origin memhex nops op*
//   op   := con0 con1 afwm alwm modA modB modC modD ptA ptB ptC ptD size channels      (hex, modulos as 16-bit hex)
// Output per case: 'CASE n', then per op 'S status ptA ptB ptC ptD', then 'M memhex' (the chip bytes after the ops).
// The chip view is the case's memory placed at 'origin' (so addresses are absolute 68k addresses and bounds are checked).
//
// A second input kind feeds the game's own planners instead of raw ops (blits derived from real game code):
//   plan := 'G' kind origin memhex <kind arguments>   -> the planner's ops are run through the blitter, then printed like a case
//     kind 0 'cel'  : depth frame x y clipH clipW clipOrigin stride planes   (cel: 3 frames, synthetic header, see the .py)
//     kind 1 'copy' : desc src dst modA modD words rows
//     kind 2 'tile' : x y tile planes                                       (arena background tile, engine/bgblit)
// The ops that were run are printed first ('O' lines with the op fields) so the Python model can replay them.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/bgblit.hpp"
#include "engine/blit.hpp"
#include "engine/softblit.hpp"

using namespace ms;

namespace {

uint8_t g_mem[1 << 17];
uint32_t g_memSize;
uint32_t g_origin;
softblit::ChipView g_view;

int nib(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

void printOp(const char *tag, const BlitOp &op) {
	printf("%s %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", tag, op.con0, op.con1, op.afwm, op.alwm, (uint16_t)op.modA, (uint16_t)op.modB,
	       (uint16_t)op.modC, (uint16_t)op.modD, op.ptA, op.ptB, op.ptC, op.ptD, op.size, op.channels);
}

void runOp(const BlitOp &op) {
	softblit::BlitEnd end;
	end.ptA = end.ptB = end.ptC = end.ptD = 0xDEADBEEF;
	const softblit::Status st = softblit::run(g_view, op, &end);
	printf("S %d %x %x %x %x\n", (int)st, end.ptA, end.ptB, end.ptC, end.ptD);
}

void printMem() {
	printf("M ");
	for(uint32_t i = 0; i < g_memSize; ++i) printf("%02x", g_mem[i]);
	printf("\n");
}

bool readMem() {
	static char hex[1 << 18];
	if(scanf("%x %262000s", &g_origin, hex) != 2) return false;
	g_memSize = (uint32_t)strlen(hex) / 2;
	for(uint32_t i = 0; i < g_memSize; ++i) g_mem[i] = (uint8_t)((nib(hex[2 * i]) << 4) | nib(hex[2 * i + 1]));
	g_view.bytes = g_mem;
	g_view.origin = g_origin;
	g_view.size = g_memSize;
	return true;
}

// ---- planners ---------------------------------------------------------------------------------------------------------------

struct Recorder {
	int n;
};
void recordBlit(void *, const BlitOp &op) {
	printOp("O", op);
	runOp(op);
}
void noCpu(void *, const CelJob &, int) {}
void noPads(void *, const CelJob &) {}

void bgMask(void *, const BgTilePlan &, uint32_t) {}

uint8_t g_cel[256];

bool planCelCase(int depth, int frame, int x, int y, int clipH, int clipW, unsigned clipOrigin, int stride, int planes) {
	// synthetic cel: header = count 3, data base 0, 4 unused; records of 10 bytes {data, width, height, hot, planeBits}
	memset(g_cel, 0, sizeof g_cel);
	g_cel[1] = 3;
	const uint16_t widths[3] = {24, 40, 17}, heights[3] = {10, 7, 31};
	const uint8_t bits[3] = {0x1F, 0x15, 0x3F};
	for(int i = 0; i < 3; ++i) {
		uint8_t *r = g_cel + 10 + 10 * i;
		r[4] = (uint8_t)(widths[i] >> 8); r[5] = (uint8_t)widths[i];
		r[6] = (uint8_t)(heights[i] >> 8); r[7] = (uint8_t)heights[i];
		r[8] = (uint8_t)(i * 0x30); r[9] = bits[i];
		r[0] = 0; r[1] = 0; r[2] = (uint8_t)(0x08 * i); r[3] = 0;   // planes of frame i at base + 0x800 * i
	}
	const uint32_t base = g_origin + 0x14400;
	g_cel[2] = (uint8_t)(base >> 24); g_cel[3] = (uint8_t)(base >> 16); g_cel[4] = (uint8_t)(base >> 8); g_cel[5] = (uint8_t)base;
	CelFrame f;
	if(!celFrame(g_cel, (int16_t)frame, f)) return false;
	CelView v;
	v.clipH = (int16_t)clipH; v.clipW = (int16_t)clipW; v.origin = (uint16_t)clipOrigin; v.stride = (uint16_t)stride;
	v.temp = g_origin + 0xC000;
	for(int i = 0; i < CEL_MAX_PLANES; ++i) v.dest[i] = g_origin + 0x2000 * (uint32_t)i;   // screen planes 8192 apart
	v.planes = (uint16_t)planes; v.cpuMask = false; v.depth = (uint8_t)depth;
	CelJob j;
	if(!planCel(f, (int16_t)x, (int16_t)y, v, j)) return false;
	const BlitSink sink = {recordBlit, noCpu, noPads, nullptr};
	runCel(j, sink);
	return true;
}

}  // namespace

int main() {
	char tag[4];
	int n = 0;
	while(scanf("%3s", tag) == 1) {
		printf("CASE %d\n", n++);
		if(tag[0] == 'C') {
			if(!readMem()) return 2;
			int nops;
			if(scanf("%d", &nops) != 1) return 3;
			for(int i = 0; i < nops; ++i) {
				BlitOp op{};
				unsigned v[14];
				for(int k = 0; k < 14; ++k) if(scanf("%x", &v[k]) != 1) return 4;
				op.con0 = (uint16_t)v[0]; op.con1 = (uint16_t)v[1]; op.afwm = (uint16_t)v[2]; op.alwm = (uint16_t)v[3];
				op.modA = (int16_t)v[4]; op.modB = (int16_t)v[5]; op.modC = (int16_t)v[6]; op.modD = (int16_t)v[7];
				op.ptA = v[8]; op.ptB = v[9]; op.ptC = v[10]; op.ptD = v[11]; op.size = (uint16_t)v[12]; op.channels = (uint8_t)v[13];
				runOp(op);
			}
			printMem();
		}
		else if(tag[0] == 'G') {
			int kind;
			if(scanf("%d", &kind) != 1 || !readMem()) return 5;
			if(kind == 0) {
				int depth, frame, x, y, clipH, clipW, stride, planes;
				unsigned clipOrigin;
				if(scanf("%d %d %d %d %d %d %u %d %d", &depth, &frame, &x, &y, &clipH, &clipW, &clipOrigin, &stride, &planes) != 9) return 6;
				planCelCase(depth, frame, x, y, clipH, clipW, clipOrigin, stride, planes);
			}
			else if(kind == 1) {
				int desc;
				unsigned src, dst, modA, modD, words, rows;
				if(scanf("%d %x %x %x %x %u %u", &desc, &src, &dst, &modA, &modD, &words, &rows) != 7) return 7;
				BlitOp ops[CEL_MAX_PLANES];
				const int cnt = desc ? planCopyPlanesDesc(ops, 3, src, dst, 0x1000, 0x1000, (uint16_t)modA, (uint16_t)modD, (uint16_t)words, (uint16_t)rows)
				                     : planCopyPlanes(ops, 3, src, dst, 0x1000, 0x1000, (uint16_t)modA, (uint16_t)modD, (uint16_t)words, (uint16_t)rows);
				for(int i = 0; i < cnt; ++i) recordBlit(nullptr, ops[i]);
			}
			else if(kind == 2) {
				int x, y, tile, planes;
				if(scanf("%d %d %d %d", &x, &y, &tile, &planes) != 4) return 8;
				BgClipState st = {0, 0};
				const BgSink sink = {bgMask, recordBlit, nullptr};
				// sheet = origin, dest = origin + 0xA000 (planes 8000 bytes apart as in the game), mask buffer = origin + 0x14000
				bgDrawTile(st, (int16_t)x, (int16_t)y, (uint8_t)tile, g_origin, g_origin + 0xA000, g_origin + 0x14000, (uint32_t)planes, sink);
			}
			printMem();
		}
		else {
			return 9;
		}
	}
	return 0;
}
