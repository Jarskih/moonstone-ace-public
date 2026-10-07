// engine/synth - mog's four-voice sequencer/synth (S_44), transcribed routine by routine from mog.asm 27888-28604 (ROADMAP 7.1g).
// Citations are line numbers of ../moonshard/moonstone-main/amiga_asm/mog.asm. Every Paula write happens in the original's order
// (the order is part of the contract: tests/test_synth.py compares the full write log with the original code in unicorn).
//
// Voice indexing: channel n = Paula AUDn = the struct SECSTRT_44 (n = 0), LAB_0F66, LAB_0F67, LAB_0F68; INT4 bits 7..10 = AUD0..3.
// Quirks kept (all visible in the original):
//   * LAB_0F8C ends with DMACON <- $0002 (not the channel's own bit): starting any sequence switches channel 1's DMA off.
//   * LAB_0F8C clears voice bytes 16..143, i.e. also the fade attenuation (142) and the transpose (140) of the started voice.
//   * the command table is indexed by the raw byte - $80, so only multiples of 4 up to $D4 are commands.
//   * $C0/$C4 loops run the body count + 1 times; $C0 with 0 pushes a null loop that $C4 pops at once.
//   * LAB_0F89 never resets the tempo word (here: the state is cleared by init like a freshly loaded hunk, tempo 0).
// Defensive deviations (never hit by the game's data; the original would fetch garbage or trap): an out-of-range sequence/instrument
// index, a channel > 3, tempo argument 0 ($94) and a command byte that is not in the table are ignored / stop the voice.
#include "engine/synth.hpp"

