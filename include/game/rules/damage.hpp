// game/rules/damage - the damage pipeline of the fights: who hit whom with which action, whether it is blocked, how many hit points
// it costs and which script plays next (mog LAB_01E0..LAB_0206, LAB_021B, LAB_0204; ROADMAP 9.6g).  Pure on purpose: no ACE, no OS, no
// globals, no job table.  The fight code (src/game/fighters.cpp) collects the facts of one blow from the two records, asks the rule,
// and applies the answer (hit points, flags, the next script); the rules never touch a record.
//
//   blow ->  1. blockDecide             does the defender's stance stop it?          (BlockRule says when the check runs)
//            2. knightHurtOutcome       what a knight takes from a creature of type T (table kHurtRules, one row per ActorType)
//            3. contactDamage           the number: base table + strength + sword bonus, doubled for a heavy blow and a moon phase
//            4. HurtOutcome::script     the animation the knight plays next
//   and knightHitOutcome for the other direction (the knight hit something: carry on, stop, or get hurt by it).
// What a creature takes from a knight or a dagger is in ai_fight_*.hpp (creatureHitTaken, per AI), with the numbers named here.
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/damage.cpp):
//  * the damage formula: contactDamage (table value + strength + sword bonus; the heavy blow doubles; a moonstone doubles once more).
//    The sword bonus per sword is data (items.ini, WeaponDef::ubDamageBonus), the table values are the creature's damage list
//    (creatures.ini `damage`);
//  * what a creature does to a knight: the DAMAGE_* values below (the flat hits of the flyer, drake, spearman, stalker, demon,
//    dragon) and the rows of kHurtRules: add a row for a new creature type, or change the function of an existing row;
//  * who can be blocked and how: blockDecide (the $1C stance blocks once until the latch is released; facing each other parries
//    with a clang) and the BlockRule of a row;
//  * the shield: protectedDamage (the defender's protection count shifts the damage down).
// The per-creature flat values match tools/monsterkit/ai_catalog.json "damage" (tests/test_rules_fight.py checks them).
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/constants.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// ---- the named numbers ------------------------------------------------------------------------------------------------------------

// Flat hit points a creature takes off a knight (the creature's damage table is not read for these; ai_catalog "damage" mode fixed).
enum : uint16_t {
	DAMAGE_FLYER = 5,               // Be (type 0): LAB_0206
	DAMAGE_DRAKE = 5,               // Balok (type 48): LAB_01ED
	DAMAGE_SPEARMAN = 3,            // Trogg with the spear (type 32): LAB_01F6
	DAMAGE_DEMON_STRIKE4 = 8,       // the demon's action 4: LAB_01F9
	DAMAGE_DEMON = 10,              // the demon's other actions (heavy $20 and the rest): LAB_01F9
	DAMAGE_STALKER = 7,             // Troll (type 64): LAB_01FD
	DAMAGE_DRAGON_STRIKE4 = 20,     // the dragon's action 4, shifted down by the knight's protection: LAB_0200
	DAMAGE_DRAGON_FLIGHT = 30,      // the dragon in flight, shifted by the protection: LAB_0201
	DAMAGE_DRAGON_OBJECT = 34,      // the dragon's object (type 44): QUIRK, the original subtracts 10 from the TYPE byte it still holds
	DAMAGE_THROWN_DAGGER = 3        // a dagger that hits a creature (brawler, dragon): LAB_0248 / LAB_0289
};

// ---- contact damage and the shield -----------------------------------------------------------------------------------------------

// LAB_021B: the damage dealt by the attacker's current action.  pDamageTable = the table at Knight +42 (longs indexed by the action
// byte offset in +64), inv = the attacker's inventory (moonstone flags +22), uwMoonFrame = ActiveKnights +18.  Base long + strength
// (+70) + the sword bonus of the weapon row for the code in +88 (+2 / +3 / +5 for $17 / $18 / $19, WeaponDef::ubDamageBonus; a code
// with no row gives no bonus), doubled for the action $20, doubled again (once) by a matching moonstone: bit 0 or bit 3 with frame
// $2E, bit 1 with $2D, bit 2 with $31.  The word arithmetic leaves the upper word of the table long alone; returns the 32-bit D0.
uint32_t contactDamage(const Knight &attacker, const uint8_t *pDamageTable, const Inventory &inv, uint16_t uwMoonFrame);

// LAB_0204: damage >> the defender's protection count (inventory +8, LSR.W by a register: counts of 16 and more give 0, the count is
// taken modulo 64 like the 68k does).
uint16_t protectedDamage(uint16_t uwDamage, const Inventory &inv);

// ---- blocking -----------------------------------------------------------------------------------------------------------------

// LAB_01E6..LAB_01EB.  uwRequired = the low word of the defender's defence table at the attacker's action (the action the defender
// must be in to stop that blow).  Stance: the defender is in the $1C stance and the latch is free (the caller sets the latch: a
// stance blocks once until it is released).  Clash: the two face each other, a parry with a clang (the caller plays sound $11).
enum class Parry : uint8_t { None, Stance, Clash };
Parry blockDecide(uint16_t uwRequired, uint16_t uwDefenderAction, bool bStanceLatched, uint8_t ubDefenderFacing, uint8_t ubAttackerFacing);

// ---- a knight is hit by a creature --------------------------------------------------------------------------------------------

