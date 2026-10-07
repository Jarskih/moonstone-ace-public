// game/combat_script - see combat_script.hpp.  Transcribed from mog.asm LAB_0328 (driver), LAB_0351 (sort),
// LAB_032E..LAB_034C (interpreter, draw, end of frame), LAB_034E (frame record) and the handlers LAB_0358..LAB_039C.
// Labels in the comments are mog's.
#include "game/combat_script.hpp"

namespace ms { namespace game {

namespace {

// Byte loops on purpose: no libc in the engine, and GCC must not turn them into memcpy/memset calls.
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

MS_NO_LIBCALLS void copy(void *pDst, const void *pSrc, uint32_t n) {
	uint8_t *d = (uint8_t *)pDst;
	const uint8_t *s = (const uint8_t *)pSrc;
	while(n--) {
		*d++ = *s++;
	}
}

// Script bytes, cel headers and draw lists are big-endian game memory whatever the CPU is; the job, the work block
// and the owner record are native structs (the same thing on the 68k).
inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }
inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
inline void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)(v >> 16)); wr16(p + 2, (uint16_t)v); }

inline const uint8_t *at(uint32_t addr) { return jobPtr<const uint8_t>(addr); }
inline CombatWork *workOf(const CombatJob *pJob) { return jobPtr<CombatWork>(pJob->ulWork); }

// Native word / long at owner + offset (the owner is a Knight-layout record; the engine also pokes arbitrary offsets).
inline void ownerSet16(Knight *pOwner, uint32_t off, uint16_t v) { __builtin_memcpy((uint8_t *)pOwner + off, &v, 2); }

// LAB_039C: clears the work block but keeps the spawn flag (+18) and the spawn script (+20).
void resetWork(CombatJob *pJob) {
	CombatWork *pW = workOf(pJob);
	const uint8_t armed = pW->ubSpawnArmed;
	const uint32_t spawn = pW->ulSpawn;
	zero(pW, sizeof(CombatWork));
	pW->ubSpawnArmed = armed;
	pW->ulSpawn = spawn;
}

// LAB_034C: the owner follows the job.
void writeBack(const CombatEnv &env, CombatJob *pJob) {
	Knight *pOwner = jobPtr<Knight>(pJob->ulOwner);
	pOwner->uwX = pJob->uwX;
	pOwner->uwHeight = pJob->uwY;
	pOwner->uwY = pJob->uwZ;
	pOwner->ubFacing = pJob->ubFlags;
	ownerSet16(pOwner, 58, *env.pBoxMinX);
	ownerSet16(pOwner, 60, *env.pBoxMaxX);
	ownerSet16(pOwner, 112, *env.pBoxMinY);
	ownerSet16(pOwner, 114, *env.pBoxMaxY);
}

