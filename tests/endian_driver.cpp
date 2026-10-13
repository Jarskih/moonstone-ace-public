// Host driver of tests/test_endian.py (ROADMAP 11.3): the big-endian accessors, the slot types, the Knight byte view, the arena
// obstacle code, the backdrop script reader and the owned-data fill, run on the host CPU (x86: little-endian).  Every command
// prints plain numbers; the Python test compares them with its own model written from the asm / the file formats.
//
//   endian_driver be                       host byte order + ld16/ld32/st16/st32/swap on fixed bytes
//   endian_driver slots                    ByteSlot / WordSlot: field order and the 68k value view
//   endian_driver rec                      recGet*/recSet* on a Knight: byte offsets with the big-endian meaning
//   endian_driver adopt HEX                arenaAdopt on the table HEX (big-endian bytes: count word, records, script)
//   endian_driver obstacles HEX X Y INPUT FACING BOX58 BOX60 BOX114 STEPX STEPY      arenaObstacles on a Knight
//   endian_driver default                  arenaDefaultTable: the bytes it writes
//   endian_driver bg HEX                   bgCompose on the script HEX (big-endian bytes): the tiles it draws
//   endian_driver fill FILE SIZE CRC OUT hunk src size unit ...    origFill with swap units
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/bgblit.hpp"
#include "engine/endian.hpp"
#include "engine/origfill.hpp"
#include "game/arena.hpp"
#include "game/record.hpp"
#include "game/world.hpp"

using namespace ms;
using namespace ms::game;

// creatures.cpp's LAB_03CA, copied here so the driver does not link the creature code (the arena probe only needs this one).
namespace ms { namespace game {
bool contactOverlap(int32_t d0, int32_t d1, int32_t d2, int32_t d3) {
	if(((uint32_t)d2 - (uint32_t)d0) >> 31) {
		return d3 >= d0;
	}
	if(((uint32_t)d2 - (uint32_t)d1) >> 31) {
		return true;
	}
	if(((uint32_t)d3 - (uint32_t)d0) >> 31) {
		return false;
	}
	return !(d3 >= d1);
}
}}  // namespace ms::game

static size_t unhex(const char *hex, uint8_t *out, size_t cap) {
	size_t n = strlen(hex) / 2;
	if(n > cap) {
		n = cap;
	}
	for(size_t i = 0; i < n; ++i) {
		unsigned v;
		sscanf(hex + 2 * i, "%2x", &v);
		out[i] = (uint8_t)v;
	}
	return n;
}

static int cmdBe() {
	const uint8_t b[4] = {0x12, 0x34, 0x56, 0x78};
	uint8_t o[4] = {};
	printf("host_big %d\n", (int)be::kHostIsBig);
	printf("ld16 %04x\n", be::ld16(b));
	printf("ld32 %08x\n", be::ld32(b));
	be::st16(o, 0xA1B2);
	printf("st16 %02x%02x\n", o[0], o[1]);
	be::st32(o, 0xA1B2C3D4u);
	printf("st32 %02x%02x%02x%02x\n", o[0], o[1], o[2], o[3]);
	printf("swap16 %04x\n", be::swap16(0x1234));
	printf("swap32 %08x\n", be::swap32(0x12345678u));
	return 0;
}

static int cmdSlots() {
	ByteSlot b = {};
	b.ub = 0xA5;
	printf("byte_raw %02x%02x\n", ((uint8_t *)&b)[0], ((uint8_t *)&b)[1]);
	printf("byte_be %04x\n", b.beWord());
	b.setBeWord(0x1234);
	printf("byte_set %02x %02x\n", b.ub, b.ubSpare);
	WordSlot w = {};
	w.setBeLong(0x12345678u);
	printf("word_set %04x %04x\n", w.uw, w.uwSpare);
	printf("word_be %08x\n", w.beLong());
	// a DS.L filled with the original bytes by the swap-unit fill (unit 2): the word at the label is the HIGH half
	const uint8_t orig[4] = {0x12, 0x34, 0x56, 0x78};
	uint8_t native[4];
	memcpy(native, orig, 4);
	if(!be::kHostIsBig) {
		for(int i = 0; i < 4; i += 2) {
			const uint8_t t = native[i];
			native[i] = native[i + 1];
			native[i + 1] = t;
		}
	}
	WordSlot f;
	memcpy(&f, native, 4);
	printf("word_fill %04x %04x\n", f.uw, f.uwSpare);
	return 0;
}

static int cmdRec() {
	Knight k;
	memset(&k, 0, sizeof(k));
	recSet16(k, 62, 0x1234);
	printf("input %04x b62 %02x b63 %02x\n", k.uwInput, recGet8(k, 62), recGet8(k, 63));
	recSet8(k, 63, 0x7F);
	printf("input2 %04x\n", k.uwInput);
	recSet32(k, 22, 0xAABBCCDDu);
	printf("idle %08x w22 %04x w24 %04x b25 %02x\n", k.ulIdleScript, recGet16(k, 22), recGet16(k, 24), recGet8(k, 25));
	k.ubCooldown = 0x0A;
	k.ubCooldownLo = 0x05;
	printf("cool w106 %04x\n", recGet16(k, 106));
	recSet16(k, 106, 0x1122);
	printf("cool bytes %02x %02x\n", k.ubCooldown, k.ubCooldownLo);
	uint16_t box = 0x4321;
	memcpy((uint8_t *)&k + 58, &box, 2);                 // an engine box word, native
	printf("box58 %04x b58 %02x b59 %02x\n", recGet16(k, 58), recGet8(k, 58), recGet8(k, 59));
	k.uwGold = 0xBEEF;
	printf("gold hi %02x lo %02x\n", recGet8(k, 74), recGet8(k, 75));
	k.ulActive = 0x01020304u;
	printf("active b0 %02x b3 %02x\n", recGet8(k, 0), recGet8(k, 3));
	printf("tiled %d cells %u\n", (int)detail::kKnightMap.isTiled, (unsigned)kKnightCellCount);
	return 0;
}