namespace ms {

namespace {

constexpr uint16_t kDmaOnBit = 0x8000;
constexpr uint8_t kCmdBase = 0x80;

uint16_t regBase(uint8_t ubCh) { return (uint16_t)(kSynthRegAud + 0x10u * ubCh); }
uint16_t dmaOff(uint8_t ubCh) { return (uint16_t)(1u << ubCh); }               // 2(A4)
uint16_t dmaOn(uint8_t ubCh) { return (uint16_t)(kDmaOnBit | (1u << ubCh)); }   // 0(A4)
uint16_t intOff(uint8_t ubCh) { return (uint16_t)(0x80u << ubCh); }             // 6(A4)
uint16_t intOn(uint8_t ubCh) { return (uint16_t)(kDmaOnBit | (0x80u << ubCh)); } // 4(A4)

// Data access by ref (an S_44 offset). Outside the blob: stop command for the sequence stream, 0 for data.
uint8_t blobByte(const SynthTables &t, uint32_t ulRef, uint8_t ubOutside) {
	const uint32_t ulIdx = ulRef - kSynthBlobBase;
	return ulIdx < kSynthBlobSize ? t.pBlob[ulIdx] : ubOutside;
}
uint16_t blobWord(const SynthTables &t, uint32_t ulRef) {
	return (uint16_t)((blobByte(t, ulRef, 0) << 8) | blobByte(t, ulRef + 1, 0));
}
uint32_t blobLong(const SynthTables &t, uint32_t ulRef) {
	return ((uint32_t)blobWord(t, ulRef) << 16) | blobWord(t, ulRef + 2);
}
uint8_t seqByte(const SynthTables &t, uint32_t ulRef) { return blobByte(t, ulRef, 0xAC); }
uint32_t seqStart(const SynthTables &t, uint16_t uwSeq) {                        // LAB_1098[uwSeq]
	return uwSeq < kSynthSeqCount ? blobLong(t, kSynthSeqTable + 4u * uwSeq) : 0;
}

// The sequence stack (A3): big-endian bytes, grows down, 24(A4) = top = 128.
uint16_t stk16(const SynthVoice &v, uint16_t uwAt) {
	return uwAt + 2u <= kSynthStackBytes ? (uint16_t)((v.aubStack[uwAt] << 8) | v.aubStack[uwAt + 1]) : (uint16_t)0;
}
uint32_t stk32(const SynthVoice &v, uint16_t uwAt) {
	return ((uint32_t)stk16(v, uwAt) << 16) | stk16(v, uwAt + 2);
}
void stkPut16(SynthVoice &v, uint16_t uwAt, uint16_t uwVal) {
	if(uwAt + 2u <= kSynthStackBytes) {
		v.aubStack[uwAt] = (uint8_t)(uwVal >> 8);
		v.aubStack[uwAt + 1] = (uint8_t)uwVal;
	}
}
void stkPush16(SynthVoice &v, uint16_t uwVal) {
	if(v.ubSp >= 2) {
		v.ubSp = (uint8_t)(v.ubSp - 2);
		stkPut16(v, v.ubSp, uwVal);
	}
}
void stkPush32(SynthVoice &v, uint32_t ulVal) {
	if(v.ubSp >= 4) {
		v.ubSp = (uint8_t)(v.ubSp - 4);
		stkPut16(v, v.ubSp, (uint16_t)(ulVal >> 16));
		stkPut16(v, v.ubSp + 2, (uint16_t)ulVal);
	}
}
uint32_t stkPop32(SynthVoice &v) {
	const uint32_t ulVal = stk32(v, v.ubSp);
	v.ubSp = (uint8_t)(v.ubSp + 4 <= kSynthStackBytes ? v.ubSp + 4 : kSynthStackBytes);
	return ulVal;
}

inline void w16(const SynthHw &hw, uint16_t uwReg, uint16_t uwVal) { hw.pfnWrite16(hw.pCtx, uwReg, uwVal); }
inline void w32(const SynthHw &hw, uint16_t uwReg, uint32_t ulVal) { hw.pfnWrite32(hw.pCtx, uwReg, ulVal); }

// Channel to its resting state: DMACON off, INTENA off, INTREQ clear (the order differs between callers, so only the part after
// the three control writes is shared): AUDxVOL 0, LC = the dummy buffer, LEN 1, PER 1.
void restBuffer(const SynthHw &hw, uint8_t ubCh, uint32_t ulLc) {
	const uint16_t uwR = regBase(ubCh);
	w16(hw, uwR + 8, 0);
	w32(hw, uwR + 0, ulLc);
	w16(hw, uwR + 4, 1);
	w16(hw, uwR + 6, 1);
}

// ---- LAB_0FC0 / LAB_0FBE: reload the modulation counters from the loaded row ------------------------------------------------
void reloadVol(SynthVoice &v) {                         // LAB_0FC0 (28570)
	for(uint8_t j = 0; j < 2; ++j) {
		v.auwVolDelay[j] = v.auwRow[10 + j];             // 74(A1) = 114(A1)
		v.auwVolSteps[j] = v.auwRow[j];                  // 78(A1) = 94(A1)
	}
}
void reloadPer(SynthVoice &v) {                         // LAB_0FBE (28561)
	for(uint8_t j = 0; j < 3; ++j) {
		v.auwPerDelay[j] = v.auwRow[12 + j];             // 82(A1) = 118(A1)
		v.auwPerSteps[j] = v.auwRow[2 + j];              // 88(A1) = 98(A1)
	}
}

// Instrument change (LAB_0F8C head, LAB_0FBC): 36/38/40, sample 28, pitch table 16.
void setInstrument(const Synth &s, SynthVoice &v, uint16_t uwIdx) {
	const SynthInstrument &i = s.pTab->pInst[uwIdx];
	v.wLoop = i.wLoop;
	v.uwLoopOff = i.uwLoopOff;
	v.uwWords = i.uwWords;
	v.ulSample = s.aulSample[uwIdx];
	v.ulPitchRef = i.uwPitch;
}

// ---- LAB_0F7C / LAB_0F7F: envelope step of one voice (28023-28089) ------------------------------------------------------------
void envelopeStep(const SynthTables &t, SynthVoice &v) {
	if(v.ulSeqPos == 0) return;                          // TST.L 58(A4)
	if(v.ulEnvRef == 0) return;                          // MOVE.L 20(A4),D0 ; BEQ
	const uint32_t ulE = v.ulEnvRef;
	if(v.ubEnvTimer != 0) {                              // TST.B 48(A4)
		v.ubEnvTimer = (uint8_t)(v.ubEnvTimer - 1);
		if(v.ubEnvTimer != 0) return;                    // SUBQ.B ; BNE
	}
	// LAB_0F7F
	const uint8_t e0 = blobByte(t, ulE + 0, 0), e1 = blobByte(t, ulE + 1, 0), e2 = blobByte(t, ulE + 2, 0), e3 = blobByte(t, ulE + 3, 0);
	const uint8_t e4 = blobByte(t, ulE + 4, 0), e5 = blobByte(t, ulE + 5, 0), e6 = blobByte(t, ulE + 6, 0), e7 = blobByte(t, ulE + 7, 0);
	if(v.uwEnvOn == 0) {                                 // LAB_0F86: release
		v.uwEnvPhase = 0;
		if(v.uwVol != 0) {
			v.ubEnvTimer = e7;
			uint16_t d0 = v.uwVol;
			const uint8_t ubLo = (uint8_t)d0;
			const bool bBorrow = ubLo < e6;              // SUB.B 6(A3),D0 ; BCC
			d0 = (uint16_t)((d0 & 0xFF00) | (uint8_t)(ubLo - e6));
			if(bBorrow) d0 = 0;
			v.uwVol = d0;
		}
		return;
	}
	uint16_t d0 = v.uwEnvPhase;
	if(d0 == 0) {                                        // attack
		v.ubEnvTimer = e1;
		d0 = v.uwVol;
		d0 = (uint16_t)((d0 & 0xFF00) | (uint8_t)((uint8_t)d0 + e0));   // ADD.B 0(A3),D0
		if(d0 >= v.uwBaseVol) {                          // CMP.W 68(A4),D0 ; BCS
			v.uwEnvPhase = (uint16_t)(v.uwEnvPhase + 1);
			d0 = v.uwBaseVol;
		}
		v.uwVol = d0;                                    // LAB_0F80
		return;
	}
	if(d0 == 1) {                                        // decay, LAB_0F84
		v.ubEnvTimer = e3;
		d0 = v.uwVol;
		d0 = (uint16_t)((d0 & 0xFF00) | (uint8_t)((uint8_t)d0 - e2));   // SUB.B 2(A3),D0
		if((uint8_t)d0 >= 0x40) d0 = 0;                  // CMP.B #$40,D0 ; BCS ; CLR.W D0
		if((uint8_t)d0 >= e4) {                          // LAB_0F85: CMP.B 4(A3),D0 ; BCC LAB_0F80
			v.uwVol = d0;
			return;
		}
		v.uwEnvPhase = (uint16_t)(v.uwEnvPhase + 1);     // ADDQ.W #1,52(A4), then sustain
	}
	// LAB_0F82: sustain level, and release once the note has (almost) run out
	v.uwVol = e4;
	if(!((int8_t)(uint8_t)v.uwNoteLeft > (int8_t)e5)) {  // CMP.B 5(A3),D0 ; BGT
		v.uwEnvOn = 0;
	}
}

// ---- LAB_0F76: Paula registers of one voice, after the envelope (27983-28014) ------------------------------------------------
void registerPass(const SynthHw &hw, SynthVoice &v, uint8_t ubCh) {
	const uint16_t uwR = regBase(ubCh);
	w16(hw, uwR + 6, v.uwPer);                                      // 6(A5) = 72(A4)
	int16_t wVol = (int16_t)(v.uwVol - v.uwFade);                   // SUB.W 142(A4),D0 ; BPL
	if(wVol < 0) wVol = 0;
	w16(hw, uwR + 8, (uint16_t)wVol);
	if(v.uwRetrigger == 0) return;                                  // TST.W 46(A4)
	v.uwRetrigger = 0;
	w32(hw, uwR + 0, v.ulSample);
	w16(hw, uwR + 4, v.uwWords);
	v.uwIrqCount = 2;
	w16(hw, kSynthRegDmacon, dmaOn(ubCh));
	w16(hw, kSynthRegIntreq, intOff(ubCh));
	if(v.wLoop < 0) {                                               // TST.W 36(A4) ; BPL LAB_0F79
		if(v.uwLoopOff == 0) {
			w16(hw, kSynthRegIntena, intOff(ubCh));                 // whole-sample loop: no reload interrupt
			return;
		}
		v.uwIrqCount = 1;                                           // LAB_0F78
	}
	w16(hw, kSynthRegIntena, intOn(ubCh));                          // LAB_0F79
}

// ---- sequence commands and the per-voice step (LAB_0F90..LAB_0FA3, 28245-28403) ------------------------------------------------
// Silences a channel before a new note (LAB_0F94 head): DMACON off, INTENA off, INTREQ clear, then the resting buffer.
void noteSilence(const Synth &s, const SynthHw &hw, uint8_t ubCh) {
	w16(hw, kSynthRegDmacon, dmaOff(ubCh));
	w16(hw, kSynthRegIntena, intOff(ubCh));
	w16(hw, kSynthRegIntreq, intOff(ubCh));
	restBuffer(hw, ubCh, s.ulWaveBase + kSynthDummyOffset);
}

void voiceStep(Synth &s, const SynthHw &hw, SynthVoice &v, uint8_t ubCh) {
	const SynthTables &t = *s.pTab;
	if(v.ulSeqPos == 0) return;                                      // LAB_0F90: TST.L 58(A4) ; BEQ LAB_0FA3
	bool bToModulation = false;
	if(v.uwNoteLeft != 0) {
		bToModulation = true;                                        // BNE LAB_0F97
	} else {
		if(v.uwLoopOff == 0) {                                       // TST.W 38(A4) ; BNE LAB_0F91
			w16(hw, kSynthRegDmacon, dmaOff(ubCh));
			w16(hw, kSynthRegIntena, intOff(ubCh));
			w16(hw, kSynthRegIntreq, intOff(ubCh));
			restBuffer(hw, ubCh, s.ulWaveBase + kSynthDummyOffset);
			w16(hw, kSynthRegDmacon, dmaOn(ubCh));
		}
		uint32_t ulA2 = v.ulSeqPos;                                  // LAB_0F91
		if(ulA2 == 0) {
			// BEQ LAB_0FA1 (never taken: 58 was tested above)
		} else {
			for(;;) {                                                // LAB_0F92
				const uint8_t ubB = seqByte(t, ulA2++);
				if(!(ubB & 0x80)) {                                  // LAB_0F94: a note
					v.uwNote = ubB;
					noteSilence(s, hw, ubCh);
					if(v.ulEnvRef != 0) {                            // TST.L 20(A4)
						v.uwEnvOn |= 0xFF00;                         // ST 50(A4)
						v.uwEnvPhase = 0;
						v.uwVol = 0;
						v.ubEnvTimer = 0;
					}
					const uint16_t uwIdx = (uint16_t)((uint16_t)(ubB * 4u) + (uint16_t)v.wTranspose);
					const int16_t wOff = (int16_t)(uint16_t)(uwIdx * 2u);
					v.uwBasePer = blobWord(t, v.ulPitchRef + (int32_t)wOff);
					v.uwRetrigger |= 0xFF00;                         // ST 46(A4)
					reloadVol(v);
					v.wVolOff = 0;
					reloadPer(v);
					v.wPerOff = 0;
					break;                                           // -> LAB_0F96
				}
				const uint8_t ubCmd = (uint8_t)(ubB - kCmdBase);
				bool bEnd = false;
				switch(ubCmd) {
					case 0x00: v.uwBaseVol = seqByte(t, ulA2++); break;                              // $80 LAB_0FA4 (68)
					case 0x04: ++ulA2; break;                                                      // $84 LAB_0FA5
					case 0x08: ulA2 = v.ulSeqStart; v.ulSeqPos = ulA2; break;                      // $88 LAB_0FA6: restart
					case 0x0C: v.uwNoteLen = (uint16_t)(seqByte(t, ulA2++) * s.uwTempo); break;       // $8C LAB_0FA7
					case 0x10: bEnd = true; break;                                                 // $90 LAB_0F96: end of the note data
					case 0x14: {                                                                   // $94 LAB_0FA8: tempo = 750 / byte
						const uint8_t ubD = seqByte(t, ulA2++);
						if(ubD != 0) s.uwTempo = (uint16_t)(0x2EEu / ubD);
						break;
					}
					case 0x18: {                                                                   // $98 LAB_0FA9: length = sum * tempo
						uint16_t uwD1 = (uint16_t)(seqByte(t, ulA2++) - 1);
						uint16_t uwSum = 0;
						do {
							uwSum = (uint16_t)(uwSum + seqByte(t, ulA2++));
						} while((uint16_t)(--uwD1) != 0xFFFF);                                    // DBF
						v.uwNoteLen = (uint16_t)(uwSum * s.uwTempo);
						break;
					}
					case 0x1C: {                                                                   // $9C LAB_0FAB: load a vibrato row
						v.ubFlags = (uint8_t)(v.ubFlags & 0xFC);
						const uint32_t ulRow = kSynthVibBase + 15u * seqByte(t, ulA2++);
						for(uint8_t i = 0; i < 15; ++i) {
							v.auwRow[i] = (uint16_t)(int16_t)(int8_t)blobByte(t, ulRow + i, 0);
						}
						v.auwVolDelay[0] = v.auwVolDelay[1] = 0;
						v.auwPerDelay[0] = v.auwPerDelay[1] = v.auwPerDelay[2] = 0;
						v.wVolOff = 0;
						v.wPerOff = 0;
						break;
					}
					case 0x20: ++ulA2; break;                                                      // $A0 LAB_0FAD
					case 0x24: ++ulA2; break;                                                      // $A4 LAB_0FAE
					case 0x28: v.ubFlags = (uint8_t)(v.ubFlags | seqByte(t, ulA2++)); break;          // $A8 LAB_0FAF
					case 0x2C:                                                                     // $AC LAB_0FB0: stop the voice
						v.uwNoteLeft = 0;
						ulA2 = 0;
						v.ulSeqStart = 0;
						w16(hw, kSynthRegIntena, intOff(ubCh));
						w16(hw, kSynthRegIntreq, intOff(ubCh));
						w16(hw, kSynthRegDmacon, dmaOff(ubCh));
						v.uwRetrigger = 0;
						bEnd = true;                                                              // BRA LAB_0F96
						break;
					case 0x30: {                                                                   // $B0 LAB_0FB1: call a sequence
						const uint8_t ubIdx = seqByte(t, ulA2++);
						stkPush32(v, ulA2);
						ulA2 = seqStart(t, ubIdx);
						break;
					}
					case 0x34: ulA2 = stkPop32(v); break;                                          // $B4 LAB_0FB2: return
					case 0x38: {                                                                   // $B8 LAB_0FB3: transpose += (signed byte)
						const uint8_t ubD = seqByte(t, ulA2++);
						if(ubD != 0) v.wTranspose = (int16_t)(v.wTranspose + (int8_t)ubD); else v.wTranspose = 0;
						break;
					}
					case 0x3C: v.wTranspose = (int16_t)(int8_t)seqByte(t, ulA2++); break;             // $BC LAB_0FB5
					case 0x40: {                                                                   // $C0 LAB_0FB6: loop start
						const uint8_t ubCount = seqByte(t, ulA2++);
						if(ubCount != 0) {
							stkPush32(v, ulA2);
							stkPush16(v, ubCount);
						} else {
							stkPush32(v, 0);
							stkPush16(v, 0);
						}
						break;
					}
					case 0x44: {                                                                   // $C4 LAB_0FB8: loop end
						const uint16_t uwSp0 = v.ubSp;
						const uint32_t ulLoop = stk32(v, (uint16_t)(uwSp0 + 2));
						if(ulLoop == 0) {
							v.ubSp = (uint8_t)(uwSp0 + 6 <= kSynthStackBytes ? uwSp0 + 6 : kSynthStackBytes);   // LAB_0FB9
						} else {
							ulA2 = ulLoop;
							const uint16_t uwCount = (uint16_t)(stk16(v, uwSp0) - 1);
							stkPut16(v, uwSp0, uwCount);
							if(uwCount == 0) {
								stkPut16(v, (uint16_t)(uwSp0 + 2), 0);
								stkPut16(v, (uint16_t)(uwSp0 + 4), 0);
							}
						}
						break;
					}
					case 0x48: v.ulEnvRef = kSynthEnvBase + 8u * seqByte(t, ulA2++); break;           // $C8 LAB_0FBA: envelope
					case 0x4C: v.ulEnvRef = 0; break;                                              // $CC LAB_0FBB
					case 0x50: {                                                                   // $D0 LAB_0FBC: instrument
						const uint8_t ubIdx = seqByte(t, ulA2++);
						if(ubIdx < kSynthInstCount) setInstrument(s, v, ubIdx);
						break;
					}
					case 0x54: {                                                                   // $D4 LAB_0FBD: jump (no return)
						const uint8_t ubIdx = seqByte(t, ulA2++);
						ulA2 = seqStart(t, ubIdx);
						break;
					}
					default: bEnd = true; ulA2 = 0; break;                                         // not a command: stop the voice
				}
				if(bEnd) break;
			}
			// LAB_0F96
			v.ulSeqPos = ulA2;
			v.uwNoteLeft = v.uwNoteLen;
			bToModulation = true;
		}
	}
	if(!bToModulation) return;                                       // (BEQ LAB_0FA1 path: nothing more this tick)
	// LAB_0F97
	v.uwNoteLeft = (uint16_t)(v.uwNoteLeft - 1);
	if(v.ulEnvRef == 0) {
		uint8_t ubFound = 0;
		for(uint8_t j = 0; j < 2 && !ubFound; ++j) {                 // LAB_0F98
			if(v.auwVolDelay[j] != 0) {
				--v.auwVolDelay[j];
				ubFound = 1;
			} else if(v.auwVolSteps[j] != 0) {
				--v.auwVolSteps[j];
				v.wVolOff = (int16_t)(v.wVolOff + (int16_t)v.auwRow[5 + j]);
				v.auwVolDelay[j] = v.auwRow[10 + j];
				ubFound = 1;
			}
		}
		if(!ubFound) {                                               // LAB_0F9B
			v.wVolOff = 0;
			if(v.ubFlags & 1) reloadVol(v);
		}
	}
	{
		uint8_t ubFound = 0;
		for(uint8_t j = 0; j < 3 && !ubFound; ++j) {                 // LAB_0F9D
			if(v.auwPerDelay[j] != 0) {
				--v.auwPerDelay[j];
				ubFound = 1;
			} else if(v.auwPerSteps[j] != 0) {
				--v.auwPerSteps[j];
				v.wPerOff = (int16_t)(v.wPerOff + (int16_t)(int8_t)v.auwRow[7 + j]);   // EXT.W on the low byte
				v.auwPerDelay[j] = v.auwRow[12 + j];
				ubFound = 1;
			}
		}
		if(!ubFound && (v.ubFlags & 2)) reloadPer(v);                // LAB_0FA0
	}
	if(v.ulEnvRef == 0) {                                            // LAB_0FA1
		v.uwVol = (uint16_t)((uint16_t)(v.uwBaseVol + (uint16_t)v.wVolOff) & 0x3F);
	}
	v.uwPer = (uint16_t)(v.uwBasePer + (uint16_t)v.wPerOff);         // LAB_0FA2
}

// LAB_0F6F: one AUDx buffer finished (27931-27962)
void channelIrq(Synth &s, const SynthHw &hw, uint8_t ubCh) {
	SynthVoice &v = s.aVoice[ubCh];
	const uint16_t uwR = regBase(ubCh);
	v.uwIrqCount = (uint16_t)(v.uwIrqCount - 1);
	if(v.wLoop < 0) {                                                // loop instrument: point Paula at the loop part
		const uint16_t uwOff = v.uwLoopOff;
		w32(hw, uwR + 0, v.ulSample + uwOff);
		w16(hw, uwR + 4, (uint16_t)(v.uwWords - (uint16_t)(uwOff >> 1)));
		w16(hw, kSynthRegIntena, intOff(ubCh));
	} else if(v.uwIrqCount == 0) {                                   // one shot played out: channel off
		w16(hw, kSynthRegIntena, intOff(ubCh));
		w16(hw, kSynthRegDmacon, dmaOff(ubCh));
	} else {                                                         // first buffer done: park on the silent word
		w32(hw, uwR + 0, s.ulWaveBase);
		w16(hw, uwR + 4, 1);
	}
	w16(hw, kSynthRegIntreq, intOff(ubCh));                          // LAB_0F72
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------
void synthInit(Synth &s, const SynthHw &hw, uint32_t ulWaveBase, const SynthTables *pTab) {   // LAB_0F89 (28090-28182)
	s.uwLock = 0xFF00;                                               // ST LAB_0FCA
	{
		uint8_t *p = reinterpret_cast<uint8_t *>(&s);
		for(uint32_t i = 0; i < sizeof(Synth); ++i) p[i] = 0;        // a freshly loaded hunk (ROADMAP 2.12)
	}
	s.uwLock = 0xFF00;
	s.ulWaveBase = ulWaveBase;
	s.pTab = pTab != nullptr ? pTab : &kSynthTables;
	w16(hw, kSynthRegDmacon, 0x800F);
	w16(hw, kSynthRegIntena, 0x0780);
	w16(hw, kSynthRegIntreq, 0x0780);
	w16(hw, kSynthRegAdkcon, 0x00FF);
	for(uint8_t c = 0; c < 4; ++c) w16(hw, regBase(c) + 8, 0);       // VOL
	for(uint8_t c = 0; c < 4; ++c) w16(hw, regBase(c) + 4, 1);       // LEN
	for(uint8_t c = 0; c < 4; ++c) w16(hw, regBase(c) + 6, 1);       // PER
	for(uint8_t c = 0; c < 4; ++c) w32(hw, regBase(c) + 0, ulWaveBase);   // LC
	for(uint8_t c = 0; c < 4; ++c) w16(hw, regBase(c) + 4, 8);       // LEN
	for(uint8_t c = 0; c < 4; ++c) w16(hw, regBase(c) + 6, 0x0100);  // PER
	w16(hw, kSynthRegDmacon, 0x000F);
	for(uint8_t c = 0; c < 4; ++c) {
		SynthVoice &v = s.aVoice[c];
		v.uwIrqCount = 2;
		v.ulSample = ulWaveBase;
		v.uwWords = 8;
		v.ubSp = kSynthStackBytes;
	}
	for(uint16_t i = 0; i < kSynthInstCount; ++i) {                  // before the first LAB_0FD4: the raw table (waveforms resolved)
		s.aulSample[i] = (i < 9 ? ulWaveBase : 0) + s.pTab->pInst[i].ulOffset;
	}
	s.uwLock = 0;                                                    // CLR.W LAB_0FCA
}

void synthRelocate(Synth &s, const uint32_t aulBank[5]) {            // LAB_0FD4 (29138-29219)
	for(uint16_t i = 0; i < kSynthInstCount; ++i) {
		const SynthInstrument &in = s.pTab->pInst[i];
		const uint32_t ulBase = in.ubBank == 0 ? s.ulWaveBase : aulBank[in.ubBank - 1];
		s.aulSample[i] = ulBase + in.ulOffset;
	}
}

void synthStart(Synth &s, const SynthHw &hw, uint16_t uwSeq, uint16_t uwChannel) {   // LAB_0F8C (28189-28233)
	if(uwChannel >= kSynthVoices) return;
	s.uwLock = 0xFF00;                                               // ST LAB_0FCA
	const uint8_t ubCh = (uint8_t)uwChannel;
	SynthVoice &v = s.aVoice[ubCh];
	{                                                                // bytes 16..143 := 0 (MOVE.L D4,(A3)+ x 32); 14 stays
		const uint16_t uwIrq = v.uwIrqCount;
		uint8_t *p = reinterpret_cast<uint8_t *>(&v);
		for(uint32_t i = 0; i < sizeof(SynthVoice); ++i) p[i] = 0;
		v.uwIrqCount = uwIrq;
	}
	v.ubSp = kSynthStackBytes;                                       // 24(A4) = the channel's stack top
	w16(hw, kSynthRegIntena, intOff(ubCh));
	w16(hw, kSynthRegIntreq, intOff(ubCh));
	w16(hw, kSynthRegDmacon, dmaOff(ubCh));
	{
		const uint16_t uwR = regBase(ubCh);
		w16(hw, uwR + 8, 0);
		w32(hw, uwR + 0, s.ulWaveBase);
		w16(hw, uwR + 4, 1);
		w16(hw, uwR + 6, 1);
	}
	w16(hw, kSynthRegDmacon, dmaOn(ubCh));
	v.ulSeqStart = v.ulSeqPos = seqStart(*s.pTab, uwSeq);                     // 54(A4) = 58(A4)
	setInstrument(s, v, 0);
	v.uwBasePer = 0x0100;                                            // 70, 72
	v.uwPer = 0x0100;
	{
		const uint16_t uwR = regBase(ubCh);
		w32(hw, uwR + 0, v.ulSample);
		w16(hw, uwR + 4, v.uwWords);
		w16(hw, uwR + 6, 0x0100);
	}
	w16(hw, kSynthRegDmacon, 0x0002);                                // (sic) channel 1's DMA off, whatever channel started
	s.uwLock = 0;                                                    // CLR.W LAB_0FCA
}

bool synthTick(Synth &s, const SynthHw &hw) {                        // LAB_0F73 -> LAB_0F8F (27963-27974, 28239-28409)
	if(s.uwLock != 0) return false;
	s.uwLock = 0xFF00;
	for(uint8_t c = 0; c < kSynthVoices; ++c) voiceStep(s, hw, s.aVoice[c], c);   // D1 = 3..0 over A4 = voice 0..3
	for(uint8_t c = 0; c < kSynthVoices; ++c) envelopeStep(*s.pTab, s.aVoice[c]);          // LAB_0F7B
	for(uint8_t c = 0; c < kSynthVoices; ++c) registerPass(hw, s.aVoice[c], c);  // LAB_0F75
	s.uwLock = 0;
	return true;
}

void synthInt4(Synth &s, const SynthHw &hw) {                        // LAB_0F69 (27888-27930)
	for(;;) {
		const uint16_t uwReq = hw.pfnRead16(hw.pCtx, kSynthRegIntreqr);
		uint8_t ubCh;
		if(uwReq & 0x0080) ubCh = 0;
		else if(uwReq & 0x0100) ubCh = 1;
		else if(uwReq & 0x0200) ubCh = 2;
		else if(uwReq & 0x0400) ubCh = 3;
		else return;
		w16(hw, kSynthRegIntreq, intOff(ubCh));                      // ack
		if(!(hw.pfnRead16(hw.pCtx, kSynthRegIntenar) & intOff(ubCh))) continue;   // BTST of INTENAR: not enabled, look again
		channelIrq(s, hw, ubCh);
	}
}

void synthFade(Synth &s, uint16_t uwFlag) {                          // LAB_0FC2 (28579-28604)
	const uint16_t uwAtt = uwFlag != 0 ? 16 : 0;
	for(uint8_t c = 0; c < kSynthVoices; ++c) s.aVoice[c].uwFade = uwAtt;
}

}  // namespace ms