// LAB_03B8: grows the bounding box of the job's draws (signed word compares, 16-bit adds).
void boxAdd(const CombatEnv &env, int16_t x, int16_t w, int16_t y, int16_t h) {
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

// LAB_034E: frame record (10 bytes at +10 of the cel table: width @+4, height @+6, cel flag @+8) -> job; asks the
// cel preparer when the mirror state wanted by the cel differs from the job's flags byte.
void loadFrame(const CombatEnv &env, CombatJob *pJob, uint32_t pCelTab, uint8_t frame) {
	const uint8_t *pRec = at(pCelTab) + 10 + (uint16_t)(frame * 10);
	pJob->uwW = rd16(pRec + 4);
	pJob->uwH = rd16(pRec + 6);
	const uint8_t want = (pRec[8] == 1) ? 1 : 3;
	if(want != pJob->ubFlags) {
		env.prepCel(pCelTab, frame);
	}
}

// LAB_0315: the first job whose owner is `owner` (the whole table is searched, inactive slots included), or null.
CombatJob *findJob(const CombatEnv &env, uint32_t owner) {
	for(uint32_t i = 0; i < COMBAT_JOB_COUNT; ++i) {
		if(env.pJobs[i].ulOwner == owner) {
			return &env.pJobs[i];
		}
	}
	return nullptr;
}

// ---- motion stepper (LAB_0378..LAB_0389) -----------------------------------------------------------------------
// D0 is a live 32-bit register across the helpers (LAB_0382 leaves the new y in its low word, and LAB_0378 then only
// replaces the low BYTE with the x speed): a negative y therefore adds $FFxx instead of $00xx to x.  Kept.
struct Motion {
	CombatJob *pJob;
	CombatWork *pW;
	uint32_t d0;
	bool moved;   // LAB_037F
};

// LAB_0380: rise (y -= d0.w), speed halves while above the limit unless bit 5 locks it.
void motionRise(Motion &m) {
	m.moved = true;
	m.pJob->uwY = (uint16_t)(m.pJob->uwY - (uint16_t)m.d0);
	if(!(m.pW->ubMotionFlags & 0x20)) {
		if((int8_t)(uint8_t)m.d0 > (int8_t)m.pW->ubVyLimit) {   // CMP.B 29(A5),D0 ; BLE skips
			m.d0 = (m.d0 & 0xFFFFFF00u) | (uint8_t)((uint8_t)m.d0 >> 1);
			m.pW->ubVy = (uint8_t)m.d0;
		}
	}
}

// LAB_0382: fall (y += d0.w) until y reaches 0; the speed doubles while y is still negative.
void motionFall(Motion &m) {
	m.moved = true;
	m.pJob->uwY = (uint16_t)(m.pJob->uwY + (uint16_t)m.d0);
	if((int16_t)m.pJob->uwY >= 0) {   // BPL: LAB_0384 lands
		m.pJob->uwY = 0;
		m.pW->ubMotionFlags &= (uint8_t)~0x02;
		return;
	}
	m.d0 = (m.d0 & 0xFFFF0000u) | m.pJob->uwY;   // MOVE.W 8(A1),D0
	if(!(m.pW->ubMotionFlags & 0x20)) {
		if(!((int8_t)(uint8_t)m.d0 >= (int8_t)m.pW->ubVyLimit)) {   // CMP.B 29(A5),D0 ; BGE skips
			m.d0 = (m.d0 & 0xFFFFFF00u) | (uint8_t)((uint8_t)m.d0 << 1);
			m.pW->ubVy = (uint8_t)m.d0;
		}
	}
}

// LAB_0387 (x -= d0.w) and LAB_0389 (x += d0.w): the speed halves while above the limit unless bit 7 locks it.
void motionX(Motion &m, bool subtract) {
	m.moved = true;
	m.pJob->uwX = (uint16_t)(subtract ? m.pJob->uwX - (uint16_t)m.d0 : m.pJob->uwX + (uint16_t)m.d0);
	if(!(m.pW->ubMotionFlags & 0x80)) {
		if((int8_t)(uint8_t)m.d0 > (int8_t)m.pW->ubVxLimit) {   // CMP.B 31(A5),D0 ; BLE skips
			m.d0 = (m.d0 & 0xFFFFFF00u) | (uint8_t)((uint8_t)m.d0 >> 1);
			m.pW->ubVx = (uint8_t)m.d0;
		}
	}
}

}  // namespace

void combatMotionStep(CombatJob *pJob, CombatWork *pW) {
	Motion m = {pJob, pW, pW->ubVy, false};   // MOVEQ #0,D0 ; MOVE.B 28(A5),D0
	if(pW->ubMotionFlags & 0x02) {
		motionFall(m);
	} else if(pW->ubMotionFlags & 0x01) {
		motionRise(m);
	}
	m.d0 = (m.d0 & 0xFFFFFF00u) | pW->ubVx;   // MOVE.B 30(A5),D0
	if(pW->ubMotionFlags & 0x10) {            // LAB_0385: bit 1 of the job flags set -> left, else right
		motionX(m, (pJob->ubFlags & 2) != 0);
	}
	if(pW->ubMotionFlags & 0x04) {            // LAB_0386: the mirror image
		motionX(m, (pJob->ubFlags & 2) == 0);
	}
	if(!(pW->ubMotionFlags & 0x40)) {
		pJob->ulScript = pW->ulReturn;
	}
	if(!m.moved) {
		pW->ubMotionActive = 0;
	}
}

