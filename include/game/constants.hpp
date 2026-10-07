// Moonstone game vocabulary: the numbers the original code uses as ids, kinds, states and item codes, named once.
//
// Everything here is a plain number in the 68k asm (a literal after CMPI / MOVE).  Names are chosen from the way the
// code uses the value; a guess is marked "meaning inferred".  The values are NOT to be changed: they are stored in
// the original data structures (Knight, Lair, Inventory) and in the asm tables that index with them.
//
// Style for phase 2 (see AGENTS.md "engine patterns"): each concept that a dispatch will switch on is an `enum class`
// with its exact underlying type.  `raw()` turns it into the stored integer for a compare or a table index:
//     if(k.ulKind == raw(KnightKind::Ai)) ...        table[raw(ActorType::Mudmen)]
// A few unscoped constants (SCENE_*, KIND_AI, ...) stay for the code that already uses them.
//
// Pools and slots (one vocabulary for all of them): a *pool* is a fixed array; a *slot* is one element; a slot is
// *active* while the element is in use; to *spawn* is to take a free slot and fill it, to *despawn* is to free it.
//   Knight records  KNIGHT_SLOTS (5): four knights + the dragon, always active (kind says which)
//   creature pool   CREATURE_POOL_SIZE (20) Knight-layout records on the heap, Knight::ulActive != 0 = active slot
//   job pool        JOB_SLOTS (10): sprite / script jobs, Job::ubActive (a job runs one script for one record)
//   lair pool       LAIR_COUNT (24) overworld encounters, loot Inventory per lair
#pragma once
#include <stdint.h>

