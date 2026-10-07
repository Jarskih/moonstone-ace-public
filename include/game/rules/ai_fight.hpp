// game/rules/ai_fight - what the creatures and the computer's knight decide in a fight (mog handlers LAB_0226..LAB_02D2 and
// SECSTRT_40 / LAB_0EC2 / LAB_0ED2 / LAB_0EFF; ROADMAP 9.6h).  Pure on purpose: no ACE, no OS, no globals, no job table.  The fight
// handlers (src/game/fighters.cpp, fight_creatures.cpp) keep the plumbing (the records, the jobs, the scripts, the daggers, the
// sound) and ask these rules at every decision: a Facts struct in, a small plan (an enum, with its numbers) out.  A decision that
// draws random numbers takes the seed and draws in the original's ORDER (the order is part of the behaviour).
//
// One AI per file, named like the monster kit's AI (tools/monsterkit/ai_catalog.json "ais"):
//   ai_fight_flyer.cpp      "flyer"      Be (type 0): crosses the arena, hurts through its walk scripts
//   ai_fight_snatcher.cpp   "snatcher"   Mudmen (4): appears, walks up, grabs and carries the first fighter (a state machine)
//   ai_fight_demon.cpp      "demon"      Guardian (8): lunge / strike / jump by distance, then a grab
//   ai_fight_knight.cpp     "ai_knight"  the computer's knight (16): blocks or attacks by distance and odds
//   ai_fight_dragon.cpp     "dragon"     the dragon (20) and its object "dragon_part" (44): breath and fireball phases
//   ai_fight_brawler.cpp    "brawler" / "brawler_b" / "spearman"   Troggs (24 / 28 / 32): walk up and pick one of three attacks
//   ai_fight_caster.cpp     "caster"     Ratmen (36): keeps its distance, throws daggers, grabs and drops the target
//   ai_fight_drake.cpp      "drake"      Balok (48): pounces in a dagger-style arc, grabs
//   ai_fight_stalker.cpp    "stalker"    Troll (64): follows the first fighter and strikes
//   ai_fight.cpp            the table below: which AI plays which ActorType.
//
// WHAT A MODDER CAN CHANGE HERE (src/game/rules/ai_fight*.cpp):
//  * which AI a creature type runs: a row of kFightAis (ActorType -> AiKind -> handler); a new type reuses an existing AI by adding a row
//    and giving the creature that type (creatures.ini `type`);
//  * the distances and odds each AI decides by: the named constants of its section (reach windows, the strike ranges, the cool-downs,
//    the 30 % gloat of the brawler), and the order of its tests;
//  * what hurts a creature and how much: brawlerHitTaken / dragonHitTaken / flyerHitTaken (the flat numbers are in damage.hpp);
//  * the states of the state-machine AIs (caster, snatcher, demon, drake, dragon): which flag bit means what and which state wins
//    when two are set (casterState, snatcherState, demonState).
// The handlers apply the answer; the animation scripts a plan names are the creature's own (see docs/MONSTER_KIT.md).
#pragma once
#include <stdint.h>

#include "game/api/clock.hpp"
#include "game/api/data.hpp"
#include "game/constants.hpp"
#include "game/rules/damage.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

// ---- the dispatch table ----------------------------------------------------------------------------------------------------------------------

// The AI families.  Brawler covers the three Trogg types (the type byte tells the axe from the spear).
enum class AiKind : uint8_t {
	Knight, Flyer, Brawler, Caster, Dragon, DragonPart, Drake, Dagger, Idle, Snatcher, Demon, AiKnight, Stalker, Count
};

// One row: the creature type byte it is keyed by (the Type Object), the AI it runs, and the monster kit's name for it.
struct FightAi {
	ActorType type;
	AiKind kind;
	const char *pName;
};
extern const FightAi kFightAis[];
extern const uint8_t kFightAiCount;

