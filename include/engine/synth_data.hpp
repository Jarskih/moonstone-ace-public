// engine/synth_data - the read-only tables of mog's synth (ROADMAP 7.1g), generated into src/engine/synth_data.cpp by
// tools/gen_synth_tables.py from the original mog hunk (S_44 tables/sequences, S_45 instruments and waveforms).
#pragma once
#include <stdint.h>

#if defined(AMIGA)
#define MS_SYNTH_CHIP __attribute__((section(".chipdata.MEMF_CHIP"), aligned(4)))   // elf2hunk puts *.MEMF_CHIP sections in chip RAM
#else
#define MS_SYNTH_CHIP
#endif

namespace ms {

// "Refs" are offsets into the original S_44 hunk (the raw values the original's tables hold before relocation); 0 is the
// null pointer. kSynthBlob holds S_44 [kSynthBlobBase, kSynthBlobBase + kSynthBlobSize).
constexpr uint32_t kSynthBlobBase = 3570;       // LAB_0FCA + 2, the first pitch table word
constexpr uint32_t kSynthBlobSize = 5890;       // up to the end of S_44 (9460)
constexpr uint32_t kSynthVibBase = 5046;        // LAB_0FE0 + 2: vibrato/tremolo rows, 15 bytes each
constexpr uint32_t kSynthEnvBase = 5106;        // LAB_0FE1: envelopes, 8 bytes each
constexpr uint32_t kSynthSeqTable = 8788;       // LAB_1098: 168 longs, sequence index -> ref of its first byte
constexpr uint16_t kSynthSeqCount = 168;        // sequences 0 (null) .. $A7 ($A7 = silence)
constexpr uint16_t kSynthInstCount = 131;       // LAB_10A2 .. LAB_10AE
constexpr uint16_t kSynthWaveSize = 170;        // S_45 bytes 0..169 (waveforms + the first word of the instrument table)
constexpr uint32_t kSynthDummyOffset = 168;     // LAB_10A2: the original's 1-word silent buffer inside kSynthWave

// Sample banks an instrument's sample address is relative to: 0 = kSynthWave, 1..5 = the sound-bank buffers
// LAB_05C7, LAB_05C8, LAB_05C9, LAB_05CA, LAB_05CB (the carve cells of mog's arena).
constexpr uint8_t kSynthBanks = 6;

struct SynthInstrument {
	int16_t wLoop;         // 36(A4): < 0 = loop (the INT4 handler reloads the loop part), else one shot
	uint16_t uwLoopOff;    // 38(A4): loop start in bytes
	uint16_t uwWords;      // 40(A4): sample length in words
	uint8_t ubBank;        // see above
	uint32_t ulOffset;     // sample address relative to the bank base
	uint16_t uwPitch;      // 16(A4): ref of the pitch table (S_44 offset)
};

// What the engine reads (one pointer pair, so the host tests can run it on a modified copy of the blob).
struct SynthTables {
	const uint8_t *pBlob;                  // kSynthBlobSize bytes
	const SynthInstrument *pInst;          // kSynthInstCount rows
};

// Not const since ROADMAP 10.2a: the game fills them at start-up from mog on the player's disk (src/rt/synth_fill.cpp, the runs
// below); tools/gen_synth_tables.py writes the compiled-in copy the host tests and MS_DATA_COMPILED use.
extern uint8_t kSynthBlob[kSynthBlobSize];
extern SynthInstrument kSynthInst[kSynthInstCount];

// Where the tables lie in mog (facts for the start-up fill): kSynthBlob = S_44 [kSynthBlobBase, end) with the word at
// kSynthBlobBase and the relocation routine [kSynthRelocBeg, kSynthRelocEnd) zero; the instruments are kSynthInstCount rows of
// 14 bytes at S_45 + kSynthInstBase: {loop word, loop offset, words, sample address (S_45 offset for the first nine, else an
// offset in the bank), pitch table (S_44 offset)}.
constexpr uint16_t kSynthHunkTables = 44;
constexpr uint16_t kSynthHunkInst = 45;
constexpr uint32_t kSynthRelocBeg = 4628;       // LAB_0FD4 .. its RTS: code IRA left among the tables
constexpr uint32_t kSynthRelocEnd = 4980;
constexpr uint32_t kSynthInstBase = 168;        // LAB_10A2
constexpr uint32_t kSynthInstRow = 14;
// The rows of kSynthInstCount * kSynthInstRow raw bytes -> kSynthInst (the bank of each row is LAB_0FD4's rule, a fact).
void synthDecodeInstruments(const uint8_t *pRaw, SynthInstrument *pOut);
// MS_SYNTH_STANDALONE: the unicorn test of the compiled synth (tests/test_synth.py) links without the owned hunk and keeps its own copy.
#if defined(MS_GAME_BUILD) && MS_GAME_BUILD && !defined(MS_SYNTH_STANDALONE)
#define MS_SYNTH_WAVE_IN_HUNK 1
#else
#define MS_SYNTH_WAVE_IN_HUNK 0
#endif
#if MS_SYNTH_WAVE_IN_HUNK
// game build (MS_GAME_BUILD, ROADMAP 7.1n5): the waveforms ARE the first bytes of mog's S_45 hunk, owned by tools/gen_data.py as
// g_mogSynthData (CHIP; the asm synth of the MS_SYNTH_ASM A/B build reads the same hunk): one copy, no duplicate.
extern uint8_t kSynthWave[kSynthWaveSize] asm("g_mogSynthData");
#else
extern uint8_t kSynthWave[kSynthWaveSize];   // CHIP on the Amiga (Paula reads it)
#endif
extern const SynthTables kSynthTables;       // {kSynthBlob, kSynthInst}

}  // namespace ms
