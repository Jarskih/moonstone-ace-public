// Host driver of tests/test_paula.py (ROADMAP 11.8): runs ms::paula (and the module player, and the real synth on top of it) over a
// scenario read from stdin and prints what happened.  Plain C++17, host only.
//
// Commands (numbers are hex unless noted; one scenario per process):
//   C rate sep lp origin size     configure: output rate (decimal), separation 0..100 (decimal), low-pass coefficient (decimal, Q16),
//                                 chip view origin and size (hex; the memory is zero until M)
//   B mode                        render mode of the next C: 0 = box (default), 1 = band-limited steps (blep)
//   M hex                         load the memory image (bytes from the origin)
//   H ch lc len off               append an action for the next interrupt of channel ch (the handler: acknowledge, write LC / LEN,
//                                 off = 1 also clears the channel's DMACON bit)
//   W frame reg val               16-bit register write before frame 'frame' is rendered
//   L frame reg val32             32-bit register write (AUDxLC)
//   G frame on                    from frame 'frame' on the interrupt master of the platform is on (1) or off (0); it starts on
//   R n                           render n frames one at a time with the scheduled writes; prints 'I frame ch' per handled interrupt,
//                                 'S' + 8 hex digits per frame (left, right as int16), then '.'
//                                 also 'K adkconr modulation' (ADKCONR and adkModulationRequested after the run)
//   V tick vol / X tick           module master volume set (V) / player stopped (X) before that tick of the next U
//   P modoff silenceoff          start the module player on the module at that memory offset
//   U ticks                       the platform loop for 'ticks' vblanks (rate / 50 frames each): modTick, 40 frames, modLoopPointers, the
//                                 rest; prints 'w frame reg val' per register write of the player and 'S' samples, then '.'
//   Y firstseq lastseq ticks      the real synth (ms::synth*) over the model: for each sequence start it on channel 0 and tick 'ticks'
//                                 times (one vblank of frames each, level-4 handler = synthInt4); prints 'Y seq irqs peak dma' per sequence
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/modplay.hpp"
#include "engine/paula.hpp"
#ifdef DRIVER_WITH_SYNTH
#include "engine/synth.hpp"
#endif

using namespace ms;
using namespace ms::paula;

