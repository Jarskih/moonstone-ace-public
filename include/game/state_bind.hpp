// Typed bindings for mog.asm's game state (ROADMAP 5.1).  The symbols are exported by asm/mog.s with the mog_ prefix
// (e.g. mogKnights).  Declaring them with their struct types lets C++ read and write game state by name instead of (LAB_0613)
// by offset.  Only mog's copy is bound: program.asm has its own, unrelated BSS.
//
// tests/test_game_state.py checks every declaration below against mog.asm: sizeof(type) * count must equal the
// extent the asm reserves for the label (and arrays spanning several labels must have each interior label on an
// element boundary).  Keep one declaration per line in the exact form `extern "C" <type> mog_LAB_XXXX[N];` or
// `extern "C" <type> mog_LAB_XXXX;` -- the test parses this file.
//
// Pointers held in the game state (Knight*, Inventory*, ...) are 32-bit values.  They are declared uint32_t here and
// converted at the use site, so the same headers stay valid on the host where pointers are 64-bit.
#pragma once
#include "game/state.hpp"

// Records ----------------------------------------------------------------------------------------------------
// Five Knight records: LAB_0613..LAB_0616 are the four knights, LAB_0617 is the dragon (Knight::ulKind == 5).
extern "C" ms::game::Knight mogKnights[5];
// Inventories: LAB_0618 is DS.L 24 (four knights, 24 bytes each), LAB_0619 is the dragon's (DS.L 6).
extern "C" ms::game::Inventory mogInventories[5];  // LAB_0618
// Fight pair and lunar clock.
extern "C" ms::game::ActiveKnights mogActive;  // LAB_05E4
// Sprite job table (10 x $32 bytes).
extern "C" ms::game::Job mogJobs[10];  // LAB_0649
// Overworld node table in S_2 DATA (9 entries + terminator).
extern "C" ms::game::MapNode mogMapNodes[10];  // LAB_069F

// Pointers to records (uint32_t holds the 68k address) --------------------------------------------------------
extern "C" uint32_t mogCurKnight;    // Knight* current knight (156 uses; the working "this" of the turn and fight code) (LAB_0633)
extern "C" uint32_t mogFirstFighter;    // Knight* first fighter copy (LAB_0036 stores ActiveKnights::ulCurrent here) (LAB_05F2)
extern "C" uint32_t mogUiKnight;    // Knight* current knight, snapshot for the UI (LAB_058A) (LAB_068B)
extern "C" uint32_t mogUiInventory;    // Inventory* of LAB_068B's knight (Knight +96) (LAB_068C)
extern "C" uint32_t mogUiKnight2;    // Knight* second knight for the UI (opponent / dragon / black knight by scene) (LAB_068D)
extern "C" uint32_t mogUiInventory2[2]; // [0] = Inventory* of LAB_068D's record (or Lair loot); DS.L 2, [1] never accessed (LAB_068E)
extern "C" uint32_t mogCurLair;    // Lair* current lair record (20-byte stride) (LAB_08C6)
extern "C" uint32_t mogCreatureHeap;    // Knight* base of the 20 heap creature records (LAB_05C3)
extern "C" uint32_t mogHeapTable[25];  // heap pointer table (68 = lair records, 72 = lair loot inventories, ...) (LAB_05B9)

// Scene and scheduler scalars --------------------------------------------------------------------------------
extern "C" uint32_t mogSceneId;    // scene id, the mog state machine selector (DOC_TECHNIQUE 10.19; LAB_04CF) (LAB_068F)
extern "C" ms::game::WordSlot mogHumanPlayers;  // number of human players (1..4); DS.L 1 but only ever accessed as .W at the label (LAB_05C5)
extern "C" uint16_t mogHumanPlayersSaved;    // LAB_05C5 saved across the two-player intro fight (LAB_0002) (LAB_05DB)
extern "C" ms::game::ByteSlot mogDefeatBits;  // defeat bits of the last fight: bit0 first fighter, bit1 second (LAB_000E); in the HIGH byte (LAB_05DC)
extern "C" ms::game::ByteSlot mogLocationMode;  // location/screen mode byte: $04, $0C, $14, $18, $20, $24, $30 (LAB_00xx menu code) (LAB_05DF)
extern "C" uint16_t mogTurnCursor;    // turn cursor: knight index 0..3 whose turn it is (LAB_0DBA, ANDI.W #3) (LAB_0654)
extern "C" uint16_t mogMoveSpent;    // movement spent this turn (compared with the budget in LAB_0DB9) (LAB_0655)
extern "C" uint16_t mogRoundDone;    // knights that have finished the round (compared with LAB_05C5 in LAB_0DBA) (LAB_0663)
extern "C" ms::game::WordSlot mogTurnBudget;  // turn budget = Knight::ubDerivedEnd << 4 (LAB_0DBE); the word is at the label address (LAB_0665)
extern "C" uint16_t mogBudgetQuarter;    // budget / 4 (LAB_0DBE) (LAB_0659)
extern "C" uint16_t mogBudgetThreeQuarters;    // budget * 3/4 (LAB_0DBE) (LAB_065A)
extern "C" uint16_t mogDayCounter;    // day counter: +1 each time the sub-counter wraps; dragon eligible from 2 (LAB_0DCB) (LAB_06C0)
extern "C" uint16_t mogLunarIndex;    // lunar index 0..7 into LAB_06C2 (LAB_0029) (LAB_06C1)
extern "C" uint8_t mogMoonFrameByIndex[8];  // S_4 DATA "-/.010./" = 45,47,46,48,49,48,46,47: moon frame per index (LAB_06C2)
