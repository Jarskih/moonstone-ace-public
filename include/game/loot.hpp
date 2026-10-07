// game/loot - the loot / inventory-transfer / item-use logic of the meeting screens of mog.asm in C++ (ROADMAP 6.9).
//
// The screens of states 1 (knight meets knight), 2 (creature loot), 3 (wizard), 6 (market), 8 and 11 (exchange),
// 9 (temple) and 10 (dragon loot) share one click handler, LAB_052A..LAB_0568.  The town agent (6.7, scene_town.hpp)
// ported the smith / market / exchange / temple branches of it; this file ports the rest: the dispatch by button
// type (LAB_052C), the loot move (LAB_053F), the use-item handler (LAB_052D..LAB_053A), the discard button (LAB_053D),
// the "next knight" cycling (LAB_0527/LAB_0528) and the per-screen setup that picks the button tables and the
// snapshot pointers (LAB_058A).
//
// Pure on purpose, like scene_town.hpp: no ACE, no OS, no globals, no UI.  Everything the asm reaches through a
// pointer is a reference the caller supplies; everything that is a sound, a jingle, a screen redraw, a map routine or
// the generator is decided here (an enum / a needs-roll predicate) and performed by src/rt/loot.cpp in the original's
// order.  tests/test_loot.py compares every function with a literal Python model of the asm.
//
// Quirks of the original that are reproduced on purpose are marked "QUIRK".
#pragma once
#include <stdint.h>

#include "game/state.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// The click dispatch, LAB_052C.  type = the low nibble of the hit region's word +20, flags = the button object's
// word +8 (bit 4: "use", bit 5: "take", bit 6: stat purchase allowed).

enum LootAction {
	LA_NONE = 0,       // RTS
	LA_USE = 1,        // type 5   -> LAB_052D (use an item; without flag bit 4 it is the loot move)
	LA_LOOT = 2,       // type 1   -> LAB_053F
	LA_TEMPLE = 3,     // type 3   -> LAB_0544 (town agent: temple stat / dagger top-up)
	LA_SMITH = 4,      // type $a  -> LAB_0558 (town agent: smith click)
	LA_DROP = 5,       // type $c  -> LAB_053D
	LA_EXCHANGE = 6    // flag bit 5, any other type -> LAB_054A (town agent: armour / sword / gold)
};

// LAB_052C: the compare chain 5, 1, 3, $a, $c, then BTST #5.
LootAction lootClassify(uint16_t type, uint16_t flags);

// ---------------------------------------------------------------------------------------------------------
// LAB_0528: the "other knight" of the exchange screens cycles through the four knights, skipping the current one.
// counter is the word LAB_0526 (updated), aKnights the table LAB_0525 (the four Knight addresses), ulCurrent LAB_068B.
// Returns the chosen index.  QUIRK: the asm loops until the entry differs from the current knight and would never
// end if all four were equal; the pure code stops after four steps (the four knights are distinct in the game).
uint16_t lootNextKnight(uint16_t &uwCounter, const uint32_t aKnights[4], uint32_t ulCurrent);

// ---------------------------------------------------------------------------------------------------------
// LAB_053F: the loot move (a "take" button, flag bit 5).  The caller has done the state-6 test (the market) and the
// BTST #5 test, and plays the sound ($9c) before.  slot is the Inventory offset of the clicked item.
//  * $14 (keys) and $16 (moonstones): the whole byte moves (flag mask), the source is cleared.
//  * 4 (sword of sharpness, LAB_0551): the source loses one, my sword becomes $19 and, except on the creature screen
//    (state 2), I gain the count and the other knight's sword falls back to $16.  pOther is only used then and may be
//    null on the creature screen.  QUIRK: no check that the source has one (the byte wraps), and my sword is set
//    whether or not it was.
//  * anything else: one item moves; slot 6 (ring of protection) also gives +20 hp and recalculates first.
// Then LAB_0542: max hp and endurance are recalculated.  The caller does LAB_0689 += 1 and redraws.
// QUIRK: the move is unchecked (no "does the source have one", no cap): counts wrap in the byte like the asm.
// Slots >= 24 (never produced by the button tables) are ignored.
void lootMove(Knight &me, Knight *pOther, Inventory &myInv, Inventory &otherInv, uint16_t slot, bool bCreatureScene);

// ---------------------------------------------------------------------------------------------------------
// The words the item handlers write.
struct LootWords {
	uint16_t uwLastSlot;       // LAB_053B  last item used ($FFFF = none); read by the callers of the screens (LAB_005A, LAB_00A5)
	uint16_t uwDone;           // LAB_0984  non-zero ends the screen loop (LAB_04D0)
	uint16_t uwBadLuck;        // LAB_05D3  set when a gamble failed
	uint16_t uwTurnBudget;     // LAB_0665  the turn budget word (WordSlot)
	uint16_t uwHandOver;       // LAB_053C  1 = the next knight takes over the exchange (LAB_04CF copies the other to LAB_0617+100)
	uint32_t ulPrevScene;      // LAB_068A  scene to return to
};

// LAB_053D: the discard button (type $c): the sword falls back to $16 when slot is 4, the count drops by one, the
// screen ends (LAB_0984 = 1) after LAB_0542's recalculation.  The caller plays the sound and does LAB_0689 += 1.
// QUIRK: as in the asm no check that the count is above zero.
void lootDrop(Knight &me, Inventory &myInv, uint16_t slot, LootWords &w);

