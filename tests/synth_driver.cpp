// Host driver for src/engine/synth.cpp (tests/test_synth.py): runs a script (tests/synth_oracle.py script_text) against ms::Synth with a
// recording fake Paula and prints the chip-register write log per operation. Plain C++17, host only.
#include <stdint.h>
#include <stdio.h>

#include "engine/synth.hpp"

using namespace ms;

namespace {

struct Fake {
	uint16_t uwPending = 0;
	uint16_t uwEnar = 0;
};
Fake g_fake;
uint8_t g_blob[kSynthBlobSize];                 // mutable copy of the tables (ops P / Q)
SynthTables g_tab = {g_blob, kSynthInst};

void w16(void *, uint16_t reg, uint16_t v) {
	printf("w %x %x\n", reg, v);
	if(reg == 0x9C && !(v & 0x8000)) g_fake.uwPending &= (uint16_t)~v;
}
void w32(void *, uint16_t reg, uint32_t v) {
	printf("w %x %x\nw %x %x\n", reg, (v >> 16) & 0xFFFF, reg + 2, v & 0xFFFF);
}
uint16_t r16(void *, uint16_t reg) {
	if(reg == 0x1E) return g_fake.uwPending;
	if(reg == 0x1C) return g_fake.uwEnar;
	return 0;
}

}  // namespace

int main() {
	static Synth s;
	for(uint32_t i = 0; i < kSynthBlobSize; ++i) g_blob[i] = kSynthBlob[i];
	const SynthHw hw = {nullptr, w16, w32, r16};
	char op;
	while(scanf(" %c", &op) == 1) {
		puts(".");
		if(op == 'Z') {
			unsigned wave, b[5];
			if(scanf("%x %x %x %x %x %x", &wave, &b[0], &b[1], &b[2], &b[3], &b[4]) != 6) return 2;
			const uint32_t aBank[5] = {b[0], b[1], b[2], b[3], b[4]};
			synthInit(s, hw, wave, &g_tab);
			synthRelocate(s, aBank);
		} else if(op == 'S') {
			unsigned seq, ch;
			if(scanf("%u %u", &seq, &ch) != 2) return 2;
			synthStart(s, hw, (uint16_t)seq, (uint16_t)ch);
		} else if(op == 'T') {
			synthTick(s, hw);
		} else if(op == 'I') {
			unsigned req, enar;
			if(scanf("%x %x", &req, &enar) != 2) return 2;
			g_fake.uwPending = (uint16_t)req;
			g_fake.uwEnar = (uint16_t)enar;
			synthInt4(s, hw);
		} else if(op == 'F') {
			unsigned f;
			if(scanf("%x", &f) != 1) return 2;
			synthFade(s, (uint16_t)f);
		} else if(op == 'K') {
			unsigned v;
			if(scanf("%x", &v) != 1) return 2;
			s.uwLock = (uint16_t)v;
		} else if(op == 'P') {                      // P ref n b0 .. bn-1: write bytes at S_44 offset ref
			unsigned ref, n;
			if(scanf("%u %u", &ref, &n) != 2) return 2;
			for(unsigned i = 0; i < n; ++i) {
				unsigned b;
				if(scanf("%x", &b) != 1) return 2;
				g_blob[ref + i - kSynthBlobBase] = (uint8_t)b;
			}
		} else if(op == 'Q') {                      // Q idx ref: sequence table entry
			unsigned idx, ref;
			if(scanf("%u %u", &idx, &ref) != 2) return 2;
			uint8_t *p = g_blob + (kSynthSeqTable - kSynthBlobBase) + 4 * idx;
			p[0] = (uint8_t)(ref >> 24); p[1] = (uint8_t)(ref >> 16); p[2] = (uint8_t)(ref >> 8); p[3] = (uint8_t)ref;
		} else if(op == 'D') {
			printf("D");
			for(uint16_t i = 0; i < kSynthInstCount; ++i) printf(" %x", (unsigned)s.aulSample[i]);
			printf("\n");
		}
	}
	return 0;
}
