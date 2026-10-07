// game/rules/settle - see include/game/rules/settle.hpp.  Byte/word arithmetic follows the asm.
#include "game/rules/settle.hpp"

namespace ms { namespace game {

namespace {

// Inventory slots are two bytes; the asm reads a slot's count as a BYTE at the even offset and moves the flag slots
// as WORDS.  Big-endian OR/CLR on a word is the same as on its two bytes, so the pure code works bytewise and does
// not depend on the host byte order.
inline uint8_t *bytes(Inventory &inv) { return reinterpret_cast<uint8_t *>(&inv); }

// LAB_0028: the slots that change hands after a fight, in this order, terminated by $FFFF.
const uint8_t kTransferSlots[12] = {0x16, 0x14, 0x04, 0x06, 0x0E, 0x12, 0x08, 0x0C, 0x10, 0x00, 0x02, 0x0A};

// LAB_0025 / LAB_0027: OR the loser's word into the winner's, clear the loser's word.
void moveFlagWord(uint8_t *pDst, uint8_t *pSrc, uint8_t slot) {
	pDst[slot] |= pSrc[slot];
	pDst[slot + 1] |= pSrc[slot + 1];
	pSrc[slot] = 0;
	pSrc[slot + 1] = 0;
}

// LAB_0024: add the loser's count byte to the winner's (mod 256), clear the loser's word.
void moveCount(uint8_t *pDst, uint8_t *pSrc, uint8_t slot) {
	pDst[slot] = (uint8_t)(pDst[slot] + pSrc[slot]);
	pSrc[slot] = 0;
	pSrc[slot + 1] = 0;
}

}  // namespace
// ---------------------------------------------------------------------------------------------------------
// LAB_0021 (mog.asm 465)
void settleGold(Knight &winner, Knight &loser) {
	uint16_t half = (uint16_t)(loser.uwGold >> 1);                     // LSR.W #1,D0
	winner.uwGold = (uint16_t)(winner.uwGold + half);                  // ADD.W D0,74(A0)
	loser.uwGold = half;                                               // MOVE.W D0,74(A1)
}

// LAB_001C (mog.asm 435): A0 = winner, A1 = loser, A2 = loser inventory, A3 = winner inventory
void settleFight(Knight &winner, Knight &loser, Inventory &winInv, Inventory &loseInv, Knight *aKnights,
                 Inventory *aInv) {
	uint8_t *pWin = bytes(winInv);
	uint8_t *pLose = bytes(loseInv);
	bool bMoved = false;                                               // D5
	if (winner.ubType == raw(ActorType::Dragon)) settleGold(winner, loser);              // CMPI.B #$14,77(A0) ; BSR LAB_0021
	if (loser.ubLives == 0) {
		// LAB_0022: the loser is out of lives, everything moves
		for (int i = 0; i < 12; ++i) {
			uint8_t slot = kTransferSlots[i];
			if (slot == 0x16 || slot == 0x14) {
				moveFlagWord(pWin, pLose, slot);                       // LAB_0025
			} else {
				if (slot == 0x04) {
					if (pLose[4] == 0) continue;                       // LAB_0026: TST.B ; BEQ next
					loser.ulSword = raw(SwordItem::Sharpness);                              // MOVE.L #$19,88(A1) (sic: the loser's)
				}
				moveCount(pWin, pLose, slot);                          // LAB_0024
			}
		}
	} else {
		// LAB_001E: the first slot the loser owns, one piece only
		for (int i = 0; i < 12; ++i) {
			uint8_t slot = kTransferSlots[i];
			if (pLose[slot] == 0) continue;                            // TST.B 0(A2,D0.W) ; BEQ next
			bMoved = true;                                             // MOVEQ #1,D5
			if (slot == 0x16 || slot == 0x14) {
				moveFlagWord(pWin, pLose, slot);                       // LAB_0027: straight to LAB_0020, no gold
				knightsRecalcAll(aKnights, aInv);
				return;
			}
			pLose[slot] = (uint8_t)(pLose[slot] - 1);
			pWin[slot] = (uint8_t)(pWin[slot] + 1);
			if (pLose[slot] == 0 && slot == 0x04) loser.ulSword = raw(SwordItem::Long);  // the last sharp sword is gone
			break;                                                     // BNE LAB_001F / fall into LAB_001F
		}
		if (!bMoved) settleGold(winner, loser);                        // LAB_001F: TST.W D5 ; BSR LAB_0021
	}
	knightsRecalcAll(aKnights, aInv);                                  // LAB_0020: JSR LAB_0011
}

// LAB_000E (mog.asm 351)
uint8_t settleDefeats(Knight &first, Knight &second) {
	uint8_t bits = 0;                                                  // MOVE.W #0,LAB_05DC
	if (!(first.swHp > 0)) {                                           // TST.W 80(A0) ; BGT
		bits |= 1;
		first.swHp = first.swHpMax;
		first.ubLives = (uint8_t)(first.ubLives - 1);
	}
	if (!(second.swHp > 0)) {
		second.swHp = second.swHpMax;
		second.ubLives = (uint8_t)(second.ubLives - 1);
		bits |= 2;
	}
	return bits;
}

// LAB_000D (mog.asm 343)
void knightMarkDead(Knight &k) { k.swHp = (int16_t)0xFFFF; }

// World form (ROADMAP 9.3e).
void settleFight(World &w, KnightIdx eWinner, KnightIdx eLoser) {
	settleFight(w.party.aRecords[raw(eWinner)], w.party.aRecords[raw(eLoser)], w.party.aInv[raw(eWinner)],
	            w.party.aInv[raw(eLoser)], w.party.aRecords, w.party.aInv);
}

}}  // namespace ms::game
