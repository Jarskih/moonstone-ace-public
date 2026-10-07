// game/combat - the fight state machine of mog.asm in C++ (ROADMAP 6.4): the fight loop LAB_0036 with its win / lose
// detection, the three ways into a fight (knight against knight LAB_004F, creature lair LAB_005B, dragon LAB_0083),
// the arena set-ups of the knights' fights (LAB_0164 / LAB_0165 / LAB_0192 / LAB_0167), and the meeting screen loop
// LAB_04CF..LAB_04D4 that shows the result (states 1, 2, 9, 10, ...).
//
// Pure on purpose, like rules.hpp / loot.hpp: no ACE, no OS, no hardware, no globals.  Everything the asm keeps in
// fixed memory cells is reached through a pointer in FightEnv / ScreenEnv (the game points them at the asm cells,
// src/rt/combat.cpp; the host test points them at its own), and everything that draws, plays, waits or is a job /
// sprite primitive is a FightOps / ScreenOps callback that the runtime maps to the original routine with the register
// contract named next to it.  The ORDER of the callbacks is the original's order; tests/test_combat.py compares the
// callback log and every cell and record with a literal Python model of the asm.
//
// Where the original loops on a hardware or keyboard flag the loop is kept (the callbacks and the volatile key cell
// make it observable); quirks of the original that are reproduced on purpose are marked "QUIRK".
//
// Not here (stays asm, behind the callbacks): the sprite/job primitives (LAB_0310, LAB_0319, LAB_0E5A, LAB_01A9 ...),
// the per-frame engine passes (LAB_0322, LAB_0328 = combat_script.hpp, LAB_03BE, LAB_039E), the creature arena
// set-ups LAB_0168..LAB_019E (ROADMAP 6.6), the screen drawing of the meeting screens (LAB_04EA, LAB_04F8, LAB_051B..
// LAB_0524) and the click handler LAB_052A (loot.hpp / scene_town.hpp).
#pragma once
#include <stdint.h>

#include "game/constants.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

struct RulesDef;   // game/api/data.hpp

// ---------------------------------------------------------------------------------------------------------
// Scene ids (SceneId, SCENE_*) and the kinds / types / actions live in game/constants.hpp.

// The defeat bits of LAB_05DC (settleDefeats): who lost the last fight.
enum { FIGHT_LOST_FIRST = 1, FIGHT_LOST_SECOND = 2 };


// ---------------------------------------------------------------------------------------------------------
// 68k addresses the code stores into records and tables (symbols of the image in the game, plain numbers in a test).
struct FightAddrs {
	uint32_t ulActionScripts;    // LAB_05F5  -> Knight +34
	uint32_t ulHurtScripts;      // LAB_05F6  -> Knight +30
	uint32_t ulDamageTable;      // LAB_05F7  -> Knight +42
	uint32_t ulKnightWalkTab;     // LAB_0610  -> Knight +46
	uint32_t ulDefenseTable;     // LAB_05F8  -> Knight +50
	uint32_t ulIdleScript;       // LAB_07DB  -> Knight +22
	uint32_t ulAltScript;        // LAB_07DC  -> Knight +26
	uint32_t ulJobParamKnight;   // LAB_05E1  -> Knight +38 (LAB_0167)
	uint32_t ulJobParamFight;    // LAB_05E0  -> Knight +38 (the arena set-ups overwrite it)
	uint32_t ulSpawnScript;      // LAB_07FC  script a knight is spawned with (LAB_01A9's A0)
	uint32_t ulNoSpawn;          // LAB_0166  the "spawn next creature" routine that does nothing (RTS)
	uint32_t ulKnight0;          // LAB_0613  (the practice fight)
	uint32_t ulKnight1;          // LAB_0614
	uint32_t ulDragonRecord;     // LAB_0617
	uint32_t ulDragonHurtA;      // LAB_07F5  dragon fight: the player's hurt-script table gets these (LAB_0192)
	uint32_t ulDragonHurtB;      // LAB_07FE
	uint32_t ulDragonHurtC;      // LAB_07FB
	uint32_t ulDragonDamage;     // LAB_0603  damage table of the dragon and the two bats (+4/+8/+20/+32 patched)
	uint32_t ulBatScript;        // LAB_0880  idle / alternate script of the two bats
	uint32_t ulDragonHurtTab;    // LAB_0604  the dragon's and the bats' hurt-script table (LAB_0195 -> Knight +30)
	uint32_t ulDragonWalkTab;    // LAB_060F  their walk-script table (LAB_0195 -> Knight +46)
	uint32_t ulDragonIdle;       // LAB_0882  the dragon's idle / alternate script (LAB_0195 -> Knight +22 / +26)
};

