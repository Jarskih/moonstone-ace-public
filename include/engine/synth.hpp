// engine/synth - mog's four-voice sequencer and synth driver (S_44: LAB_0F66..LAB_0FC2, ROADMAP 7.1g), pure: no ACE, no globals,
// all hardware through ms::SynthHw. Builds for the host tests and the Amiga.
//
// What the original is (docs/AUDIO.md section 2): four 148-byte voice structs kept IN the CODE hunk (the "self-modifying" state of
// 2.12), a per-tick routine (VBL job LAB_0F73 -> LAB_0F8F: sequence interpreter, modulation, envelope, Paula registers), a start
// routine (LAB_0F8C: sequence index + channel) and an INT4 audio handler (LAB_0F69) that reloads the loop buffers. All of it is
// transcribed here with the original's register write ORDER (tests/test_synth.py compares the whole Paula write log against the
// original code in unicorn, per tick). The state is a plain struct (ms::Synth) the rt layer owns; init clears it, which is what
// a fresh load of the mog hunk (zero voice structs) gave the original on every overlay entry (ROADMAP 2.12).
//
// Tables (engine/synth_data.hpp) are addressed by "refs" = S_44 offsets. Sample addresses: waveforms in kSynthWave (CHIP) and the
// sound-bank buffers LAB_05C7..LAB_05CB; the relocation of the instrument table (LAB_0FD4) is synthRelocate.
#pragma once
#include <stdint.h>

#include "engine/synth_data.hpp"

