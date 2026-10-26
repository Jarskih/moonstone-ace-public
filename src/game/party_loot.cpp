// game/party_loot - see include/game/party_loot.hpp.
#include "game/party_loot.hpp"

#include "game/loot.hpp"
#include "game/rules/party.hpp"
#include "game/scene_places.hpp"
#include "game/scene_town.hpp"

namespace ms { namespace game {

namespace {

inline uint8_t *bytes(Inventory &inv) { return reinterpret_cast<uint8_t *>(&inv); }

enum : uint16_t { SLOT_KEYS = 0x14, SLOT_MOONSTONES = 0x16, SLOT_LAST_COUNT = 0x12 };

inline bool living(const Knight &k) { return (int8_t)k.ubLives > 0; }

// The scratch copy of a pile the original functions may change (they write the other knight's armour / sword / hp and its counts):
// only the fields they read are set (a whole-record copy would call memcpy, which the 68k test images do not link).
inline void scratchKnight(Knight &k, const PileKnight &p) {
	k.ulArmour = p.ulArmour;
	k.ulSword = p.ulSword;
	k.swHp = p.swHp;
}
inline void copyInventory(Inventory &dst, const Inventory &src) {
	uint8_t *d = bytes(dst);
	const uint8_t *s = reinterpret_cast<const uint8_t *>(&src);
	for (unsigned i = 0; i < sizeof(Inventory); ++i) {
		d[i] = s[i];
		__asm__ volatile("" ::: "memory");                         // keep the loop a loop: the compiler would turn it into a memcpy call
	}
}

}  // namespace

void partyCopyMove(Knight &member, Inventory &memberInv, const PileKnight &srcKnight, const Inventory &srcInv, uint16_t uwSlot,
                   bool bCreatureScene) {
	Knight scratch;
	Inventory scratchInv;
	scratchKnight(scratch, srcKnight);
	copyInventory(scratchInv, srcInv);
	lootMove(member, &scratch, memberInv, scratchInv, uwSlot, bCreatureScene);
}

void partyCopyGold(Knight &member, uint16_t uwPile, const GameData &d) {
	uint16_t scratch = uwPile;
	exchangeGold(member, scratch, d);
}

bool partyCopyArmour(Knight &member, const PileKnight &srcKnight) {
	Knight scratch;
	scratchKnight(scratch, srcKnight);
	return exchangeArmour(member, scratch);
}

bool partyCopySword(Knight &member, Inventory &memberInv, const PileKnight &srcKnight, const Inventory &srcInv, bool bCreatureScene) {
	Knight scratch;
	Inventory scratchInv;
	scratchKnight(scratch, srcKnight);
	copyInventory(scratchInv, srcInv);
	return exchangeSword(member, scratch, memberInv, scratchInv, bCreatureScene);
}

bool partyBkPotFill(const RecordSet &rs, int iPot) {
	if (!rs.coop() || iPot < 0 || iPot >= rs.count() || iPot == REC_DRAGON) return false;
	Knight &pot = rs.rec(iPot);
	Inventory &potInv = rs.inv(iPot);
	uint8_t *pPot = bytes(potInv);
	bool bMoved = false;
	for (int i = REC_BK0; i < REC_BK0 + rs.pParty->ubBk; ++i) {
		if (i == iPot) continue;
		Knight &k = rs.rec(i);
		uint8_t *pSrc = bytes(rs.inv(i));
		for (unsigned s = 0; s < sizeof(Inventory); s += 2) {
			if (pSrc[s] == 0) continue;
			bMoved = true;
			if (s >= SLOT_KEYS) pPot[s] = (uint8_t)(pPot[s] | pSrc[s]);       // keys, moonstones: flag bytes
			else pPot[s] = (uint8_t)(pPot[s] + pSrc[s]);
			pSrc[s] = 0;
		}
		if (k.uwGold != 0) {
			pot.uwGold = (uint16_t)(pot.uwGold + k.uwGold);
			k.uwGold = 0;
			bMoved = true;
		}
	}
	return bMoved;
}

uint16_t lootNextMember(uint16_t &uwCounter, const uint32_t aKnights[4], uint32_t ulCurrent, uint8_t ubMembers) {
	uint16_t idx = 0;
	for (int i = 0; i < 4; ++i) {
		idx = (uint16_t)((uwCounter + 1) & 3);
		uwCounter = idx;
		if (idx < ubMembers && aKnights[idx] != ulCurrent) break;
	}
	return idx;
}

void shopQueueBegin(ShopQueue &q, const RecordSet &rs) {
	q.ubCount = 0;
	q.ubPos = 0;
	q.ubComputer = 0;
	if (!rs.coop()) return;
	q.ubComputer = rs.pParty->ubComputer;
	const int n = rs.pParty->ubSize;
	const int iLeader = partyLeader(rs);
	for (int k = 0; k < n; ++k) {
		const int i = (iLeader + k) % n;
		if (living(rs.rec(i)) || k == 0) q.aubRec[q.ubCount++] = (uint8_t)i;      // the leader is always in

	}
}

int partyStoneOwnerRecord(const RecordSet &rs, uint16_t uwMoonFrame) {
	ShopQueue q;
	shopQueueBegin(q, rs);
	for (uint8_t i = 0; i < q.ubCount; ++i)
		if (stoneMatches(uwMoonFrame, rs.inv(q.aubRec[i]).ubMoonstones)) return q.aubRec[i];
	return -1;
}

int shopQueueStep(ShopQueue &q, int iDir) {
	if (q.ubCount == 0) return -1;
	const int n = q.ubCount;
	for (int s = 0; s < n; ++s) {                                     // a computer knight does not shop on its own (ROADMAP 8.3b: it echoes the human's buys): the arrows skip it
		q.ubPos = (uint8_t)((q.ubPos + (iDir < 0 ? n - 1 : 1)) % n);
		if (!((q.ubComputer >> q.aubRec[q.ubPos]) & 1)) break;
	}
	return q.aubRec[q.ubPos];

}

}}  // namespace ms::game