// Access to the records the asm reaches through 32-bit addresses.  Never null.
struct FightMemory {
	Knight *(*pfnKnight)(uint32_t ulAddr);        // a Knight record (knights, dragon, creatures)
	Inventory *(*pfnInventory)(uint32_t ulAddr);  // a 24-byte Inventory
	Lair *(*pfnLair)(uint32_t ulAddr);            // a 20-byte Lair record
};

// The asm cells (the asm label in the comment).  Everything is a pointer to the live cell.
struct FightCells {
	ActiveKnights *pAct;            // LAB_05E4  fight pair, +8 fight-active flag, +16 end timer
	ByteSlot *pDefeat;              // LAB_05DC  defeat bits (HIGH byte of the word)
	const ByteSlot *pMode;          // LAB_05DF  screen mode byte (4 = the knights' own fight screen)
	uint32_t *pFighter;             // LAB_05F2  first fighter
	uint32_t *pCurrent;             // LAB_0633  current knight of the turn
	uint32_t *pTarget;              // LAB_0634  second fighter set by the arena set-ups
	uint32_t *pSkipJob;             // LAB_05F4  creature record whose job keeps running while the others are paused
	uint16_t *pActionLatch;            // LAB_0620
	uint16_t *pSoundStep;           // LAB_02EC+2  step of the sound sequence of LAB_02E8
	uint32_t *pWarn;                // LAB_05A5..LAB_05AA  six job handles of the two low-hp warnings
	uint16_t *pWarnColours;         // SECSTRT_1, LAB_05A3, LAB_05A4  three colour words
	uint16_t *pFrameFlag;           // LAB_05AB  set to 1 at the start of every fight
	uint32_t *pFrameStart;          // LAB_05AC  tick counter at the start
	const uint32_t *pTick;          // LAB_0B9D  free-running tick counter
	const uint32_t *pArenaKind;     // LAB_0411  screen kind set by LAB_03F3 (12 / 16: both fighters get a warning)
	const uint32_t *pCreatureBase;  // LAB_05C3  base of the 20 heap creature records
	const uint16_t *pExtraPassFlag;      // LAB_06FC
	const volatile uint16_t *pKey;  // SECSTRT_21  last key code (written by the keyboard interrupt)
	uint16_t *pTurnSpent;           // LAB_0655
	const uint16_t *pTurnBudget;    // LAB_0665 (word)
	uint16_t *pBadLuck;             // LAB_05D3
	uint16_t *pSwapped;             // LAB_05AD  1 = the attacker lost and the pair was swapped for the screen
	uint32_t *pAvoidBy;             // LAB_05D1  knight whose controls are reversed (cursed protection scroll)
	uint32_t *pAvoidWho;            // LAB_05D2  defender of the avoid dialog
	uint16_t *pLastSlot;            // LAB_053B  item the last meeting screen used ($FFFF none)
	const uint16_t *pTravel;        // LAB_065E  non-zero: loot the lair without a fight (travel mark)
	uint16_t *pDragonActive;            // LAB_0667
	uint32_t *pEncounterKind;            // LAB_076D  2 while a creature fight runs
	uint32_t *pLairParam;           // LAB_076E
	uint32_t *pArenaParam;          // LAB_08C4
	uint32_t *pLair;                // LAB_08C6  current lair record
	uint16_t *pFightTotal;           // LAB_05EC  creatures still to come (+ those alive)
	const RulesDef *pRules = nullptr; // [coop] rules for the lair write-back (null: single knight, unchanged); rules.ini
	uint16_t *pMaxAlive;           // LAB_05ED  creatures alive at most
	uint16_t *pAliveNow;           // LAB_05EE  creatures alive
	uint32_t *pSpawnFn;             // LAB_05F0  routine that spawns the next creature
	uint32_t *pSpawnFn2;            // LAB_05F1
	uint16_t *pFrameDelay;          // LAB_05BA  ticks per frame
	uint16_t *pDragonFlags;            // LAB_0623
	uint32_t *pHandleBat1;          // LAB_0193  (stored, never read)
	uint32_t *pHandleBat2;          // LAB_0194
};

