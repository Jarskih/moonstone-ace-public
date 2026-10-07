// game/fighters - the per-fighter AI handlers of mog.asm in C++ (ROADMAP 6.4a): the player / AI knight LAB_01CA with its
// hurt reactions (LAB_01E1..LAB_0206), the creature handlers LAB_0226 / LAB_0236 / LAB_0251 / LAB_027A / LAB_0298 /
// LAB_029F / LAB_02CB / LAB_02D2 and the movement helpers they share (LAB_0F1A..LAB_0F34).  creatureDispatch (LAB_0322,
// creatures.hpp) reaches them through the handler table LAB_08C7, indexed by the job's type byte; src/rt/creatures.cpp
// routes the nine handler addresses below to fighterRun() and leaves every other table entry to the asm.
//
// ROADMAP 7.1h: the four creature handlers of the S_40 hunk (fighterSnatcher SECSTRT_40, fighterDemon LAB_0ED2, fighterAiKnight
// LAB_0EFF, fighterStalker LAB_0EC2: src/game/fight_creatures.cpp), the routines the combat scripts call through opcode $B0
// (fightOpRun, src/game/fight_ops.cpp), the handler table fill (fightTablesInit) and the knight defaults (knightDefaults) are
// C++ too.  The only handlers still asm are the map ones LAB_04AC / LAB_04C4 / LAB_0DCF (they jump to LAB_02BA).
//
// ROADMAP 9.6g / 9.6h: the DECISIONS of the handlers are rules (src/game/rules/damage.cpp: what a knight takes from each creature type
// and the block check; src/game/rules/ai_fight*.cpp: the AI table kFightAis and one decision file per AI).  The handlers here build the
// facts of the moment from the records, ask the rule, and apply the answer (hit points, flags, the next script, the jobs).
//
// Pure: no ACE, no OS, no globals.  Every cell the asm handlers read or write (the next-script cell LAB_061D, the
// movement scratch LAB_061E.., the dragon / dagger state words, the two fighters, the job table, the random seed) is
// reached through FighterEnv; what stays asm (the joystick read LAB_00EE, the arena wall probe LAB_0A71, sound LAB_0AA2,
// the copper effect LAB_0427, the beam position register VHPOSR) is a callback.  Transcribed from mog.asm, labels cited
// per function.  tests/test_fighters.py runs every handler against the lifted asm (tools/lift.py output) on random
// arenas; the deterministic stand-ins for the callbacks are the same on both sides.
//
// The common exit LAB_02BA (return A0 = next script, D0..D3 = x / y / z / facing of the record) is HandlerResult
// (creatures.hpp).  A handler returns the script it leaves in LAB_061D; -1 keeps the running one, 0 kills the job.
//
// Handlers (job type byte -> handler, from the writes of LAB_08C7 in mog.asm; the table is dynamic, the scenes set
// entries as they start):
//   $0C / $38  fighterKnight    LAB_01CA   a fighting knight: input -> action, walking, being hit
//   $00        fighterFlyer     LAB_0226   crosses the arena between x = -50 and 380 at the target's depth
//   $18/$1C/$20 fighterBrawler  LAB_0236   walks up to the target and picks one of three attacks
//   $24        fighterCaster    LAB_0251   keeps its distance, throws daggers, grabs and drops the target
//   $14        fighterDragon    LAB_027A   the dragon (record LAB_0617): breath / fireball phases, spawns LAB_0297's object
//   $2C        fighterDragonPart LAB_0298  object of the dragon fight (follows the dragon, strikes at the target)
//   $30        fighterDrake     LAB_029F   flying attacker with a dagger-style arc and a ground hit LAB_02AF
//   $34        fighterDagger    LAB_02CB   a thrown dagger: runs its script until it leaves the screen or hits
//   $28        fighterIdle      LAB_02D2   clears the hit links and keeps the script (effects, spawned objects)
// plus the script-called sound pickers LAB_02DC..LAB_02E9 (soundPick), which the combat engine's opcode $B0 calls.
//
// Quirks kept (each commented where it happens): a handler entered from a hit (links +14 / +18) sometimes continues with
// A0 pointing at the OTHER record and clears its action word there (the caster's LAB_0252, the brawler's LAB_0237, and
// LAB_0237 leaves the brawler's own stale input bits); LAB_028E restores the dragon's tunables through the JOB pointer
// (job + 116 / 118 = the width / height of the job two on, the dragon keeps 2 / 1); LAB_0200 / LAB_0201 rewrite the
// attacker's (or the hit record's) action word to $20; LAB_0203 subtracts 10 from the attacker TYPE (44), which is what D0
// still holds, as the damage; the knight's reaction tables LAB_0621 / LAB_0622 are indexed by the unscaled type byte; the
// flyer reaches the beam position through VHPOSR; LAB_0F24 leaves D7 = the last probe's tunable for the walk helpers.
//
// Deliberate differences (each commented where it happens): a type whose table slot the asm never fills (a jump to
// address 0) does nothing; a job lookup that misses where the asm then writes through address 0 (the dragon's job in LAB_028E,
// the first / second fighter's job in the caster and the drake) writes nothing; a dagger flight whose slot is missing leaves
// the record's position alone (the asm stores whatever the registers hold); the asm spins forever on an all-null walk-script
// table, LAB_0F32 here gives up after 16 steps; input and action indexes past the end of the asm's data tables read 0.
// tests/test_fighters.py excludes all of these from its random arenas (host: lifted asm as oracle, ~99 % of the C++ lines),
// tests/test_fighters_emu.py runs the m68k build of the shims against the original handlers in unicorn.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "game/creatures.hpp"
#include "game/rules/ai_fight.hpp"
#include "game/world.hpp"