// The scripts a hurt or hit reaction can ask for.  The first group are the knight's own tables, the second the labelled scripts of
// the original (named by their role; the label is the cite).  The fight code turns a name into the address.
enum class FightScript : uint8_t {
	Idle,                   // the record's idle script (no reaction)
	Stop,                   // $FFFFFFFF: keep the running script
	Free,                   // 0: the job ends
	Alternate,              // Knight::ulScript26: the record's "after hit" script
	HurtByAction,           // the knight's hurt table at the attacker's action (LAB_020E)
	GuardOfOwnAction,       // the knight's action table at its own action: the guard animation that parried
	KnightParry07F3,        // LAB_07F3: the knight parries the spearman
	KnightDown07F8,         // LAB_07F8: the knight, already out, is knocked down
	KnightDown07F9,         // LAB_07F9: the same by a strike / axe
	KnightHop07FB,          // LAB_07FB: the knight is thrown by the dragon's object
	KnightFlung07FD,        // LAB_07FD: the knight is flung down (troll's heavy kill, the drake's landing)
	KnightCursed085F,       // LAB_085F: the ratmen's action 4 (also marks a life loss)
	KnightStruck085E,       // LAB_085E: the ratmen's action 8
	KnightByBeSame084A,     // LAB_084A: hurt by the flyer, same facing
	KnightByBeOpposite084B, // LAB_084B: facing each other
	FlyerKillSame0849,      // LAB_0849: the flyer's finishing move (same facing)
	FlyerKillOpposite084E,  // LAB_084E: facing each other
	SpearKill081C           // LAB_081C: the spearman's finishing thrust
};

// The facts of one blow.  The fight code fills them from the two records.
struct HurtFacts {
	int16_t swHp;           // the knight's hit points before the blow
	uint8_t ubFacing;       // the knight's facing
	bool bHitting;          // the knight is itself hitting a record (Knight +14 != 0)
	bool bBlocked;          // blockDecide stopped the blow (only valid when the row's BlockRule asked for the check)
	bool bDemo;             // the demo runs: nothing dies
	uint8_t ubAttType;      // the creature's type byte
	uint16_t uwAttAction;   // its action word
	uint8_t ubAttFacing;    // its facing
};

// How the hit points are taken.
enum class HitDamage : uint8_t {
	None,                   // nothing (a parried blow)
	Contact,                // contactDamage of the attacker
	TableByAction,          // the attacker's damage table at its action, raw (no strength, no sword): LAB_01F3
	TableSlot4,             // the attacker's damage table entry 1 (action 4), raw
	TableSlot8,             // the attacker's damage table entry 2 (action 8), raw
	Fixed,                  // uwAmount
	FixedProtected          // uwAmount shifted down by the knight's protection (protectedDamage)
};

// Side effects of a blow besides the hit points (HurtOutcome::ubEffects).
enum : uint8_t {
	HURT_FACE_AWAY = 0x01,          // the knight turns to face away from the attacker (attacker facing ^ 2)
	HURT_FACE_LEFT = 0x02,          // the knight faces left (3)
	HURT_LIFE_LOSS = 0x04,          // the knight is flagged to lose a life when the fight ends (ratmen)
	HURT_ATTACKER_HEAVY = 0x08,     // QUIRK: the ATTACKER's action becomes $20 (the dragon rewrites the other record)
	HURT_MATCH_DEPTH = 0x10,        // the knight takes the attacker's depth minus 1 (the dragon's reach)
	HURT_HOP = 0x20,                // the knight is thrown: hop right, the hop table of the dragon's object
	HURT_RESTART_ATTACKER = 0x40    // the attacker's job restarts on attackerScript (its finishing move); the knight's script is Free
};

struct HurtOutcome {
	HitDamage damage;
	uint16_t uwAmount;
	FightScript script;             // the knight's next script
	uint8_t ubEffects;
	FightScript attackerScript;     // with HURT_RESTART_ATTACKER
};

// When the fight code runs blockDecide for a row: never, only while the knight has hit points left, or always.  (The check has a side
// effect, the stance latch and the parry sound, so it only runs where the original runs it.)
enum class BlockRule : uint8_t { Never, WhileAlive, Always };

typedef void (*HurtFn)(const HurtFacts &f, HurtOutcome &o);

// One row of the Type Object table: what a knight does when a creature of `type` hit it (LAB_0621, indexed by the type byte).
struct HurtRule {
	ActorType type;
	BlockRule block;
	HurtFn fn;
	const char *pName;
};
extern const HurtRule kHurtRules[];
extern const uint8_t kHurtRuleCount;

// The row of an attacker type, or null for a type the original has no reaction for (the knight carries on with its idle script).
const HurtRule *hurtRuleFor(uint8_t ubType);

// ---- the knight hit something ----------------------------------------------------------------------------------------------------

// LAB_01E0: the facts: the record the knight hit, and the knight's own action.
struct HitFacts {
	uint8_t ubTargetType;
	int16_t swTargetHp;
	uint16_t uwTargetAction;
	uint16_t uwAction;          // the knight's action
	bool bDemo;
};

// The reaction: the next script, and bHurtByTarget = the target hurts the knight back (the dragon in flight: the DragonFlight row of
// kHurtRules is applied with the target as the attacker).
struct HitOutcome {
	FightScript script;
	bool bHurtByTarget;
};
HitOutcome knightHitOutcome(const HitFacts &f);

}}  // namespace ms::game