// The callbacks.  uwArg = the original register named in the comment.
struct FightOps {
	// the loop LAB_0036
	void (*keyReset)();                                       // LAB_0B82
	void (*clearScriptSlots)();                               // LAB_02F2
	void (*flip)();                                           // LAB_0416
	void (*togglePause)(uint32_t ulRecord);                   // LAB_0319, D0 = record (EORI #1 on the job's pause word)
	void (*initFightScreen)();                                // LAB_0412
	void (*frameStart)();                                     // LAB_031D
	void (*jobPass)();                                        // LAB_0322
	void (*tick)();                                           // LAB_0328 (combatTick)
	void (*contactPass)();                                    // LAB_03BE
	void (*drawPass)();                                       // LAB_039E
	void (*extraPass)();                                      // LAB_0B46
	uint16_t (*translateKey)(uint16_t uwCode);                // LAB_0D8D, D0.w -> D0.w
	void (*frameWait)();                                      // LAB_031F
	uint32_t (*spawnWarning)(uint16_t uwType, uint16_t uwColour);   // LAB_0E5A: D0 = type, D1 = colour, D2 = 1, D3 = 0 -> D0 = job
	void (*killJob)(uint32_t ulJob);                          // CLR.L (A0): the job record's first long
	void (*soundTick)();                                      // LAB_0AA9
	void (*afterFight)();                                     // LAB_0114
	void (*fadeIn)();                                         // LAB_03F1
	void (*clearJobs)();                                      // LAB_0305
	// entries
	void (*resetScreen)();                                    // LAB_0DC8
	void (*screen)(uint32_t ulScene);                         // LAB_04CF, D0 = scene
	void (*returnToMap)();                                    // SECSTRT_36
	void (*clearObjects)();                                   // LAB_02CE
	void (*op0155)();                                         // LAB_0155
	void (*fadeOut)();                                        // LAB_03F0
	void (*copyName)(uint32_t ulName);                        // LAB_0059's loop: the name at ulName -> LAB_06C8
	void (*showAvoidText)();                                  // LAB_0137 with A0 = LAB_06C3
	void (*waitFire)();                                       // LAB_00EC
	void (*op03EB)();                                         // LAB_03EB
	void (*mapStep)();                                        // LAB_0E03
	void (*callSpawn)(uint32_t ulRoutine);                    // JSR (A0) with A0 = LAB_05F0: spawn the next creature
	// arenas
	void (*commonSetup)(uint32_t ulMode);                     // LAB_016F, D0 = mode (passed on to LAB_0100)
	void (*op0100)(uint32_t ulMode);                          // LAB_0100, D0
	void (*op015F)();                                         // LAB_015F
	void (*op0116)();                                         // LAB_0116
	void (*op0121)();                                         // LAB_0121
	void (*placeAttacker)();                                  // LAB_01A4
	void (*spawn)(uint32_t ulRecord, uint32_t ulScript);      // LAB_01A9: A1 = record, A0 = script (LAB_01A8: A0 = record +22)
	uint32_t (*allocCreature)();                              // LAB_0171 -> A1 (a free creature record, +0 := 1)
	void (*paletteMode)(uint32_t ulMode);                     // LAB_03F3, D0
	void (*poke32)(uint32_t ulAddr, uint32_t ulValue);        // MOVE.L #imm,d(A0) into a table of the image
	void (*creatureArena)(int32_t slHandlerOffset);           // JSR (LAB_08C8 + offset)
};