const FightAi *fightAiByType(uint8_t ubType);       // null: no AI for the type (a map handler or an unused slot)
const FightAi *fightAiByName(const char *pName);    // "flyer", "brawler" ... as in the catalog; null when unknown
const char *fightAiName(AiKind kind);               // the first row of the kind

// What a creature takes from one attacker: nothing, the attacker's contact damage (damage.hpp contactDamage), or a flat number.
struct HitTaken {
	bool bHurt;
	bool bContact;
	uint16_t uwFixed;
};

// ---- flyer (Be, type 0) -------------------------------------------------------------------------------------------------------------------------

enum : int16_t { FLYER_LEFT_EDGE = 0, FLYER_RIGHT_EDGE = 0x154 };   // x at which it turns (signed word)
enum : uint16_t { FLYER_ENTER_LEFT = 0xFFCE, FLYER_ENTER_RIGHT = 0x017C };   // where it comes back in: -50 and 380

// LAB_0230 / LAB_0231: everything but another flyer hurts it, and it only reacts to a hit on a non-flyer.
HitTaken flyerHitTaken(uint8_t ubAttackerType);
bool flyerHitsTarget(uint8_t ubTargetType);

// LAB_0231: it hit a knight that is already down (not itself hitting, no hit points, not the demo): it finishes it off.
bool flyerFinishesTarget(bool bTargetHitting, int16_t swTargetHp, bool bDemo);

// LAB_0228: the turn at the edge.  bTurned = false leaves x and facing as they were.
struct FlyerTurn {
	bool bTurned;
	uint16_t uwX;
	uint8_t ubFacing;
};
FlyerTurn flyerEdgeTurn(uint8_t ubFacing, uint16_t uwX);

// LAB_022A..LAB_022C: after a turn the flyer's lane alternates: one pass at the target's depth, the next jittered by the raster beam
// (bWobble: ((beam & 7) << 2) added); then it waits (random & 15) | 5 frames.
uint16_t flyerLaneY(bool bWobble, uint16_t uwTargetY, uint16_t uwBeam);
uint8_t flyerPauseFrames(uint32_t &ulSeed);

// LAB_022E: x step of the flight cycle (33 27 17 33), negated facing left.  A phase the original could only reach with a garbage record is 0.
uint16_t flyerStepX(uint8_t ubPhase, uint8_t ubFacing);

// ---- snatcher (Mudmen, type 4) --------------------------------------------------------------------------------------------------------------

// The state flags LAB_0EB6 (mirrored in the record's +104).
enum : uint8_t { SNATCH_CARRY = 0x01, SNATCH_FREE = 0x02, SNATCH_GONE = 0x04, SNATCH_RELEASE = 0x40 };
enum : uint8_t { SNATCH_SHOWN = 0x08, SNATCH_GRAB_READY = 0x10 };   // record flags: on the screen, next to the target
enum : uint8_t { SNATCH_HURT_TIMER = 0x14, SNATCH_HOLD_TIMER = 0x28 };
enum : int16_t { SNATCH_GRAB_MIN = 0x14, SNATCH_GRAB_MAX = 0x50, SNATCH_WALK_RANGE = 0x32, SNATCH_STRIKE_RANGE = 0x4B, SNATCH_REACH_RANGE = 0x64 };
enum : int16_t { SNATCH_APPEAR_OFFSET = 0x4B, SNATCH_APPEAR_SIDE_X = 0xA0 };

// LAB_0EAC..LAB_0EB2: which state wins when several flags are set (the original tests in this order).
enum class SnatchState : uint8_t { Approach, Gone, Carrying, Freed, Released };
SnatchState snatcherState(uint8_t ubFlags);

// LAB_0E98..LAB_0EA5: the move once it is beside the target, by the x distance and whether the depth is within reach.
enum class SnatchPlan : uint8_t { Walk, StrikeFar, Reach };
SnatchPlan snatcherApproach(int16_t swDist, bool bDepthNear);

