// game/overworld - see include/game/overworld.hpp.  Every function cites the mog.asm labels it transcribes.  Word and byte
// arithmetic follows the asm (16/8-bit wrap, signed compares where the asm branches with BGT/BLT/BGE/BLE, N-flag tests where
// it branches with BPL/BMI after a SUB or MOVE).  Host-testable: no globals, no OS.
#include "game/api/party.hpp"
#include "game/overworld.hpp"

#include "engine/util.hpp"

namespace ms { namespace game {

namespace {

inline bool neg16(uint16_t uw) { return (uw & 0x8000u) != 0; }                       // BMI / BPL after a word op
inline uint16_t negW(uint16_t uw) { return (uint16_t)(0u - uw); }                    // NEG.W
inline bool lt16(uint16_t a, uint16_t b) { return (int16_t)a < (int16_t)b; }

// LAB_03CA: the coarse interval test on 32-bit signed compares (callers pass zero-extended words).
bool overlap1(int32_t d0, int32_t d1, int32_t d2, int32_t d3) {
	if (((uint32_t)d2 - (uint32_t)d0) >> 31) return d3 >= d0;     // CMP.L D0,D2 ; BMI.S LAB_03CC ; CMP.L D0,D3 ; BGE
	if (((uint32_t)d2 - (uint32_t)d1) >> 31) return true;         // CMP.L D1,D2 ; BMI.S
	if (((uint32_t)d3 - (uint32_t)d0) >> 31) return false;        // CMP.L D0,D3 ; BMI.S LAB_03CD
	return !(d3 >= d1);                                           // CMP.L D1,D3 ; BGE.S LAB_03CD
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Map movement

// LAB_0E07 (mog.asm 25409)
uint16_t mapClipMove(uint16_t uwMove, uint16_t uwX, uint16_t uwY) {
	if (!((int16_t)uwX > 0)) uwMove &= (uint16_t)~MV_LEFT;            // CMP.W #0,D0 ; BGT.S ; BCLR #1,LAB_0657
	if (!((int16_t)uwX < 0x136)) uwMove &= (uint16_t)~MV_RIGHT;       // CMP.W #$136,D0 ; BLT.S ; BCLR #0
	if (!((int16_t)uwY > 0)) uwMove &= (uint16_t)~MV_UP;              // CMP.W #0,D1 ; BGT.S ; BCLR #3
	if (!((int16_t)uwY < 0xBE)) uwMove &= (uint16_t)~MV_DOWN;         // CMP.W #$be,D1 ; BLT.S ; BCLR #2
	return uwMove;
}

// LAB_0DA6 (mog.asm 24730)
void mapMove(uint16_t &uwX, uint16_t &uwY, uint16_t &uwMove) {
	if (uwMove == 0) return;                                          // TST.W LAB_0656 ; BEQ.W LAB_0DAA
	uwMove = mapClipMove(uwMove, uwX, uwY);                           // JSR LAB_0E07 (clears bits of the LOW byte, LAB_0657)
	if (uwMove & MV_RIGHT) uwX = (uint16_t)(uwX + 1);                 // BTST #0 ; ADDI.W #1,126(A0)
	if (uwMove & MV_LEFT) uwX = (uint16_t)(uwX - 1);                  // BTST #1 ; SUBI.W #1,126(A0)
	if (uwMove & MV_UP) uwY = (uint16_t)(uwY - 1);                    // BTST #3 ; SUBI.W #1,128(A0)
	if (uwMove & MV_DOWN) uwY = (uint16_t)(uwY + 1);                  // BTST #2 ; ADDI.W #1,128(A0)
}

// LAB_0E22 (mog.asm 25576)
void mapTile(Knight &k, uint16_t uwFrame0W, uint16_t uwFrame0H) {
	const uint16_t uwHalf = (uint16_t)(uwFrame0W >> 1);               // LSR.W #1,D2
	k.uwTileX = (uint16_t)((uint16_t)(k.uwMapX + uwHalf) >> 3);       // ADD.W D2,D0 ; LSR.W #3,D0
	k.uwTileY = (uint16_t)((uint16_t)(k.uwMapY + uwFrame0H) >> 3);    // ADD.W D3,D1 ; LSR.W #3,D1
}

// LAB_0E20 (mog.asm 25560)
uint16_t mapTileIndex(const Knight &k) {
	const uint32_t ulRow = (uint32_t)k.uwTileY * 40u;                 // MULU #$28,D1
	return (uint16_t)(ulRow + k.uwTileX);                             // ADD.W D0,D1 ; EXT.L ; MOVE.W D1,LAB_0E21
}

// LAB_0DD8 (mog.asm 25163)
void terrainStep(TerrainState &ts, bool bModeBlocks, uint8_t ubMask) {
	ts.uwBlocked = 0;                                                 // MOVE.W #0,LAB_0DDA+2
	if (bModeBlocks) return;                                          // TST.W LAB_065E / LAB_065C ; BNE
	if (ubMask == 0) return;                                          // MOVE.B 0(A0,D1.W),D7 ; BEQ.S
	const uint16_t uwMask = (uint16_t)(int16_t)(int8_t)ubMask;        // EXT.W D7
	ts.uwCounter = (uint16_t)(ts.uwCounter + 1);                      // ADDQ.W #1,LAB_0DDA
	if ((ts.uwCounter & uwMask) != 0) ts.uwBlocked = 1;               // AND.W D7,D6 ; BEQ ; MOVE.W #1,LAB_0DDA+2
}

// LAB_0E04 / 0E02 / 0E03 / 0E05 / 0E06 (mog.asm 25380..25408)
void mapModeClear(MapMode &m) {
	m.uwPosSaved = 0;
	m.uwForced = 0;
	m.uwAmbush = 0;
}

void mapModeForce(MapMode &m, const Knight &k) {
	m.uwForced = 1;                                                   // MOVE.W #1,LAB_065E
	m.uwSavedX = k.uwMapX;                                            // MOVE.L 126(A0),LAB_065F
	m.uwSavedY = k.uwMapY;
}

void mapModeRestore(MapMode &m, Knight &k) {
	k.uwMapX = m.uwSavedX;                                            // MOVE.L LAB_065F,126(A0)
	k.uwMapY = m.uwSavedY;
	mapModeClear(m);                                                  // falls into LAB_0E04
}

void mapModeAmbush(MapMode &m, const Knight &k) {
	m.uwPosSaved = 1;
	m.uwAmbush = 1;
	m.uwSavedX = k.uwMapX;
	m.uwSavedY = k.uwMapY;
}

void mapRandomSpot(Knight &k, uint32_t &ulSeed) {
	k.uwMapX = (uint16_t)(rngNext(ulSeed) & 0xFF);                    // JSR LAB_04A1 ; ANDI.W #$ff,D0 ; MOVE.W D0,126(A0)
	k.uwMapY = (uint16_t)(rngNext(ulSeed) & 0x7F);                    // ANDI.W #$7f,D0 ; MOVE.W D0,128(A0)
	k.uwMapY = (uint16_t)(k.uwMapY + 0x24);                           // ADDI.W #$24,128(A0)
	k.uwMapX = (uint16_t)(k.uwMapX + 0x20);                           // ADDI.W #$20,126(A0)
}

// ---------------------------------------------------------------------------------------------------------
// AI knight: walk

// LAB_0E0C (mog.asm 25432)
uint16_t aiWalkStep(AiWalk &w, uint16_t uwX, uint16_t uwY, const AiTarget &t) {
	if (w.uwInit == 0) {
		w.uwInit = 1;                                                 // MOVE.W #1,LAB_0674
		uint16_t uwTx, uwTy;
		if (t.pEngaged) {                                             // TST.L 100(A0) ; BEQ.S LAB_0E0D
			uwTx = t.pEngaged->uwMapX;
			uwTy = t.pEngaged->uwMapY;
		} else if (t.ulShop != 0) {                                   // TST.L LAB_066C ; BEQ.S LAB_0E0E
			uwTx = (uint16_t)(t.ulShop >> 16);
			uwTy = (uint16_t)t.ulShop;
		} else {                                                      // LAB_0E0E: the roam lair, +10 / +12
			uwTx = (uint16_t)t.pLair->swMapX;
			uwTy = (uint16_t)t.pLair->swMapY;
		}
		uint16_t uwDx = (uint16_t)(uwTx - uwX);                       // SUB.W D0,D2
		uint16_t uwXBit = 1;                                          // MOVEQ #1,D4
		if (neg16(uwDx)) {                                            // BPL.W LAB_0E10
			uwDx = negW(uwDx);
			uwXBit = 2;
		}
		uint16_t uwDy = (uint16_t)(uwTy - uwY);                       // SUB.W D1,D3
		uint16_t uwYBit = 4;                                          // MOVEQ #4,D5
		if (neg16(uwDy)) {                                            // BPL.S LAB_0E11
			uwDy = negW(uwDy);
			uwYBit = 8;
		}
		uint16_t uwMajor = 0;                                         // MOVEQ #0,D6
		uint16_t uwErr = uwDx;                                        // MOVE.W D2,D7
		if (lt16(uwDx, uwDy)) {                                       // CMP.W D3,D2 ; BGE.S LAB_0E12
			uwMajor = 0xFFFF;                                         // MOVEQ #-1,D6
			uwErr = uwDy;
		}
		w.uwXBit = uwXBit;
		w.uwYBit = uwYBit;
		w.uwErr = uwErr;
		w.uwDx = uwDx;
		w.uwDy = uwDy;
		w.uwYMajor = uwMajor;
	}
	uint16_t uwMove, uwErr;
	if (!neg16(w.uwYMajor)) {                                         // LAB_0E13: TST.W LAB_067A ; BMI.S LAB_0E15
		uwMove = w.uwXBit;                                            // x is the major axis
		uwErr = (uint16_t)(w.uwErr - w.uwDy);
		if (neg16(uwErr)) {                                           // BPL.S LAB_0E14
			uwErr = (uint16_t)(uwErr + w.uwDx);
			uwMove |= w.uwYBit;
		}
	} else {                                                          // LAB_0E15
		uwMove = w.uwYBit;
		uwErr = (uint16_t)(w.uwErr - w.uwDx);
		if (neg16(uwErr)) {
			uwErr = (uint16_t)(uwErr + w.uwDy);
			uwMove |= w.uwXBit;
		}
	}
	w.uwErr = uwErr;                                                  // MOVE.W D1,LAB_0677
	return uwMove;                                                    // MOVE.W D0,LAB_0656
}

// ---------------------------------------------------------------------------------------------------------
// Arrival list and node menu

// LAB_0E45 (mog.asm 25860)
ArrivalAction arrivalClassify(uint32_t ulKind) {
	if (ulKind == 0x21 || ulKind == 1) return AR_DUEL;                // CMP.L #$21,D0 ; BEQ.S ; CMP.L #1,D0 ; BNE.S
	if (ulKind == 2) return AR_LAIR;                                  // LAB_0E47
	return AR_PLACE;                                                  // LAB_0E48
}

// LAB_0E1C (mog.asm 25544)
bool arrivalFind(const ArrivalEntry *aRows, int iField, uint32_t ulValue) {
	bool bFound = false;
	for (int i = 0; i < 7; ++i) {                                     // MOVEQ #6,D7
		if (aRows[i].ulKey == 0) break;                               // TST.L (A2) ; BEQ.S LAB_0E1F
		const uint32_t ulV = iField == 0 ? aRows[i].ulKey : aRows[i].ulKind;   // CMP.L 0(A2,D2.W),D1
		if (ulV == ulValue) bFound = true;
	}
	return bFound;
}

// LAB_0E17 (mog.asm 25506)
void aiArrival(const ArrivalEntry *aRows, uint32_t &ulShop, uint32_t ulEngaged, uint32_t ulRoamLair, uint16_t &uwSpent,
               uint16_t uwBudget, const ArrivalOps &ops) {
	if (ulShop != 0) {                                                // TST.L LAB_066C ; BEQ.S LAB_0E19
		if (arrivalFind(aRows, 4, 0x19) || arrivalFind(aRows, 4, 0x1A)) {   // LAB_0E1C with D1 = $19 / $1A, D2 = 4
			uwSpent = uwBudget;                                       // MOVE.W LAB_0665,LAB_0655
			if (ops.pfnShop) ops.pfnShop(ops.pCtx);                   // JSR LAB_0E37
			ulShop = 0;                                               // MOVE.L #0,LAB_066C
		}
		return;
	}
	if (ulEngaged != 0) {                                             // LAB_0E19: MOVE.L 100(A0),D1 ; BEQ.S LAB_0E1A
		if (!arrivalFind(aRows, 0, ulEngaged)) return;                // BSR.S LAB_0E1C ; BEQ.W LAB_0E1B
		if (ops.pfnDuel) ops.pfnDuel(ops.pCtx, ulEngaged);            // MOVEA.L 100(A0),A1 ; JSR LAB_004F
		uwSpent = uwBudget;
	}
	if (arrivalFind(aRows, 0, ulRoamLair)) uwSpent = uwBudget;        // LAB_0E1A: MOVE.L LAB_0673,D1
}

namespace {

uint32_t dispatchRow(const ArrivalEntry &row, const NodeMenuOps &ops) {
	switch (arrivalClassify(row.ulKind)) {                            // LAB_0E45: A1 = (A2)+ ; D0 = (A2)
		case AR_DUEL: return ops.pfnDuel(ops.pCtx, row.ulKey);        // JSR LAB_004F
		case AR_LAIR: return ops.pfnLair(ops.pCtx, row.ulKey);        // JSR LAB_005B
		default: return ops.pfnPlace(ops.pCtx, row.ulKind, row.ulKey);   // JSR LAB_007B
	}
}

}  // namespace

// LAB_0E3D (mog.asm 25810)
uint32_t arrivalMenu(const ArrivalEntry *aRows, bool bForced, const NodeMenuOps &ops) {
	int n = 0;
	while (n < ARRIVAL_ROWS && aRows[n].ulKey != 0) ++n;             // LAB_0E3E: TST.L (A2) ; BEQ.S ; ADDQ.L #1,D0
	if (n == 0) return 0;                                             // TST.L D0 ; BEQ.S LAB_0E41
	if (n == 1) return dispatchRow(aRows[0], ops);                    // CMP.L #1,D0 ; BEQ.W LAB_0E45
	if (bForced) {                                                    // TST.W LAB_065E ; BNE.W LAB_0E42
		for (int i = 0; i < ARRIVAL_ROWS; ++i) {
			if (aRows[i].ulKind == 2) return dispatchRow(aRows[i], ops);   // LAB_0E43: CMPI.L #2,4(A2) ; BEQ.S LAB_0E44
			if (aRows[i].ulKey == 0) return 0;                        // TST.L (A2) ; BEQ.W LAB_0E41 (D0 = 0)
		}
		return 0;
	}
	if (ops.pfnKeyReset) ops.pfnKeyReset(ops.pCtx);                   // JSR LAB_0B82
	if (ops.pfnDrawMenu) ops.pfnDrawMenu(ops.pCtx);                   // BSR.W LAB_0E49 ; JSR LAB_0D9B ; JSR LAB_0416
	for (;;) {                                                        // LAB_0E40
		const uint16_t uwKey = ops.pfnReadKey(ops.pCtx);              // MOVE.W SECSTRT_21,D0 ; BEQ.S ; JSR LAB_0D8D
		if (uwKey < 0x31 || uwKey > 0x39) continue;                   // CMP.W #$31,D0 ; BLT.S ; CMP.W #$39,D0 ; BGT.S (signed)
		const int iRow = uwKey - 0x31;                                // SUBI.W #$31,D0 ; MULU #8,D0
		if (aRows[iRow].ulKey == 0) continue;                         // TST.L (A2) ; BEQ.W LAB_0E40
		return dispatchRow(aRows[iRow], ops);                         // ADDA.L #8,A2 ; BRA.W LAB_0E45
	}
}

// ---------------------------------------------------------------------------------------------------------
// Proximity scan

// LAB_0067 (mog.asm 998)
bool boxesTouch(const ScanOps &ops, uint16_t uwSprite, uint16_t uwAx, uint16_t uwAy, uint16_t uwBx, uint16_t uwBy) {
	uint16_t uwAw, uwAh, uwBw, uwBh;
	ops.pfnSize(ops.pCtx, uwSprite, uwAw, uwAh);                      // BSR.S LAB_0066 with D0 = the sprite
	ops.pfnSize(ops.pCtx, 0, uwBw, uwBh);                             // MOVEQ #0,D0 ; BSR.S LAB_0066
	if (!overlap1(uwAx, (uint16_t)(uwAx + uwAw), uwBx, (uint16_t)(uwBx + uwBw))) return false;   // x, JSR LAB_03CA
	return overlap1(uwAy, (uint16_t)(uwAy + uwAh), uwBy, (uint16_t)(uwBy + uwBh));              // y
}

namespace {

struct Appender {
	ArrivalEntry *aList;
	int n;
	void add(uint32_t ulKey, uint32_t ulKind) {
		if (n < ARRIVAL_ROWS) {
			aList[n].ulKey = ulKey;
			aList[n].ulKind = ulKind;
		}
		++n;
	}
};

// LAB_0079: highlight a knight: sprite kind + 5, or $2A when out of lives, at the knight's map position.
void drawKnight(const ScanOps &ops, const Knight &k) {
	uint32_t ulSprite = k.ulKind + 5;                                 // MOVE.L 54(A0),D0 ; ADDQ.L #5,D0
	if (!((int8_t)k.ubLives > 0)) ulSprite = (ulSprite & 0xFFFF0000u) | 0x2A;   // TST.B 73(A0) ; BGT.S ; MOVE.W #$2a,D0
	ops.pfnDraw(ops.pCtx, ulSprite, k.uwMapX, k.uwMapY);
}

}  // namespace

// LAB_0069 (mog.asm 1032)
void scanMap(ScanCells &c, const RecordSet &rs, int iCur, const MapNode *aNodes, const Lair *aLairs, uint32_t ulLairBase,
             bool bForced, bool bDragonActive, const ScanOps &ops) {
	for (int i = 0; i < 5; ++i) {                                     // MOVEQ #4,D0 ; CLR.L (A2)+ ; CLR.L (A2)+
		c.aList[i].ulKey = 0;
		c.aList[i].ulKind = 0;
	}
	c.uwLastId = 0xFFFF;                                              // MOVE.W #$ffff,LAB_069C
	Appender ap = {c.aList, 0};
	const Knight &cur = rs.aRec[iCur];
	const uint16_t uwCx = cur.uwMapX, uwCy = cur.uwMapY;
	if (!bForced) {                                                   // TST.W LAB_065E ; BNE.W LAB_0076
		for (int i = 0;; ++i) {                                       // LAB_006B: the node table, up to the negative id
			const MapNode &nd = aNodes[i];
			if (nd.swId < 0) break;                                   // TST.W D0 ; BMI.W LAB_0070
			if (!boxesTouch(ops, (uint16_t)nd.swId, nd.uwX, nd.uwY, uwCx, uwCy)) continue;   // BSR.W LAB_0067 ; CMP.L #2,D5
			const uint32_t ulKind = cur.ulKind;
			if (nd.swId == raw(PlaceId::Village0) && ulKind != raw(KnightKind::Knight0)) continue;             // only the home village of the knight
			if (nd.swId == raw(PlaceId::Village1) && ulKind != raw(KnightKind::Knight1)) continue;
			if (nd.swId == raw(PlaceId::Village2) && ulKind != raw(KnightKind::Knight2)) continue;
			if (nd.swId == raw(PlaceId::Village3) && ulKind != raw(KnightKind::Knight3)) continue;
			c.uwLastX = nd.uwX;                                       // LAB_006F: MOVE.W D1,LAB_069D
			c.uwLastId = (uint16_t)nd.swId;                           // MOVE.W D0,LAB_069C
			ops.pfnDraw(ops.pCtx, (uint32_t)(uint16_t)nd.swId, nd.uwX, nd.uwY);   // JSR LAB_0CDA
			ap.add(((uint32_t)nd.uwX << 16) | nd.uwY, (uint32_t)(int32_t)nd.swId);   // MOVE.W D1,(A2)+ ; MOVE.W D2,(A2)+ ; EXT.L D0
		}
		for (int i = 0; i < 4; ++i) {                                 // LAB_0070: the other knights
			if (i == iCur) continue;                                  // CMPA.L A0,A1 ; BEQ.S LAB_0073
			const Knight &o = rs.aRec[i];
			if (!boxesTouch(ops, 0, uwCx, uwCy, o.uwMapX, o.uwMapY)) continue;
			drawKnight(ops, o);                                       // BSR.W LAB_0079
			ap.add(rs.addr(i), (int8_t)o.ubLives > 0 ? 1u : 0x21u);   // MOVE.L A0,(A2)+ ; MOVE.L D0,(A2)+
		}
		for (int i = 0; i < 4; ++i) c.aulTouch[i] = 0;                // the four CLR of LAB_069E
		if (bDragonActive) {                                          // TST.W LAB_0667 ; BEQ.W LAB_0076
			const Knight &dr = rs.aRec[4];
			int iT = 0;
			for (int i = 0; i < 4; ++i) {                             // LAB_0074
				const Knight &o = rs.aRec[i];
				if (!boxesTouch(ops, 0x14, (uint16_t)(dr.uwX - 10), dr.uwY, o.uwMapX, o.uwMapY)) continue;   // $25 - $11 ; x - $A
				c.aulTouch[iT++] = rs.addr(i);                        // MOVE.L A0,(A3)+
				drawKnight(ops, o);
			}
		}
	}
	for (int i = 0; i < 24; ++i) {                                    // LAB_0076: the lairs
		const Lair &l = aLairs[i];
		if ((int16_t)l.swMapX < 0) continue;                          // TST.L 10(A0) ; BMI.S LAB_0078
		if (!boxesTouch(ops, 31, (uint16_t)l.swMapX, (uint16_t)l.swMapY, uwCx, uwCy)) continue;
		ops.pfnDraw(ops.pCtx, 31, (uint16_t)l.swMapX, (uint16_t)l.swMapY);
		ap.add(ulLairBase + 20u * (uint32_t)i, 2);                    // MOVE.L A0,(A2)+ ; MOVE.L #2,(A2)+
	}
}

// ---------------------------------------------------------------------------------------------------------
// Dragon

// LAB_0DB6 (mog.asm 24864)
bool dragonAttacks(const Knight &dragon, uint16_t uwActive, uint32_t ulCurrent, const uint32_t aulTouch[4]) {
	if ((int8_t)dragon.ubLives < 0) return false;                     // TST.B 73(A1) ; BMI.W LAB_0DB9
	if (uwActive == 0) return false;                                  // TST.W LAB_0667 ; BEQ.W
	if (knightEngagedWith(dragon) != ulCurrent) return false;              // MOVE.L 100(A1),D0 ; CMP.L LAB_0633,D0 ; BNE.W
	for (int i = 0; i < 4; ++i) {                                     // LAB_0DB7: CMP.L (A0)+,D0
		if (aulTouch[i] == ulCurrent) return true;
	}
	return false;
}

// LAB_0DCB (mog.asm 25049)
bool dragonShouldSpawn(uint16_t uwDay, const Knight &dragon, const GameData &d) {
	if (lt16(uwDay, d.encounters.ubDragonDay)) return false;          // CMPI.W #2,LAB_06C0 ; BLT.W LAB_0DCD (original day 2)
	if ((int8_t)dragon.ubLives < 0) return false;                     // TST.B 73(A0) ; BMI.W LAB_0DCD
	return true;
}

void dragonInitRecord(Knight &dragon) {
	dragon.uwX = 10;                                                  // MOVE.W #$a,4(A0)
	dragon.uwHeight = 0;                                                // MOVE.W #0,6(A0)
	dragon.uwY = 100;                                                 // MOVE.W #$64,8(A0)
	dragon.ubFacing = 3;                                              // MOVE.B #3,10(A0)
	dragon.ubType = raw(ActorType::DragonFlight);                                             // MOVE.B #$28,77(A0)
	dragon.ubAnimPhase = 0;                                           // MOVE.B #0,12(A0)
}

int dragonPickTarget(const Knight *aKnights, uint32_t &ulSeed) {
	for (int i = 0; i < 4096; ++i) {
		const uint32_t ulR = rngNext(ulSeed) & 3;                     // JSR LAB_04A1 ; ANDI.L #3,D0
		if ((int8_t)aKnights[ulR].ubLives > 0) return (int)ulR;       // MULU #$84 ; TST.B 73(A0) ; BLE.S LAB_0DCC
	}
	return -1;
}

// LAB_0DCB (mog.asm 25049) .. LAB_0DCC
bool dragonSpawn(const RecordSet &rs, uint16_t uwDay, const DragonSpawnEnv &env, const DragonSpawnCells &cells, uint32_t &ulSeed,
                 const DragonSpawnOps &ops, const GameData &d) {
	Knight &dr = rs.aRec[4];
	if (!dragonShouldSpawn(uwDay, dr, d)) return false;
	if (ops.pfnClearJobs) ops.pfnClearJobs(ops.pCtx);                 // JSR LAB_0305
	*cells.pulHandlerSlot = env.ulHandler;                            // MOVE.L #LAB_0DCF,40(A0) with A0 = LAB_08C7
	for (int i = 0; i < 5; ++i) cells.paulWork[i] = env.ulCelTable;   // MOVE.L LAB_0664,(A0)+ x5 with A0 = LAB_0671
	dragonInitRecord(dr);
	dr.ulJobParam = env.ulWork;                                       // MOVE.L #LAB_0671,38(A0)
	knightSetWalkScripts(dr, env.ulScriptTab);                               // MOVE.L #LAB_08FC,46(A0)
	if (ops.pfnSpawnJob) ops.pfnSpawnJob(ops.pCtx, env.ulScript0, rs.addr(4), env.ulWork, env.auwScriptHdr, 3, 0x28);   // JSR LAB_0310
	cells.pFlight->swSpeedX = 2;                                      // MOVE.W #2,LAB_0DDC
	cells.pFlight->uwTimer = 0x64;                                    // MOVE.W #$64,LAB_0666
	*cells.puwActive = 1;                                             // MOVE.W #1,LAB_0667
	const int iT = dragonPickTarget(rs.aRec, ulSeed);
	if (iT >= 0) knightEngage(dr, rs.addr(iT));                      // LAB_0DCC: MOVE.L A0,100(A1)
	return true;
}

// LAB_0DCF (mog.asm 25107)
DragonStep dragonFlyStep(Knight &d, DragonFlight &f, uint16_t uwTargetMapY) {
	DragonStep s = {DP_HOVER, 0};
	f.uwTimer = (uint16_t)(f.uwTimer - 1);                            // SUBQ.W #1,LAB_0666
	if (neg16(f.uwTimer)) f.uwTimer = 0x64;                           // BPL.S LAB_0DD0 ; MOVE.W #$64
	if (!lt16(0x3C, f.uwTimer)) {                                     // CMPI.W #$3c ; BGT.W LAB_0DD1: not above 60 = hover
		d.uwX = (uint16_t)(d.uwX + (uint16_t)f.swSpeedX);             // MOVE.W LAB_0DDC,D5 ; ADD.W D5,4(A0)
		return s;                                                     // script LAB_0900, JMP LAB_02BA
	}
	d.uwX = (uint16_t)(d.uwX + (uint16_t)f.swSpeedX);                 // LAB_0DD1
	int16_t swStep = f.swSpeedY;                                      // MOVE.W LAB_0DDC+2,D5
	if (uwTargetMapY != d.uwY) {                                      // CMP.W 8(A0),D0 ; BEQ.S LAB_0DD3
		if (!lt16(d.uwY, uwTargetMapY)) swStep = (int16_t)(0 - swStep);   // BGT.S LAB_0DD2 ; NEG.W D5
		d.uwY = (uint16_t)(d.uwY + (uint16_t)swStep);                 // ADD.W D5,8(A0)
	}
	if ((int16_t)d.uwX > 0x15E) {                                     // CMPI.W #$15e,4(A0) ; BLE.S LAB_0DD4
		d.uwX = 0x159;
		d.ubFacing ^= 2;                                              // BCHG #1,10(A0)
		f.swSpeedX = (int16_t)(0 - f.swSpeedX);                       // NEG.W LAB_0DDC
	}
	if ((int16_t)d.uwX <= (int16_t)0xFFEC) {                          // CMPI.W #$ffec,4(A0) ; BGT.S LAB_0DD5
		d.uwX = 0xFFF6;
		d.ubFacing ^= 2;
		f.swSpeedX = (int16_t)(0 - f.swSpeedX);
	}
	if ((int16_t)d.uwY > 0xC8) d.uwY = 0;                             // CMPI.W #$c8,8(A0) ; BLE.S ; MOVE.W #0,8(A0)
	if ((int16_t)d.uwY < 0) d.uwY = 0xC8;                             // TST.W 8(A0) ; BPL.S ; MOVE.W #$c8
	d.ubAnimPhase = (uint8_t)((d.ubAnimPhase + 1) & 0x0F);            // ADDI.B #1,12(A0) ; ANDI.B #$f,12(A0)
	s.ubPath = DP_FLY;
	s.ubFrame = d.ubAnimPhase;                                        // MOVE.B 12(A0),D0 ; EXT.W ; LSL.W #2 ; 0(A1,D0.W)
	return s;
}

// ---------------------------------------------------------------------------------------------------------
// Turn scheduler glue

namespace {

struct TurnCtx {
	TurnState *pTs;
	ActiveKnights *pAct;
	const RecordSet *pRs;
	const TurnPorts *pPorts;
};

void hookTurnEnd(void *p) {
	TurnCtx &c = *static_cast<TurnCtx *>(p);
	const bool bRoundEnds = ((c.pTs->uwTurn + 1) & 3) == 0;           // the turn word wraps to 0 right after this hook
	if (c.pPorts->pfnTurnEnd) c.pPorts->pfnTurnEnd(c.pPorts->pCtx, *c.pTs, bRoundEnds);
}

void hookNewDay(void *p) {
	TurnCtx &c = *static_cast<TurnCtx *>(p);
	if (c.pPorts->pfnNewDay) c.pPorts->pfnNewDay(c.pPorts->pCtx, *c.pTs);
}

void hookTurnStart(void *p) {
	TurnCtx &c = *static_cast<TurnCtx *>(p);
	const uint16_t uwIdx = c.pTs->uwTurn;
	c.pAct->ulCurrent = c.pRs->addr(uwIdx);                           // MOVE.L LAB_0633,0(A0) (LAB_05E4)
	if (c.pPorts->pfnTurnStart) c.pPorts->pfnTurnStart(c.pPorts->pCtx, *c.pTs, c.pRs->aRec[uwIdx]);
}

void hookAiDay(Knight &k, void *p) {
	TurnCtx &c = *static_cast<TurnCtx *>(p);
	if (c.pPorts->pfnAiDay) c.pPorts->pfnAiDay(c.pPorts->pCtx, *c.pTs, k);
}

}  // namespace

// LAB_0DBD (mog.asm 24927)
void turnSetup(TurnState &ts, ActiveKnights &act, const RecordSet &rs, const TurnPorts &ports) {
	TurnCtx c = {&ts, &act, &rs, &ports};
	RuleHooks h = {hookTurnEnd, hookNewDay, hookTurnStart, hookAiDay, &c};
	setupTurn(ts, rs.aRec, &h);
}

// LAB_0DB9 .. LAB_0DBB (mog.asm 24883)
TurnNext turnRun(TurnState &ts, ActiveKnights &act, const RecordSet &rs, const TurnPorts &ports) {
	TurnCtx c = {&ts, &act, &rs, &ports};
	RuleHooks h = {hookTurnEnd, hookNewDay, hookTurnStart, hookAiDay, &c};
	ts.uwSkipped = 0;                                                 // CLR.W LAB_0663
	TurnNext r;
	do {
		r = advanceTurn(ts, act, rs.aRec, &h);                        // BSR LAB_0E04 .. LAB_0DBB; the asm loops to LAB_0DBA
	} while (r == TURN_REPEAT);
	return r;
}

// ---------------------------------------------------------------------------------------------------------
// The map screen: drawing

// LAB_0DA3 (mog.asm 24704)
void mapDrawLairs(const Lair *aLairs, uint32_t ulLairBase, uint32_t ulTarget, uint32_t &ulCursor, const MapDrawOps &ops) {
	ops.pfnSetTarget(ops.pCtx, ulTarget);                             // MOVE.L LAB_05C0,D0 ; JSR LAB_0426+2
	ulCursor = ulLairBase;                                            // MOVE.L 68(A0),LAB_08C6 (LAB_05B9 +68)
	for (int i = 0; i < 24; ++i) {                                    // MOVEQ #23,D0 ; DBF
		const Lair &l = aLairs[i];
		if (!neg16((uint16_t)l.swMapX)) {                             // TST.W 10(A0) ; BMI.S LAB_0DA5
			ops.pfnDraw(ops.pCtx, 0x14, (uint16_t)l.swMapX, (uint16_t)l.swMapY);   // MOVE.W #$14,D0 ; LAB_0CDA
		}
		ulCursor += 20;                                               // ADDI.L #$14,LAB_08C6
	}
}

// LAB_0D9E (mog.asm 24679)
void mapDrawOthers(const RecordSet &rs, uint32_t ulCurrent, const MapDrawOps &ops) {
	for (int i = 0; i < 4; ++i) {                                     // MOVEQ #3,D0 ; DBF
		if (rs.addr(i) == ulCurrent) continue;                        // CMPA.L A0,A1 ; BEQ.S LAB_0DA2
		const Knight &k = rs.aRec[i];
		uint32_t ulSprite = k.ulKind;                                 // MOVE.L 54(A0),D0
		if ((int8_t)k.ubLives > 0) {                                  // TST.B 73(A0) ; BGT.S LAB_0DA0
			if (k.ubFrogDays != 0) ulSprite += 0x2B;                  // TST.B 82(A0) ; BEQ.S ; ADDI.L #$2b,D0
		} else {
			ulSprite = (ulSprite & 0xFFFF0000u) | 0x21;              // MOVE.W #$21,D0
		}
		ops.pfnDraw(ops.pCtx, ulSprite, k.uwMapX, k.uwMapY);
	}
}

// LAB_0D9B (mog.asm 24660)
void mapDrawSelf(const Knight &cur, bool bForced, bool bAmbush, uint32_t *pCurrent, uint32_t *pSaved, const MapDrawOps &ops) {
	uint32_t ulSprite = cur.ulKind + 5;                               // MOVE.L 54(A1),D0 ; ADDQ.L #5,D0
	if (bForced) ulSprite += 5;                                       // TST.W LAB_065E
	if (bAmbush) ulSprite += 10;                                      // TST.W LAB_065C ; ADDI.L #$a,D0
	ops.pfnDraw(ops.pCtx, ulSprite, cur.uwMapX, cur.uwMapY);
	*pSaved = *pCurrent;                                              // MOVE.L LAB_0633,LAB_066E
	ops.pfnJobsRun(ops.pCtx);                                         // JSR LAB_0322
	ops.pfnJobsDraw(ops.pCtx);                                        // JSR LAB_0328
	*pCurrent = *pSaved;                                              // MOVE.L LAB_066E,LAB_0633: a job handler may have changed it
}

// ---------------------------------------------------------------------------------------------------------
// The colour jobs

// LAB_0DC5 (mog.asm 25003)
void mapColourStart(const MapColourCells &c, const MapColourOps &ops) {
	if (*c.puwOn == 0) {                                              // TST.W LAB_0658 ; BNE.S LAB_0DC6
		ops.pfnFadeTo(ops.pCtx);                                      // LEA LAB_0D2B,A0 ; JSR LAB_03F2
		*c.puwOn = 1;
		*c.pulRamp = ops.pfnRampAdd(ops.pCtx, 0x1F, 0xFF, 1, 0);      // JSR LAB_0E5A
		*c.pulCycle = ops.pfnCycleAdd(ops.pCtx, 0x15, 0x17, 1, 0x18); // JSR LAB_0E56
	}
	if (*c.puwDragonActive == 0) ops.pfnDragonSpawn(ops.pCtx);        // LAB_0DC6: TST.W LAB_0667 ; BNE.S ; JSR LAB_0DCB
}

// LAB_0DC8 (mog.asm 25027)
void mapColourStop(const MapColourCells &c, const MapColourOps &ops) {
	if (*c.puwOn != 0) {                                              // TST.W LAB_0658 ; BEQ.S LAB_0DC9
		ops.pfnSlotFree(ops.pCtx, *c.pulRamp);                        // MOVE.L LAB_0661,D0 ; JSR LAB_0E59
		ops.pfnSlotFree(ops.pCtx, *c.pulCycle);                       // MOVE.L LAB_0DDE,D0 ; JSR LAB_0E59
		*c.puwOn = 0;                                                 // CLR.W LAB_0658
	}
	ops.pfnFadeOut(ops.pCtx);                                         // LAB_0DC9: JSR LAB_03F0
}

// LAB_0E52 (mog.asm 25967)
uint32_t mapArenaOfTile(const uint8_t *pTable, uint16_t uwTileIndex) {
	return pTable[(int16_t)uwTileIndex];                              // MOVE.B 0(A0,D1.W),D0 (signed word index) ; MOVE.L D0,LAB_08C4
}

// ---------------------------------------------------------------------------------------------------------
// The menu text

namespace {

// LAB_0E51: MOVE.B (A1)+,(A2)+ until the NUL (copied too).  Returns the length written without the NUL; at most ulCap - 1 bytes.
uint32_t appendStr(char *pcOut, uint32_t ulAt, uint32_t ulCap, const char *pcSrc) {
	while (*pcSrc && ulAt + 1 < ulCap) pcOut[ulAt++] = *pcSrc++;
	pcOut[ulAt] = 0;
	return ulAt;
}

}  // namespace

// LAB_0E4E (mog.asm 25915)
void mapMenuRow(const ArrivalEntry &row, const RecordSet &rs, const MapMenuText &t, const MapDrawOps &ops, char *pcOut, uint32_t ulCap) {
	pcOut[0] = 0;
	if (row.ulKind == 2) {                                            // CMPI.L #2,4(A5) ; LEA LAB_08F1,A1
		appendStr(pcOut, 0, ulCap, t.pcLair);
	} else if (row.ulKind == 1) {                                     // CMPI.L #1,4(A5) ; LEA LAB_08F2,A1 ; BSR LAB_0E51
		uint32_t n = appendStr(pcOut, 0, ulCap, t.pcBattle);
		const int i = rs.index(row.ulKey);                            // MOVEA.L (A5),A4 ; MOVEA.L 108(A4),A1
		if (i >= 0) appendStr(pcOut, n, ulCap, ops.pfnString(ops.pCtx, rs.aRec[i].ulName));   // SUBQ.L #1,A2: the NUL is overwritten
	} else {
		const uint32_t ulIdx = row.ulKind - 0x15;                     // SUBI.L #$15,D0 ; LSL.L #2,D0 ; MOVEA.L 0(A1,D0.L),A1
		if (ulIdx < (uint32_t)MAP_PLACE_TEXTS) appendStr(pcOut, 0, ulCap, t.apcPlaces[ulIdx]);
	}
}

// LAB_0E49 (mog.asm 25880)
void mapMenuDraw(const Knight &cur, ActiveKnights &act, uint32_t ulE3, const ArrivalEntry *aRows, const RecordSet &rs,
                 const MapMenuText &t, uint16_t &uwX, uint16_t &uwY, uint8_t &ubDigit, char *pcBuf, const MapDrawOps &ops) {
	enum { CAP = 64 };
	act.ulSpriteBank = ulE3;                                               // MOVE.L 0(A1),10(A0) with A1 = LAB_05E3
	uwX = 0x32;                                                       // MOVE.W #$32,LAB_08F5
	uwY = 0x64;                                                       // MOVE.W #$64,LAB_08F6
	ops.pfnDraw(ops.pCtx, 0x20, 0x32, 0x64);                          // the frame: MOVE.L #$20,D0 ; LAB_0CDA
	uint32_t n = appendStr(pcBuf, 0, CAP, ops.pfnString(ops.pCtx, cur.ulName));   // MOVEA.L 108(A0),A1 ; copy to LAB_08E9
	appendStr(pcBuf, n, CAP, t.pcMay);                                // SUBQ.L #1,A2 ; copy LAB_08EA
	ops.pfnDraw(ops.pCtx, cur.ulKind, (uint16_t)(uwX + 5), (uint16_t)(uwY + 5));   // the knight's own sprite
	ops.pfnText(ops.pCtx, pcBuf, (uint16_t)(uwX + 0x0F), (uint16_t)(uwY + 5));     // LAB_0431, D2 = 0
	uwY = (uint16_t)(uwY + 0x0F);                                     // ADDI.W #$f,LAB_08F6
	ubDigit = 0x31;                                                   // MOVE.B #$31,LAB_0653
	for (int i = 0; i < ARRIVAL_ROWS; ++i) {                          // LAB_0E4C: until TST.L (A5) ; BEQ.S LAB_0E4D
		if (aRows[i].ulKey == 0) break;
		pcBuf[0] = (char)ubDigit;                                     // MOVE.B LAB_0653,(A2)+ ; MOVE.B #$20,(A2)+
		pcBuf[1] = ' ';
		mapMenuRow(aRows[i], rs, t, ops, pcBuf + 2, CAP - 2);         // BSR LAB_0E4E
		ops.pfnText(ops.pCtx, pcBuf, (uint16_t)(uwX + 5), uwY);       // LAB_0431 at (LAB_08F5 + 5, LAB_08F6)
		ubDigit = (uint8_t)(ubDigit + 1);                             // ADDI.B #1,LAB_0653
		uwY = (uint16_t)(uwY + 6);                                    // ADDI.W #6,LAB_08F6
	}
}

// ---------------------------------------------------------------------------------------------------------
// The frame loop

namespace {

inline const Knight &currentOf(const MapLoopCells &c) {
	const int i = c.pRs->index(*c.pulCurrent);
	return c.pRs->aRec[i < 0 ? 0 : i];                                // always one of the four knights on the map
}

inline void step(const MapLoopOps &ops, MapStep e) { ops.pfnStep(ops.pCtx, e); }

}  // namespace

// LAB_0DAB, LAB_0DAC (mog.asm 24755)
void mapScreenEnter(const MapLoopCells &c, const MapLoopOps &ops) {
	step(ops, MS_KEY_RESET);                                          // JSR LAB_0B82
	step(ops, MS_SET_TARGET_BG);                                      // MOVE.L LAB_05C0,D0 ; JSR LAB_0426+2
	step(ops, MS_COPY_BACKDROP);                                      // the DBF copy, then LAB_0C21 on LAB_05C2
	step(ops, MS_DRAW_LAIRS);                                         // JSR LAB_0DA3
	step(ops, MS_DRAW_OTHERS);                                        // BSR LAB_0D9E
	step(ops, MS_BLIT_BOTH);                                          // JSR LAB_0418
	step(ops, MS_SET_TARGET_SHOWN);                                   // MOVE.L LAB_0D92,D0 ; JSR LAB_0426+2
	step(ops, MS_SETUP_TURN);                                         // JSR LAB_0DBD
	step(ops, MS_MOVE);                                               // BSR LAB_0DA6
	step(ops, MS_DRAW_SELF);                                          // BSR LAB_0D9B
	step(ops, MS_FLIP);                                               // JSR LAB_0416
	step(ops, MS_COLOUR_START);                                       // JSR LAB_0DC5
	*c.puwFrameBudget = 2;                                            // MOVE.W #2,LAB_05BA
}

// LAB_0DAD .. LAB_0DBC (mog.asm 24785)
MapFrameResult mapFrame(const MapLoopCells &c, const MapLoopOps &ops) {
	step(ops, MS_FRAME_START);                                        // LAB_0DAD: JSR LAB_031D
	uint16_t uwMove = 0;                                              // D1
	if (currentOf(c).ulKind == KIND_AI) {                                   // CMPI.L #4,54(A0) ; BNE.S LAB_0DAF: the AI knight
		step(ops, MS_ROAM_SORT);                                      // BSR LAB_0DDF
		step(ops, MS_ROAM_PICK);                                      // BSR LAB_0DEA
		step(ops, MS_AI_STAT);                                        // JSR LAB_0E2B
		step(ops, MS_POTION);                                         // JSR LAB_0E23
		if (*c.puwStepGate == 0) {                                    // TST.W LAB_066B ; BNE.S LAB_0DAE
			step(ops, MS_OPPONENT);                                   // JSR LAB_0DED
			step(ops, MS_SCROLL);                                     // JSR LAB_0E27
			step(ops, MS_SPEED);                                      // JSR LAB_0E29
			if (ops.pfnWish(ops.pCtx)) step(ops, MS_TOWN);            // JSR LAB_0E2D ; TST.W D0 ; BEQ.S ; JSR LAB_0E35
		}
		step(ops, MS_ARRIVE);                                         // LAB_0DAE: BSR LAB_0E17
		step(ops, MS_TERRAIN);                                        // BSR LAB_0DD8
		*c.puwSpent = (uint16_t)(*c.puwSpent + 1);                    // ADDQ.W #1,LAB_0655
		if (*c.puwBlocked == 0) uwMove = ops.pfnAiWalk(ops.pCtx);     // TST.W LAB_0DDA+2 ; BNE.S ; BSR LAB_0E0C ; MOVE.W D0,D1
	} else {
		step(ops, MS_TERRAIN);                                        // LAB_0DAF: BSR LAB_0DD8
		if (*c.puwBlocked == 0) uwMove = ops.pfnJoystick(ops.pCtx);   // JSR LAB_00EE (D1)
	}
	*c.puwMove = uwMove;                                              // LAB_0DB0: MOVE.W D1,LAB_0656
	bool bKeys = true;
	if (*c.puwAmbush == 0 && *c.puwForced == 0) {                     // TST.W LAB_065C ; BNE ; TST.W LAB_065E ; BNE
		if (*c.pulLocked != 0) {
			bKeys = false;                                            // TST.L LAB_0662 ; BNE.W LAB_0DB4
		} else if ((uwMove & 0x0F) != 0 && currentOf(c).ulKind != KIND_AI) {   // ANDI.W #$f,D1 ; BEQ.S ; CMPI.L #4,54(A0) ; BEQ.S
			*c.puwSpent = (uint16_t)(*c.puwSpent + 1);                // ADDQ.W #1,LAB_0655: a step costs one more
		}
	}
	if (bKeys) {                                                      // LAB_0DB1
		const uint16_t uwKey = ops.pfnReadKey(ops.pCtx);              // MOVE.W SECSTRT_21,D0 ; JSR LAB_0D8D
		if (currentOf(c).ulKind != KIND_AI) {                               // CMPI.L #4,54(A1) ; BEQ.S LAB_0DB3
			if (uwKey == 0x20) return MAPFRAME_STATUS;                // space: the status screen (mapStatusScreen), then BRA.W LAB_0DAB
			if (uwKey == 0x45) *c.puwSpent = *c.puwBudget;            // 'E': end the turn
		}
		if (uwKey == 0x51) return MAPFRAME_QUIT;                      // LAB_0DB3: 'Q' -> JMP LAB_0064
	}
	if (*c.puwMove & 0x10) {                                          // LAB_0DB4: MOVE.W LAB_0656,D0 ; BTST #4,D0: fire
		if (*c.puwAmbush != 0) step(ops, MS_MODE_CLEAR);              // TST.W LAB_065C ; BEQ.S ; BSR LAB_0E04
		if (ops.pfnMenu(ops.pCtx) != 0) return MAPFRAME_RESTART;      // LAB_0DB5: BSR LAB_0E3D ; TST.W D0 ; BRA.W LAB_0DAB
	}
	if (ops.pfnDragonAttacks(ops.pCtx)) step(ops, MS_DRAGON_FIGHT);   // LAB_0DB6: the dragon attacks: LAB_0DC8 then LAB_0083
	if (lt16(*c.puwSpent, *c.puwBudget)) {                            // LAB_0DB9: CMP.W LAB_0665,D0 ; BLT.W LAB_0DBC
		step(ops, MS_MOVE);                                           // LAB_0DBC: JSR LAB_0DA6
		step(ops, MS_DRAW_SELF);                                      // JSR LAB_0D9B
		step(ops, MS_FLIP);                                           // JSR LAB_0416
		step(ops, MS_SHOW_BACKGROUND);                                // LAB_05C0 -> LAB_0D92, JSR LAB_0419
		step(ops, MS_FRAME_WAIT);                                     // JSR LAB_031F
		return MAPFRAME_NEXT;                                         // BRA.W LAB_0DAD
	}
	return MAPFRAME_TURN_OVER;                                        // the budget is used up: mapTurnOver
}

// The space key's status screen (mog.asm 24846): JSR LAB_0DC8 ; MOVEQ #9,D0 ; JSR LAB_04CF ; JSR LAB_0B82, then BRA.W LAB_0DAB.
void mapStatusScreen(const MapLoopOps &ops) {
	step(ops, MS_COLOUR_STOP);                                        // JSR LAB_0DC8
	step(ops, MS_STATUS_SCREEN);                                      // MOVEQ #9,D0 ; JSR LAB_04CF
	step(ops, MS_KEY_RESET);                                          // JSR LAB_0B82
}

// LAB_0DB9 once the budget is used up: rt_ow_turn; TST.W D0 ; BEQ.W LAB_0DAB (play on) else JMP LAB_0064 (the game is over).
bool mapTurnOver(const MapLoopOps &ops) {
	return ops.pfnTurn(ops.pCtx) != 0;
}

// LAB_0DAB (mog.asm 24755).  The game runs the same pieces as scenes (src/game/scenes/map.cpp, status.cpp, turn_end.cpp);
// this loop is their composition for the host tests that compare the call order with the original.
void mapLoopRun(const MapLoopCells &c, const MapLoopOps &ops) {
	for (;;) {
		mapScreenEnter(c, ops);
		MapFrameResult r;
		do {
			r = mapFrame(c, ops);
		} while (r == MAPFRAME_NEXT);
		if (r == MAPFRAME_STATUS) mapStatusScreen(ops);
		if (r == MAPFRAME_TURN_OVER && !mapTurnOver(ops)) continue;   // play on: BEQ.W LAB_0DAB
		if (r == MAPFRAME_QUIT || r == MAPFRAME_TURN_OVER) {
			ops.pfnQuit(ops.pCtx);                                    // JMP LAB_0064
			return;
		}
	}
}

}}  // namespace ms::game
