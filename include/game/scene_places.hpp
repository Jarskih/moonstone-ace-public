// game/scene_places - the logic of the overworld locations of mog.asm in C++ (ROADMAP 6.8): which place a map node
// is (LAB_007B), the town menus, Math the wizard's gifts (LAB_045E), Mythral the mystic's stat gamble (LAB_047C),
// Stonehenge with Danu's offer and the winning moon phase (LAB_00A1), the Valley of the Gods (LAB_009D) and the
// shared random-gift routines (LAB_0469, LAB_046C, LAB_0471).  Pure on purpose, like scene_town.hpp: no ACE, no OS,
// no globals, no UI.  Every function takes the records and the generator seed the caller owns, so it builds for the
// host tests (tests/test_scene_places.py) as well as the Amiga.  The screens, the text, the sound and the fights stay
// asm; src/rt/scene_places.cpp sequences them around these functions.
//
// Which node is which (node id = MapNode::swId, LAB_069F; the chain of LAB_007B; the asm wins over DOC_MODE_OVERWORLD):
//  * $15..$18  the four home villages (one per knight): a castle visit gives a life (LAB_00B0, scene_town.hpp
//    castleBlessing), then the temple screen (state 9).
//  * $19  a town (LAB_0093): smith (state 5), dice house, healer (LAB_048E), market (state 6); space = temple (state 9).
//  * $1A  a town (LAB_008A) with the same menu but Mythral the mystic (LAB_047C) instead of the market; space opens the
//    scene number that is left in D0 (QUIRK, stays asm: the town-A button 4 / space differ, see townButton).
//  * $1B  Stonehenge (LAB_00A1): with the moonstone of the current moon phase the knight wins the game (LAB_00A8);
//    without one Danu offers a longer life for a magic item (scene 3 = the wizard's item screen, loot.hpp UE_WIZARD).
//  * $1C  the Valley of the Gods (LAB_009D): needs all four keys; the guardian fight (state 10) is the combat engine;
//    a win gives 3 progress points, takes the keys and gives a random moonstone, a loss costs 2 lives and a stat point.
//  * $1E  Math the wizard (LAB_007C, LAB_0456/LAB_045E): a daily gift (an item, a stat point, gold) or a frog curse.
//  * $21  a duel with another knight (LAB_004F, the combat FSM, ROADMAP 6.4).
//
// Quirks of the original that are reproduced on purpose are marked "QUIRK" at their function.  Authority:
// moonshard/moonstone-main/amiga_asm/mog.asm (labels cited per function); tests/test_scene_places.py compares each
// function with a literal Python model of the asm.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/rules/levelling.hpp"
#include "game/rules/rituals.hpp"    // gifts, Math, the mystic, Stonehenge, the valley moved there (ROADMAP 9.6e)   // pickStat, aiBuyStat (ROADMAP 9.6a)
#include "game/state.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// Locations

enum PlaceKind {
	PK_NONE = 0,       // RTS (any other id)
	PK_VILLAGE = 1,    // $15..$18 -> LAB_00B0
	PK_TOWN_A = 2,     // $19      -> LAB_0093
	PK_TOWN_B = 3,     // $1A      -> LAB_008A
	PK_STONEHENGE = 4, // $1B      -> LAB_00A1
	PK_VALLEY = 5,     // $1C      -> LAB_009D
	PK_WIZARD = 6,     // $1E      -> LAB_007C
	PK_DUEL = 7        // $21      -> LAB_004F
};

// LAB_007B: the compare chain on the node id (CMP.W: only the low word counts).  Note the ids $1D, $1F and $20 are not
// places; $1D is also what the node table never contains.
// With pData the rows of [place] (places.ini, ROADMAP 9.5c) decide: the first row whose node range holds the id gives the kind.
// Without (nullptr: host tests, the original) the chain above is used; the two agree for the built-in rows (tests/test_places_data.py).
PlaceKind placeClassify(uint16_t uwNodeId, const GameData *pData = nullptr);

// The five buttons of the two town menus (LAB_008C / LAB_0095): the long at button record +16 selects the action, any
// other value does nothing.  Town A: 4 = market (scene 6); town B: 4 = the mystic.  5 leaves the town.
enum TownButton { TB_NONE = 0, TB_SMITH = 1, TB_DICE = 2, TB_HEALER = 3, TB_THIRD = 4, TB_EXIT = 5 };
TownButton townButton(uint32_t ulButtonId);

}}  // namespace ms::game