namespace ms { namespace game {

// The integer behind an enum class value (for compares with the stored fields and for table indices).
template<class E> constexpr auto raw(E e) { return static_cast<__underlying_type(E)>(e); }

// ---------------------------------------------------------------------------------------------------------
// Pool sizes
constexpr uint32_t KNIGHT_SLOTS = 5;          // LAB_0613..LAB_0617 (LAB_0029 loops D0 = 4)
constexpr uint32_t PLAYER_KNIGHTS = 4;        // slots 0..3 are the four knights (LAB_01C4 loop D7 = 3)
constexpr uint32_t CREATURE_POOL_SIZE = 20;   // heap records at LAB_05C3 (LAB_000A, LAB_003C)
constexpr uint32_t JOB_SLOTS = 10;            // LAB_0649 (LAB_0310 searches with D7 = 9)
constexpr uint32_t LAIR_COUNT = 24;           // LAB_05B9[17] lair records

// ---------------------------------------------------------------------------------------------------------
// Knight::ulKind (+54): which of the five persistent records this is.
enum class KnightKind : uint32_t {
	Knight0 = 0,    // the four player-selectable knights; the number is also the home village (PlaceId Village0..3)
	Knight1 = 1,
	Knight2 = 2,
	Knight3 = 3,
	Ai = 4,         // AI-controlled knight (CMPI.L #4,54(A0) in LAB_0030, LAB_01AE, LAB_0DB6)
	Dragon = 5      // the dragon record (LAB_01AE sets 5; LAB_0617)
};
constexpr uint32_t KIND_AI = raw(KnightKind::Ai);
constexpr uint32_t KIND_DRAGON = raw(KnightKind::Dragon);

// Knight::ubType (+77): the behaviour type.  It is a byte offset in steps of 4 into the handler tables LAB_08C7 (the
// per-type AI step), LAB_0621 / LAB_0622 (the hurt handlers); the Job copies it (Job::ubType).  Phase 2: this is the
// "type object" key; the handler tables are the dispatch table.
enum class ActorType : uint8_t {
	Be = 0x00,            // the "Be" creature (init LAB_018B); meaning of the name unknown
	Mudmen = 0x04,        // LAB_019D
	Demon = 0x08,         // the demon creature record (hurt handler LAB_01F9, "type 8"); meaning of the name inferred from mogDemon*Script
	KnightFight = 0x0C,   // a knight inside a fight (LAB_0DBD, LAB_004F)
	KnightMap = 0x10,     // a knight on the overworld map (LAB_01C6)
	Dragon = 0x14,        // dragon / black-knight fight type (LAB_001C pays gold first, LAB_0195)
	TroggAxe = 0x18,      // LAB_0169
	TroggAxeB = 0x1C,     // LAB_0170
	TroggSpear = 0x20,    // LAB_0176
	Ratmen = 0x24,        // LAB_018F
	DragonFlight = 0x28,  // the dragon in flight on the map / an effect object (LAB_0DCB)
	DragonPart = 0x2C,    // bat of the dragon arena (LAB_0192); an object, not a fighter
	Balok = 0x30,         // LAB_0198
	Dagger = 0x34,        // a thrown dagger (LAB_02D0 spawns it with type $34)
	Troll = 0x40,         // LAB_019F
	KnightAlt = 0x38,     // the second knight slot of the handler table (LAB_01AE writes the knight handler at 56 as well as at 12); no record carries it
	Dice = 0x44           // not a creature: the handler-table slot the dice screen installs (rt_scr_dice_idle); meaning inferred
};

// Controller port of a fighter, Knight::ubInputPort (+11).  The fighter step reads the joystick word of port 0 for
// 1 and of port 1 for anything else (LAB_00EA).
enum class InputPort : uint8_t {
	Joy0 = 1,   // data of port 0 (the mouse port; the second human in a duel)
	Joy1 = 2,   // data of port 1 (the joystick port; the first human)
	Ai = 4      // no controller: the AI step drives the record (LAB_01AE, every creature initialiser)
};

// Knight::uwAction (+64): the current action as a byte offset (step 4) into Knight::ulActionScripts / ulHurtScripts /
// ulDamageTable / ulDefenseTable.  The 16-entry tables at LAB_07D9 / LAB_07DA map the joystick word to one of these.
// Names are inferred from damage and defence use (contactDamage LAB_021B: 0x20 doubles; ORIGINAL_FOUNDATION.md:
// "offensive 4/8/20/24/32, defense 16, block 28, dagger 12").
enum class Action : uint16_t {
	Idle = 0x00,
	Strike4 = 0x04,    // offensive; fight_creatures treats it as a defence-capable move
	Strike8 = 0x08,    // offensive
	Dagger = 0x0C,     // throw a dagger (LAB_02D0, record action $0C)
	Defend = 0x10,     // required-defence action (LAB_01E6)
	Strike20 = 0x14,   // offensive
	Strike24 = 0x18,   // offensive
	Block = 0x1C,      // block (ORIGINAL_FOUNDATION.md "block28")
	Heavy = 0x20       // two-handed / heavy: damage doubled (LAB_021B)
};

// Knight::ulSword (+88) and Knight::ulArmour (+92): the item code of the equipment (LAB_0013, LAB_021B).
enum class SwordItem : uint32_t {
	Long = 0x16,       // start weapon, damage bonus +0
	Broad = 0x17,      // +2
	Claymore = 0x18,   // +3
	Sharpness = 0x19   // +5; forced when the inventory has a sword of sharpness (Inventory::ubSharpSword)
};
enum class ArmourItem : uint32_t {
	Padded = 0x1B,     // start armour, max-HP bonus +0
	Mail = 0x1C,       // +10
	Plate = 0x1D,      // +20
	Battle = 0x1E      // +30
};

// ActiveKnights::uwMoonFrame (+18): the lunar image 45..49 ($2D..$31).  Moonstones in Inventory::ubMoonstones double the
// damage / gate places on matching frames (LAB_021F, LAB_00A1).  Meaning of the phase names inferred from the cycle
// 45, 47, 46, 48, 49, 48, 46, 47 (LAB_06C2): 45 is the initial frame, 49 the extreme.
enum class MoonFrame : uint16_t {
	New = 0x2D,        // stone bit 1; the value LAB_01AE starts with
	Quarter = 0x2E,    // stone bits 0 and 3
	Full = 0x31        // stone bit 2
};
constexpr uint16_t MOON_FRAME_NEW = raw(MoonFrame::New);
constexpr uint16_t MOON_FRAME_QUARTER = raw(MoonFrame::Quarter);
constexpr uint16_t MOON_FRAME_FULL = raw(MoonFrame::Full);

// ---------------------------------------------------------------------------------------------------------
// Scene ids of the screens (mogSceneId, LAB_068F, DOC_TECHNIQUE 10.19) - the state the screen machine dispatches on.
enum class SceneId : uint32_t {
	Meet = 1,         // two knights meet (after a fight, or a meeting without one): loot / exchange
	Creature = 2,     // loot of a creature lair
	Wizard = 3,
	Smith = 5,
	Market = 6,
	Exchange8 = 8,    // exchange between two knights
	Temple = 9,       // also the "you lost" screen and the avoid-the-fight scroll screen
	Dragon = 10,      // loot of the dragon
	Exchange11 = 11   // exchange between two knights
};
constexpr uint32_t SCENE_MEET = raw(SceneId::Meet);
constexpr uint32_t SCENE_CREATURE = raw(SceneId::Creature);
constexpr uint32_t SCENE_WIZARD = raw(SceneId::Wizard);
constexpr uint32_t SCENE_SMITH = raw(SceneId::Smith);
constexpr uint32_t SCENE_MARKET = raw(SceneId::Market);
constexpr uint32_t SCENE_EXCHANGE_8 = raw(SceneId::Exchange8);
constexpr uint32_t SCENE_TEMPLE = raw(SceneId::Temple);
constexpr uint32_t SCENE_DRAGON = raw(SceneId::Dragon);
constexpr uint32_t SCENE_EXCHANGE_11 = raw(SceneId::Exchange11);

// mogEncounterKind (LAB_076D): the kind of the fight in progress; only the lair value is ever tested.
enum class EncounterKind : uint32_t { None = 0, Lair = 2 };

// Map node ids (MapNode::swId, LAB_069F): the four home villages are $15..$18, one per knight kind 0..3.
enum class PlaceId : uint16_t {
	Village0 = 0x15,
	Village1 = 0x16,
	Village2 = 0x17,
	Village3 = 0x18,
	TownA = 0x19,
	TownB = 0x1A,
	Stonehenge = 0x1B,
	Valley = 0x1C,
	Wizard = 0x1E,
	Duel = 0x21
};

// The combat script bytecode (LAB_0328 interpreter, combat_script.cpp): the first byte of an instruction.  A byte below
// $80 is a draw event; $80 and above is one of these.  Phase 2: this is the instruction set of the script VM.
enum class ScriptOp : uint8_t {
	SetFacing = 0x80,         // LAB_0358: set / toggle the facing
	Call = 0x84,              // LAB_035B: arm a jump taken at the next marker (or jump at once)
	LoopBegin = 0x88,         // LAB_035D: counted loop start
	MotionBlock = 0x8C,       // LAB_0361: motion parameter block
	Loop2Begin = 0x94,        // LAB_0362: second counted loop start
	JumpIfDemo = 0x98,        // LAB_0363: jump when the demo flag is set
	MovePos = 0xA0,           // LAB_0368: set or move x / y / z
	Sound = 0xA4,             // LAB_0367: play a sound
	PokeOwner = 0xA8,         // LAB_0374: write into the owner record
	SpawnArm = 0xAC,          // LAB_0372: arm a second script for this job
	CallRoutine = 0xB0,       // LAB_038B: call an engine routine with the job state
	JumpIfOwnerDead = 0xB4,   // LAB_038E: jump and reset when the owner is dead
	SpawnJob = 0xB8,          // LAB_0390: start another job
	KillJob = 0xBC,          // LAB_0391: kill the job and clear the owner's first long
	FrameList = 0xC0,         // LAB_0392: select a frame-table list
	JumpIfSameFacing = 0xC4,  // LAB_038C
	JumpIfZero = 0xC8,        // LAB_0393
	JumpIfNonZero = 0xCC,     // LAB_0397
	ResetWork = 0xD0,         // LAB_039B
	Return = 0xFD,            // jump to the position after the motion block
	Loop2Jump = 0xFE,         // jump to the second loop start
	EndOfFrame = 0xFF         // LAB_034A: end of the frame / script
};

}}  // namespace ms::game
