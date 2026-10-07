// engine/anim - see anim.hpp. Transcribed from program.asm LAB_01F1 (interpreter), LAB_0201 (end of frame),
// LAB_0215..LAB_0241 (opcode handlers), LAB_020B, LAB_024F, LAB_0015/LAB_0016 and LAB_003C.
#include "engine/anim.hpp"

namespace ms {

namespace {

// Byte loop on purpose: no libc in the engine, and GCC must not turn it into a memset call.
#if defined(__GNUC__) && !defined(__clang__)
#define MS_NO_LIBCALLS __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define MS_NO_LIBCALLS
#endif

MS_NO_LIBCALLS void zero(void *pDst, uint32_t n) {
	uint8_t *p = (uint8_t *)pDst;
	while(n--) {
		*p++ = 0;
	}
}

// Game memory (script bytes, cel headers, draw lists) is big-endian bytes whatever the CPU is.
inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }
inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
inline void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)(v >> 16)); wr16(p + 2, (uint16_t)v); }

inline const uint8_t *at(uint32_t addr) { return jobPtr<const uint8_t>(addr); }

// LAB_020B: reads the frame record (10 bytes at +10 of the cel table, width @+4, height @+6, hot/flag byte @+8) into
// the job and lets the cel renderer prepare the cel when its mirror state differs from the job's.
void loadFrame(const AnimEnv &env, Job *pJob, uint32_t pCelTab, uint8_t frame) {
	const uint8_t *pRec = at(pCelTab) + 10 + (uint16_t)(frame * 10);
	pJob->w = rd16(pRec + 4);
	pJob->h = rd16(pRec + 6);
	const uint8_t want = (pRec[8] == 1) ? 1 : 3;
	if(want != pJob->flags) {
		env.prepCel(pCelTab, frame);
	}
}

// Opcodes $B4: the two ways to call out of a script.
void opCall(const AnimEnv &env, Job *pJob, const uint8_t *s) {
	if(s[1]) {
		// LAB_022F, selector != 0: routine from the call table, D0 = (selector - 1) * 4
		const uint32_t fn = env.pCallTable[(uint8_t)(s[1] - 1)];
		env.callTable(fn, pJob, jobAddr(s), (uint16_t)((uint8_t)(s[1] - 1) << 2));
	} else {
		env.callDirect(rd32(s + 2), pJob->x, pJob->y, pJob->z, pJob->flags, pJob->pFrames, pJob->id);
	}
	pJob->pScript += 6;
}

// Opcodes $CC / $D0: test the byte ($01), word ($02) or long (else) at id + offset; jump when it is zero ($CC) or
// non-zero ($D0), else skip the 8 bytes.
void opCondJump(Job *pJob, const uint8_t *s, bool jumpIfNonZero) {
	const uint8_t *pVar = at(pJob->id) + (int16_t)rd16(s + 2);
	bool nonZero;
	if(s[1] & 1) {
		nonZero = pVar[0] != 0;
	} else if(s[1] & 2) {
		nonZero = rd16(pVar) != 0;
	} else {
		nonZero = rd32(pVar) != 0;
	}
	if(nonZero == jumpIfNonZero) {
		pJob->pScript = rd32(s + 4);
	} else {
		pJob->pScript += 8;
	}
}

// Opcode $A0 (LAB_0222): set or move x/y/z. Bit 6 of the flag byte = absolute. Otherwise a signed step with the
// direction bits: x bit0 (inverted while the job's flags are exactly 3), y bit3, z bit5 = subtract.
void opMove(Job *pJob, const uint8_t *s) {
	if(s[1] & 0x40) {
		pJob->x = rd16(s + 2);
		pJob->y = rd16(s + 4);
		pJob->z = rd16(s + 6);
	} else {
		const uint16_t dx = rd16(s + 2);
		const bool xAdd = (pJob->flags == 3) ? !(s[1] & 1) : (s[1] & 1);
		pJob->x = (uint16_t)(xAdd ? pJob->x + dx : pJob->x - dx);
		const uint16_t dy = rd16(s + 4);
		pJob->y = (uint16_t)((s[1] & 8) ? pJob->y - dy : pJob->y + dy);
		const uint16_t dz = rd16(s + 6);
		pJob->z = (uint16_t)((s[1] & 0x20) ? pJob->z - dz : pJob->z + dz);
	}
	pJob->pScript += 8;
}

// Opcode $88 (LAB_021A): loop begin. A zero count is a random 1..31 from the tick counter.
void opLoopBegin(const AnimEnv &env, Job *pJob, JobState *pSt, const uint8_t *s) {
	if(s[1] == 0) {
		uint8_t n = (uint8_t)(*env.pRandom & 0x1f);
		if(n == 0) {
			n = 1;
		}
		pSt->loopCount = n;
	} else {
		pSt->loopCount = s[1];
	}
	pSt->loopActive = 1;
	pJob->pScript += 2;
	pSt->pLoop = pJob->pScript;
}