// LAB_0EAA: next to the target and within the grab window.
bool snatcherCanGrab(int16_t swDist);

// LAB_0EA6: where it appears: beside the target, on the side with more room.
struct SnatchAppear {
	int16_t swOffsetX;
	uint8_t ubFacing;
};
SnatchAppear snatcherAppear(uint16_t uwTargetX);

// LAB_0EA1..LAB_0EA4: x step per walk phase (12 12 10 14, twice) and the depth step (2).
int16_t snatcherStepX(uint8_t ubPhase);
enum : int16_t { SNATCH_STEP_Y = 2 };

// ---- demon (Guardian, type 8) ----------------------------------------------------------------------------------------------------------------

enum : uint8_t { DEMON_E1 = 0x01, DEMON_E9 = 0x02, DEMON_E3 = 0x08, DEMON_E7 = 0x10, DEMON_E5 = 0x20, DEMON_BODY = 0x40 };
enum : int16_t { DEMON_LUNGE_RANGE = 0x64, DEMON_STRIKE8_RANGE = 0x82, DEMON_STRIKE4_RANGE = 0x8C, DEMON_STEP = 5 };
enum : uint8_t { DEMON_COOLDOWN_LUNGE = 6, DEMON_COOLDOWN_STRIKE8 = 6, DEMON_COOLDOWN_STRIKE4 = 5, DEMON_COOLDOWN_GRAB_A = 5, DEMON_COOLDOWN_GRAB_B = 4 };

// LAB_0EE1..LAB_0EE5: which state wins when several flags are set.
enum class DemonState : uint8_t { Think, StageE1, StageE3, StageE9, StageE7, StageE5 };
DemonState demonState(uint8_t ubFlags);

// LAB_0EDD..LAB_0EDF: the attack by the x distance (after the cool-down is spent).
enum class DemonPlan : uint8_t { Walk, Lunge, Strike8, Strike4 };
DemonPlan demonDecide(int16_t swDist);

// LAB_0EE7 / LAB_0EE5: the windows in which the second stage of the jump grabs the first fighter.
bool demonGrabsAtE7(int16_t swDist);    // 0x82..0x96
bool demonGrabsAtE5(int16_t swDist);    // 0x78..0x8C

// LAB_0EF2: struck: within 0x3C of the target it jumps back to 0x3C from it (facing left: +, else -).
bool demonSnapsBack(int16_t swDist);
int16_t demonSnapBackOffset(uint8_t ubFacing);

// ---- the computer's knight (type 16) ---------------------------------------------------------------------------------------------------------

enum : int16_t { AIK_STRIKE8_RANGE = 0x5A, AIK_HEAVY_RANGE = 0x5F, AIK_STRIKE4_RANGE = 0x64, AIK_GUARD_RANGE = 0x78 };

struct AiKnightFacts {
	int16_t swDist;             // the x distance to the opponent as LAB_0F24 / LAB_02C2 left it (D1)
	int16_t swOppDist;          // |dx| to the opponent (LAB_02C2)
	bool bOppLeft;              // the opponent is to the left (LAB_02C2's flag)
	int16_t swOppHp;
	uint8_t ubOppFacing;
	uint16_t uwOppAction;
	uint8_t ubFacing;
	uint8_t ubFlags;            // Knight +104: bit 7 = it has already blocked this blow
	uint8_t ubDaggers;
	uint16_t uwLastAction;      // its action of the previous frame
	bool bGloated;              // LAB_0620 is set
	uint8_t ubOdds;             // the guard / attack odds of the difficulty (LAB_097B)
};
enum class AiKnightMove : uint8_t { Stand, Walk, Strike8, Strike4, Heavy, Defend, Block, Dagger };
struct AiKnightPlan {
	AiKnightMove move;
	bool bClearCooldown;        // the opponent is down: the cool-down is cleared first
};