namespace {

// LAB_0341: an $FF marker.  Every path ends in the write-back LAB_034C.
void endOfFrame(const CombatEnv &env, CombatJob *pJob, const uint8_t *s) {
	CombatWork *pW = workOf(pJob);
	do {
		if(pW->ubLoopActive) {
			if(--pW->ubLoopCount != 0) {
				pJob->ulScript = pW->ulLoop;
				break;
			}
		}
		pW->ubLoopActive = 0;                                   // LAB_0342
		if(pW->ubMotionActive) {
			--pW->ubMotionCount;
			if((int8_t)pW->ubMotionCount >= 0) {
				combatMotionStep(pJob, pW);                     // LAB_0378; the script may have been moved
				break;
			}
		} else {
			pW->ubMotionActive = 0;                             // LAB_0343
		}
		if(pW->ubCallArmed) {                                   // LAB_0344
			pW->ubCallArmed = 0;
			pJob->ulScript = pW->ulCall;
			break;
		}
		const uint8_t next = s[1];                              // LAB_0347
		if(next == raw(ScriptOp::EndOfFrame)) {                                      // LAB_034A: end of the script unless loop2 still runs
			if(pW->ubLoop2Active) {
				if(--pW->ubLoop2Count != 0) {
					pJob->ulScript = pW->ulLoop2;
					break;
				}
			}
			pW->ubLoop2Active = 0;                              // LAB_034B
			pJob->ubRunning = 0;
			break;
		}
		if(next == raw(ScriptOp::Loop2Jump)) {
			if(pW->ubLoop2Active) {
				if(--pW->ubLoop2Count != 0) {
					pJob->ulScript = pW->ulLoop2;
					break;
				}
			}
			pW->ubLoop2Active = 0;                              // LAB_0348
		}
		pJob->ulScript += 2;                                    // LAB_0349
	} while(0);
	writeBack(env, pJob);
}

// One draw event, LAB_0334..LAB_033E.  The caller advances the script by 6.
void drawEvent(const CombatEnv &env, CombatJob *pJob, const uint8_t *s) {
	const uint32_t pCelTab = rd32(at(pJob->ulFrames) + (s[0] & 0x1f));
	const uint8_t frame = s[1];
	pJob->ubFrame = frame;
	loadFrame(env, pJob, pCelTab, frame);
	const uint16_t dy = (uint16_t)(int16_t)(int8_t)s[2];
	uint16_t x;
	if(!(pJob->ubFlags & 2)) {
		x = (uint16_t)(rd16(s + 4) + pJob->uwX);
	} else {
		x = (uint16_t)(pJob->uwX - rd16(s + 4) - pJob->uwW);
	}
	const uint16_t y = (uint16_t)(dy + pJob->uwY + pJob->uwZ);
	pJob->uwSx = x;
	pJob->uwSy = y;
	const uint8_t flags = s[3];
	if(!(flags & 0x40)) {
		boxAdd(env, (int16_t)x, (int16_t)pJob->uwW, (int16_t)y, (int16_t)pJob->uwH);
	}
	if(*env.pDemoFlag != 0 && (flags & 0x80)) {
		return;   // LAB_033F: nothing is drawn or listed
	}
	*env.pTextFlag = (flags & 0x20) ? 1 : 0;
	if(flags & 0x10) {
		env.setTarget(*env.pBackground);
		env.waitBlitter();
		env.drawCel(pCelTab, frame, x, y);
		env.setTarget(*env.pScreen);
	} else {
		// LAB_033A: dirty-rect list, capped at 45 entries (the count keeps growing past it)
		uint8_t *p = jobPtr<uint8_t>(*env.pRectList);
		++*env.pRectCount;
		if((int16_t)*env.pRectCount <= (int16_t)COMBAT_RECT_MAX) {
			wr16(p, pJob->uwSx);
			wr16(p + 2, pJob->uwSy);
			wr16(p + 4, pJob->uwW);
			wr16(p + 6, pJob->uwH);
			wr16(p + 12, 0xFFFF);
			*env.pRectList += 8;
		}
	}
	if(flags & 2) {
		uint8_t *p = jobPtr<uint8_t>(*env.pAttackList);
		wr32(p, pCelTab);
		wr16(p + 4, frame);
		wr16(p + 6, x);
		wr16(p + 8, y);
		wr32(p + 10, 0);
		*env.pAttackList += 10;
	}
	if(flags & 1) {
		uint8_t *p = jobPtr<uint8_t>(*env.pHurtList);
		wr32(p, pCelTab);
		wr16(p + 4, frame);
		wr16(p + 6, x);
		wr16(p + 8, y);
		wr32(p + 10, 0);
		*env.pHurtList += 10;
	}
	env.waitBlitter();
	env.drawCel(pCelTab, frame, x, y);
}

// $C0/$C8-style tests on the owner: byte ($01), word ($02) or long (else) at owner + signed word offset.
bool ownerNonZero(const Knight *pOwner, const uint8_t *s) {
	const uint8_t *p = (const uint8_t *)pOwner + (int16_t)rd16(s + 2);
	if(s[1] & 1) {
		return p[0] != 0;
	}
	if(s[1] & 2) {
		uint16_t v;
		__builtin_memcpy(&v, p, 2);
		return v != 0;
	}
	uint32_t v;
	__builtin_memcpy(&v, p, 4);
	return v != 0;
}

}  // namespace