namespace ms { namespace game {

// The labels of the next-script values the handlers load (mog LAB_xxxx addresses; the rt fills them from the symbols,
// the host test from the symbol header).  One field per label: FightScripts::s07F8 is the address of LAB_07F8.
#define MS_FIGHT_SCRIPTS(X) \
	X(07EC) X(07F3) X(07F7) X(07F8) X(07F9) X(07FA) X(07FB) X(07FD) X(081B) X(081C) X(0849) X(084A) X(084B) X(084E) \
	X(0850) X(0851) X(0857) X(0858) X(0859) X(085A) X(085B) X(085C) X(085D) X(085E) X(085F) X(0860) X(0862) X(0863) \
	X(0864) X(0865) X(0866) X(0867) X(0868) X(0869) X(086B) X(0872) X(087E) X(0881) X(0883) X(0884) X(0886) X(0887) \
	X(088A) X(088B) X(088D) X(088E) X(0890) X(0891) X(0893) X(0894) X(0895) X(0896) X(08BF) X(0897) X(0898) X(0899) \
	X(089E) X(089F) X(08A0) X(08A1) X(08A2) X(08A3) X(08AA) X(08AB) X(08AC) X(08AF) X(08B4) X(08B5) X(08B6) X(08B7) \
	X(08B8) X(08B9) X(08BA) X(08BB) X(08BC) X(08BD)

struct FightScripts {
#define MS_X(n) uint32_t s##n;
	MS_FIGHT_SCRIPTS(MS_X)
#undef MS_X
	uint32_t sDaggerScript;    // LAB_07EB the script of a thrown dagger (daggerRelease)
	uint32_t tDaggerDamage;    // LAB_0302 its damage table
	uint32_t fFrames0648;      // LAB_0648: frame-table list of the spawned effect objects (LAB_0F34, LAB_02B1)
	uint32_t fFrames05E0;      // LAB_05E0: frame-table list of the dragon's object (LAB_0297)
	uint32_t hIdle;            // LAB_02D2: written into the handler table slot of type $28 by LAB_0297 / LAB_0F34
	uint32_t tHop;             // LAB_08CC: the hop-height table LAB_0203 / LAB_0298 / LAB_029F hand to LAB_0211
	uint32_t tHop2;            // LAB_08CD: the demon's second hop table (LAB_0EDF)
	// handler entry points (what the handler table holds): fighterRun maps them to the functions.  They are identities
	// only (nothing executes the asm bodies any more, ROADMAP 7.1h); fightTablesInit writes them into the table.
	uint32_t hKnight, hFlyer, hBrawler, hCaster, hDragon, hDragonPart, hDrake, hDagger;
	uint32_t hSnatcher, hDemon, hAiKnight, hStalker;   // SECSTRT_40, LAB_0ED2, LAB_0EFF, LAB_0EC2
};

#pragma pack(push, 2)

// LAB_061D.. : the working cells of the handlers, one block in the asm's BSS (the rt overlays it on the symbols; the host
// test checks every offset against the symbol table).  Flag bytes the asm tests with BTST #n,LABEL are the first byte of
// a word / long.
struct FightVars {
	uint32_t ulNextScript;      // +0    LAB_061D   the script the handler hands back through LAB_02BA
	int16_t  swStepX;           // +4    LAB_061E   knight: x step of the walk
	int16_t  swStepY;           // +6    LAB_061F   knight: depth step
	uint16_t uwLairFlag;        // +8    LAB_0620   LAB_02DB sets 1; the brawler's "target dead, already gloated" latch
	uint32_t aulHurtTab[18];    // +10   LAB_0621   attacker-type handler table of the knight (nothing fills it any more: the C++ switches)
	uint32_t aulHitTab[18];     // +82   LAB_0622   hit-target-type handler table
	uint16_t uwPad154;          // +154  (DS.W 1 before LAB_0623)
	uint8_t  ubDragonFlags[4];  // +156  LAB_0623   dragon: bit4 attack running, bit5 phase, bit6 reduced tunables, bit7 hit
	uint16_t uwPad160;          // +160
	uint16_t uwDragonDist;      // +162  LAB_0624   |dx| dragon -> target
	uint16_t uwDrakeX;          // +164  LAB_0625   target x seen by the drake
	uint16_t uwDrakeY;          // +166  LAB_0626   target depth seen by the drake
	uint8_t  ubDrakeFlags[2];   // +168  LAB_0627   drake: bit0 grabbed, bit1 flying, bit5 recovering, bit6 knocked
	uint16_t uwAimSteps;        // +170  LAB_0628   dagger aim: flight steps
	uint16_t uwAimArc;          // +172  LAB_0629   dagger aim: arc parameter
	uint16_t uwDrakeSteps;      // +174  LAB_062A   flight steps left of the drake
	uint8_t  ubCasterFlags[4];  // +176  LAB_062B   caster: bit2 dagger in flight, bit3 grabbing, bit5 grabbed
	uint16_t uwCasterWait;      // +180  LAB_062C   caster pause between actions
	uint16_t uwDrakeToggle;     // +182  LAB_062D   drake alternates its recovery script
};
static_assert(sizeof(FightVars) == 184, "LAB_061D..LAB_062D");
static_assert(offsetof(FightVars, aulHitTab) == 82 && offsetof(FightVars, ubDragonFlags) == 156 &&
	offsetof(FightVars, uwDragonDist) == 162 && offsetof(FightVars, ubDrakeFlags) == 168 &&
	offsetof(FightVars, uwAimSteps) == 170 && offsetof(FightVars, ubCasterFlags) == 176 &&
	offsetof(FightVars, uwDrakeToggle) == 182, "");

// LAB_0F3A..LAB_0F3F: the scratch the brawler / dragon movement helpers (LAB_0F1A..LAB_0F34) pass between each other.
struct MoveVars {
	int16_t  swDx;              // +0   LAB_0F3A   x step
	int16_t  swDy;              // +2   LAB_0F3B   depth step
	int16_t  swDyOverride;      // +4   LAB_0F3C   when non-zero replaces the depth step (LAB_0F1E / 0F1F set it)
	uint16_t uwNear;            // +6   LAB_0F3D   1 = target within depth reach
	uint16_t uwTabOffset;       // +8   LAB_0F3E   offset (0 / 32 / 64) into the walk-script table
	uint16_t uwVertical;        // +10  LAB_0F3F   1 = the depth correction bit was set
};
static_assert(sizeof(MoveVars) == 12, "LAB_0F3A..LAB_0F3F");

#pragma pack(pop)

// The joystick state LAB_00EE returns: D0 = LAB_062F (port 0 bits), D1 = LAB_0630 (port 1 bits); bit0 right, bit1 left,
// bit2 down, bit3 up, bit4 fire.
struct Joystick {
	uint16_t uwPort0;
	uint16_t uwPort1;
};

struct SoundCycle;

struct FighterEnv {
	FightVars *pVars;               // LAB_061D
	MoveVars *pMove;                // LAB_0F3A
	uint16_t *pToggle;              // LAB_0234: the flyer's alternation word (the 4 walk steps behind it are constants)
	uint16_t *pAimDist;             // LAB_02DA: dagger aim distance
	CombatJob *pJobs;               // LAB_0649, COMBAT_JOB_COUNT slots
	Knight *pKnights;               // LAB_0613: four knights, then the dragon (LAB_0617)
	Knight *pCreatures;             // LAB_05C3's value: the creature heap
	DaggerSlot *pSlots;             // LAB_0301
	DaggerBlock *pBlock;            // LAB_062E
	uint8_t *pHandlerTable;         // LAB_08C7 (big-endian longs; LAB_0297 / LAB_0F34 write the slot of type $28)
	ActiveKnights *pActive;         // LAB_05E4
	uint32_t *pFirst;               // LAB_05F2: Knight* of the first fighter (every creature's target)
	const uint32_t *pSecond;        // LAB_05F4: Knight* of the second fighter
	const uint32_t *pConfused;      // LAB_05D1: Knight* whose controls are mirrored while LAB_05D3 is non-zero
	const uint16_t *pConfusedOn;    // LAB_05D3
	uint32_t *pHopTable;            // LAB_08CE: table the hop routine LAB_0211 follows (the handlers set it to scripts.tHop)
	uint8_t *pHopDir;               // LAB_08D0: facing of the hop (LAB_0211 takes it as the record's facing; the byte at the label)
	uint32_t *pSelf;                // LAB_0633: Knight* the running handler works on (asm callees read it)
	uint32_t *pTarget;              // LAB_0634: Knight* the handler's target (the first fighter)
	const uint32_t *pDemoFlag;      // LAB_06DA: non-zero in the demo (nothing dies)
	uint32_t *pSeed;                // LAB_0973: random seed
	const uint32_t *pLinkA;         // LAB_0193: records the dragon fight keeps in step with the dragon (+8 = depth)
	const uint32_t *pLinkB;         // LAB_0194
	uint16_t uwJunkA, uwJunkC;      // the two words daggerStart copies from low memory (rt_abs_a / rt_abs_c)
	// ROADMAP 7.1h: the cells of the S_40 handlers and the script-called routines (mog S_0 / S_40 / S_41)
	uint8_t *pSnatchFlags;          // LAB_0EB6 (first byte): state flags of the type 4 creature (code hunk, written by the asm)
	uint8_t *pDemonFlags;           // LAB_0EEA (two bytes, one word write clears both): state flags of the demon
	uint16_t *pBatCounter;          // LAB_0EC1: the counter of LAB_0EBE
	uint32_t *pBodyA;               // LAB_01A1: Knight* of the demon's body
	uint32_t *pBodyB;               // LAB_01A2: Knight* of the record that follows its job
	const uint16_t *pDifficulty;    // LAB_06C0: word index into the AI odds
	const uint8_t *pAiOdds;         // LAB_097B: byte per difficulty (the AI knight's guard / attack odds)
	uint16_t *pAiLastAction;        // LAB_0F38: the AI knight's action of the previous frame
	const uint8_t *pAiSteps;        // SECSTRT_41: step tables of the AI knight (side +0, up +20, down +36), 52 bytes
	uint16_t *pHopStep;             // LAB_08CF: the hop counter of LAB_0210 / LAB_0211
	uint16_t *pFade;                // LAB_08D9: palette block the demon's death clears (words 2..7) before the fade calls
	SoundCycle *pPickCycle;         // LAB_02EC: the counters of the sound pickers
	FightScripts scripts;
	// callbacks (asm in the game, deterministic stand-ins in the host test).  The comments give the original's registers.
	Joystick (*joystick)();                                          // LAB_00EE: JSR; D0 / D1
	void (*obstacles)(Knight &self, int16_t swDx, int16_t swDy);     // LAB_0A71: A0, D0, D1 (clears wall-blocked bits of +63)
	void (*sound)(uint16_t uwId);                                    // LAB_0AA2: D0
	void (*screenEffect)();                                          // LAB_0427
	uint16_t (*beamPosition)();                                      // MOVE.W VHPOSR
	void (*sfxFixed)(uint8_t ubChannel, uint16_t uwSeq);             // SECSTRT_16 / LAB_0A9B / 0A9C / 0A9D: fixed Paula channel
	void (*fadeA)();                                                 // LAB_0D8A with A0 = LAB_08D9 (palette write)
	void (*fadeB)();                                                 // LAB_03EE with A0 = LAB_08D9 (palette to live)
};

// The nine handler entries: runs the handler whose address is ulHandler for the record ulOwner.  Returns false when the
// address is not one of FightScripts::h* (the caller then runs the asm handler).
bool fighterRun(const FighterEnv &env, uint32_t ulHandler, uint32_t ulOwner, HandlerResult *pOut);
// The handler identity of an AI kind (the value the handler table LAB_08C7 holds for the creature types that run it).  Which type runs
// which kind is the table kFightAis (rules/ai_fight.hpp); fightTablesInit writes it.
uint32_t fighterHandlerAddress(const FightScripts &scripts, AiKind kind);

// The handlers.  Each sets the cells the asm sets (LAB_0633, LAB_0634, LAB_061D ...) so the asm callees see them.
HandlerResult fighterKnight(const FighterEnv &env, Knight &self);       // LAB_01CA
HandlerResult fighterFlyer(const FighterEnv &env, Knight &self);        // LAB_0226
HandlerResult fighterBrawler(const FighterEnv &env, Knight &self);      // LAB_0236
HandlerResult fighterCaster(const FighterEnv &env, Knight &self);       // LAB_0251
HandlerResult fighterDragon(const FighterEnv &env, Knight &self);       // LAB_027A
HandlerResult fighterDragonPart(const FighterEnv &env, Knight &self);   // LAB_0298
HandlerResult fighterDrake(const FighterEnv &env, Knight &self);        // LAB_029F
HandlerResult fighterDagger(const FighterEnv &env, Knight &self);       // LAB_02CB
HandlerResult fighterIdle(const FighterEnv &env, Knight &self);         // LAB_02D2
// S_40 (ROADMAP 7.1h, src/game/fight_creatures.cpp)
HandlerResult fighterSnatcher(const FighterEnv &env, Knight &self);     // SECSTRT_40 (type 4): grabs the first fighter
HandlerResult fighterDemon(const FighterEnv &env, Knight &self);        // LAB_0ED2 (type 8): the demon and its body
HandlerResult fighterAiKnight(const FighterEnv &env, Knight &self);     // LAB_0EFF (type 16): the computer's knight
HandlerResult fighterStalker(const FighterEnv &env, Knight &self);      // LAB_0EC2 (type 64): follows the first fighter

// ---- small routines the handlers share, exposed for the tests -------------------------------------------------

// LAB_0215: arena edge clip of a walking fighter: clears input bit 1 (left) when x + 25 (facing right) / x - 25 (facing
// left) is below 10, bit 0 (right) above 320, bit 2 (down) when depth + 9 is above 155, bit 3 (up) below 30.
void clipToArena(Knight &k);

// LAB_01E6: does the defender (self) block the attacker's current action?  Writes the "blocked" latch into the
// defender's flags (+104 bit 7, only for the $1C stance) and plays the parry sound for a facing mismatch.  Returns
// LAB_01EB: true = the hit is stopped.
bool blockCheck(const FighterEnv &env, Knight &self, const Knight &attacker);

// LAB_0315 / LAB_0319 / LAB_031B / LAB_030D on the job table: the job whose owner is ulOwner, pause toggle, kill (job
// and owner record freed), restart with a new script.
CombatJob *jobOfOwner(CombatJob *pJobs, uint32_t ulOwner);
void jobTogglePause(CombatJob *pJobs, uint32_t ulOwner);
void jobKill(CombatJob *pJobs, uint32_t ulOwner);
void jobRestart(CombatJob *pJobs, uint32_t ulOwner, uint32_t ulScript);

// ---- the script-called sound pickers (opcode $B0 targets; asm/patches/mog.fighters.json) -----------------------------

// State of LAB_02EC: the two counters of LAB_02E6 / LAB_02E8 / LAB_02E9 (words).
struct SoundCycle {
	uint16_t uwPlain;           // LAB_02EC      LAB_02E6: 0..4
	uint16_t uwList;            // LAB_02EC+2    LAB_02E8 / LAB_02E9: byte offset into the id list
};
// id = base + (rng & mask), with the "0 stays 0, else minus 1" fold some of them apply; the ids are LAB_0AA2's.
enum SoundPick { SOUND_02DC, SOUND_02DE, SOUND_02E0, SOUND_02E1, SOUND_02E2, SOUND_02E3, SOUND_02E4, SOUND_02E6,
                 SOUND_02E8, SOUND_02E9 };
// Runs the picker for the labels above (LAB_02DC .. LAB_02E9): draws from the seed, plays the id(s) through sound().
void soundPick(SoundPick which, uint32_t &ulSeed, SoundCycle &cycle, void (*sound)(uint16_t uwId));


// ---- ROADMAP 7.1h: what the combat script's opcode $B0 and the handler table used to reach through asm addresses ----------

// The routines the scripts call through opcode $B0 used to be asm addresses in the script data.  They are C++ now and the
// operand is a tag instead (asm/patches/mog.fight_ops.json): FIGHT_OP_TAG | the label number (LAB_01C8 -> $F00001C8, the
// S_40 ones $0EB8..).  The tag is no valid RAM address on any Amiga, so an operand that is not a tag is still an asm routine
// (LAB_0A9F ...).  fightOpRun runs the op for the owner record (A1 of the original, the other registers are the script
// engine's: frame-table list A2, x / y / z D0..D2, facing D3) and returns false when ulFn is not a tag of ours.
const uint32_t FIGHT_OP_TAG = 0xF0000000u;
bool fightOpRun(const FighterEnv &env, uint32_t ulFn, uint32_t ulOwner, uint32_t ulFrames, uint16_t uwX, uint16_t uwY,
                uint16_t uwZ, uint8_t ubFacing);

// The first sixteen entries of the handler table LAB_08C7 that LAB_01AE filled with asm addresses (slot 68, LAB_04AC, is a
// map handler and stays asm): hKnight at 12 / 56, ..., written big-endian into env.pHandlerTable.
void fightTablesInit(const FighterEnv &env);

// LAB_01C6: a freshly initialised knight record (strength / constitution / endurance 1, 5 lives, 20 HP, long sword,
// padded armour, 10 gold and daggers ...) and a cleared inventory (the record's +96 must be set).
void knightDefaults(Knight &k);

}}  // namespace ms::game