static int cmdAdopt(const char *hex) {
	static uint8_t blob[2 * 8 + 2 + 0x960 + 64];
	static uint8_t script[ARENA_SCRIPT_BYTES];
	const size_t n = unhex(hex, blob, sizeof(blob));
	memset(script, 0, sizeof(script));
	const uint16_t edge = arenaAdopt(blob, script);
	printf("n %u edge %d script0 %02x%02x%02x%02x\n", (unsigned)n, (int)(int16_t)edge, script[0], script[1], script[2], script[3]);
	return 0;
}

static int cmdObstacles(char **a) {
	static uint8_t blob[512];
	const size_t n = unhex(a[0], blob, sizeof(blob));
	(void)n;
	Knight k;
	memset(&k, 0, sizeof(k));
	k.uwInput = (uint16_t)strtoul(a[2], 0, 0);
	k.ubFacing = (uint8_t)strtoul(a[3], 0, 0);
	k.uwY = (uint16_t)strtoul(a[1], 0, 0);
	uint16_t v = (uint16_t)strtoul(a[4], 0, 0);
	memcpy((uint8_t *)&k + 58, &v, 2);
	v = (uint16_t)strtoul(a[5], 0, 0);
	memcpy((uint8_t *)&k + 60, &v, 2);
	v = (uint16_t)strtoul(a[6], 0, 0);
	memcpy((uint8_t *)&k + 114, &v, 2);
	arenaObstacles(blob, &k, (int16_t)strtol(a[7], 0, 0), (int16_t)strtol(a[8], 0, 0));
	printf("input %04x\n", k.uwInput);
	return 0;
}

static int cmdDefault() {
	uint8_t t[16];
	memset(t, 0xEE, sizeof(t));
	const uint16_t edge = arenaDefaultTable(t);
	printf("edge %u bytes", edge);
	for(int i = 0; i < 10; ++i) {
		printf(" %02x", t[i]);
	}
	printf("\n");
	return 0;
}

struct BgLog {
	int n;
};
static void bgMask(void *ctx, const BgTilePlan &plan, uint32_t) {
	BgLog *l = (BgLog *)ctx;
	++l->n;
	printf("tile sheet %08x off %04x cd %08x\n", (unsigned)plan.sheet, plan.sheetOffset, (unsigned)plan.blitCD);
}
static void bgBlit(void *, const BlitOp &) {}

static int cmdBg(const char *hex) {
	static uint8_t script[256];
	memset(script, 0xFF, sizeof(script));
	unhex(hex, script, sizeof(script));
	BgClipState st;
	st.skipSheet = 0;
	st.skipMask = 0;
	BgLog log = {0};
	const BgSink sink = {bgMask, bgBlit, &log};
	bgCompose(st, script, 0x10000, 0x20000, 0x30000, 0x40000, 5, sink);
	printf("tiles %d\n", log.n);
	return 0;
}

struct FillCtx {
	FILE *f;
};
static uint32_t fillRead(void *c, uint8_t *p, uint32_t n) {
	return (uint32_t)fread(p, 1, n, ((FillCtx *)c)->f);
}

static int cmdFill(char **a, int argc) {
	FillCtx c = {fopen(a[0], "rb")};
	const uint32_t size = strtoul(a[1], 0, 0), crc = strtoul(a[2], 0, 0);
	const int nr = (argc - 4) / 4;
	OrigFillRun *runs = (OrigFillRun *)calloc(nr + 1, sizeof(OrigFillRun));
	uint32_t total = 0;
	for(int i = 0; i < nr; ++i) {
		total += strtoul(a[4 + 4 * i + 2], 0, 0);
	}
	uint8_t *out = (uint8_t *)calloc(total + 1, 1), *p = out;
	for(int i = 0; i < nr; ++i) {
		runs[i].ubHunk = (uint8_t)strtoul(a[4 + 4 * i], 0, 0);
		runs[i].ulSrc = strtoul(a[4 + 4 * i + 1], 0, 0);
		runs[i].ulSize = strtoul(a[4 + 4 * i + 2], 0, 0);
		runs[i].ubUnit = (uint8_t)strtoul(a[4 + 4 * i + 3], 0, 0);
		runs[i].pDst = p;
		p += runs[i].ulSize;
	}
	OrigFillTable t = {runs, (uint32_t)nr};
	uint32_t got = 0;
	const OrigFillResult e = origFill(fillRead, &c, size, crc, &t, 1, &got);
	FILE *o = fopen(a[3], "wb");
	fwrite(out, 1, total, o);
	fclose(o);
	printf("%d %08lx\n", (int)e, (unsigned long)got);
	return 0;
}

int main(int argc, char **argv) {
	if(argc < 2) {
		return 2;
	}
	const char *cmd = argv[1];
	if(!strcmp(cmd, "be")) return cmdBe();
	if(!strcmp(cmd, "slots")) return cmdSlots();
	if(!strcmp(cmd, "rec")) return cmdRec();
	if(!strcmp(cmd, "adopt")) return cmdAdopt(argv[2]);
	if(!strcmp(cmd, "obstacles")) return cmdObstacles(argv + 2);
	if(!strcmp(cmd, "default")) return cmdDefault();
	if(!strcmp(cmd, "bg")) return cmdBg(argv[2]);
	if(!strcmp(cmd, "fill")) return cmdFill(argv + 2, argc - 2);
	return 2;
}