void combatRunJob(const CombatEnv &env, CombatJob *pJob) {
	for(;;) {                                    // LAB_032F
		if(pJob->ulScript == 0) {
			return;                              // LAB_034D: no write-back
		}
		for(;;) {                                // LAB_0330
			const uint8_t *s = at(pJob->ulScript);
			const uint8_t op = s[0];
			CombatWork *pW = workOf(pJob);
			if(op == raw(ScriptOp::EndOfFrame)) {
				endOfFrame(env, pJob, s);
				return;
			}
			if(op == raw(ScriptOp::Return) || op == raw(ScriptOp::Loop2Jump)) {
				pJob->ulScript = (op == raw(ScriptOp::Return)) ? pW->ulReturn : pW->ulLoop2;
				if(pJob->ulScript == 0) {
					return;                      // the original reads whatever is at address 0
				}
				continue;
			}
			if(!(op & 0x80)) {
				drawEvent(env, pJob, s);
				pJob->ulScript += 6;
				break;                           // JMP LAB_032F
			}
			switch(op) {
				case raw(ScriptOp::SetFacing):                       // LAB_0358: set / toggle the facing
					if(s[1] == 0xFF) {
						pJob->ubFlags ^= 2;
					} else {
						pJob->ubFlags = s[1];
					}
					jobPtr<Knight>(pJob->ulOwner)->ubFacing = pJob->ubFlags;   // LAB_035A: owner +10 now, LAB_034C again
					pJob->ulScript += 2;
					break;
				case raw(ScriptOp::Call):                       // LAB_035B
					if(s[1] == 3) {
						pJob->ulScript = rd32(s + 2);
					} else {
						pW->ulCall = rd32(s + 2);
						pW->ubCallArmed = 1;
						pJob->ulScript += 6;
					}
					break;
				case raw(ScriptOp::LoopBegin): {                     // LAB_035D
					if(s[1] == 0) {
						uint8_t n = (uint8_t)(*env.pRandom & 0x1f);
						pW->ubLoopCount = n ? n : 1;
					} else {
						pW->ubLoopCount = s[1];
					}
					pW->ubLoopActive = 1;
					pJob->ulScript += 2;
					pW->ulLoop = pJob->ulScript;
					break;
				}
				case raw(ScriptOp::MotionBlock):                       // LAB_0361: motion parameter block
					pW->ubMotionActive = 1;
					pW->ubParam24 = s[1];
					pW->ubMotionFlags = s[3];
					pW->ubMotionCount = s[2];
					pW->ubVy = s[4];
					pW->ubVyLimit = s[5];
					pW->ubVx = s[6];
					pW->ubVxLimit = s[7];
					pJob->ulScript += 8;
					pW->ulReturn = pJob->ulScript;
					break;
				case raw(ScriptOp::Loop2Begin):                       // LAB_0362
					pW->ubLoop2Count = s[1];
					pW->ubLoop2Active = 1;
					pJob->ulScript += 2;
					pW->ulLoop2 = pJob->ulScript;
					break;
				case raw(ScriptOp::JumpIfDemo):                       // LAB_0363
					if(*env.pDemoFlag != 0) {
						pJob->ulScript = rd32(s + 2);
					} else {
						pJob->ulScript += 6;
					}
					break;
				case raw(ScriptOp::MovePos):                       // LAB_0368: set or move x/y/z
					if(s[1] & 0x40) {
						pJob->uwX = rd16(s + 2);
						pJob->uwY = rd16(s + 4);
						pJob->uwZ = rd16(s + 6);
					} else {
						const uint16_t dx = rd16(s + 2);
						const bool xAdd = (pJob->ubFlags == 3) ? !(s[1] & 1) : (s[1] & 1);
						pJob->uwX = (uint16_t)(xAdd ? pJob->uwX + dx : pJob->uwX - dx);
						const uint16_t dy = rd16(s + 4);
						pJob->uwY = (uint16_t)((s[1] & 8) ? pJob->uwY - dy : pJob->uwY + dy);
						const uint16_t dz = rd16(s + 6);
						pJob->uwZ = (uint16_t)((s[1] & 0x20) ? pJob->uwZ - dz : pJob->uwZ + dz);
					}
					pJob->ulScript += 8;
					break;
				case raw(ScriptOp::Sound):                       // LAB_0367
					env.sound(s[1]);
					pJob->ulScript += 2;
					break;
				case raw(ScriptOp::PokeOwner): {                     // LAB_0374: poke the owner at a signed word offset
					uint8_t *p = jobPtr<uint8_t>(pJob->ulOwner) + (int16_t)rd16(s + 2);
					const uint32_t v = rd32(s + 4);
					if(s[1] & 1) {
						*p = (uint8_t)v;
					} else if(s[1] & 2) {
						const uint16_t w = (uint16_t)v;
						__builtin_memcpy(p, &w, 2);
					} else {
						__builtin_memcpy(p, &v, 4);
					}
					pJob->ulScript += 8;
					break;
				}
				case raw(ScriptOp::SpawnArm):                       // LAB_0372
					pW->ulSpawn = rd32(s + 2);
					pW->ubSpawnArmed = s[1] ? 1 : 0;
					pJob->ulScript += 6;
					break;
				case raw(ScriptOp::CallRoutine):                       // LAB_038B
					env.callRoutine(rd32(s + 2), pJob->ulOwner, pJob->ulFrames, pJob->uwX, pJob->uwY, pJob->uwZ,
						pJob->ubFlags);
					pJob->ulScript += 6;
					break;
				case raw(ScriptOp::JumpIfOwnerDead):                       // LAB_038E: jump and reset when the owner is dead
					if(jobPtr<Knight>(pJob->ulOwner)->swHp > 0) {
						pJob->ulScript += 6;
					} else {
						pJob->ulScript = rd32(s + 2);
						resetWork(pJob);
					}
					break;
				case raw(ScriptOp::SpawnJob):                       // LAB_0390
					env.spawn(rd32(s + 2), pJob->ulFrames, pJob->uwX, pJob->uwY, pJob->uwZ, pJob->ubFlags,
						pJob->ulOwner);
					pJob->ulScript += 6;
					break;
				case raw(ScriptOp::KillJob):                       // LAB_0391: kill the job and clear the owner's first long
					*jobPtr<uint32_t>(pJob->ulOwner) = 0;
					pJob->ubActive = 0;
					pJob->ubRunning = 0;
					pJob->ulScript += 2;
					break;
				case raw(ScriptOp::FrameList): {                     // LAB_0392: select a frame-table list
					const uint16_t ofs = (uint16_t)((uint16_t)(s[1] - 1) << 2);
					pJob->ulFrames = env.pFrameSets[(int16_t)ofs / 4];
					pJob->ulScript += 2;
					jobPtr<Knight>(pJob->ulOwner)->ulJobParam = pJob->ulFrames;
					break;
				}
				case raw(ScriptOp::JumpIfSameFacing): {                     // LAB_038C: jump when the current knight's job faces the same way
					const CombatJob *pOther = findJob(env, *env.pCurrentKnight);
					if(pOther && pJob->ubFlags == pOther->ubFlags) {
						pJob->ulScript = rd32(s + 2);
					} else {
						pJob->ulScript += 6;
					}
					break;
				}
				case raw(ScriptOp::JumpIfZero):                       // LAB_0393: jump if the owner value is zero
					if(!ownerNonZero(jobPtr<Knight>(pJob->ulOwner), s)) {
						pJob->ulScript = rd32(s + 4);
					} else {
						pJob->ulScript += 8;
					}
					break;
				case raw(ScriptOp::JumpIfNonZero):                       // LAB_0397: jump if non-zero
					if(ownerNonZero(jobPtr<Knight>(pJob->ulOwner), s)) {
						pJob->ulScript = rd32(s + 4);
					} else {
						pJob->ulScript += 8;
					}
					break;
				case raw(ScriptOp::ResetWork):                       // LAB_039B
					resetWork(pJob);
					pJob->ulScript += 2;
					break;
				default:                         // empty table slot or the bare RTS $9C: spins in the original
					return;
			}
		}
	}
}

