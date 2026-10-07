// game/overworld - the logic of the overworld map (scene state 6) of mog.asm in C++ (ROADMAP 6.3): the knight's step on the
// map and its tile, the terrain slow-down, the AI knight's walk, target and shopping, the arrival list and the node menu
// with its dispatcher (LAB_0E3D / LAB_0E45), the proximity scan LAB_0069, the dragon's flight and attack, and the turn
// scheduler glue that wires rules.hpp (advanceTurn / setupTurn) in.  Pure on purpose, like scene_places.hpp: no ACE, no OS,
// no globals, no UI.  Every function works on the records and cells the caller passes (src/rt/overworld.cpp owns the
// game's cells), so it builds for the host tests (tests/test_overworld.py) as well as the Amiga.  The blitter, the text printer,
// the palette jobs, the key table, the fights and the screens stay asm and are reached through the *Ops tables.  Since 7.1l the
// map screen itself is here too (the last section: drawing, colour jobs, menu text, the frame loop mapLoopRun); the visit of a
// location (LAB_007B ..) is game/placevisit.hpp.
//
// The map frame (LAB_0DAB .. LAB_0DBC), as the asm sequences it (7.1l: mapScreenEnter / mapFrame run it; each decision below
// was one block of it):
//   scene entry  LAB_0DAB: reset, draw, LAB_0DBD (setupTurn), LAB_0DA6 (mapMove + tile + scan), LAB_0D9B (draw), LAB_0DC5
//   AI knight    LAB_0DDF roamSort once per turn, LAB_0DEA roamPick every frame (its RNG draw is part of the stream),
//                LAB_0E2B (scene_places aiBuyStat), LAB_0E23 aiUsePotion, LAB_0DED aiPickOpponent, LAB_0E27 aiUseScroll,
//                LAB_0E29 aiUseSpeed, LAB_0E2D aiShopWish (then LAB_0E35 aiTownTarget), LAB_0E17 aiArrival, LAB_0DD8
//                terrainStep, LAB_0E0C aiWalkStep; the budget +1 per frame
//   human knight LAB_0DD8 terrainStep, input (LAB_00EE), +1 spent per step, keys (space = status screen, Esc = end turn,
//                $51 = quit), fire = LAB_0E3D (arrivalMenu), then LAB_0DB6 (dragonAttacks) and LAB_0DB9 (turnRun)
//
// What the movement words are: LAB_0656 is a BYTE pair; the move bits live in the LOW byte (LAB_0657): bit 0 right, 1 left,
// 2 down, 3 up, 4 fire (the same bits as Knight +63).  LAB_07BA..LAB_07BC are NOT overworld paths: they are the creatures'
// spawn lists (x word, facing word pairs, ended by a zero) that the lair arena set-ups LAB_0168.. hand to LAB_016D (ROADMAP
// 6.4).  The overworld has no path tables: the human steps by input, the AI by the Bresenham line of aiWalkStep.
//
// Quirks of the original that are reproduced on purpose are marked "QUIRK" at their function.  Authority:
// moonshard/moonstone-main/amiga_asm/mog.asm (labels cited per function); tests/test_overworld.py compares every function
// with the lifted asm (tools/lift.py, a literal 68k transliteration) over random states.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/rules.hpp"
#include "game/rules/ai_map.hpp"   // RecordSet, roam target, opponent, shopping of the black knights (ROADMAP 9.6f)
#include "game/state.hpp"