// LAB_0201: an $FF marker. Loop bookkeeping decides where the script goes next; the frame is over either way.
void endOfFrame(Job *pJob, JobState *pSt, const uint8_t *s) {
	if(pSt->loopActive) {
		if(--pSt->loopCount != 0) {
			pJob->pScript = pSt->pLoop;
			return;
		}
	}
	pSt->loopActive = 0;
	if(pSt->hold) {
		if((int8_t)(--pSt->holdCount) >= 0) {
			return;  // LAB_022E is a bare RTS
		}
	} else {
		pSt->hold = 0;
		if(pSt->moveFlag) {
			// MOVE.W D1,6(A1) / D2,10(A1) / D3,8(A1): D1 and D2 are 0 here (reset before every pass); D3 is whatever
			// the cel renderer left (unknowable, taken as 0). Nothing in the game sets moveFlag; kept for fidelity.
			pJob->x = 0;
			pJob->z = 0;
			pJob->y = 0;
			if((uint16_t)jobAddr(pSt) == 0) {
				return;
			}
		}
	}
	pSt->moveFlag = 0;
	if(pSt->deferred) {
		pSt->deferred = 0;
		pJob->pScript = pSt->pDeferred;
		return;
	}
	const uint8_t next = s[1];
	if(next != 0xFF) {
		if(next == 0xFE) {
			if(pSt->loop2Active) {
				if(--pSt->loop2Count != 0) {
					pJob->pScript = pSt->pLoop2;
					return;
				}
			}
			pSt->loop2Active = 0;
		}
		pJob->pScript += 2;
		return;
	}
	// $FF $FF: end of the script unless loop2 still runs
	if(pSt->loop2Active) {
		if(--pSt->loop2Count != 0) {
			pJob->pScript = pSt->pLoop2;
			return;
		}
	}
	pSt->loop2Active = 0;
	pJob->hasScript = 0;
}

}  // namespace

void animBoxAdd(const AnimEnv &env, int16_t x, int16_t w, int16_t y, int16_t h) {
	if(!*env.pBoxArmed) {
		*env.pBoxMinX = (uint16_t)x;
		*env.pBoxMaxX = (uint16_t)x;
		*env.pBoxMinY = (uint16_t)y;
		*env.pBoxMaxY = (uint16_t)y;
		*env.pBoxArmed = 1;
	}
	if(!(x > (int16_t)*env.pBoxMinX)) {
		*env.pBoxMinX = (uint16_t)x;
	}
	const int16_t xe = (int16_t)(uint16_t)(x + w);
	if(!(xe < (int16_t)*env.pBoxMaxX)) {
		*env.pBoxMaxX = (uint16_t)xe;
	}
	if(!(y > (int16_t)*env.pBoxMinY)) {
		*env.pBoxMinY = (uint16_t)y;
	}
	const int16_t ye = (int16_t)(uint16_t)(y + h);
	if(!(ye < (int16_t)*env.pBoxMaxY)) {
		*env.pBoxMaxY = (uint16_t)ye;
	}
}