struct CreatureDef;   // game/api/data.hpp
struct FightEnv {
	FightAddrs a;
	FightMemory m;
	FightCells c;
	Knight *aKnights;               // the knight records LAB_0613.. (at least 5: the dragon is record 4)
	Inventory *aInv;                // their inventories LAB_0618.. (5)
	const CreatureDef *pDragonRow;   // creatures.ini row of the dragon (hp/reach/damage overrides, ROADMAP 9.7); null = the original numbers
};

// ---------------------------------------------------------------------------------------------------------
// LAB_0036: run one fight until it is decided.  The body of the loop, in the original's order: frame start, job pass
// (LAB_0322), engine tick (LAB_0328), flip, contact pass, draw pass, low-hp warnings (LAB_003E), the optional extra
// pass, LAB_004B (a dead first fighter ends the fight; the key $20 pauses until another key), frame wait.  The
// fight is over when the active flag (act +8) is clear and the end timer (act +16, a BYTE) counts down to zero.
// Afterwards: LAB_000E (settleDefeats on the two fighters) into the defeat bits, the turn budget is restored, the
// "bad luck" word cleared and the jobs cleared; a creature fight stores the creatures still to come in the lair.
// QUIRK: the end timer is decremented as a byte, so a timer of 0 would run 256 more frames.
// QUIRK: only act +8 (not the defeat bits) ends the loop; the second fighter's death is signalled by the script
// engine through LAB_0005 / LAB_0006, not by LAB_004B.
void fightRun(const FightEnv &e, const FightOps &o);

// LAB_0005 (a creature or a fighter died; called by the scripts through the $B0 opcode): when the first fighter is
// dead the fight ends (LAB_0006); otherwise one creature fewer is alive and to come, and as long as creatures are
// still to come and fewer than the maximum are alive the spawn routine in LAB_05F0 is called (LAB_0008).  When none
// is left alive or to come the fight ends.
// QUIRK: LAB_0008 calls the spawn routine until alive == max; a routine that does not raise the alive count (the
// "no spawn" RTS) while creatures are to come would never return; the original is kept (the PvP set-up has
// to-come = alive = max = 1 so it cannot happen there).
void fightCreatureDied(const FightEnv &e, const FightOps &o);

// LAB_0006: the end of the fight: when it is still active the end timer becomes $23 and the active flag is cleared.
void fightEnd(const FightEnv &e);

// ---------------------------------------------------------------------------------------------------------
// LAB_004F: A0 (first) meets A1 (second).  The second knight has lives and no frog curse: he may avoid the fight with
// the scroll (LAB_0058), otherwise both are put in the fight state (type $0C; +11 = 2 for the first, 1 for the
// second -- the joystick port each uses; an AI knight keeps his type) and fight.  An AI knight against an AI knight
// does nothing.  Afterwards: both lost -> the temple screen (9) for a human first knight; otherwise the winner (the
// pair is swapped for the screen when the first lost, and swapped back) sees the meeting screen (1), or, when the
// winner is an AI knight, he gets +1 progress and takes the loot by himself (LAB_001C).  Ends with the map return.
// QUIRK: the swap flag LAB_05AD is only cleared when a fight was fought; a meeting without a fight (second knight
// frogged or out of lives) sees the flag of the last fight and swaps the pair after the screen when it was 1.
void fightMeet(const FightEnv &e, const FightOps &o, uint32_t ulFirst, uint32_t ulSecond);