// ---------------------------------------------------------------------------------------------------------
// LAB_052D: the use-item handler (type 5 with flag bit 4).  The caller has set LAB_053B to $FFFF, tested the market
// (state 6) and bit 4, and plays the text sound LAB_0BB3 (a stub RTS in the original).
enum UseEffect {
	UE_WIZARD = 1,         // state 3: item consumed twice (QUIRK below), screen ends; redraw
	UE_REST,               // slot 0: sound $9c, knightRest (curse off, hp full, +1 life when already full); redraw
	UE_TURNS_UP,           // slot $a, roll > 10: good jingle LAB_05A1, turn budget doubled; redraw
	UE_TURNS_DOWN,         // slot $a, roll <= 10: bad jingle LAB_05A0, LAB_05D3 = 1, budget halved; redraw
	UE_SCENE_8,            // slot $e: LAB_068A = scene, good jingle, next knight (LAB_0527), JMP LAB_04CF with D0 = 8
	UE_MARK_POS,           // slot 2: good jingle, LAB_0E02 (remember the map position), LAB_0984 = 1; redraw
	UE_TELEPORT_GOOD,      // slot $c, roll > 15: good jingle, LAB_0E05, LAB_0984 = 1; redraw
	UE_TELEPORT_BAD,       // slot $c, roll <= 15: bad jingle, LAB_0E06 (random map position), LAB_0984 = 1; redraw
	UE_SCENE_11,           // slot $10: good jingle, LAB_053C = 1, next knight, JMP LAB_04CF with D0 = 11
	UE_LUCK_GOOD,          // slot $12, roll > 10: LAB_0984 = 1, good jingle; redraw
	UE_LUCK_BAD,           // slot $12, roll <= 10: LAB_0984 = 1, bad jingle, LAB_05D3 = 1, bad jingle again; redraw
	UE_PLAIN               // any other slot: only the decrement; redraw
};

// Slots $a, $c and $12 call the generator (LAB_04A3, 0..100) on the non-wizard path; the shim calls it only then so
// the random stream matches the original.
bool useItemNeedsRoll(uint16_t slot, bool bWizardScene);

// ulScene is LAB_068F.  uwD1AfterSfx is D1 after the sound call of the wizard path: QUIRK, see below; ignored
// elsewhere.  ulRoll is LAB_04A3's result (ignored unless useItemNeedsRoll).
//
// QUIRK (wizard, state 3): the original decrements the item twice.  The second time it uses D1 after
// `JSR LAB_0AA2` (the sound start) without saving it: LAB_0AA2 returns the chosen Paula channel 0..3 (or $0F when
// the sound system is busy) in D1, so the second decrement hits Inventory byte 0..3 / 15, and LAB_053B ends up holding
// that number instead of the item.  uwD1AfterSfx is that D1.W, supplied by the shim.
UseEffect useItem(Knight &me, Inventory &myInv, uint16_t slot, bool bWizardScene, uint16_t uwD1AfterSfx,
                  uint32_t ulRoll, uint32_t ulScene, LootWords &w);

// ---------------------------------------------------------------------------------------------------------
// LAB_058A: the screen setup.  Two arrays of 27 button cells (LAB_0699: the "use" buttons, LAB_069A: the "take"
// buttons; 14 bytes each) are filled from one of the pointer tables LAB_0692..LAB_0698 per scene.

#pragma pack(push, 2)
struct ButtonCell {
	uint32_t ulObject;         // +0  the screen object (copied from the table)
	uint16_t uw4;              // +4  0
	uint16_t uw6;              // +6  3
	uint16_t uwFlags;          // +8  3, | $10 (use) or $20 (take) when the table is not LAB_0698; bit 6 = stat purchase possible (LAB_0544)
	uint32_t ul10;             // +10 0
};
#pragma pack(pop)
static_assert(sizeof(ButtonCell) == 14, "LAB_0699: DS.L 94 + DS.W 1 = 27 x 14");

enum { LOOT_TABLES = 7, LOOT_TABLE_LEN = 27 };    // LAB_0692..LAB_0698 (index = label - LAB_0692), 27 pointers each

// Which snapshot the "other" side of the screen comes from (LAB_068D / LAB_068E).
enum LootOther {
	LO_NONE = 0,               // unchanged (screens of one knight)
	LO_OPPONENT,               // states 1, 8, 11: ActiveKnights +4 and its inventory (+96)
	LO_LAIR,                   // state 2: the lair record LAB_08C6 and its loot (lair +0)
	LO_DRAGON                  // state 10: the dragon record LAB_0617 and its inventory
};

struct LootScreen {
	uint8_t ubTabUse;          // table index (0 = LAB_0692 .. 6 = LAB_0698) for the LAB_0699 array
	uint8_t ubTabTake;         // table index for the LAB_069A array
	uint8_t ubOther;           // LootOther
};

// The choices of LAB_058A..LAB_059D by scene.  bTravelMark = LAB_065E != 0 (state 2 uses the plain table then).
LootScreen lootScreenPick(uint32_t ulScene, bool bTravelMark);

// Fills aUse / aTake (27 cells each) from aTabs[0..6] (the 27-pointer tables).  Afterwards, when the knight has at
// least uwStatCost (LAB_06DE = 3) progress points (signed word compare), the first three cells of aUse are replaced
// by the stat "+" buttons aTabs[0][0..2] and their bit 6 toggled, for every stat byte (+70, +71, +72) that is not 5.
// QUIRK: this runs for every scene, not only the temple (state 9), so with 3+ progress points the first three cells
// of every screen's use array are overwritten and have bit 6 flipped.
void lootFillButtons(ButtonCell *aUse, ButtonCell *aTake, const uint32_t *const aTabs[LOOT_TABLES], const LootScreen &scr,
                     const Knight &me, uint16_t uwStatCost);

}}  // namespace ms::game