// LAB_0F08..LAB_0F13.  Draws: one percent draw to try to block (only without the latch), one for the attack odds, and one wasted
// draw when it decides not to attack.  QUIRK: a block attempt that was out of reach leaves 0 / 1 (the opponent's side) in place of the
// distance for the attack choice that follows.
AiKnightPlan aiKnightDecide(const AiKnightFacts &f, uint32_t &ulSeed);

// LAB_0F17: it hit something: a knight (or a heavy / action 4 blow) that meets a blocking target stops, else the alternate script.
FightScript aiKnightHitReply(uint8_t ubTargetType, uint16_t uwOwnAction, uint16_t uwTargetAction);

// ---- the dragon (type 20) and its object (type 44) -------------------------------------------------------------------------------------------

enum : uint8_t { DRAGON_FLIGHT = 0x10, DRAGON_PHASE2 = 0x20, DRAGON_REDUCED = 0x40, DRAGON_HIT = 0x80 };
enum : int16_t { DRAGON_FAR = 0x8C, DRAGON_BREATH_RANGE = 0x46 };
enum : uint16_t { DRAGON_X_MIN = 0x1E, DRAGON_X_MAX = 0x64, DRAGON_STEP = 5, DRAGON_REDUCED_REACH = 2, DRAGON_REDUCED_TOO_CLOSE = 1 };

// LAB_0287: the dragon is hurt only by a knight (contact damage) and a dagger (3); anything else lets it think.
HitTaken dragonHitTaken(uint8_t ubAttackerType);

// LAB_028E: the dragon's step (input bits right 1, left 2, down 4, up 8) kept inside x 30..100.
uint16_t dragonStepX(uint16_t uwX, uint16_t uwInput);
uint16_t dragonStepY(uint16_t uwY, uint16_t uwInput);

// LAB_027B: after the step, the next phase by the distance to the target: pick a melee / breath attack, or start a flight.
enum class DragonThink : uint8_t { PickAttack, StartFlight };
DragonThink dragonNextPhase(uint8_t ubFlags, int16_t swDist);

// LAB_0283..LAB_0286: the attack.  Flags are updated as the original does (the hit bit is spent, the breath marks the reduced step).
enum class DragonAttack : uint8_t { None, Strike8, Strike4, Breath };
DragonAttack dragonPickAttack(uint8_t &ubFlags, int16_t swDist, int16_t swTargetHp, bool bDepthNear);

// LAB_027B / LAB_027C: the flight plan of the near (first) and far (second) phase.
struct DragonFlightPlan {
	uint8_t ubFlagsSet;
	uint8_t ubFlagsClear;
	uint16_t uwWalkOffset;      // into the walk-script table
	uint16_t uwSteps;           // flight steps (also the cool-down word)
	uint16_t uwArc;
	uint16_t uwTargetX;
	uint16_t uwTargetHeight;
};
DragonFlightPlan dragonFlightPlan(bool bFar);

// LAB_027D: during the flight the walk script offset follows the phase.
uint16_t dragonFlightWalkOffset(uint8_t ubFlags);

// LAB_0299: the object strikes a target that is alive, within depth reach and left of x 100; it hops when it hits anything but the dragon.
bool dragonPartStrikes(int16_t swTargetHp, bool bDepthNear, int16_t swTargetX);
bool dragonPartHopsOnHit(uint8_t ubTargetType);

// ---- the brawlers (Troggs, types 24 / 28 / 32) -----------------------------------------------------------------------------------------------

enum : int16_t { BRAWLER_CLOSE_RANGE = 0x64, BRAWLER_HEAVY_RANGE = 0x78, BRAWLER_GLOAT_ODDS = 0x1E };
enum : uint8_t { BRAWLER_COOLDOWN_STRIKE = 0x0A, BRAWLER_COOLDOWN_HEAVY = 0x0A, BRAWLER_COOLDOWN_THRUST = 0x14 };