// LAB_0058: the "may use their scroll of protection to avoid this battle" dialog.  Returns true when the fight is
// avoided: the second knight is not AI and has the scroll in his inventory (+18), the dialog's meeting screen (9,
// shown with him as the current knight) ends with item $12 used.  QUIRK: when the scroll's gamble failed (LAB_05D3)
// the fight goes on and the first knight's controls are reversed (LAB_05D1 := the defender).
bool fightAvoid(const FightEnv &e, const FightOps &o);

// LAB_005B: a knight meets a creature lair.  With the travel mark set (LAB_065E) the lair is only looted; otherwise
// the arena (LAB_01A3) and the fight run first; a lost fight ends in the temple screen (9), a won one gives +1
// progress.  Then the creature loot screen (2), the lair clean-up (LAB_005F) and the map return (plus LAB_0E03 with
// the travel mark).
void fightCreature(const FightEnv &e, const FightOps &o, uint32_t ulLair);

// LAB_005F: without the travel mark, a lair whose loot (24 inventory bytes) is empty and whose flag word (+8) is
// clear is hidden: its map position is set to -1, -1.
void lairTidy(const FightEnv &e);

// LAB_0083: the dragon attacks the current knight (called from the turn loop).  An AI knight loses a life and
// the dragon wins without a fight; a human knight with lives and no frog curse fights.  A lost fight: the dragon
// takes the loot (LAB_001C) and the defeat bit 0 is set; a won one: the dragon is marked ($FF lives), the knight
// gets +2 progress and the dragon loot screen (10).  Ends with the turn budget restored and the map return.
void fightDragon(const FightEnv &e, const FightOps &o);

// ---------------------------------------------------------------------------------------------------------
// Arena set-ups of the knights' fights.

// LAB_0167: the script / table pointers and the tunables of a knight's record.
void arenaKnightTables(const FightEnv &e, Knight &k);

// LAB_0164: the arena of a knight against a knight: the second fighter (act +4) is placed at x $1E, y $4B facing right.
void arenaMeet(const FightEnv &e, const FightOps &o);

// LAB_0165: the practice fight of the two-player intro: knight 0 against knight 1.
void arenaPractice(const FightEnv &e, const FightOps &o);

// LAB_0192: the dragon's arena: the dragon and two bats.  Patches the player's hurt-script table and the damage
// table of the dragon in place (the image's data; QUIRK: shared by every later fight).
void arenaDragon(const FightEnv &e, const FightOps &o);

// LAB_01A3: the creature arena: the lair record sets the sound parameter, the arena parameter and picks the
// arena set-up from the handler table LAB_08C8 (asm, ROADMAP 6.6).
void arenaCreature(const FightEnv &e, const FightOps &o);

// ---------------------------------------------------------------------------------------------------------
// The meeting screen loop LAB_04CF (states 1, 2, 3, 5, 6, 8, 9, 10, 11) and its redraw LAB_04D4.

struct ScreenCells {
	uint16_t *pTextFlag;            // LAB_0D05
	const uint32_t *pSpriteSrc;     // LAB_05E2+4 (the long LAB_05E3)
	uint32_t *pSprites;             // LAB_0986
	uint16_t *pChanged;             // LAB_0689  "something changed" counter of the inventory screens
	uint32_t *pScene;               // LAB_068F
	const uint32_t *pPrevScene;     // LAB_068A
	uint16_t *pDone;                // LAB_0984  non-zero ends the loop
	const uint16_t *pCursorX;       // LAB_097F
	const uint16_t *pCursorY;       // LAB_0980
	const uint16_t *pCursorLock;    // LAB_0981
	uint16_t *pHandOver;            // LAB_053C
	Knight *pDragon;                // LAB_0617, the dragon record (its +100 link word is used as storage)
	ActiveKnights *pAct;            // LAB_05E4
	uint32_t *pButtons;             // LAB_0688  button array in use
	uint32_t *pStatKnight;          // LAB_0632  knight whose stats LAB_04F8 draws
	uint16_t *pLineBase;            // LAB_0680
	uint16_t *pXShift;              // LAB_0985
	const uint32_t *pKnightA;       // LAB_068B  knight of the screen
	const uint32_t *pKnightB;       // LAB_068D  second knight (opponent / dragon)
	uint16_t *pPalette;             // LAB_09F1  four palette words
	const uint16_t *pWideScenes;    // LAB_098A  scenes with the wide layout, ended by a negative word
};