namespace {

uint8_t g_mem[1 << 20];
uint32_t g_origin, g_size;
Paula g_paula;
Config g_cfg;
ModPlayer g_mod;
bool g_masterOn = true;
int g_modMaster[256];            // module master volume to set before tick t (-1 = leave); Z resets
int g_modStop = -1;              // tick before which the player is stopped (-1 = never)
uint32_t g_frame = 0;
[[maybe_unused]] uint32_t g_irqCalls = 0;   // read only by the DRIVER_WITH_SYNTH sequence report (clang 23: -Wunused-but-set-global)
uint32_t g_irqLog[1 << 16][2];   // frame, channel of every handled interrupt (printed after the samples of a run)
uint32_t g_irqLogCount = 0;

struct Action { uint32_t lc; uint16_t len; bool off; };
Action g_actions[4][64];
uint32_t g_actionCount[4], g_actionNext[4];

struct Event { uint32_t frame; int kind; uint16_t reg; uint32_t val; };   // kind 0 = write16, 1 = write32, 2 = master
Event g_events[8192];
uint32_t g_eventCount;

char g_hex[(1 << 21) + 16];

int nib(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

softblit::ChipView view() {
	softblit::ChipView v;
	v.bytes = g_mem;
	v.origin = g_origin;
	v.size = g_size;
	return v;
}

bool masterOn(void *) { return g_masterOn; }

// The test handler: for every channel whose request is set and enabled (lowest first): acknowledge it and run its next action.
void handler(void *) {
	for(;;) {
		const uint16_t req = read16(g_paula, kRegIntreqr);
		const uint16_t ena = read16(g_paula, kRegIntenar);
		int ch = -1;
		for(int c = 0; c < 4; ++c) {
			if((req & ena & intAudBit(static_cast<uint8_t>(c))) != 0) {
				ch = c;
				break;
			}
		}
		if(ch < 0) {
			return;
		}
		write16(g_paula, kRegIntreq, intAudBit(static_cast<uint8_t>(ch)));
		++g_irqCalls;
		if(g_irqLogCount < (1u << 16)) {
			g_irqLog[g_irqLogCount][0] = g_frame;
			g_irqLog[g_irqLogCount][1] = static_cast<uint32_t>(ch);
			++g_irqLogCount;
		}
		if(g_actionNext[ch] < g_actionCount[ch]) {
			const Action &a = g_actions[ch][g_actionNext[ch]++];
			write32(g_paula, static_cast<uint16_t>(kRegAud + 0x10 * ch), a.lc);
			write16(g_paula, static_cast<uint16_t>(kRegAud + 0x10 * ch + 4), a.len);
			if(a.off) {
				write16(g_paula, kRegDmacon, static_cast<uint16_t>(1u << ch));
			}
		}
	}
}

Hooks hooks() {
	Hooks h;
	h.pCtx = nullptr;
	h.pfnMasterOn = masterOn;
	h.pfnInterrupt = handler;
	return h;
}

// ---- module player over the model -----------------------------------------------------------------------------------------------
void modW16(void *, uint16_t reg, uint16_t v) {
	printf("w %u %x %x\n", g_frame, reg, v);
	write16(g_paula, reg, v);
}
void modW32(void *, uint16_t reg, uint32_t v) {
	printf("w %u %x %x\n", g_frame, reg, static_cast<unsigned>(v >> 16));
	printf("w %u %x %x\n", g_frame, reg + 2, static_cast<unsigned>(v & 0xFFFF));
	write32(g_paula, reg, v);
}

void printSamples(const int16_t *p, uint32_t frames) {
	printf("S");
	for(uint32_t i = 0; i < frames; ++i) {
		printf("%04x%04x", static_cast<unsigned>(static_cast<uint16_t>(p[2 * i])), static_cast<unsigned>(static_cast<uint16_t>(p[2 * i + 1])));
	}
	printf("\n");
}

#ifdef DRIVER_WITH_SYNTH
// ---- the real synth over the model ----------------------------------------------------------------------------------------------
Synth g_synth;
void synW16(void *, uint16_t reg, uint16_t v) { write16(g_paula, reg, v); }
void synW32(void *, uint16_t reg, uint32_t v) { write32(g_paula, reg, v); }
uint16_t synR16(void *, uint16_t reg) { return read16(g_paula, reg); }
const SynthHw g_hw = {nullptr, synW16, synW32, synR16};
void synthIrq(void *) { ++g_irqCalls; synthInt4(g_synth, g_hw); }
#endif

}  // namespace

int main() {
	char cmd;
	static int16_t chunk[4096 * 2];
	for(int t = 0; t < 256; ++t) g_modMaster[t] = -1;
	while(scanf(" %c", &cmd) == 1) {
		if(cmd == 'B') {   // render mode for the next C: 0 = box, 1 = blep
			unsigned mode;
			if(scanf("%u", &mode) != 1) return 2;
			g_cfg.ubRender = static_cast<uint8_t>(mode);
		}
		else if(cmd == 'C') {
			unsigned rate, sep, lp, org, size;
			if(scanf("%u %u %u %x %x", &rate, &sep, &lp, &org, &size) != 5) return 2;
			g_cfg.ulRate = rate;
			g_cfg.ubSeparation = static_cast<uint8_t>(sep);
			g_cfg.ulLowPass = lp;
			g_origin = org;
			g_size = size;
			reset(g_paula, g_cfg, view());
		}
		else if(cmd == 'M') {
			if(scanf("%s", g_hex) != 1) return 2;
			const size_t n = strlen(g_hex) / 2;
			for(size_t i = 0; i < n && i < sizeof(g_mem); ++i) {
				g_mem[i] = static_cast<uint8_t>(nib(g_hex[2 * i]) * 16 + nib(g_hex[2 * i + 1]));
			}
		}
		else if(cmd == 'H') {
			unsigned ch, lc, len, off;
			if(scanf("%u %x %x %u", &ch, &lc, &len, &off) != 4 || ch > 3) return 2;
			Action &a = g_actions[ch][g_actionCount[ch]++];
			a.lc = lc;
			a.len = static_cast<uint16_t>(len);
			a.off = off != 0;
		}
		else if(cmd == 'W' || cmd == 'L' || cmd == 'G') {
			unsigned frame, reg = 0, val;
			if(cmd == 'G') {
				if(scanf("%u %u", &frame, &val) != 2) return 2;
			}
			else if(scanf("%u %x %x", &frame, &reg, &val) != 3) return 2;
			Event &e = g_events[g_eventCount++];
			e.frame = frame;
			e.kind = cmd == 'W' ? 0 : (cmd == 'L' ? 1 : 2);
			e.reg = static_cast<uint16_t>(reg);
			e.val = val;
		}
		else if(cmd == 'R') {
			unsigned n;
			if(scanf("%u", &n) != 1) return 2;
			static char line[1 << 20];
			uint32_t pos = 0;
			g_irqLogCount = 0;
			const Hooks h = hooks();
			for(g_frame = 0; g_frame < n; ++g_frame) {
				for(uint32_t i = 0; i < g_eventCount; ++i) {
					const Event &e = g_events[i];
					if(e.frame != g_frame) continue;
					if(e.kind == 0) write16(g_paula, e.reg, static_cast<uint16_t>(e.val));
					else if(e.kind == 1) write32(g_paula, e.reg, e.val);
					else g_masterOn = e.val != 0;
				}
				int16_t s[2];
				render(g_paula, s, 1, h);
				if(pos + 16 < sizeof(line)) {
					pos += static_cast<uint32_t>(snprintf(line + pos, 16, "%04x%04x", static_cast<unsigned>(static_cast<uint16_t>(s[0])),
						static_cast<unsigned>(static_cast<uint16_t>(s[1]))));
				}
			}
			for(uint32_t i = 0; i < g_irqLogCount; ++i) printf("I %u %u\n", g_irqLog[i][0], g_irqLog[i][1]);
			printf("K %x %d\n", static_cast<unsigned>(read16(g_paula, kRegAdkconr)), adkModulationRequested(g_paula) ? 1 : 0);
			printf("S%s\n.\n", line);
		}
		else if(cmd == 'P') {
			unsigned modoff, silence;
			if(scanf("%x %x", &modoff, &silence) != 2) return 2;
			if(!modStart(g_mod, g_mem + modoff, g_origin + modoff, g_origin + silence)) { printf("E\n"); }
		}
		else if(cmd == 'U') {
			unsigned ticks;
			if(scanf("%u", &ticks) != 1) return 2;
			const ModHw hw = {nullptr, modW16, modW32};
			const Hooks h = hooks();
			const uint32_t perTick = g_cfg.ulRate / 50;
			g_frame = 0;
			for(unsigned t = 0; t < ticks; ++t) {
				const uint32_t base = g_frame;
				uint32_t done = 0;
				if(g_modMaster[t < 256 ? t : 255] >= 0) modSetMaster(g_mod, hw, static_cast<uint8_t>(g_modMaster[t < 256 ? t : 255]));
				if(g_modStop >= 0 && t == static_cast<unsigned>(g_modStop)) modStop(g_mod, hw);
				modTick(g_mod, hw);
				if(modNeedsLoopPointers(g_mod)) {
					done = perTick < 40 ? perTick : 40;
					render(g_paula, chunk, done, h);
					g_frame = base + done;
					modLoopPointers(g_mod, hw);
				}
				render(g_paula, chunk + 2 * done, perTick - done, h);
				printSamples(chunk, perTick);
				g_frame = base + perTick;
			}
			printf(".\n");
		}
		else if(cmd == 'V') {
			unsigned tick, vol;
			if(scanf("%u %u", &tick, &vol) != 2 || tick >= 256) return 2;
			g_modMaster[tick] = static_cast<int>(vol);
		}
		else if(cmd == 'X') {
			unsigned tick;
			if(scanf("%u", &tick) != 1) return 2;
			g_modStop = static_cast<int>(tick);
		}
		else if(cmd == 'Z') {
			for(int t = 0; t < 256; ++t) g_modMaster[t] = -1;
			g_modStop = -1;
			g_eventCount = 0;
			g_masterOn = true;
			g_irqCalls = 0;
			for(int c = 0; c < 4; ++c) g_actionCount[c] = g_actionNext[c] = 0;
		}
#ifdef DRIVER_WITH_SYNTH
		else if(cmd == 'Y') {
			unsigned first, last, ticks;
			if(scanf("%u %u %u", &first, &last, &ticks) != 3) return 2;
			const uint32_t waveAddr = g_origin + 0x100;
			for(unsigned i = 0; i < kSynthWaveSize; ++i) g_mem[0x100 + i] = kSynthWave[i];
			const uint32_t banks[5] = {g_origin + 0x10000, g_origin + 0x20000, g_origin + 0x30000, g_origin + 0x40000, g_origin + 0x50000};
			for(unsigned seq = first; seq <= last; ++seq) {
				reset(g_paula, g_cfg, view());
				synthInit(g_synth, g_hw, waveAddr);
				synthRelocate(g_synth, banks);
				synthStart(g_synth, g_hw, static_cast<uint16_t>(seq), 0);
				Hooks h;
				h.pCtx = nullptr;
				h.pfnMasterOn = nullptr;
				h.pfnInterrupt = synthIrq;
				g_irqCalls = 0;
				int peak = 0;
				for(unsigned t = 0; t < ticks; ++t) {
					synthTick(g_synth, g_hw);
					int16_t out[2 * 960];
					render(g_paula, out, 960, h);
					for(int i = 0; i < 1920; ++i) {
						const int a = out[i] < 0 ? -out[i] : out[i];
						if(a > peak) peak = a;
					}
				}
				printf("Y %u %u %d %u\n", seq, static_cast<unsigned>(g_irqCalls), peak, static_cast<unsigned>(read16(g_paula, kRegDmaconr) & 15));
			}
			printf(".\n");
		}
#endif
	}
	return 0;
}
