// game/creatures - see creatures.hpp.  Transcribed from mog.asm: the contact family LAB_03A9..LAB_03DB, damage
// LAB_021B / LAB_0204, dagger flight LAB_02D3 / LAB_02F6 / LAB_02FD / LAB_02CA, creature plumbing LAB_0161 / LAB_0171 /
// LAB_02D0 / LAB_0310 / LAB_0322 and the predicates LAB_02BC / LAB_02BF / LAB_02C2 / LAB_02C4.  Labels in the comments
// are mog's.  Every arithmetic step is the 16-bit word operation of the asm (casts to uint16_t), every branch the
// condition the 68k evaluates (BMI / BPL = N only, BGE / BLT / BGT = N xor V, i.e. the true signed compare).
#include "game/creatures.hpp"

namespace ms { namespace game {

namespace {

// Big-endian game bytes (cel tables, hit records, draw lists, the hit-set pairs).
inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }

inline const uint8_t *at(uint32_t addr) { return jobPtr<const uint8_t>(addr); }

// The record words the Knight struct only has as raw bytes (the engine's bounding box, +58 / +60 / +112 / +114).
inline uint16_t rec16(const Knight &k, uint32_t off) {
	uint16_t v;
	__builtin_memcpy(&v, (const uint8_t *)&k + off, 2);
	return v;
}

// NEG.W on a word that is negative per the N flag (BPL not taken): the asm's "make positive" idiom.
inline uint16_t absW(uint16_t v) { return (v & 0x8000u) ? (uint16_t)(0u - v) : v; }

// DIVS.W src,Dn / DIVU.W src,Dn: Dn = (remainder << 16) | quotient.  A quotient that does not fit 16 bits leaves Dn
// untouched (V set).  Returns false for a zero divisor (the 68k takes the divide exception).
bool divs(uint32_t &dn, uint16_t src) {
	const int32_t s = (int32_t)(int16_t)src;
	if(s == 0) {
		return false;
	}
	const int32_t d = (int32_t)dn;
	if(d == (int32_t)0x80000000 && s == -1) {
		return true;
	}
	const int32_t q = d / s;
	const int32_t rem = d % s;
	if(q > 32767 || q < -32768) {
		return true;
	}
	dn = ((uint32_t)rem << 16) | ((uint32_t)q & 0xFFFFu);
	return true;
}

bool divu(uint32_t &dn, uint16_t src) {
	if(src == 0) {
		return false;
	}
	const uint32_t q = dn / src;
	if(q > 0xFFFFu) {
		return true;
	}
	dn = ((dn % src) << 16) | q;
	return true;
}

inline uint32_t setW(uint32_t reg, uint16_t v) { return (reg & 0xFFFF0000u) | v; }
inline uint32_t extL(uint32_t reg) { return (uint32_t)(int32_t)(int16_t)(uint16_t)reg; }

// LAB_0A55: popcount of the plane mask 0..15 (the asm reads the table with the byte times two; bigger masks walk
// off the table into LAB_0A56 and the file name string, see contactTest).
unsigned popcount4(unsigned mask) {
	unsigned n = 0;
	while(mask) {
		n += mask & 1u;
		mask >>= 1;
	}
	return n;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Contact

// LAB_03CA.  D0/D1 = interval a, D2/D3 = interval b; CMP.L D0,D2 is D2 - D0.
bool contactOverlap(int32_t d0, int32_t d1, int32_t d2, int32_t d3) {
	if(((uint32_t)d2 - (uint32_t)d0) >> 31) {      // CMP.L D0,D2 / BMI.S LAB_03CC: N only
		return d3 >= d0;                           // LAB_03CC: CMP.L D0,D3 / BGE.S LAB_03CB
	}
	if(((uint32_t)d2 - (uint32_t)d1) >> 31) {      // CMP.L D1,D2 / BMI.S LAB_03CB
		return true;
	}
	if(((uint32_t)d3 - (uint32_t)d0) >> 31) {      // CMP.L D0,D3 / BMI.S LAB_03CD
		return false;
	}
	return !(d3 >= d1);                            // CMP.L D1,D3 / BGE.S LAB_03CD, else LAB_03CB
}

// LAB_03B3.
bool depthClose(uint16_t uwDepthA, uint16_t uwDepthB) {
	// D1 = 8(A1); D2 = 8(A0); SUB.W D1,D2; BPL; NEG.W D2; CMP.W #10,D2; BGT.
	const uint16_t d = absW((uint16_t)(uwDepthA - uwDepthB));
	return (int16_t)d <= 10;
}

// LAB_03DD / LAB_03DE.
uint32_t hitSetFind(const uint8_t *pPairs, uint32_t ulCount, uint32_t ulKey) {
	for(uint32_t i = 0; i < ulCount; ++i) {
		if(rd32(pPairs + 8 * i) == ulKey) {
			return rd32(pPairs + 8 * i + 4);
		}
	}
	return 0;
}

// LAB_03DF / LAB_03E0.
const uint8_t *hitRecordAt(const uint8_t *pSet, uint16_t uwIndex) {
	for(uint16_t n = uwIndex; n; --n) {
		const uint16_t twice = (uint16_t)(2u * *pSet++);   // MOVE.B (A1)+,D6 ; ADD.W D6,D6
		if(twice) {
			pSet += (uint16_t)(twice + 3u);                // ADDQ.W #3,D6 ; LEA 0(A1,D6.W),A1 (word index)
		}
	}
	return pSet;
}

// LAB_03DB.
bool contactTest(const uint8_t *pDefTab, uint16_t uwDefFrame, uint16_t uwDefX, uint16_t uwDefY,
                 const uint8_t *pAttTab, const uint8_t *pHitSet, uint16_t uwAttFrame, uint16_t uwAttX,
                 uint16_t uwAttY, ContactPoint *pPoint) {
	// LAB_0A54 = the attacker's mirror amount: its cel width, unless bit 0 of the entry's flag byte is set.
	// D6 = frame * 10 is used as a signed word index (MULU #10, then 18(A1,D6.W) / 14(A1,D6.W)).
	const int16_t attOfs = (int16_t)(uint16_t)(uwAttFrame * 10u);
	uint16_t uwMirror = 0;
	if(!(pAttTab[18 + attOfs] & 1u)) {
		uwMirror = rd16(pAttTab + 14 + attOfs);
	}

	const uint8_t *pRec = hitRecordAt(pHitSet, uwAttFrame);
	if(pRec[0] == 0) {                                    // LAB_03E1: a frame without contact points
		return false;
	}

	// A2 = 10(A0, 10 * frame): the defender's cel entry.
	const int16_t defOfs = (int16_t)(uint16_t)(uwDefFrame * 10u);
	const uint8_t *pCel = pDefTab + 10 + defOfs;
	const uint16_t uwCelW = rd16(pCel + 4);
	const uint16_t uwCelH = rd16(pCel + 6);

	// Coarse gates: x interval of the defender cel against the attacker's (mirrored) box, then y.
	const uint8_t ubMaxDx = pRec[2];
	const uint8_t ubMaxDy = pRec[3];
	uint16_t d2 = uwAttX;
	if(uwMirror) {
		d2 = (uint16_t)(d2 + uwMirror);
		d2 = (uint16_t)(d2 - ubMaxDx);
	}
	const uint16_t d3x = (uint16_t)(ubMaxDx + d2);
	if(!contactOverlap(uwDefX, (uint16_t)(uwDefX + uwCelW), d2, d3x)) {
		return false;
	}
	if(!contactOverlap(uwDefY, (uint16_t)(uwDefY + uwCelH), uwAttY, (uint16_t)(ubMaxDy + uwAttY))) {
		return false;
	}

	// Plane data of the defender cel and the row geometry.
	const uint8_t *pPlanes = at(rd32(pDefTab + 2)) + rd32(pCel);
	const uint16_t uwRowBytes = (uint16_t)(((uint16_t)(uwCelW + 15u) >> 4) << 1);   // ADDI.W #15 / LSR.W #4 / ADD.W
	const uint16_t uwStride = (uint16_t)(uwRowBytes * uwCelH);                      // MULU, MOVE.W D7,LAB_0A56
	// Planes in the mask: popcount table LAB_0A55 indexed by the low byte of the word at +8.  DEVIATION: the asm
	// indexes up to byte 255 * 2 (the table has 16 words, then LAB_0A56 and the string "collide.hit") and loops 65536
	// times for a zero count; no shipped cel has more than 4 planes in the mask. Masks up to 63 are counted so the
	// 6-plane art of MS_ENHANCED (plane bits 4-5, ROADMAP 4.8a) still collides; above that, no contact.
	const unsigned uPlaneMask = rd16(pCel + 8) & 0xFFu;
	if(uPlaneMask > 63) {
		return false;
	}
	const unsigned uPlanes = popcount4(uPlaneMask);
	if(uPlanes == 0) {
		return false;
	}

	const uint8_t *pPoint0 = pRec + 4;
	for(uint16_t i = 0; i < pRec[0]; ++i) {
		const uint16_t px = pPoint0[2 * i];
		const uint16_t py = pPoint0[2 * i + 1];

		// The window gate uses the mirrored x: NEG.W / ADD.W LAB_0A54, then + attacker x.
		uint16_t sx = px;
		if(uwMirror) {
			sx = (uint16_t)(0u - sx);
			sx = (uint16_t)(sx + uwMirror);
		}
		sx = (uint16_t)(sx + uwAttX);
		if((int16_t)sx < (int16_t)uwDefX) {                                 // CMP.W D1,D5 / BLT
			continue;
		}
		if((int16_t)(uint16_t)(sx - uwDefX) >= (int16_t)uwCelW) {           // SUB.W D1,D5; SUB.W 4(A2),D5 / BGE
			continue;
		}
		const uint16_t sy = (uint16_t)(py + uwAttY);
		if((int16_t)sy < (int16_t)uwDefY) {
			continue;
		}
		if((int16_t)(uint16_t)(sy - uwDefY) >= (int16_t)uwCelH) {
			continue;
		}

		// QUIRK: from here the RAW point bytes are used again, mirror or not: the reported point and the bit tested.
		const uint16_t cx = (uint16_t)(px + uwAttX);                        // MOVE.W D5,LAB_0A52
		const uint16_t cy = (uint16_t)(py + uwAttY);                        // MOVE.W D6,LAB_0A53
		const uint16_t xw = (uint16_t)(cx - uwDefX);
		const uint16_t yw = (uint16_t)(cy - uwDefY);
		// D0 = (xw >> 4) * 2 + yw * rowBytes, a word index (sign extended by 0(A4,D0.W)).
		const uint16_t uwOfs = (uint16_t)((uint16_t)((xw >> 4) << 1) + (uint16_t)((uint32_t)yw * uwRowBytes));
		const unsigned uBit = 15u - (xw & 15u);                             // ANDI.W #15 / EORI.W #15

		const uint8_t *pPlane = pPlanes;
		for(unsigned p = 0; p < uPlanes; ++p) {
			if(rd16(pPlane + (int16_t)uwOfs) & (1u << uBit)) {              // MOVE.W 0(A4,D0.W),D6 / BTST D5,D6
				if(pPoint) {
					pPoint->uwX = cx;
					pPoint->uwY = cy;
				}
				return true;
			}
			pPlane += (int16_t)uwStride;                                    // LEA 0(A4,D6.W),A4: LAB_0A56 as a signed word
		}
	}
	return false;
}

// LAB_0161.
void clearHitLinks(const ContactEnv &env) {
	env.pDragon->ulHitTarget = 0;
	env.pDragon->ulAttacker = 0;
	// MOVE.L #$14,D7 / DBF: 21 records although the heap holds 20 (LAB_0171 and LAB_02CE use 20).
	for(uint32_t i = 0; i < LINK_CLEAR_RECORDS; ++i) {
		env.pCreatures[i].ulHitTarget = 0;
		env.pCreatures[i].ulAttacker = 0;
	}
	for(uint32_t i = 0; i < 4; ++i) {
		env.pKnights[i].ulHitTarget = 0;
		env.pKnights[i].ulAttacker = 0;
	}
}

// LAB_03BE.
void contactScan(const ContactEnv &env) {
	clearHitLinks(env);                                                     // JSR LAB_0161

	for(uint32_t j = 0; j < COMBAT_JOB_COUNT; ++j) {                        // A6, D7
		CombatJob *pAtk = &env.pJobs[j];
		if(!pAtk->ubActive) {
			continue;
		}
		const uint8_t *pA = at(pAtk->ulAttackList);                         // 40(A6)
		bool bHit = false;
		for(; !bHit && rd32(pA) != 0; pA += HIT_ENTRY_BYTES) {              // LAB_03C0: a zero cel table ends the slot
			for(uint32_t k = 0; k < COMBAT_JOB_COUNT && !bHit; ++k) {       // A5, D6
				CombatJob *pHurt = &env.pJobs[k];
				if(pHurt == pAtk || !pHurt->ubActive) {
					continue;
				}
				if(!depthClose(pHurt->uwZ, pAtk->uwZ)) {                    // |10(A5) - 10(A6)| > 10
					continue;
				}
				for(const uint8_t *pH = at(pHurt->ulHurtList); rd32(pH) != 0; pH += HIT_ENTRY_BYTES) {   // 44(A5)
					const uint32_t ulAttTab = rd32(pA);
					// DEVIATION: a cel table without a registered hit set finds nothing (the asm scans on).
					const uint32_t ulSet = hitSetFind(env.pHitPairs, env.ulHitCount, ulAttTab);
					if(!ulSet) {
						continue;
					}
					ContactPoint pt;
					if(contactTest(at(rd32(pH)), rd16(pH + 4), rd16(pH + 6), rd16(pH + 8), at(ulAttTab), at(ulSet),
					               rd16(pA + 4), rd16(pA + 6), rd16(pA + 8), &pt)) {
						Knight *pAtkRec = jobPtr<Knight>(pAtk->ulOwner);    // 24(A6)
						Knight *pHurtRec = jobPtr<Knight>(pHurt->ulOwner);  // 24(A5)
						pAtkRec->ulHitTarget = pHurt->ulOwner;              // 14(A0)
						pHurtRec->ulAttacker = pAtk->ulOwner;               // 18(A1)
						pHurtRec->uwHitX = pt.uwX;                          // 122 / 124(A1)
						pHurtRec->uwHitY = pt.uwY;
						bHit = true;                                        // BRA LAB_03C6: next job
						break;
					}
				}
			}
		}
	}

	// LAB_03C7: both lists are cleared for the next frame.
	for(uint32_t i = 0; i < HIT_LIST_BYTES; ++i) {
		env.pAttackLists[i] = 0;
		env.pHurtLists[i] = 0;
	}
}

// LAB_03A9 with LAB_03AC / LAB_03AF / LAB_03B1.
uint16_t blockedMask(const CombatJob *pJobs, const Knight *pSelf, uint16_t uwStep, uint16_t uwFacing) {
	const uint16_t uwDir = uwFacing & 3u;                                   // ANDI.W #3,LAB_0636
	uint16_t uwMask = 0x1F;                                                 // MOVE.W #$1F,LAB_03B6
	const Knight &a = *pSelf;
	for(uint32_t j = 0; j < COMBAT_JOB_COUNT; ++j) {
		const CombatJob &job = pJobs[j];
		if(!job.ubActive) {
			continue;
		}
		const Knight &b = *jobPtr<const Knight>(job.ulOwner);
		if(&b == pSelf) {                                                   // CMPA.L A0,A1
			continue;
		}
		if(rec16(b, 58) == 0 || b.swHp <= 0) {                              // TST.W 58(A1); TST.W 80(A1) / BLE
			continue;
		}

		// LAB_03AC
		if(depthClose(a.uwY, b.uwY)) {                                      // JSR LAB_03B3 (8(A0) - 8(A1))
			// Sideways: facing 1 tests bit 0 when self is not right of the other (BGT), else bit 1 when not left (BLT).
			unsigned uBit;
			bool bSide;
			if(uwDir == 1) {
				uBit = 0;
				bSide = !((int16_t)a.uwX > (int16_t)b.uwX);
			} else {
				uBit = 1;
				bSide = !((int16_t)a.uwX < (int16_t)b.uwX);
			}
			if(bSide) {
				// The box x range moves by the step (word adds), then the plain y box (112 / 114).
				unsigned n = 0;
				n += contactOverlap((uint16_t)(rec16(a, 58) + uwStep), (uint16_t)(rec16(a, 60) + uwStep), rec16(b, 58), rec16(b, 60));
				n += contactOverlap(rec16(a, 112), rec16(a, 114), rec16(b, 112), rec16(b, 114));
				if(n == 2) {
					uwMask &= (uint16_t)~(1u << uBit);                      // BCLR D6,LAB_03B6+1
				}
			}
		}

		// LAB_03AF: vertical probe.  D5 counts the x box overlap (no step) and the y box overlap.
		unsigned n = contactOverlap(rec16(a, 58), rec16(a, 60), rec16(b, 58), rec16(b, 60)) ? 1u : 0u;
		const uint16_t dDepth = (uint16_t)(a.uwY - b.uwY);                  // D2 = 8(A0) - 8(A1)
		unsigned uBit;
		uint16_t d2 = dDepth;
		if(dDepth & 0x8000u) {                                              // BPL not taken: D6 = 2, NEG.W
			uBit = 2;
			d2 = (uint16_t)(0u - dDepth);
		} else {
			uBit = 3;
		}
		if(!((int16_t)d2 > 20)) {                                           // CMP.W #20,D2 / BGT
			n += contactOverlap(rec16(a, 112), rec16(a, 114), rec16(b, 112), rec16(b, 114)) ? 1u : 0u;
			if(n == 2) {
				uwMask &= (uint16_t)~(1u << uBit);
			}
		}
	}
	return uwMask;
}

// ---------------------------------------------------------------------------------------------------------------
// Dagger flight

// LAB_02D3.
DaggerAim daggerAim(DaggerBlock &blk, Knight &self, const Knight &target, Knight *pStray, int16_t swSpeed) {
	uint16_t d7 = (uint16_t)swSpeed;
	if(!(((uint16_t)(target.uwX - self.uwX)) & 0x8000u)) {                  // SUB.W D0,D1 / BMI skips the NEG
		d7 = (uint16_t)(0u - d7);                                           // NEG.W D7
	}
	blk.ulOwner = jobAddr(&self);
	blk.uwX = self.uwX;
	blk.uwDepth = self.uwY;                                                 // 8(A1)
	blk.uwHeight = self.uwHeight;                                             // 6(A1)
	blk.uwTargetX = (uint16_t)(target.uwX + d7);
	self.uwMapX = (uint16_t)(target.uwX + d7);                              // 126(A1)
	blk.uwTargetDepth = target.uwY;
	pStray->uwMapY = target.uwY;                                            // MOVE.W 8(A2),128(A0): A0, not A1
	blk.uwTargetHeight = target.uwHeight;

	uint16_t uwDist = absW((uint16_t)(blk.uwTargetX - blk.uwX));            // LAB_02DA
	const uint16_t uwDy = absW((uint16_t)(blk.uwTargetDepth - blk.uwDepth));
	if(!((int16_t)uwDy < (int16_t)uwDist)) {                                // CMP.W LAB_02DA,D1 / BLT keeps
		uwDist = uwDy;
	}
	DaggerAim r;
	r.uwDist = uwDist;
	r.uwArc = (uint16_t)(uwDist >> 1);                                      // LAB_0629
	r.uwSteps = (uint16_t)(uwDist >> 3);                                    // LAB_0628
	if(!((int16_t)r.uwArc > 2)) {
		r.uwArc = 3;
	}
	if(!((int16_t)r.uwSteps > 4)) {
		r.uwSteps = 4;
		r.uwArc = 10;
	}
	blk.uwSteps = r.uwSteps;
	blk.uwExtra = r.uwArc;
	self.ubCooldown = (uint8_t)(r.uwSteps >> 8);                            // MOVE.W LAB_0628,106(A1): big-endian word
	self.ubCooldownLo = (uint8_t)r.uwSteps;
	return r;
}

// LAB_02F6.
int32_t daggerStart(DaggerSlot *aSlots, const DaggerBlock &blk, Knight *pSelf, uint16_t uwJunkA, uint16_t uwJunkC,
                    uint32_t *pIndex) {
	DaggerSlot *pSlot = 0;
	uint32_t uSlot = 0;
	for(; uSlot < DAGGER_SLOTS; ++uSlot) {                                  // key slot, else the first free one
		if(aSlots[uSlot].ulOwner == blk.ulOwner || aSlots[uSlot].ulOwner == 0) {
			pSlot = &aSlots[uSlot];
			break;
		}
	}
	if(pIndex) {
		*pIndex = uSlot;
	}
	if(!pSlot) {
		return -1;
	}
	// LAB_02F8: two words of low memory land in the thrower's map position (rt_abs_a / rt_abs_c).
	pSelf->uwMapX = uwJunkA;
	pSelf->uwMapY = uwJunkC;

	const uint16_t uwN = blk.uwSteps;                                       // 16(A1)
	pSlot->ulOwner = blk.ulOwner;
	pSlot->uwSteps = uwN;

	const uint16_t uwDh = absW((uint16_t)(blk.uwHeight - blk.uwTargetHeight));   // MOVE.W 8(A1),D0; SUB.W 14(A1),D0; TST / NEG
	uint32_t d0 = (uint32_t)(blk.ulOwner >> 16) << 16 | uwN;                // D0 = key long, MOVE.W 16(A1),D0 keeps its high word
	uint32_t d1;
	if(!((int16_t)uwDh > 5)) {
		// LAB_02FB: the arc from the extra word.
		d0 = setW(d0, (uint16_t)(uwN + 1u));                                // ADDQ.W #1,D0
		d0 = setW(d0, (uint16_t)((uint16_t)d0 >> 1));                       // LSR.W #1,D0
		d1 = (uint16_t)(blk.uwExtra << 8);                                  // MOVEQ #0,D1 ; MOVE.W 18(A1),D1 ; LSL.W #8,D1
		if(!divu(d1, (uint16_t)d0)) {
			return -2;
		}
		d1 = setW(d1, (uint16_t)((uint16_t)d1 + (uint16_t)d1));             // ADD.W D1,D1
		pSlot->swVz = (int16_t)(uint16_t)d1;                                // MOVE.W D1,(A0)+
		d1 = extL(d1);                                                      // EXT.L D1
		d0 = setW(d0, (uint16_t)((uint16_t)d0 - 1u));                       // SUBQ.W #1,D0
		if(!divu(d1, (uint16_t)d0)) {
			return -2;
		}
		pSlot->swGravity = (int16_t)(uint16_t)d1;
	} else {
		d1 = (uint16_t)(blk.uwHeight - blk.uwTargetHeight);                 // MOVEQ #0,D1 ; MOVE.W 8(A1),D1 ; SUB.W 14(A1),D1
		const bool bBelow = (int16_t)blk.uwHeight < (int16_t)blk.uwTargetHeight;   // BLT (N xor V)
		d1 = extL(d1);
		d1 <<= 8;                                                           // ASL.L #8,D1
		if(!bBelow) {
			if(!divs(d1, uwN)) {
				return -2;
			}
			d1 = setW(d1, (uint16_t)((uint16_t)d1 + (uint16_t)d1));
			pSlot->swVz = (int16_t)(uint16_t)d1;
			d1 = extL(d1);
			d0 = setW(d0, (uint16_t)((uint16_t)d0 - 1u));
			if(!divs(d1, (uint16_t)d0)) {
				return -2;
			}
			pSlot->swGravity = (int16_t)(uint16_t)d1;
		} else {
			pSlot->swVz = 0;                                                // LAB_02FA: CLR.W (A0)+
			if(!divs(d1, uwN)) {
				return -2;
			}
			d1 = setW(d1, (uint16_t)((uint16_t)d1 + (uint16_t)d1));
			d1 = extL(d1);
			d0 = setW(d0, (uint16_t)((uint16_t)d0 - 1u));
			if(!divs(d1, (uint16_t)d0)) {
				return -2;
			}
			pSlot->swGravity = (int16_t)(uint16_t)(0u - (uint16_t)d1);      // NEG.W D1
		}
	}

	// LAB_02FC: per-step x / depth deltas (ASL.W #6, EXT.L, DIVS by the step count).
	d1 = extL((uint16_t)((uint16_t)(blk.uwTargetX - blk.uwX) << 6));
	if(!divs(d1, uwN)) {
		return -2;
	}
	pSlot->swDx = (int16_t)(uint16_t)d1;
	d1 = extL((uint16_t)((uint16_t)(blk.uwTargetDepth - blk.uwDepth) << 6));
	if(!divs(d1, uwN)) {
		return -2;
	}
	pSlot->swDy = (int16_t)(uint16_t)d1;
	pSlot->swX = (int16_t)(uint16_t)(blk.uwX << 6);
	pSlot->swY = (int16_t)(uint16_t)(blk.uwDepth << 6);
	pSlot->swZ = (int16_t)(uint16_t)(blk.uwHeight << 8);
	return 0;
}

// LAB_02FD.
int32_t daggerStep(DaggerSlot *aSlots, uint32_t ulOwner, uint16_t aOut[3], uint32_t *pIndex) {
	DaggerSlot *s = 0;
	uint32_t uSlot = 0;
	for(; uSlot < DAGGER_SLOTS; ++uSlot) {
		if(aSlots[uSlot].ulOwner == ulOwner) {
			s = &aSlots[uSlot];
			break;
		}
	}
	if(pIndex) {
		*pIndex = uSlot;
	}
	if(!s) {
		return -1;
	}
	const uint16_t uwVz = (uint16_t)s->swVz;                                // D1 = 6(A0)
	s->swVz = (int16_t)(uint16_t)((uint16_t)s->swVz - (uint16_t)s->swGravity);   // SUB.W D0,6(A0)
	s->swZ = (int16_t)(uint16_t)((uint16_t)s->swZ - uwVz);                  // SUB.W D1,18(A0): the OLD vertical speed
	s->swX = (int16_t)(uint16_t)((uint16_t)s->swX + (uint16_t)s->swDx);
	s->swY = (int16_t)(uint16_t)((uint16_t)s->swY + (uint16_t)s->swDy);
	aOut[0] = (uint16_t)(s->swX >> 6);                                      // ASR.W
	aOut[1] = (uint16_t)(s->swY >> 6);
	aOut[2] = (uint16_t)(s->swZ >> 8);
	s->uwSteps = (uint16_t)(s->uwSteps - 1u);
	if(s->uwSteps == 0) {
		s->ulOwner = 0;                                                     // CLR.L (A0): the slot is free
		return 1;
	}
	return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Creature plumbing

// LAB_0310.
uint32_t jobCreate(CombatJob *pJobs, uint32_t ulScript, uint32_t ulOwner, uint32_t ulFrames, uint16_t uwX,
                   uint16_t uwY, uint16_t uwZ, uint8_t ubFacing, uint8_t ubType) {
	for(uint32_t i = 0; i < COMBAT_JOB_COUNT; ++i) {
		CombatJob &job = pJobs[i];
		if(job.ubActive) {
			continue;
		}
		uint8_t *pWork = (uint8_t *)jobPtr<CombatWork>(job.ulWork);         // MOVE.B #0,(A5)+ x 36
		for(uint32_t n = 0; n < sizeof(CombatWork); ++n) {
			pWork[n] = 0;
		}
		job.ulScript = ulScript;
		job.ulOwner = ulOwner;
		job.ulFrames = ulFrames;
		job.uwX = uwX;
		job.uwY = uwY;
		job.uwZ = uwZ;
		job.ubFlags = ubFacing;
		job.ubType = ubType;
		job.ubActive = 1;
		job.ubRunning = 1;
		return 0;
	}
	return 1;                                                               // all ten in use (the asm prints a debug string)
}

// LAB_0171.
Knight *recordAlloc(Knight *pCreatures) {
	Knight *p = pCreatures;
	for(uint32_t i = 0; i < CREATURE_RECORDS; ++i, ++p) {
		if(p->ulActive == 0) {
			p->ulActive = 1;
			return p;
		}
	}
	return p;                                                               // one record past the heap, not marked
}

// LAB_02D0.
SpawnResult creatureSpawn(CombatJob *pJobs, Knight *pCreatures, uint32_t ulScript, uint32_t ulFrames, uint16_t uwX,
                          uint16_t uwY, uint16_t uwZ, uint8_t ubFacing, uint8_t ubType) {
	Knight *p = recordAlloc(pCreatures);
	p->uwX = uwX;
	p->uwHeight = uwY;
	p->uwY = uwZ;
	p->ubFacing = ubFacing;
	p->ulJobParam = ulFrames;
	p->ulHitTarget = 0;
	p->ulAttacker = 0;
	p->ulActive = 1;
	p->ubType = ubType;
	SpawnResult r;
	r.pRecord = p;
	r.ulStatus = jobCreate(pJobs, ulScript, jobAddr(p), ulFrames, uwX, uwY, uwZ, ubFacing, ubType);
	return r;
}

// LAB_02CA.
SpawnResult daggerRelease(CombatJob *pJobs, Knight *pCreatures, Knight &thrower, uint32_t ulDaggerScript,
                      uint32_t ulDamageTable, uint32_t ulFrames, uint16_t uwX, uint16_t uwY, uint16_t uwZ,
                      uint8_t ubFacing) {
	thrower.ubDaggers = (uint8_t)(thrower.ubDaggers - 1u);                  // SUBI.B #1,76(A1)
	SpawnResult r = creatureSpawn(pJobs, pCreatures, ulDaggerScript, ulFrames, uwX, uwY, uwZ, ubFacing, 0x34);
	r.pRecord->ulDamageTable = ulDamageTable;                               // LAB_0302: the dagger damage table
	r.pRecord->uwAction = raw(Action::Dagger);
	r.pRecord->ubType = raw(ActorType::Dagger);
	return r;
}

// LAB_0322.
void creatureDispatch(const DispatchEnv &env) {
	for(uint32_t i = 0; i < COMBAT_JOB_COUNT; ++i) {
		CombatJob &job = env.pJobs[i];
		if(!job.ubActive) {
			continue;
		}
		Knight *pOwner = jobPtr<Knight>(job.ulOwner);
		if(pOwner->ulHitTarget == 0 && pOwner->ulAttacker == 0 && job.ubRunning) {
			continue;                                                       // idle owner with a running script
		}
		const uint32_t ulHandler = rd32(env.pHandlerTable + job.ubType);    // 0(A1,D0.W): the type byte, unscaled
		HandlerResult res;
		res.ulScript = 0;
		res.uwX = res.uwY = res.uwZ = 0;
		res.ubFacing = 0;
		env.callHandler(ulHandler, job.ulOwner, &res);
		if(res.ulScript == 0xFFFFFFFFu) {
			continue;
		}
		if(res.ulScript == 0) {                                             // LAB_0325: the creature is done
			job.ubActive = 0;
			job.ubRunning = 0;                                              // MOVE.W #0,0(A6)
			jobPtr<Knight>(job.ulOwner)->ulActive = 0;                       // CLR.L (A1)
			continue;
		}
		job.ulScript = res.ulScript;
		job.uwX = res.uwX;
		job.uwY = res.uwY;
		job.uwZ = res.uwZ;
		job.ubFlags = res.ubFacing;
		job.ubRunning = 1;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Predicates

bool depthNear(const Knight &self, const Knight &other) {
	const uint16_t d = absW((uint16_t)(other.uwY - self.uwY));              // 8(A0) - 8(A1)
	return !((int16_t)d > (int16_t)self.uwDepthReach);                          // CMP.W 120(A1),D1 / BGT
}

bool xNear(const Knight &self, const Knight &target, int16_t swLimit) {
	const uint16_t d = absW((uint16_t)(target.uwX - self.uwX));
	return !((int16_t)d > swLimit);
}

uint16_t xDelta(const Knight &self, const Knight &target, bool &bSelfLeft) {
	const uint16_t d = (uint16_t)(self.uwX - target.uwX);
	bSelfLeft = (d & 0x8000u) != 0;
	return bSelfLeft ? (uint16_t)(0u - d) : d;
}

void faceTarget(Knight &self, const Knight &target) {
	self.ubFacing = ((int16_t)self.uwX < (int16_t)target.uwX) ? 1 : 3;     // CMP.W 4(A0),D0 / BLT
}

}}  // namespace ms::game