struct ScreenAddrs {
	uint32_t ulUseButtons;          // LAB_0699
	uint32_t ulTakeButtons;         // LAB_069A
	uint32_t ulMarketRecord;        // LAB_0690
};

struct ScreenOps {
	void (*fadeOut)();                                        // LAB_03F0
	void (*op0575)();                                         // LAB_0575
	void (*op0588)();                                         // LAB_0588
	void (*op057B)();                                         // LAB_057B
	uint32_t (*hitTest)(uint32_t ulX, uint32_t ulY);          // LAB_0451: D0 = x, D1 = y -> A0 = region, 0 = none (D0 = 0)
	void (*click)(uint32_t ulRegion);                         // LAB_052A, A0 = region
	void (*flip)();                                           // LAB_0416
	void (*drawPass)();                                       // LAB_039E
	void (*op044E)();                                         // LAB_044E
	void (*op04EA)();                                         // LAB_04EA
	void (*blitBackground)();                                 // LAB_0419: A0 = LAB_05C0, A1 = LAB_0D92
	void (*setTarget)();                                      // LAB_0426+2: D0 = LAB_0D92
	void (*lootSetup)();                                      // LAB_058A
	void (*op03A7)();                                         // LAB_03A7
	void (*drawStats)();                                      // LAB_04F8 (reads LAB_0632, LAB_0680, LAB_0985, LAB_0688)
	void (*op051B)();                                         // LAB_051B
	void (*op0524)();                                         // LAB_0524
	void (*op0522)();                                         // LAB_0522
	void (*op04FE)();                                         // LAB_04FE
	void (*op051F)(uint32_t ulKnight);                        // LAB_051F, A0 = LAB_0632
	void (*op051D)();                                         // LAB_051D
	void (*blitScreen)();                                     // LAB_0419: A0 = SECSTRT_35, A1 = LAB_0D92
	void (*waitBlitter)();                                    // LAB_0D1B
	void (*op0D8A)();                                         // LAB_0D8A with A0 = LAB_09F0
	void (*op03EE)();                                         // LAB_03EE with A0 = LAB_09F0
};

struct ScreenEnv {
	ScreenCells c;
	ScreenAddrs a;
	Knight *(*pfnKnight)(uint32_t ulAddr);
};

// LAB_04CF: run a meeting screen of the given scene until a click handler sets the done word.  Entry: the scene is
// stored, the done word and the change counter cleared, the screen drawn (LAB_04D4); the loop polls the cursor
// (LAB_0451); a click handler (LAB_052A) runs when a region is under the cursor and the cursor is not locked.  On
// exit the screen is faded out and, when the hand-over word (LAB_053C) is set, the opponent becomes the dragon
// record's link word (QUIRK: Knight +100 of LAB_0617 is used as storage for the exchange partner) and the word
// is cleared.  The text flag is 1 while the screen runs.
void screenRun(const ScreenEnv &e, const ScreenOps &o, uint32_t ulScene);

// LAB_04D4: (re)draw the screen of the current scene.  Scene 1 with an opponent without lives clears the change
// counter; a changed scene 1 turns into the temple screen (9); a changed scene 8 returns to the previous scene.
// Both restart the redraw.  QUIRK: the "wide layout" list LAB_098A (scenes 9 and 3) moves the right part by $4A.
void screenRedraw(const ScreenEnv &e, const ScreenOps &o);

// LAB_04E1: the two palette pairs of the knights' colours: the first pair from the knight of the screen, the second
// (scenes 1, 8 and 11 only) from the second knight.  Kind 0 = blue, 1 = yellow/brown, 3 = red, 2 = green, others
// (the dragon, AI knights) dark blue.
void screenPalette(const ScreenEnv &e);

}}  // namespace ms::game