void combatSortJobs(const CombatEnv &env) {
	bool swapped;
	do {                                         // LAB_0352
		swapped = false;
		for(uint32_t i = 0; i + 1 < COMBAT_JOB_COUNT; ++i) {
			CombatJob *pA = &env.pJobs[i];
			if(pA[1].uwZ < pA->uwZ) {            // LAB_0353: CMP.W ; BCC = no swap when next >= this (unsigned)
				copy(env.pScratchJob, pA, sizeof(CombatJob));
				copy(pA, pA + 1, sizeof(CombatJob));
				copy(pA + 1, env.pScratchJob, sizeof(CombatJob));
				swapped = true;
			}
		}
	} while(swapped);
}

void combatTick(const CombatEnv &env) {
	combatSortJobs(env);
	// First pass: a job with a second script armed ($AC) runs it on a scratch copy of itself, y forced to 0.
	*env.pJobIndex = 0;
	for(CombatJob *pJob = env.pJobs; *env.pJobIndex != COMBAT_JOB_COUNT; ++pJob, ++*env.pJobIndex) {
		*env.pCurJob = jobAddr(pJob);
		if(!pJob->ubActive || pJob->uwPaused) {
			continue;
		}
		const CombatWork *pW = workOf(pJob);
		if(!pW->ubSpawnArmed) {
			continue;
		}
		const uint32_t ulSpawnScript = pW->ulSpawn;
		copy(env.pScratchJob, pJob, sizeof(CombatJob));
		env.pScratchJob->uwY = 0;
		env.pScratchJob->ulScript = ulSpawnScript;
		env.pScratchJob->ulWork = jobAddr(env.pScratchWork);
		combatRunJob(env, env.pScratchJob);
	}
	// Second pass: the real jobs.
	*env.pJobIndex = 0;
	for(CombatJob *pJob = env.pJobs; *env.pJobIndex != COMBAT_JOB_COUNT; ++pJob, ++*env.pJobIndex) {
		*env.pCurJob = jobAddr(pJob);
		if(!pJob->ubActive || pJob->uwPaused) {
			continue;
		}
		*env.pHurtList = pJob->ulHurtList;
		*env.pAttackList = pJob->ulAttackList;
		*env.pBoxArmed = 0;
		*env.pBoxMinX = 0;
		*env.pBoxMaxX = 0;
		*env.pBoxMinY = 0;
		*env.pBoxMaxY = 0;
		combatRunJob(env, pJob);
	}
}

}}  // namespace ms::game