namespace ms { namespace game {

#pragma pack(push, 2)

// ---------------------------------------------------------------------------------------------------------
// Map movement

enum MoveBit { MV_RIGHT = 1, MV_LEFT = 2, MV_DOWN = 4, MV_UP = 8, MV_FIRE = 16 };

// LAB_0E07: the edge test on the move word (LAB_0657 is its low byte): at x <= 0 the left bit is cleared, at x >= $136 the
// right bit, at y <= 0 the up bit, at y >= $BE the down bit (signed word compares on the position before the step).
uint16_t mapClipMove(uint16_t uwMove, uint16_t uwX, uint16_t uwY);

// LAB_0DA6 up to LAB_0DAA: a zero move word does nothing; otherwise the word is clipped (mapClipMove, the clipped word is
// written back: LAB_0DB4 reads it again for the fire bit) and the knight steps one pixel per set bit: right +x, left -x,
// up -y, down +y (both of a pair cancel).  uwX / uwY = Knight +126 / +128.
void mapMove(uint16_t &uwX, uint16_t &uwY, uint16_t &uwMove);

// LAB_0E22: the tile of the knight: +66 = (mapX + width/2) >> 3, +68 = (mapY + height) >> 3 with the size of cel frame 0
// of the map sprite table (LAB_0664, 10 bytes per frame, width at +14, height at +16), word arithmetic.
void mapTile(Knight &k, uint16_t uwFrame0W, uint16_t uwFrame0H);

// LAB_0E20: the tile index into the terrain tables LAB_08FA / LAB_08FB: tileY * 40 + tileX, low word (the asm indexes the
// tables with it as a SIGNED word).
uint16_t mapTileIndex(const Knight &k);

// LAB_0DD8: the terrain slow-down.  uwBlocked (LAB_0DDA+2) is cleared first; in the forced / ambush modes (LAB_065E,
// LAB_065C non-zero) or on a tile whose mask byte is 0 that is all.  Otherwise the counter LAB_0DDA counts up and the
// step is blocked (uwBlocked = 1) when counter & sign-extended mask is non-zero: a mask of 3 lets one step in four through.
struct TerrainState {
	uint16_t uwCounter;        // LAB_0DDA
	uint16_t uwBlocked;        // LAB_0DDA+2
};
void terrainStep(TerrainState &ts, bool bModeBlocks, uint8_t ubMask);

// Ambush / forced-fight mode cells (LAB_0E02..LAB_0E06, set by the loot screens): LAB_065C, LAB_065E, LAB_0660 and the saved
// position LAB_065F (x word, y word).  LAB_0660 is only ever written.
struct MapMode {
	uint16_t uwAmbush;         // LAB_065C
	uint16_t uwForced;         // LAB_065E
	uint16_t uwPosSaved;       // LAB_0660
	uint16_t uwSavedX;         // LAB_065F
	uint16_t uwSavedY;         // LAB_065F+2
};
void mapModeClear(MapMode &m);                          // LAB_0E04
void mapModeForce(MapMode &m, const Knight &k);         // LAB_0E02: forced = 1, save the position
void mapModeRestore(MapMode &m, Knight &k);             // LAB_0E03: position back, then LAB_0E04
void mapModeAmbush(MapMode &m, const Knight &k);        // LAB_0E05: LAB_0660 = LAB_065C = 1, save the position
// LAB_0E06: a random spot: x = (rng & $FF) + $20, y = (rng & $7F) + $24 (two draws, x first).
void mapRandomSpot(Knight &k, uint32_t &ulSeed);

// ---------------------------------------------------------------------------------------------------------
// AI knight: walk

// The AI walker (a Bresenham line towards a target), cells LAB_0674..LAB_067A.
struct AiWalk {
	uint16_t uwInit;           // LAB_0674: 0 = the line is not set up yet (cleared at every turn start)
	uint16_t uwXBit;           // LAB_0675: 1 right / 2 left
	uint16_t uwYBit;           // LAB_0676: 4 down / 8 up
	uint16_t uwErr;            // LAB_0677: error accumulator
	uint16_t uwDx;             // LAB_0678: |dx|
	uint16_t uwDy;             // LAB_0679: |dy|
	uint16_t uwYMajor;         // LAB_067A: $FFFF when |dy| > |dx|
};

// Where the walk heads (read only when the line is set up): the engaged opponent (Knight +100) first, else the shop /
// town target LAB_066C (x word, y word; the long is non-zero), else the roam lair LAB_0673.
struct AiTarget {
	const Knight *pEngaged;    // Knight +100 resolved (0 = none)
	uint32_t ulShop;           // LAB_066C as a long (x << 16 | y)
	const Lair *pLair;         // LAB_0673 resolved
};

// LAB_0E0C: returns the move word (also stored in LAB_0656 by the caller).  First call of a turn: the line from the knight
// (uwX, uwY) to the target; every call: one Bresenham step.  QUIRK: standing on the target (dx = dy = 0) the walker keeps
// stepping in x (the x bit, 1 or 2): the error never goes negative.
uint16_t aiWalkStep(AiWalk &w, uint16_t uwX, uint16_t uwY, const AiTarget &t);

// ---------------------------------------------------------------------------------------------------------
// Arrival list and node menu

// One row of the arrival list (SECSTRT_2, 8 bytes, up to 10 rows; a row whose first long is 0 ends the list).  Nodes are
// {x word, y word, node id}, knights {record address, 1 alive / $21 dead}, lairs {lair address, 2}.
struct ArrivalEntry {
	uint32_t ulKey;            // +0
	uint32_t ulKind;           // +4
};
static_assert(sizeof(ArrivalEntry) == 8, "SECSTRT_2 rows are 8 bytes");
enum { ARRIVAL_ROWS = 10 };

enum ArrivalAction {
	AR_DUEL = 0,               // $21 (dead knight's grave) or 1: LAB_004F with A0 = the knight, A1 = the other record
	AR_LAIR = 1,               // 2: LAB_005B with A1 = the lair
	AR_PLACE = 2               // anything else: LAB_007B with D0 = the id
};
// LAB_0E45: the long at +4 of the chosen row picks the callee.
ArrivalAction arrivalClassify(uint32_t ulKind);

// LAB_0E1C: does any row (up to 7, stops at the first row with a zero first long) have the long at byte offset 0 or 4
// equal to ulValue?
bool arrivalFind(const ArrivalEntry *aRows, int iField, uint32_t ulValue);

// What LAB_0E17 does for an AI knight on arrival, as calls: the asm callees are passed in.
struct ArrivalOps {
	void *pCtx;
	void (*pfnShop)(void *pCtx);                     // LAB_0E37 (aiShopApply on the game's cells)
	void (*pfnDuel)(void *pCtx, uint32_t ulOpp);     // LAB_004F with A0 = LAB_0633, A1 = the opponent record
};
// LAB_0E17: heading for a shop (ulShop != 0): when a row of kind $19 or $1A is near, the turn ends (spent = budget), the
// purchase is made and the target cleared.  Else, with an opponent engaged (ulEngaged) that is in the list: the duel, the
// turn ends; then (also with no opponent) when the roam lair is in the list the turn ends.  Not near: nothing.
void aiArrival(const ArrivalEntry *aRows, uint32_t &ulShop, uint32_t ulEngaged, uint32_t ulRoamLair, uint16_t &uwSpent,
               uint16_t uwBudget, const ArrivalOps &ops);

// The node menu LAB_0E3D and what it calls, as ops.
struct NodeMenuOps {
	void *pCtx;
	void (*pfnKeyReset)(void *pCtx);                 // LAB_0B82
	void (*pfnDrawMenu)(void *pCtx);                 // LAB_0E49 (menu text), then LAB_0D9B (map sprites), LAB_0416 (flip)
	uint16_t (*pfnReadKey)(void *pCtx);              // key word SECSTRT_21 (0 = none), else LAB_0D8D(key)
	uint32_t (*pfnDuel)(void *pCtx, uint32_t ulOpp); // LAB_004F: A0 = LAB_0633, A1 = ulOpp; returns D0
	uint32_t (*pfnLair)(void *pCtx, uint32_t ulLair);// LAB_005B: A1 = the lair; returns D0
	uint32_t (*pfnPlace)(void *pCtx, uint32_t ulId, uint32_t ulKey);  // LAB_007B: D0 = id (A1 = ulKey as the row left it); returns D0
};
// LAB_0E3D + LAB_0E45.  No row: returns 0.  One row: dispatched.  Forced mode (LAB_065E): the first row of kind 2 (a lair)
// is dispatched, none -> 0.  Else the menu: keys '1'..'9' (codes $31..$39) pick row 0..8, a key outside or an empty row is
// ignored (wait for the next).  Returns what the callee returned (D0): non-zero makes the caller restart the scene.
// QUIRK: in forced mode the scan tests the kind BEFORE the end marker, so a stale kind 2 in the terminator row counts.
uint32_t arrivalMenu(const ArrivalEntry *aRows, bool bForced, const NodeMenuOps &ops);

// ---------------------------------------------------------------------------------------------------------
// Proximity scan LAB_0069

// What the scan needs from the asm: the cel sizes (LAB_0066: width at +14, height at +16 of 10 bytes per sprite) and the
// highlight draw (LAB_0CDA with A0 = the cel table).
struct ScanOps {
	void *pCtx;
	void (*pfnSize)(void *pCtx, uint16_t uwSprite, uint16_t &uwW, uint16_t &uwH);
	void (*pfnDraw)(void *pCtx, uint32_t ulSprite, uint16_t uwX, uint16_t uwY);
};
// The cells of the scan: the list, the dragon-touch pointers LAB_069E (4) and the write-only LAB_069C / LAB_069D (the id and
// x of the last near node; $FFFF at the start).
struct ScanCells {
	ArrivalEntry aList[ARRIVAL_ROWS];   // SECSTRT_2
	uint16_t uwLastId;                  // LAB_069C
	uint16_t uwLastX;                   // LAB_069D
	uint32_t aulTouch[4];               // LAB_069E
};
// LAB_0067: the two boxes touch in x and in y (closed intervals, words): box A at (uwAx, uwAy) with the size of sprite
// uwSprite, box B at (uwBx, uwBy) with the size of frame 0.
bool boxesTouch(const ScanOps &ops, uint16_t uwSprite, uint16_t uwAx, uint16_t uwAy, uint16_t uwBx, uint16_t uwBy);
// LAB_0069: what the knight (record index iCur) is near.  The first five rows are cleared and rows are appended in order:
//  1. (not in forced mode) the nodes of the table (stop at a negative id): the node id is the sprite, the village ids
//     $15..$18 only count for the knight of that index; near nodes are drawn and listed as {x, y, id};
//  2. (not in forced mode) every other knight near: drawn (sprite kind + 5, or $2A when out of lives) and listed as
//     {record, 1} or {record, $21} when out of lives;
//  3. the dragon's touch list LAB_069E is cleared and, with the dragon active (LAB_0667) and not in forced mode, each knight
//     whose box touches the dragon's (sprite $14 at x - 10, y) is added and drawn;
//  4. the lairs (a negative x hides it): near ones drawn (sprite 31) and listed as {lair, 2}.
// QUIRK: only the first five rows are cleared, so stale rows beyond them survive a short list.  The asm's scratch cells
// LAB_05AE..LAB_05B6 and the write-only LAB_069C / LAB_069D are LAB_0067's and written by it; the scratch is not kept here.
void scanMap(ScanCells &c, const RecordSet &rs, int iCur, const MapNode *aNodes, const Lair *aLairs, uint32_t ulLairBase,
             bool bForced, bool bDragonActive, const ScanOps &ops);

// ---------------------------------------------------------------------------------------------------------
// Dragon

// LAB_0DB6: the dragon attacks the current knight (and the asm runs LAB_0DC8 + LAB_0083): it is alive (+73 not negative),
// active (LAB_0667), has the current knight engaged (+100) and that knight is in the touch list.
bool dragonAttacks(const Knight &dragon, uint16_t uwActive, uint32_t ulCurrent, const uint32_t aulTouch[4]);

// LAB_0DCB, the decisions: spawn only from day 2 (signed) on and with the dragon alive (+73 not negative).
// The day is d.encounters.ubDragonDay (original 2).
bool dragonShouldSpawn(uint16_t uwDay, const Knight &dragon, const GameData &d);
// LAB_0DCB: the record fields set before the job is created: x 10, +6 0, y 100, facing 3, type $28, anim phase 0.  (The
// pointers +38 / +46 and the job itself are the asm's.)
void dragonInitRecord(Knight &dragon);
// LAB_0DCC: the dragon engages a random living knight: draw (rng & 3) until the record has lives (signed byte > 0).
// QUIRK: the original resets its loop counter inside the loop so it never ends when nobody lives; here a bound of 4096
// draws returns -1 (the asm hangs).  Returns the index 0..3.
int dragonPickTarget(const Knight *aKnights, uint32_t &ulSeed);

// LAB_0DCF (the dragon's job handler, once per frame): flight cells LAB_0666 / LAB_0DDC / LAB_0DDC+2.
struct DragonFlight {
	uint16_t uwTimer;          // LAB_0666: 100 -> 0 cycle
	int16_t swSpeedX;          // LAB_0DDC: 2, negated at the screen edges
	int16_t swSpeedY;          // LAB_0DDC+2: 1
};
enum DragonPath {
	DP_HOVER = 0,              // timer <= 60: script LAB_0900, only x moves
	DP_FLY = 1                 // timer > 60: script LAB_08FC[frame], x and y move towards the target
};
struct DragonStep {
	uint8_t ubPath;
	uint8_t ubFrame;          // DP_FLY: the walk frame 0..15 (+12 after the increment)
};
// The flight: the timer counts down and wraps from -1 to 100 (BPL).  Hover: x += speed.  Fly: x += speed; y moves one speed
// step towards the target's map y (up when the target is above: a signed compare, equal = still); at x > $15E (signed) x = $159,
// facing bit 1 flips, speed negates; at x <= -20: x = -10, flip, negate; y > $C8 wraps to 0, y < 0 to $C8; the frame
// advances modulo 16.  QUIRK: the target's y is read from the record the dragon engaged (+100); a NULL there reads zero page
// in the asm, here uwTargetMapY = 0.
DragonStep dragonFlyStep(Knight &dragon, DragonFlight &f, uint16_t uwTargetMapY);

// LAB_0DCB as a whole: the dragon appears.  The asm pieces it runs are passed in: LAB_0305 (clear the job table) and LAB_0310
// (create the dragon's job).  The addresses of the asm labels it stores come in through DragonSpawnEnv.  Order as the asm:
// clear jobs; handler slot (LAB_08C7 +40) = the flight handler LAB_0DCF; the five work longs LAB_0671 = the cel table
// (LAB_0664); the record fields; LAB_0310(A0 = first script LAB_08FC[0], A1 = dragon, A2 = LAB_0671, D0..D2 = the script
// header words +4/+6/+8, D3 = 3, D5 = $28); LAB_0DDC = 2; LAB_0666 = 100; LAB_0667 = 1; then the target pick (+100).
struct DragonSpawnEnv {
	uint32_t ulHandler;        // address of LAB_0DCF
	uint32_t ulCelTable;       // LAB_0664's value
	uint32_t ulWork;           // address of LAB_0671
	uint32_t ulScriptTab;      // address of LAB_08FC
	uint32_t ulScript0;        // LAB_08FC[0], the first script
	uint16_t auwScriptHdr[3];  // words +4, +6, +8 of that script
};
struct DragonSpawnCells {
	uint32_t *pulHandlerSlot;  // LAB_08C7 + 40
	uint32_t *paulWork;        // LAB_0671, 5 longs
	DragonFlight *pFlight;     // LAB_0666 / LAB_0DDC
	uint16_t *puwActive;       // LAB_0667
};
struct DragonSpawnOps {
	void *pCtx;
	void (*pfnClearJobs)(void *pCtx);                                                    // LAB_0305
	void (*pfnSpawnJob)(void *pCtx, uint32_t ulScript, uint32_t ulOwner, uint32_t ulParam, const uint16_t auwHdr[3],
	                    uint16_t uwD3, uint32_t ulD5);                                   // LAB_0310
};
// Returns false (and does nothing) when the dragon does not spawn (dragonShouldSpawn).
bool dragonSpawn(const RecordSet &rs, uint16_t uwDay, const DragonSpawnEnv &env, const DragonSpawnCells &cells, uint32_t &ulSeed,
                 const DragonSpawnOps &ops, const GameData &d);

// ---------------------------------------------------------------------------------------------------------
// Turn scheduler glue (LAB_0DB9 .. LAB_0DBE): rules.hpp's advanceTurn / setupTurn on the game's cells.

// What the glue needs from the asm / the cells.  The TurnState is the glue's copy of the scalar cells; every port that runs
// asm gets it first so the shim can store it back (the asm hooks read the day, the turn, ...).
struct TurnPorts {
	void *pCtx;
	void (*pfnTurnEnd)(void *pCtx, TurnState &ts, bool bRoundEnds);   // LAB_0E04 (mapModeClear); with bRoundEnds also the copy LAB_05E2+4 -> LAB_05C4 (head of LAB_0029)
	void (*pfnNewDay)(void *pCtx, TurnState &ts);                     // LAB_0DC8, LAB_012B, LAB_00EC, LAB_03EB, in this order
	void (*pfnTurnStart)(void *pCtx, TurnState &ts, Knight &k);       // LAB_0633 := k, then LAB_0E52
	void (*pfnAiDay)(void *pCtx, TurnState &ts, Knight &k);           // LAB_045E with LAB_0633 := k (the AI knight's daily step)
};
// LAB_0DBD / LAB_0DBE: setupTurn for aRec[ts.uwTurn]; the ActiveKnights +0 and (through pfnTurnStart) LAB_0633 get the record.
void turnSetup(TurnState &ts, ActiveKnights &act, const RecordSet &rs, const TurnPorts &ports);
// LAB_0DB9 once the budget is used up: LAB_0663 = 0, then LAB_0DBA's loop (advanceTurn while it says REPEAT).  Returns
// TURN_PLAY or TURN_GAME_OVER.
TurnNext turnRun(TurnState &ts, ActiveKnights &act, const RecordSet &rs, const TurnPorts &ports);

// ---------------------------------------------------------------------------------------------------------
// The map screen (ROADMAP 7.1l): the sprites, the colour jobs, the menu text and the frame loop that sequences all of the above.
// LAB_0D9B / 0D9E / 0DA3 (draw), LAB_0DC5 / 0DC8 (colour jobs), LAB_0E49 / 0E4E (menu text), LAB_0E52 (arena of the tile),
// LAB_0DAB .. LAB_0DBC (the loop).  What stays outside is reached through the ops: every asm routine of another subsystem
// (blitter, text, palette, key table, the screens and fights) and the C++ ports of this file (a "step" the glue maps to the
// function or shim).  The loop never returns in the game (its exits are JMP LAB_0064 = ops.pfnQuit and a restart of the
// screen); a host test sees ops.pfnQuit return and the loop stop.

// The draw primitives.  pfnSetTarget = MOVE.L x,D0 ; JSR LAB_0426+2 (selects the screen the sprites go to); pfnDraw = LAB_0CDA with
// A0 = the cel table of the map (D0 sprite, D1 x, D2 y); pfnText = LAB_0431 (A0 string, D0 x, D1 y, D2 = 0).
struct MapDrawOps {
	void *pCtx;
	void (*pfnSetTarget)(void *pCtx, uint32_t ulBuffer);
	void (*pfnDraw)(void *pCtx, uint32_t ulSprite, uint16_t uwX, uint16_t uwY);
	void (*pfnText)(void *pCtx, const char *pcStr, uint16_t uwX, uint16_t uwY);
	void (*pfnJobsRun)(void *pCtx);                    // LAB_0322 (the sprite jobs of the frame)
	void (*pfnJobsDraw)(void *pCtx);                   // LAB_0328
	const char *(*pfnString)(void *pCtx, uint32_t ulAddr);   // a NUL-terminated game string at a 68k address (a record's name)
};

// LAB_0DA3: the lair markers: the draw target (LAB_05C0) is selected, then every one of the 24 lairs (20 bytes, LAB_05B9 +68)
// whose x (+10) is not negative is drawn as sprite $14 at (+10, +12).  The cursor cell LAB_08C6 ends one past the table.
void mapDrawLairs(const Lair *aLairs, uint32_t ulLairBase, uint32_t ulTarget, uint32_t &ulCursor, const MapDrawOps &ops);

// LAB_0D9E: the other knights (not the current one) at their map positions: sprite = kind (+ $2B when frogged, but only for a
// living knight: lives > 0 signed), $21 for a dead one (the low word only: the kind's high word stays, it is 0).
void mapDrawOthers(const RecordSet &rs, uint32_t ulCurrent, const MapDrawOps &ops);

// LAB_0D9B: the current knight: sprite kind + 5, + 5 in the forced mode (LAB_065E), + 10 in the ambush mode (LAB_065C), at its map
// position; then the sprite jobs run (they may change the current knight: it is saved in LAB_066E around them and put back).
void mapDrawSelf(const Knight &cur, bool bForced, bool bAmbush, uint32_t *pCurrent, uint32_t *pSaved, const MapDrawOps &ops);

// The colour jobs of the map.  LAB_0658 says they exist; the ramp (LAB_0661) and cycle (LAB_0DDE) slots are kept.
struct MapColourCells {
	uint16_t *puwOn;                                   // LAB_0658
	uint32_t *pulRamp;                                 // LAB_0661
	uint32_t *pulCycle;                                // LAB_0DDE
	const uint16_t *puwDragonActive;                   // LAB_0667
};
struct MapColourOps {
	void *pCtx;
	void (*pfnFadeTo)(void *pCtx);                     // LAB_03F2 with A0 = LAB_0D2B (the map palette), 36 frames
	uint32_t (*pfnRampAdd)(void *pCtx, uint16_t uwD0, uint16_t uwD1, uint16_t uwD2, uint32_t ulD3);    // LAB_0E5A
	uint32_t (*pfnCycleAdd)(void *pCtx, uint16_t uwD0, uint16_t uwD1, uint16_t uwD2, uint32_t ulD3);   // LAB_0E56
	void (*pfnSlotFree)(void *pCtx, uint32_t ulSlot);  // LAB_0E59
	void (*pfnFadeOut)(void *pCtx);                    // LAB_03F0
	void (*pfnDragonSpawn)(void *pCtx);                // LAB_0DCB
};
// LAB_0DC5: once (LAB_0658 = 0): fade to the map palette, a ramp over colours $1F..$FF (step 1) and a cycle over $15..$17 (step 1,
// speed $18); then the dragon is looked at (spawn unless already active).
void mapColourStart(const MapColourCells &c, const MapColourOps &ops);
// LAB_0DC8: free the two slots when they exist, then fade out.
void mapColourStop(const MapColourCells &c, const MapColourOps &ops);

// LAB_0E52 (without the cell write LAB_076D = 0): the arena of the tile the knight stands on: LAB_08C4 = the byte of the table
// LAB_08FB at the (signed word) tile index.
uint32_t mapArenaOfTile(const uint8_t *pTable, uint16_t uwTileIndex);

// The menu text (LAB_0E49 / LAB_0E4E / LAB_0E51).  Strings of the game: " may ... ", "Enter Lair", "Battle with " and the table of
// the places LAB_08F4 (index = row kind - $15, 14 entries).
struct MapMenuText {
	const char *pcMay;                                 // LAB_08EA
	const char *pcLair;                                // LAB_08F1
	const char *pcBattle;                              // LAB_08F2
	const char *const *apcPlaces;                      // LAB_08F4: 14 pointers
};
enum { MAP_PLACE_TEXTS = 14 };
// The line of one arrival row: kind 2 "Enter Lair", kind 1 "Battle with " + the other knight's name, else the place text of
// (kind - $15).  Writes into pcOut (NUL-terminated, at most uwCap bytes).  QUIRK: a kind outside the table reads garbage in the asm
// (the rows are built from known kinds only); here it gives an empty text.
void mapMenuRow(const ArrivalEntry &row, const RecordSet &rs, const MapMenuText &t, const MapDrawOps &ops, char *pcOut, uint32_t ulCap);
// LAB_0E49: the node menu: LAB_05E4 +10 = LAB_05E3 (long), the frame sprite $20 at (50, 100), the headline "<name> may ... " and
// the numbered rows ("1 Enter Village", ...) up to the first empty row (at most ARRIVAL_ROWS here: the asm has no bound).
// pcBuf = 64 bytes of scratch (the asm uses LAB_08E9).  uwX / uwY end as LAB_08F5 / LAB_08F6 do ($32 / $64 + $F + 6 per row).
// The digit of the row number is kept in the byte cell LAB_0653 by the asm: it ends at '1' + the number of rows.
void mapMenuDraw(const Knight &cur, ActiveKnights &act, uint32_t ulE3, const ArrivalEntry *aRows, const RecordSet &rs,
                 const MapMenuText &t, uint16_t &uwX, uint16_t &uwY, uint8_t &ubDigit, char *pcBuf, const MapDrawOps &ops);

// The frame loop.
enum MapStep : uint8_t {
	MS_KEY_RESET,        // LAB_0B82
	MS_COPY_BACKDROP,    // the DBF copy of the packed backdrop (LAB_05B9 +92) to LAB_05C2, then LAB_0C21 on it
	MS_BLIT_BOTH,        // LAB_0418
	MS_FLIP,             // LAB_0416
	MS_FRAME_START,      // LAB_031D
	MS_FRAME_WAIT,       // LAB_031F
	MS_SETUP_TURN,       // LAB_0DBD
	MS_MOVE,             // LAB_0DA6: the step, LAB_0E22 (tile), LAB_0069 (scan)
	MS_DRAW_LAIRS,       // LAB_0DA3
	MS_DRAW_OTHERS,      // LAB_0D9E
	MS_DRAW_SELF,        // LAB_0D9B
	MS_COLOUR_START,     // LAB_0DC5
	MS_SET_TARGET_BG,    // MOVE.L LAB_05C0,D0 ; JSR LAB_0426+2
	MS_SET_TARGET_SHOWN, // MOVE.L LAB_0D92,D0 ; JSR LAB_0426+2
	MS_SHOW_BACKGROUND,  // MOVEA.L LAB_05C0,A0 ; MOVEA.L LAB_0D92,A1 ; JSR LAB_0419
	MS_ROAM_SORT,        // LAB_0DDF
	MS_ROAM_PICK,        // LAB_0DEA
	MS_AI_STAT,          // LAB_0E2B
	MS_POTION,           // LAB_0E23
	MS_OPPONENT,         // LAB_0DED
	MS_SCROLL,           // LAB_0E27
	MS_SPEED,            // LAB_0E29
	MS_TOWN,             // LAB_0E35
	MS_ARRIVE,           // LAB_0E17
	MS_TERRAIN,          // LAB_0DD8
	MS_MODE_CLEAR,       // LAB_0E04
	MS_DRAGON_FIGHT,     // LAB_0DC8 then LAB_0083
	MS_COLOUR_STOP,      // LAB_0DC8
	MS_STATUS_SCREEN     // MOVEQ #9,D0 ; JSR LAB_04CF (the knight status screen)
};
struct MapLoopOps {
	void *pCtx;
	void (*pfnStep)(void *pCtx, MapStep eStep);
	bool (*pfnWish)(void *pCtx);                       // LAB_0E2D: a wish stands
	uint16_t (*pfnAiWalk)(void *pCtx);                 // LAB_0E0C: the AI's move word
	uint16_t (*pfnJoystick)(void *pCtx);               // LAB_00EE: the human's move word (D1)
	uint16_t (*pfnReadKey)(void *pCtx);                // LAB_0D8D on the key cell: the translated key
	uint16_t (*pfnMenu)(void *pCtx);                   // LAB_0E3D: non-zero restarts the screen
	bool (*pfnDragonAttacks)(void *pCtx);              // rt_ow_dragon_attack
	uint16_t (*pfnTurn)(void *pCtx);                   // rt_ow_turn: 0 = play on (restart the screen), else the game is over
	void (*pfnQuit)(void *pCtx);                       // JMP LAB_0064
};
// The cells the loop tests (all in the game's memory).
struct MapLoopCells {
	const RecordSet *pRs;                              // the five records
	const uint32_t *pulCurrent;                        // LAB_0633
	uint16_t *puwSpent;                                // LAB_0655
	uint16_t *puwMove;                                 // LAB_0656
	const uint32_t *pulLocked;                         // LAB_0662
	const uint16_t *puwAmbush;                         // LAB_065C
	const uint16_t *puwForced;                         // LAB_065E
	const uint16_t *puwBudget;                         // LAB_0665
	const uint16_t *puwStepGate;                       // LAB_066B
	const uint16_t *puwBlocked;                        // LAB_0DDA+2
	uint16_t *puwFrameBudget;                          // LAB_05BA
};
// How a frame ends: the next frame, or one of the map's exits (ROADMAP 9.2a: each exit is a scene transition, see
// src/game/scenes/map.cpp).  The frame itself runs none of the exit's work: the caller does (the scene manager, or mapLoopRun).
enum MapFrameResult {
	MAPFRAME_NEXT = 0,      // BRA.W LAB_0DAD
	MAPFRAME_RESTART,       // BRA.W LAB_0DAB: the screen starts again (after the node menu / a place / a fight)
	MAPFRAME_QUIT,          // the Q key: JMP LAB_0064
	MAPFRAME_STATUS,        // the space key: mapStatusScreen, then BRA.W LAB_0DAB
	MAPFRAME_TURN_OVER      // the move budget is used up: mapTurnOver decides (play on = BRA.W LAB_0DAB, game over = JMP LAB_0064)
};
// LAB_0DAB .. LAB_0DAC: one entry of the screen.
void mapScreenEnter(const MapLoopCells &c, const MapLoopOps &ops);
// LAB_0DAD .. LAB_0DBC: one frame.
MapFrameResult mapFrame(const MapLoopCells &c, const MapLoopOps &ops);
// The status screen of the space key (LAB_0DC8, LAB_04CF with D0 = 9, LAB_0B82).
void mapStatusScreen(const MapLoopOps &ops);
// The turn scheduler once the budget is used up (ops.pfnTurn): true = the game is over.
bool mapTurnOver(const MapLoopOps &ops);
// LAB_0DAB: the loop (screen entry, frames; a restart goes back to the entry).  Returns only when ops.pfnQuit does.  The
// game runs these pieces as scenes (docs/GAME_FLOW.md); this composition is kept for the host tests.
void mapLoopRun(const MapLoopCells &c, const MapLoopOps &ops);

#pragma pack(pop)

}}  // namespace ms::game