void animRun(const AnimEnv &env, Job *pJob) {
	JobState *pSt = jobPtr<JobState>(pJob->pState);
	while(pJob->pScript != 0) {  // LAB_01F2
		for(;;) {                // LAB_01F3
			const uint8_t *s = at(pJob->pScript);
			const uint8_t op = s[0];
			if(op == 0xFF) {
				endOfFrame(pJob, pSt, s);
				return;
			}
			if(op == 0xFD) {
				pJob->pScript = pSt->pRestart;
				continue;
			}
			if(op == 0xFE) {
				pJob->pScript = pSt->pLoop2;
				continue;
			}
			if(op & 0x80) {
				switch(op) {
					case 0x80:  // LAB_0215
						if(s[1] == 0xFF) {
							pJob->flags ^= 2;
						} else {
							pJob->flags = s[1];
						}
						pJob->pScript += 2;
						break;
					case 0x84:  // LAB_0218
						if(s[1] == 3) {
							pJob->pScript = rd32(s + 2);
						} else {
							pSt->pDeferred = rd32(s + 2);
							pSt->deferred = 1;
							pJob->pScript += 6;
						}
						break;
					case 0x88:  // LAB_021A
						opLoopBegin(env, pJob, pSt, s);
						break;
					case 0x8C:  // LAB_021E
						pJob->pScript += 8;
						break;
					case 0x94:  // LAB_021F
						pSt->loop2Count = s[1];
						pSt->loop2Active = 1;
						pJob->pScript += 2;
						pSt->pLoop2 = pJob->pScript;
						break;
					case 0xA0:  // LAB_0222
						opMove(pJob, s);
						break;
					case 0xA4:  // LAB_0221
						pJob->pScript += 4;
						break;
					case 0xB4:  // LAB_022F
						opCall(env, pJob, s);
						break;
					case 0xB8:  // LAB_0233
					case 0xBC:  // LAB_0234
					case 0xC8:  // LAB_0232
						pJob->pScript += 6;
						break;
					case 0xC0:  // LAB_0235: frees the slot; the pass over the script goes on
						pJob->active = 0;
						pJob->hasScript = 0;
						pJob->pScript += 2;
						break;
					case 0xC4:  // LAB_0236
						pJob->pFrames = env.pFrameSets[(uint16_t)(s[1] - 1)];
						pJob->pScript += 2;
						break;
					case 0xCC:  // LAB_0237
						opCondJump(pJob, s, false);
						break;
					case 0xD0:  // LAB_023B
						opCondJump(pJob, s, true);
						break;
					case 0xD4:  // LAB_023F
						zero(pSt, sizeof(JobState));
						pJob->pScript += 2;
						break;
					default:    // empty table slot or bare RTS: spins in the original
						return;
				}
				continue;
			}

			// Draw instruction (LAB_01F7)
			const uint32_t pCelTab = rd32(at(pJob->pFrames) + (op & 0x1f));
			const uint8_t frame = s[1];
			pJob->frame = frame;
			loadFrame(env, pJob, pCelTab, frame);
			uint16_t x, y;
			const uint16_t dy = (uint16_t)(int16_t)(int8_t)s[2];
			if(!(pJob->flags & 2)) {
				x = (uint16_t)(rd16(s + 4) + pJob->x);
				y = (uint16_t)(dy + pJob->y + pJob->z);
			} else {
				x = (uint16_t)(pJob->x - rd16(s + 4) - pJob->w);
				y = (uint16_t)(dy + pJob->y + pJob->z);
			}
			pJob->sx = x;
			pJob->sy = y;
			if(!(s[3] & 0x40)) {
				animBoxAdd(env, (int16_t)x, (int16_t)pJob->w, (int16_t)y, (int16_t)pJob->h);
			}
			const uint8_t d3 = s[3];
			pJob->flagsA = d3;
			if(d3 & 0x10) {
				env.setTarget(*env.pBufShown);
				env.drawCel(pCelTab, frame, x, y);
				env.setTarget(*env.pBufWork);
			} else {
				uint8_t *p = jobPtr<uint8_t>(*env.pRectEnd);
				wr16(p, pJob->sx);
				wr16(p + 2, pJob->sy);
				wr16(p + 4, pJob->w);
				wr16(p + 6, pJob->h);
				wr16(p + 12, 0xFFFF);
				*env.pRectEnd += 8;
			}
			if(d3 & 2) {
				uint8_t *p = jobPtr<uint8_t>(*env.pListB);
				wr32(p, pCelTab);
				wr16(p + 4, frame);
				wr16(p + 6, x);
				wr16(p + 8, y);
				wr32(p + 10, 0);
				*env.pListB += 10;
			}
			if(d3 & 1) {
				uint8_t *p = jobPtr<uint8_t>(*env.pListA);
				wr32(p, pCelTab);
				wr16(p + 4, frame);
				wr16(p + 6, x);
				wr16(p + 8, y);
				wr32(p + 10, 0);
				*env.pListA += 10;
			}
			*env.pCpuMask = (d3 & 0x20) ? 1 : 0;
			env.drawCel(pCelTab, frame, x, y);
			pJob->pScript += 6;
			break;  // JMP LAB_01F2
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------

bool animSpawn(const AnimSpawnEnv &env, AnimKind kind, uint32_t pScript, uint32_t id) {
	JobInit init;
	init.pScript = pScript;
	init.id = id;
	init.pFrames = env.pFrames;
	init.x = (kind == AnimKindA) ? 160 : 120;
	init.y = 0;
	init.z = (uint16_t)(100 + *env.pZ[kind]);
	init.flags = (kind == AnimKindA) ? 1 : 3;
	init.handler = 0;
	return jobAlloc(env.jobs, init) != nullptr;
}

void animOverlay(const AnimEnv &env, uint32_t pCels, uint16_t phase) {
	// MOVEQ #0,D2 ; MOVE.B #k,D2 ; ADDI.B #$64,D2 wraps in the byte; x = (k - $A0 as word) + $A0 wraps in the word.
	struct Cel {
		uint16_t frame, x, y;
	};
	static const Cel s_first[] = {{0, 0, 0x38}, {3, 0x33, 0x86}};
	static const Cel s_second[] = {{1, 0x9D, 0x68}, {2, 0x129, 0x52}};
	*env.pCpuMask = 1;
	for(const Cel &c : s_first) {
		env.drawCel(pCels, c.frame, c.x, c.y);
	}
	if(phase != 2) {
		for(const Cel &c : s_second) {
			env.drawCel(pCels, c.frame, c.x, c.y);
		}
	}
	*env.pCpuMask = 0;
}

// The intro and ending scenes moved to engine/scenes.cpp (ROADMAP 7.1i).

}  // namespace ms