namespace ms {

constexpr uint8_t kSynthVoices = 4;
constexpr uint16_t kSynthStackBytes = 128;

// Custom chip register offsets from $DFF000 (all word registers; AUDxLC is written as one long).
constexpr uint16_t kSynthRegIntenar = 0x1C;
constexpr uint16_t kSynthRegIntreqr = 0x1E;
constexpr uint16_t kSynthRegDmacon = 0x96;
constexpr uint16_t kSynthRegIntena = 0x9A;
constexpr uint16_t kSynthRegIntreq = 0x9C;
constexpr uint16_t kSynthRegAdkcon = 0x9E;
constexpr uint16_t kSynthRegAud = 0xA0;          // + 0x10 * channel: +0 LC (long), +4 LEN, +6 PER, +8 VOL

// The only way the synth touches the chip set. Writes are single accesses in the original's order; the rt implementation must
// act as a compiler barrier (the INT4 handler shares the voice state).
struct SynthHw {
	void *pCtx;
	void (*pfnWrite16)(void *pCtx, uint16_t uwReg, uint16_t uwValue);
	void (*pfnWrite32)(void *pCtx, uint16_t uwReg, uint32_t ulValue);
	uint16_t (*pfnRead16)(void *pCtx, uint16_t uwReg);
};

// One voice = the original's 148-byte struct (offsets in comments). The constants of the original's init (DMACON/INTENA words,
// the AUDx register base) are derived from the channel number.
struct SynthVoice {
	volatile uint16_t uwIrqCount;    // 14: INT4 services still to come before the channel is switched off (2, or 1)
	uint32_t ulPitchRef;             // 16: pitch table of the current instrument (S_44 ref)
	uint32_t ulEnvRef;               // 20: envelope in use (S_44 ref), 0 = none (volume = base + tremolo)
	uint8_t aubStack[kSynthStackBytes];  // 24: the sequence call/loop stack (A3, grows down; the original keeps it in S_44 data)
	uint8_t ubSp;                    //     stack pointer, 0..128
	uint32_t ulSample;               // 28: sample address of the current instrument
	volatile int16_t wLoop;          // 36: instrument loop flag (< 0 = loop)
	volatile uint16_t uwLoopOff;     // 38: loop start (bytes)
	volatile uint16_t uwWords;       // 40: sample length (words)
	uint16_t uwNote;                 // 42: last note byte
	uint16_t uwVol;                  // 44: current volume (envelope or base + tremolo)
	uint16_t uwRetrigger;            // 46: new note: reload the channel on the next register pass
	uint8_t ubEnvTimer;              // 48: envelope tick counter
	uint16_t uwEnvOn;                // 50: envelope active (attack/decay/sustain) vs release
	uint16_t uwEnvPhase;             // 52: 0 attack, 1 decay, >= 2 sustain
	uint32_t ulSeqStart;             // 54: first byte of the sequence ($88 restarts it); 0 after $AC
	uint32_t ulSeqPos;               // 58: next byte; 0 = voice idle
	uint16_t uwNoteLeft;             // 62: ticks left of the current note
	uint16_t uwNoteLen;              // 64: note length in ticks (byte * tempo)
	uint16_t uwBaseVol;              // 68: $80 volume / envelope ceiling
	uint16_t uwBasePer;              // 70: period from the pitch table
	uint16_t uwPer;                  // 72: period written to Paula (base + vibrato)
	uint16_t auwVolDelay[2];         // 74: tremolo stage delay counters
	uint16_t auwVolSteps[2];         // 78: tremolo stage step counters
	uint16_t auwPerDelay[3];         // 82: vibrato stage delay counters
	uint16_t auwPerSteps[3];         // 88: vibrato stage step counters
	uint16_t auwRow[15];             // 94: the loaded vibrato row (sign-extended bytes): [0..1] vol steps, [2..4] per steps, [5..6] vol delta,
	                                 //     [7..9] per delta, [10..11] vol delay, [12..14] per delay
	int16_t wVolOff;                 // 134: tremolo offset (added to the base volume)
	int16_t wPerOff;                 // 136: vibrato offset (added to the base period)
	uint8_t ubFlags;                 // 138: bit 0 = repeat the tremolo, bit 1 = repeat the vibrato
	int16_t wTranspose;              // 140: added to 4 * note before the pitch table lookup
	uint16_t uwFade;                 // 142: attenuation from the fade (LAB_0FC2): 0 or 16
};

struct Synth {
	const SynthTables *pTab;         // the read-only tables (kSynthTables; tests pass their own copy)
	SynthVoice aVoice[kSynthVoices];
	uint16_t uwTempo;                // LAB_0FC5+2: ticks per sequence time unit (750 / $94 argument); NOT reset by init
	volatile uint16_t uwLock;        // LAB_0FCA: set while the tick or a start runs; the tick skips a frame when it is set
	uint32_t ulWaveBase;             // address of kSynthWave (SECSTRT_45)
	uint32_t aulSample[kSynthInstCount];   // the instrument sample addresses (the original relocates them in place, LAB_0FD4)
};

// LAB_0F89: initialise Paula and the four voices (the whole state is cleared first = a freshly loaded hunk). ulWaveBase is the
// address of kSynthWave in chip RAM; pTab = nullptr selects the generated tables.
void synthInit(Synth &s, const SynthHw &hw, uint32_t ulWaveBase, const SynthTables *pTab = nullptr);

// LAB_0FD4: sample addresses of the instrument table = bank base + offset. aulBank = LAB_05C7, 05C8, 05C9, 05CA, 05CB.
void synthRelocate(Synth &s, const uint32_t aulBank[5]);

// LAB_0F8C: stop channel uwChannel and start sequence uwSeq (index into LAB_1098) on it. A channel > 3 does nothing (the original
// indexes outside its voice table).
void synthStart(Synth &s, const SynthHw &hw, uint16_t uwSeq, uint16_t uwChannel);

// LAB_0F73 (VBL job): one tick. Returns false when the frame is skipped because the lock is set.
bool synthTick(Synth &s, const SynthHw &hw);

// LAB_0F69 body (INT4, audio): services every pending, enabled AUD0..3 interrupt until none is left.
void synthInt4(Synth &s, const SynthHw &hw);

// LAB_0FC2: fade step. uwFlag = LAB_0FC4 (non-zero: attenuate every voice by 16, else 0).
void synthFade(Synth &s, uint16_t uwFlag);

}  // namespace ms