// LAB_0246: hurt by a knight (contact damage), a dagger (3) and the spear; anything else leaves it alone.
HitTaken brawlerHitTaken(uint8_t ubAttackerType);

struct BrawlerFacts {
	int16_t swDist;             // x distance to the target (D1)
	int16_t swTooClose;         // Knight +118
	int16_t swReach;            // Knight +116
	uint8_t ubCooldown;
	int16_t swTargetHp;
	bool bSpear;                // the type with the thrust
	bool bDemo;
	bool bGloated;              // LAB_0620: it has already gloated over the downed target
	bool bFirstDefends;         // the first fighter is in the Defend action
};
// What the brawler does this frame.  Thrust = the spear (action 4, script LAB_081B); Strike / GloatOnBody = action 8 (GloatOnBody
// also latches "gloated"); Heavy = action $20.  CountDown spends one cool-down tick.
enum class BrawlerPlan : uint8_t { Walk, Stand, CountDown, Thrust, GloatOnBody, Strike, Heavy };
// LAB_023E..LAB_0245.  One percent draw when the target stands within BRAWLER_CLOSE_RANGE (a hit above BRAWLER_GLOAT_ODDS gloats
// at once; otherwise it gloats unless the first fighter is defending).
BrawlerPlan brawlerDecide(const BrawlerFacts &f, uint32_t &ulSeed);
Action brawlerActionOf(BrawlerPlan plan);
uint8_t brawlerCooldownOf(BrawlerPlan plan);

// LAB_024B: it hit a record: the target a knight and not itself hitting, down: stop (or the spear finishes it); the target another kind:
// think as if nothing hit it (QUIRK: the original then clears the HIT record's input and action).
enum class BrawlerAfterHit : uint8_t { Think, FinishOff, Stop, Idle, Alternate };
BrawlerAfterHit brawlerAfterHit(bool bTargetIsKnight, bool bTargetHitting, int16_t swTargetHp, bool bSpear, bool bDemo);

// ---- the caster (Ratmen, type 36) -------------------------------------------------------------------------------------------------------------

// The record flags +104 / +105 and the shared flags LAB_062B.
enum : uint8_t { CASTER_DAGGER_OUT = 0x01, CASTER_HOLD = 0x08, CASTER_THROW = 0x10, CASTER_GRABBED = 0x20, CASTER_QUICK = 0x80 };
enum : uint8_t { CASTER2_RELEASE = 0x01, CASTER2_SLAM = 0x04 };
enum : uint8_t { CASTER_SHARED_THROWN = 0x04, CASTER_SHARED_SLAM = 0x08, CASTER_SHARED_GRABBED = 0x20 };
enum : int16_t { CASTER_STRIKE8_RANGE = 0x28, CASTER_STRIKE4_RANGE = 0x32, CASTER_CLOSE_THROW = 0x28, CASTER_HOLD_NEAR = 0x3C };
enum : uint16_t { CASTER_WAIT = 0x0F, CASTER_HOLD_FRAMES = 0x1E, CASTER_THROW_FRAMES = 0x11 };

// LAB_0252: which state the caster is in (the original tests the flags in this order).
enum class CasterState : uint8_t { Think, Flight, Grabbed, Release, Throw, Hold, Slam };
CasterState casterState(uint8_t ubFlags, uint8_t ubFlags2);

// LAB_0252..LAB_0255: standing in front of the target.
enum class CasterMelee : uint8_t { StartThrow, Stand, Wait, Strike8, Strike4 };
CasterMelee casterMelee(bool bDepthNear, uint8_t ubSharedFlags, int16_t swTargetHp, uint16_t uwWait, int16_t swDist);

// LAB_0256: aiming: a close target gets a quick underhand throw; the flight is at least 8 steps.
bool casterThrowsClose(int16_t swAimDist);
uint16_t casterThrowSteps(uint16_t uwAimSteps);

