// rt/party_loot - see rt/party_loot.hpp.
#include "rt/party_loot.hpp"

#include "game/api/data.hpp"
#include "game/constants.hpp"
#include "game/rules/party.hpp"
#include "game/party_loot.hpp"
#include "game/state_bind.hpp"
#include "rt/party.hpp"

namespace {

using namespace ms::game;

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }
inline Knight *knightAt(uint32_t ulAddr) { return reinterpret_cast<Knight *>((uintptr_t)ulAddr); }
inline Inventory *invAt(uint32_t ulAddr) { return reinterpret_cast<Inventory *>((uintptr_t)ulAddr); }

RecordSet recordSet() {
	RecordSet rs = {mogKnights, mogInventories, addr(mogKnights)};
	rt::partyRecords(rs);
	return rs;
}

bool isMemberAddr(const RecordSet &rs, uint32_t ulAddr) { return partyIsMember(rs, rs.index(ulAddr)); }
bool living(const Knight &k) { return (int8_t)k.ubLives > 0; }

// Every living member except the one at address ulExcept (the screen's knight, the visiting knight).
template <typename F> void forOthers(uint32_t ulExcept, F fn) {
	const RecordSet rs = recordSet();
	for (int i = 0; i < rs.pParty->ubSize; ++i) {
		if (rs.addr(i) == ulExcept || !living(rs.rec(i))) continue;
		fn(rs.rec(i), invAt(rs.rec(i).ulInventory));
	}
}

}  // namespace

namespace rt {

void lootShareBegin(LootShare &s) {
	s.bOn = false;
	if (!partyCoop()) return;
	const RecordSet rs = recordSet();
	const uint32_t ulScene = mogSceneId;
	s.bCreature = ulScene == SCENE_CREATURE;
	if (!(s.bCreature || ulScene == SCENE_DRAGON || ulScene == SCENE_MEET)) return;
	if (!isMemberAddr(rs, mogUiKnight)) return;
	if (!s.bCreature && isMemberAddr(rs, mogUiKnight2)) return;           // an exchange between members is no loot
	s.bOn = true;
	const Inventory &pile = *invAt(mogUiInventory2[0]);
	uint8_t *pDst = reinterpret_cast<uint8_t *>(&s.srcInv);
	for (unsigned i = 0; i < sizeof(Inventory); ++i) pDst[i] = reinterpret_cast<const uint8_t *>(&pile)[i];
	if (s.bCreature) {
		s.srcKnight.ulArmour = 0;
		s.srcKnight.ulSword = 0;
		s.srcKnight.swHp = 0;
		s.uwPileGold = *reinterpret_cast<const uint16_t *>((uintptr_t)mogCurLair + 8);   // Lair +8
	} else {
		const Knight &src = *knightAt(mogUiKnight2);
		s.srcKnight = pileKnightOf(src);
		s.uwPileGold = src.uwGold;
	}
}

void lootShareMove(const LootShare &s, uint16_t uwSlot) {
	if (!s.bOn) return;
	forOthers(mogUiKnight, [&](Knight &k, Inventory *pInv) { partyCopyMove(k, *pInv, s.srcKnight, s.srcInv, uwSlot, s.bCreature); });
}

void lootShareGold(const LootShare &s) {
	if (!s.bOn) return;
	forOthers(mogUiKnight, [&](Knight &k, Inventory *) { partyCopyGold(k, s.uwPileGold, g_gameData); });
}

void lootShareArmour(const LootShare &s) {
	if (!s.bOn || s.bCreature) return;
	forOthers(mogUiKnight, [&](Knight &k, Inventory *) { partyCopyArmour(k, s.srcKnight); });
}

void lootShareSword(const LootShare &s) {
	if (!s.bOn) return;
	forOthers(mogUiKnight, [&](Knight &k, Inventory *pInv) { partyCopySword(k, *pInv, s.srcKnight, s.srcInv, s.bCreature); });
}

void lootBkPot() {
	if (partyCoop()) partyBkPotFill(recordSet(), recordSet().index(mogActive.ulOpponent));   // the defeated black knight's pile
}

uint16_t lootNextMemberIndex(uint16_t &uwCounter, const uint32_t aKnights[4], uint32_t ulCurrent) {
	return lootNextMember(uwCounter, aKnights, ulCurrent, partyMap().ubSize);
}

// ---- the town queue ------------------------------------------------------------------------------------------------------------

namespace {
ShopQueue s_queue;
ShopVisit *s_pVisit;                                   // the guard alive on the stack (set by its constructor)
}

ShopVisit::ShopVisit() : m_bOn(partyCoop()), m_ulCur(mogCurKnight), m_ulAct(mogActive.ulCurrent) {
	if (!m_bOn) return;
	shopQueueBegin(s_queue, recordSet());
	s_pVisit = this;
	turnActivate(true);                                            // the corner token / name and the banner (ROADMAP 8.5a)
	turnBegin();                                                   // the first member's turn: banner due
}

ShopVisit::~ShopVisit() {
	if (!m_bOn) return;
	s_pVisit = nullptr;
	turnActivate(false);
	mogCurKnight = m_ulCur;
	mogActive.ulCurrent = m_ulAct;
}

void shopSwitch(void *, int iDir) {
	if (!s_pVisit) return;
	const int iNext = shopQueueStep(s_queue, iDir);
	if (iNext < 0) return;
	const uint32_t ulOld = mogCurKnight;
	const uint32_t ulMember = recordSet().addr(iNext);
	mogCurKnight = ulMember;
	mogActive.ulCurrent = ulMember;
	if (mogUiKnight == ulOld) mogUiKnight = ulMember;              // the sheet's knight (LAB_068B) follows
	turnBegin();                                                   // the new member's turn: banner due
}

unsigned shopMembers(uint32_t aAddr[PARTY_MAX]) {
	if (!s_pVisit) return 0;
	const RecordSet rs = recordSet();
	for (unsigned i = 0; i < s_queue.ubCount; ++i) aAddr[i] = rs.addr(s_queue.aubRec[i]);
	return s_queue.ubCount;
}

}  // namespace rt

// ---- valley and Stonehenge for the party ---------------------------------------------------------------------------------------

#include "game/rules/rituals.hpp"
#include "game/scene_places.hpp"

namespace rt {

void partyValleyWin() {
	if (!partyCoop()) return;
	forOthers(mogCurKnight, [](Knight &k, Inventory *pInv) { valleyVictory(k, *pInv); });         // +3 progress, the keys are taken from everyone
}

void partyValleyStone(uint8_t ubBit) {
	if (!partyCoop()) return;
	forOthers(mogCurKnight, [ubBit](Knight &, Inventory *pInv) { pInv->ubMoonstones = (uint8_t)(pInv->ubMoonstones | (1u << ubBit)); });
}

void partyStoneOwner(uint16_t uwMoonFrame) {
	if (!partyCoop()) return;
	const RecordSet rs = recordSet();
	const int iOwner = partyStoneOwnerRecord(rs, uwMoonFrame);
	if (iOwner < 0) return;
	mogCurKnight = rs.addr(iOwner);                            // the stone owner steps up: its ending (stoneEnding reads its kind)
	mogActive.ulCurrent = rs.addr(iOwner);
}

}  // namespace rt