// LAB_025E: while the dagger is out and held, the script depends on how near the target is.
bool casterTargetNear(int16_t swDist);

// LAB_026B: a record hit it.  Think = not a knight (QUIRK: the original clears the ATTACKER's action word).  FinishedOff / Killed end
// the fight for the caster; Wound is the plain hit (the first fighter's contact damage); InFlight is a hit while the dagger is out.
enum class CasterHurt : uint8_t { Think, FinishedOff, Wound, Killed, InFlight };
CasterHurt casterHurtBy(uint8_t ubAttackerType, uint8_t ubFlags, uint16_t uwAttackerAction);

// LAB_0273: it hit a record.  Grab = the dagger is out and nothing holds yet; SlamStart = the hold turns into the slam; Counter = a
// plain strike that pushes the target and picks the strike script by the action.
enum class CasterHit : uint8_t { Think, Flight, Grab, SlamStart, Counter };
CasterHit casterHitReply(uint8_t ubTargetType, uint8_t ubFlags, uint8_t ubSharedFlags);

// ---- the drake (Balok, type 48) ---------------------------------------------------------------------------------------------------------------

enum : uint8_t { DRAKE_GRABBED = 0x01, DRAKE_FLYING = 0x02, DRAKE_RECOVER = 0x20, DRAKE_KNOCKED = 0x40 };
enum : int16_t { DRAKE_CLOSE = 0x46, DRAKE_HOP = 0x50, DRAKE_HEAVY = 0x78, DRAKE_WAIT = 0xB4, DRAKE_LAND_DIST = 0x0A, DRAKE_RELEASE_OFFSET = 0x4B };
enum : uint16_t { DRAKE_MAX_STEPS = 0x14, DRAKE_MIN_STEPS = 3 };

// LAB_029F..LAB_02A6: the drake's move when the target is within depth reach (bDepthNear) by the x distance.
enum class DrakeMelee : uint8_t { Jump, JumpClose, HopStrike, Heavy, Stand };
DrakeMelee drakeMelee(int16_t swDist, uint16_t uwAction, uint8_t ubFirstDaggers);

// LAB_02A7: the jump: refused with 3 steps or fewer; the flight has at most 20 steps.
bool drakeCanJump(uint16_t uwAimSteps);
uint16_t drakeJumpSteps(uint16_t uwAimSteps);

// LAB_02A9: a drake in flight lands on its target when the flight is past half way, low enough and within 10 of it.
bool drakeLandsOnTarget(uint16_t uwAimSteps, uint16_t uwStepsLeft, int16_t swHeight, int16_t swDist);

// LAB_02AA..LAB_02B0: the landing: a short arc is a quiet landing (sound $2F), a long one shakes the screen (sounds $2D $2E).
bool drakeLandsHard(int16_t swAimArc);

// LAB_02B2: it hit a record: action $20 grabs it, action 8 strikes, else the alternate script.
enum class DrakeHit : uint8_t { Grab, Strike, Alternate };
DrakeHit drakeHitReply(uint16_t uwAction);

// ---- the stalker (Troll, type 64) -------------------------------------------------------------------------------------------------------------

enum : int16_t { STALKER_STRIKE_NEAR = 0x64, STALKER_STRIKE_FAR = 0x96, STALKER_STEP_Y = 5 };

// LAB_0EC8..LAB_0ECA: in reach, the troll strikes (action 8, with a hop) unless the target is 100..149 away, where it swings heavy
// (action $20) - but only if it did not just swing heavy.
enum class StalkerPlan : uint8_t { Strike8, Heavy };
StalkerPlan stalkerDecide(int16_t swDist, uint16_t uwLastAction);

// LAB_0EC6 / LAB_0EC7: {dx, dy} of the walk by phase (the original reads code bytes past phase 3).
int16_t stalkerStepX(uint8_t ubPhase);
int16_t stalkerStepDy(uint8_t ubPhase);

}}  // namespace ms::game
