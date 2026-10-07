// game/fight_creatures - the four creature handlers of mog's S_40 hunk in C++ (ROADMAP 7.1h): the type 4 "snatcher"
// SECSTRT_40, the demon LAB_0ED2 (type 8) with its body record, the computer's knight LAB_0EFF (type 16) and the type 64
// stalker LAB_0EC2.  Transcribed from mog.asm; the labels in the comments are mog's.  They are dispatched like the nine
// handlers of fighters.cpp (fighterRun, fighters.hpp): the handler table LAB_08C7 holds their identities, nothing jumps to
// the asm bodies any more.  tests/test_fighters.py runs every handler against the lifted asm on random arenas.
//
// Quirks kept (each commented where it happens): LAB_02C2 leaves D1 = 1 / 0 (the sign of the x difference), which LAB_0F0D
// then reads as the AI knight's distance after a block attempt that was out of reach; LAB_0EB2 hands LAB_030D the registers the wrong way round (A1 = the
// target's idle script as the "owner", A0 = the target as the script), which finds no job; the demon's LAB_0EE1 / LAB_0EE3
// write a byte through A0 = the script LAB_07FB (the original meant the demon); LAB_0EF2 reads the target cell LAB_0634
// before this frame's handler has set it; LAB_0F0A's second facing compare can never fail.
// Deliberate differences: the "DEMON HIT" / "DEMON STRUCK" / " Stupid " messages play through LAB_0BB3, which is a bare RTS
// in mog, so they are left out; a job lookup that misses where the asm then writes through address 0 writes nothing; the
// AI knight's step tables read 0 past the three tables of S_41 (the asm reads a variable and text there); with the opponent
// down and 256 or more away the AI knight's walk (LAB_0F02) indexes its step table with the stale high byte of D0, which
// holds that distance, and reads far memory (the port walks by the table); LAB_0F32 gives up after 16 steps (fighters.cpp).
#include "game/api/party.hpp"
#include "engine/util.hpp"
#include "game/fighters.hpp"
#include "game/fighters_int.hpp"
#include "game/rules/ai_fight.hpp"
#include "game/rules/damage.hpp"

namespace ms { namespace game {

using namespace fi;

namespace {

// ---- LAB_0EB6: the snatcher ----------------------------------------------------------------------------------------

// The state flags are rules/ai_fight.hpp SNATCH_* (BTST #n,LAB_0EB6 and, mirrored, +104); the decisions are rules/ai_fight_snatcher.cpp.

// LAB_0E99 .. LAB_0EA0: one walk step towards the input bits LAB_0F24 set.
HandlerResult snatchWalk(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	MoveVars &m = *e.pMove;
	advancePhase(e, k, 0, 1);                                      // MOVEQ #0,D6 ; MOVEQ #1,D7 ; JSR LAB_0F32
	if(bit(k.uwInput, 0)) {                                        // LAB_0EA1
		m.swDx = snatcherStepX(k.ubAnimPhase);
	}
	if(bit(k.uwInput, 1)) {                                        // LAB_0EA2
		m.swDx = (int16_t)-snatcherStepX(k.ubAnimPhase);
	}
	if(bit(k.uwInput, 3)) {                                        // LAB_0EA3
		m.swDy = -SNATCH_STEP_Y;
	}
	if(bit(k.uwInput, 2)) {                                        // LAB_0EA4
		m.swDy = SNATCH_STEP_Y;
	}
	const uint16_t mask = blockedMask(e.pJobs, &k, (uint16_t)m.swDx, k.ubFacing);
	k.uwInput = (uint16_t)(k.uwInput & mask);                      // AND.W D0,62(A0)
	if((k.uwInput & 3) == 0) {
		m.swDx = 0;
	}
	if((k.uwInput & 0x0C) == 0) {
		m.swDy = 0;
	}
	v.ulNextScript = tab32(knightWalkScripts(k), (uint16_t)((uint16_t)k.ubAnimPhase << 2));
	k.uwX = (uint16_t)(k.uwX + (uint16_t)m.swDx);
	k.uwY = (uint16_t)(k.uwY + (uint16_t)m.swDy);
	if(m.swDx == 0 && m.swDy == 0) {                               // TST.L LAB_0F3A: standing still
		k.ubAnimPhase = 0;
		v.ulNextScript = k.ulIdleScript;
	}
	return finish(e);
}

}  // namespace

HandlerResult fighterSnatcher(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	uint8_t &f = *e.pSnatchFlags;
	*e.pSelf = addr(&k);
	*e.pTarget = *e.pFirst;
	v.ulNextScript = k.ulIdleScript;
	k.uwInput = 0;
	if(k.ulAttacker != 0) {                                        // LAB_0EB5: hit: the damage of the attacker
		subHp(k, damageOf(e, *rec(k.ulAttacker)));
		k.ubAnimTimer = SNATCH_HURT_TIMER;
		v.ulNextScript = e.scripts.s08A3;
		return finish(e);
	}
	if(k.ulHitTarget != 0) {                                       // LAB_0EB4: it hit something: freeze that and carry it
		jobTogglePause(e.pJobs, k.ulHitTarget);
		v.ulNextScript = e.scripts.s089F;
		f |= SNATCH_CARRY;
		k.ubBehaviourFlags |= SNATCH_CARRY;
		k.ubAnimTimer = SNATCH_HOLD_TIMER;
		return finish(e);
	}
	Knight &tgt = *rec(*e.pTarget);
	SnatchState state = snatcherState(f);
	if(state == SnatchState::Gone) {
		return finish(e);
	}
	if(state == SnatchState::Carrying) {                               // LAB_0EAC
		if(!(k.ubBehaviourFlags & SNATCH_CARRY)) {
			return finish(e);
		}
		k.ubAnimTimer = (uint8_t)(k.ubAnimTimer - 1);
		if(k.ubAnimTimer != 0) {
			const Joystick j = e.joystick();                       // the second joystick breaks the hold: fire + down
			if(bit(j.uwPort1, 4) && bit(j.uwPort1, 2)) {
				v.ulNextScript = e.scripts.s08A0;
				f &= (uint8_t)~SNATCH_CARRY;
				k.ubBehaviourFlags &= (uint8_t)~SNATCH_CARRY;
				f |= SNATCH_RELEASE;
				k.ubBehaviourFlags |= SNATCH_RELEASE;
				return finish(e);
			}
			v.ulNextScript = e.scripts.s089F;                      // LAB_0EAE
			return finish(e);
		}
		f &= (uint8_t)~SNATCH_CARRY;                                   // LAB_0EAF: the hold ran out: swallow it
		f |= SNATCH_FREE;
		k.ubBehaviourFlags |= SNATCH_FREE;
		killCurrent(e);
		state = SnatchState::Freed;                                // falls into LAB_0EB0
	}
	if(state == SnatchState::Freed) {                                  // LAB_0EB0
		if(k.ubBehaviourFlags & SNATCH_FREE) {
			v.ulNextScript = e.scripts.s08A1;
			jobKill(e.pJobs, *e.pTarget);
			f &= (uint8_t)~SNATCH_FREE;
			k.ubBehaviourFlags &= (uint8_t)~SNATCH_FREE;
		}
		return finish(e);
	}
	if(state == SnatchState::Released) {                               // LAB_0EB2: the hold was broken
		if(k.ubBehaviourFlags & SNATCH_RELEASE) {
			f &= (uint8_t)~SNATCH_RELEASE;
			k.ubBehaviourFlags &= (uint8_t)~SNATCH_RELEASE;
			jobTogglePause(e.pJobs, *e.pTarget);
			jobRestart(e.pJobs, tgt.ulIdleScript, addr(&tgt));     // QUIRK: A1 / A0 the wrong way round, matches no job
			subHp(k, 1);
			v.ulNextScript = e.scripts.s08A3;
		}
		return finish(e);
	}
	if(!(k.ubBehaviourFlags & SNATCH_SHOWN)) {                                 // LAB_0EA6: not on the screen yet: appear beside the target
		k.ubAnimTimer = (uint8_t)(k.ubAnimTimer - 1);
		if(k.ubAnimTimer == 0) {
			return finish(e);
		}
		k.uwX = tgt.uwX;
		k.uwY = tgt.uwY;
		const SnatchAppear at = snatcherAppear(k.uwX);             // the side with more room
		k.ubFacing = at.ubFacing;
		k.uwX = (uint16_t)(k.uwX + (uint16_t)at.swOffsetX);
		v.ulNextScript = e.scripts.s0897;
		k.ubBehaviourFlags |= SNATCH_SHOWN;
		k.ubBehaviourFlags |= SNATCH_GRAB_READY;
		return finish(e);
	}
	if(k.ubBehaviourFlags & SNATCH_GRAB_READY) {                                  // LAB_0EAA: next to the target: grab it
		if(depthNear(k, tgt)) {
			bool left;
			const uint16_t d0 = xDelta(k, tgt, left);
			if(snatcherCanGrab((int16_t)d0)) {
				jobTogglePause(e.pJobs, addr(&tgt));
				killCurrent(e);
				v.ulNextScript = e.scripts.s08A2;
				f |= SNATCH_GONE;
				return finish(e);
			}
		}
		k.ubBehaviourFlags &= (uint8_t)~SNATCH_GRAB_READY;               // LAB_0EAB
		v.ulNextScript = e.scripts.s0899;
		return finish(e);
	}
	const Approach ap = approach(e);                               // JSR LAB_0F24
	uint16_t d1 = ap.d1;
	if(ap.d0 != 0) {
		if(e.pMove->uwNear == 0) {
			return snatchWalk(e, k);
		}
		bool left;
		d1 = xDelta(k, tgt, left);                                 // JSR LAB_02C2 ; MOVE.W D0,D1
	}
	switch(snatcherApproach((int16_t)d1, depthNear(k, tgt))) {
	case SnatchPlan::Walk: return snatchWalk(e, k);                // LAB_0E98
	case SnatchPlan::StrikeFar:                                    // LAB_0EA9: strike from a distance
		v.ulNextScript = e.scripts.s0898;
		k.ubBehaviourFlags = 0;
		k.ubBehaviourFlags |= SNATCH_SHOWN;
		k.ubBehaviourFlags |= SNATCH_GRAB_READY;
		return finish(e);
	case SnatchPlan::Reach:                                        // LAB_0EA5
		v.ulNextScript = e.scripts.s089E;
		return finish(e);
	}
	return snatchWalk(e, k);
}

// ---- LAB_0EC2: the type 64 stalker ----------------------------------------------------------------------------------

namespace {

// LAB_0EC6 (left, dx mirrored) / LAB_0EC7 (right): the step of the current phase, D7 from LAB_0F2E.
void stalkSide(const FighterEnv &e, const Knight &k, bool bMirror, Dir &d) {
	MoveVars &m = *e.pMove;
	d.d7 = dirSign(k);
	m.swDx = stalkerStepX(k.ubAnimPhase);                         // the walk table is rules/ai_fight_stalker.cpp
	m.swDy = stalkerStepDy(k.ubAnimPhase);
	if(bMirror) {
		m.swDx = (int16_t)-m.swDx;
	}
}

}  // namespace

HandlerResult fighterStalker(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	MoveVars &m = *e.pMove;
	*e.pSelf = addr(&k);
	v.ulNextScript = k.ulIdleScript;
	k.uwInput = 0;
	*e.pTarget = *e.pFirst;
	if(k.ulHitTarget != 0) {                                       // LAB_0ECB
		v.ulNextScript = k.ulScript26;
		return finish(e);
	}
	if(k.ulAttacker != 0) {                                        // LAB_0ECC: hit: damage and a spark at the hit point
		subHp(k, damageOf(e, *rec(k.ulAttacker)));
		hitSpark(e, k);
		v.ulNextScript = e.scripts.s08AC;
		return finish(e);
	}
	const Knight &first = *rec(*e.pFirst);
	if((int16_t)first.swHp <= 0) {                                 // LAB_0ECD: nobody left to chase
		return finish(e);
	}
	const Approach ap = approach(e);
	if(ap.d0 == 0) {                                               // LAB_0EC8: in reach: strike
		bool left;
		const uint16_t d0 = xDelta(k, *rec(*e.pTarget), left);
		if(stalkerDecide((int16_t)d0, k.uwAction) == StalkerPlan::Strike8) {   // LAB_0EC9
			k.uwAction = raw(Action::Strike8);
			v.ulNextScript = e.scripts.s08AA;
			*e.pHopDir = k.ubFacing;
			*e.pHopTable = e.scripts.tHop;
			return finish(e);
		}
		k.uwAction = raw(Action::Heavy);
		v.ulNextScript = e.scripts.s08AB;
		return finish(e);
	}
	Dir d;
	d.d6 = 0;
	d.d7 = ap.d7;
	if(bit(k.uwInput, 0)) {                                        // LAB_0EC7
		stalkSide(e, k, false, d);
	} else if(bit(k.uwInput, 1)) {                                 // LAB_0EC6
		stalkSide(e, k, true, d);
	}
	if(bit(k.uwInput, 3)) {                                        // LAB_0EC4
		d.d7 = dirSign(k);
		m.swDy = -STALKER_STEP_Y;
	} else if(bit(k.uwInput, 2)) {                                 // LAB_0EC5
		d.d7 = dirSign(k);
		m.swDy = STALKER_STEP_Y;
	}
	return moveApply(e, k, d);                                     // BRA LAB_0F20
}

// ---- LAB_0ED2: the demon ---------------------------------------------------------------------------------------------

namespace {

// The flag bits of LAB_0EEA are rules/ai_fight.hpp DEMON_*, named by the label that handles them; the distances are rules/ai_fight_demon.cpp.

// The record LAB_01A2 follows the demon: its job takes the demon's x and depth.
void bodyFollow(const FighterEnv &e, const Knight &k) {
	CombatJob *j = jobOfOwner(e.pJobs, *e.pBodyB);                 // MOVE.L LAB_01A2,D0 ; JSR LAB_0315
	if(j != 0) {
		j->uwX = k.uwX;
		j->uwZ = k.uwY;
	}
}

// LAB_0EDA: follow, then the exit.
HandlerResult demonSync(const FighterEnv &e, const Knight &k) {
	bodyFollow(e, k);
	return finish(e);
}

// LAB_0ED5 .. LAB_0ED9: a step of five towards the input bits LAB_0F24 set.
HandlerResult demonWalk(const FighterEnv &e, Knight &k) {
	int16_t dx = 0, dy = 0;
	if(bit(k.uwInput, 0)) {
		dx = DEMON_STEP;
	} else if(bit(k.uwInput, 1)) {
		dx = -DEMON_STEP;
	}
	if(bit(k.uwInput, 3)) {
		dy = -DEMON_STEP;
	} else if(bit(k.uwInput, 2)) {
		dy = DEMON_STEP;
	}
	k.uwX = (uint16_t)(k.uwX + (uint16_t)dx);
	k.uwY = (uint16_t)(k.uwY + (uint16_t)dy);
	return demonSync(e, k);
}

// The tail of LAB_0EE1 / LAB_0EE3 while the grab is on (flag bit 6): the body record's facing is the hop direction, the first
// fighter is thawed and restarted on LAB_07FB; the demon's byte goes to +106 of A0 = LAB_07FB (QUIRK, see the file header).
void demonRelease(const FighterEnv &e, uint8_t ubTimer) {
	*e.pHopDir = rec(*e.pBodyA)->ubFacing;
	jobTogglePause(e.pJobs, *e.pFirst);
	jobRestart(e.pJobs, *e.pFirst, e.scripts.s07FB);
	*jobPtr<uint8_t>(e.scripts.s07FB + 106) = ubTimer;
}

}  // namespace

HandlerResult fighterDemon(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	uint8_t *f = e.pDemonFlags;
	*e.pSelf = addr(&k);
	v.ulNextScript = k.ulIdleScript;
	k.uwInput = 0;
	bodyFollow(e, k);                                              // the body's job follows at once
	if(k.ulAttacker != 0) {                                        // LAB_0EF2: struck ("DEMON STRUCK")
		f[0] = 0;
		f[1] = 0;
		v.ulNextScript = e.scripts.s08BD;
		subHp(k, damageOf(e, *rec(k.ulAttacker)));
		bool left;
		const uint16_t d0 = xDelta(k, *rec(*e.pTarget), left);     // QUIRK: LAB_0634 is last frame's
		if(demonSnapsBack((int16_t)d0)) {
			k.uwX = rec(*e.pTarget)->uwX;
			k.uwX = (uint16_t)(k.uwX + (uint16_t)demonSnapBackOffset(k.ubFacing));
		}
		return demonSync(e, k);
	}
	if(k.ulHitTarget != 0) {                                       // LAB_0EF5: it hit ("DEMON HIT")
		k.ubCooldown = 5;
		f[0] = 0;
		f[1] = 0;
		v.ulNextScript = 0xFFFFFFFFu;
		return demonSync(e, k);
	}
	*e.pTarget = *e.pFirst;
	switch(demonState(f[0])) {
	case DemonState::StageE1:                                      // LAB_0EE1
		f[0] &= (uint8_t)~DEMON_E1;
		f[0] |= DEMON_E7;
		v.ulNextScript = e.scripts.s08B8;
		if(f[0] & DEMON_BODY) {
			demonRelease(e, 3);
			f[0] &= (uint8_t)~DEMON_BODY;
		}
		return demonSync(e, k);
	case DemonState::StageE3:                                      // LAB_0EE3
		f[0] &= (uint8_t)~DEMON_E3;
		v.ulNextScript = e.scripts.s08BA;
		if(f[0] & DEMON_BODY) {
			demonRelease(e, 5);
			f[0] &= (uint8_t)~DEMON_BODY;
		}
		return demonSync(e, k);
	case DemonState::StageE9:                                      // LAB_0EE9
		f[0] &= (uint8_t)~DEMON_E9;
		return demonSync(e, k);
	case DemonState::StageE7: {                                    // LAB_0EE7
		f[0] |= DEMON_E3;
		f[0] &= (uint8_t)~DEMON_E7;
		v.ulNextScript = e.scripts.s08B9;
		bool left;
		const uint16_t d0 = xDelta(k, *rec(*e.pTarget), left);
		if(demonGrabsAtE7((int16_t)d0)) {
			jobTogglePause(e.pJobs, *e.pTarget);
			v.ulNextScript = e.scripts.s08BB;
			f[0] |= DEMON_BODY;
			k.ubCooldown = DEMON_COOLDOWN_GRAB_A;
		}
		return demonSync(e, k);
	}
	case DemonState::StageE5: {                                    // LAB_0EE5
		f[0] |= DEMON_E1;
		f[0] &= (uint8_t)~DEMON_E5;
		v.ulNextScript = e.scripts.s08B7;
		bool left;
		const uint16_t d0 = xDelta(k, *rec(*e.pTarget), left);
		if(demonGrabsAtE5((int16_t)d0)) {
			jobTogglePause(e.pJobs, *e.pTarget);
			v.ulNextScript = e.scripts.s08BC;
			f[0] |= DEMON_BODY;
			k.ubCooldown = DEMON_COOLDOWN_GRAB_B;
		}
		return demonSync(e, k);
	}
	case DemonState::Think: break;
	}
	const Knight &first = *rec(*e.pTarget);
	if(!((int16_t)first.swHp > 0)) {
		return finish(e);
	}
	const Approach ap = approach(e);                               // LAB_0ED4
	uint16_t d1 = ap.d1;
	if(ap.d0 != 0) {
		if(e.pMove->uwNear == 0) {
			return demonWalk(e, k);
		}
		bool left;
		d1 = xDelta(k, *rec(*e.pTarget), left);
	}
	if(k.ubCooldown != 0) {                                        // LAB_0EDC: the cool-down
		k.ubCooldown = (uint8_t)(k.ubCooldown - 1);
		if(k.ubCooldown != 0) {
			return demonWalk(e, k);
		}
	}
	switch(demonDecide((int16_t)d1)) {
	case DemonPlan::Lunge:                                         // LAB_0EDD: close: the lunge
		k.uwAction = raw(Action::Heavy);
		v.ulNextScript = e.scripts.s08B4;
		*e.pHopDir = k.ubFacing;
		*e.pHopTable = e.scripts.tHop;
		k.ubCooldown = DEMON_COOLDOWN_LUNGE;
		return finish(e);
	case DemonPlan::Strike8:                                       // LAB_0EDE
		k.uwAction = raw(Action::Strike8);
		f[0] |= DEMON_E9;
		v.ulNextScript = e.scripts.s08B5;
		k.ubCooldown = DEMON_COOLDOWN_STRIKE8;
		return demonSync(e, k);
	case DemonPlan::Strike4:                                       // LAB_0EDF
		k.uwAction = raw(Action::Strike4);
		v.ulNextScript = e.scripts.s08B6;
		f[0] |= DEMON_E5;
		*e.pHopDir = k.ubFacing;
		*e.pHopTable = e.scripts.tHop2;
		k.ubCooldown = DEMON_COOLDOWN_STRIKE4;
		break;
	case DemonPlan::Walk: break;
	}
	return demonWalk(e, k);                                        // LAB_0EE0 -> LAB_0ED5
}

// ---- LAB_0EFF: the computer's knight -----------------------------------------------------------------------------------

namespace {

const unsigned kAiSide = 0, kAiUp = 20, kAiDown = 36, kAiBytes = 52;     // SECSTRT_41, LAB_0F36, LAB_0F37, LAB_0F38

// One {dx, dy} word of a step table (4 bytes per phase); past the three tables the asm reads a variable and text: 0 here.
int16_t aiStep(const FighterEnv &e, unsigned uTab, uint8_t ubPhase, unsigned uWhich) {
	const unsigned off = uTab + (unsigned)ubPhase * 4u + uWhich * 2u;
	return off + 2 <= kAiBytes ? (int16_t)rd16(e.pAiSteps + off) : (int16_t)0;
}

// LAB_0F1A / LAB_0F1C with A1 = SECSTRT_41.
Dir aiSide(const FighterEnv &e, const Knight &k, bool bMirror) {
	MoveVars &m = *e.pMove;
	Dir d;
	d.d7 = dirSign(k);
	m.swDx = aiStep(e, kAiSide, k.ubAnimPhase, 0);
	m.swDy = aiStep(e, kAiSide, k.ubAnimPhase, 1);
	d.d6 = 0;
	if(bMirror) {
		m.swDx = (int16_t)-m.swDx;
	}
	if(m.swDyOverride != 0) {
		m.swDy = m.swDyOverride;
	}
	return d;
}

// LAB_0F1E (A1 = LAB_0F36) / LAB_0F1F (A1 = LAB_0F37).
Dir aiVert(const FighterEnv &e, const Knight &k, bool bUp) {
	MoveVars &m = *e.pMove;
	Dir d;
	d.d7 = 1;
	const unsigned tab = bUp ? kAiUp : kAiDown;
	m.swDx = aiStep(e, tab, k.ubAnimPhase, 0);
	m.swDy = aiStep(e, tab, k.ubAnimPhase, 1);
	if(bUp) {
		d.d6 = 32;
		m.swDy = (int16_t)-m.swDy;
		m.swDyOverride = (int16_t)0xFFFB;
	} else {
		m.swDyOverride = 5;
		d.d6 = 64;
	}
	return d;
}

// LAB_0F02: walk the way LAB_0F24 pointed (D6 / D7 come from the last helper that ran, D7 from LAB_0F24 when none did).
HandlerResult aiMove(const FighterEnv &e, Knight &k, uint16_t d7) {
	if((k.uwInput & 0xFF) == 0) {                                  // TST.B 63(A0)
		return finish(e);
	}
	k.ubBehaviourFlags &= 0x7F;
	Dir d;
	d.d6 = 0;
	d.d7 = d7;
	if(bit(k.uwInput, 3)) {
		d = aiVert(e, k, true);
	}
	if(bit(k.uwInput, 2)) {
		d = aiVert(e, k, false);
	}
	if(bit(k.uwInput, 0)) {
		d = aiSide(e, k, false);
	}
	if(bit(k.uwInput, 1)) {
		d = aiSide(e, k, true);
	}
	return moveApply(e, k, d);
}

// LAB_0F08: choose the next action against the opponent (d1 = the distance LAB_0F24 / LAB_02C2 left in D1).  The decision is
// rules/ai_fight_knight.cpp aiKnightDecide; the action's script is the knight's own action table at that action.
HandlerResult aiDecide(const FighterEnv &e, Knight &k, uint16_t d1, uint16_t d7) {
	FightVars &v = *e.pVars;
	const Knight &opp = *rec(*e.pTarget);                          // A2 = LAB_0634
	AiKnightFacts f;
	f.swDist = (int16_t)d1;
	bool left;
	f.swOppDist = (int16_t)xDelta(k, opp, left);
	f.bOppLeft = left;
	f.swOppHp = opp.swHp;
	f.ubOppFacing = opp.ubFacing;
	f.uwOppAction = opp.uwAction;
	f.ubFacing = k.ubFacing;
	f.ubFlags = k.ubBehaviourFlags;
	f.ubDaggers = k.ubDaggers;
	f.uwLastAction = *e.pAiLastAction;
	f.bGloated = v.uwLairFlag != 0;
	f.ubOdds = e.pAiOdds[(int16_t)*e.pDifficulty];                 // 0(A5,D2.W)
	const AiKnightPlan plan = aiKnightDecide(f, *e.pSeed);
	if(plan.bClearCooldown) {
		k.ubCooldown = 0;
	}
	Action act = Action::Idle;
	switch(plan.move) {
	case AiKnightMove::Walk: return aiMove(e, k, d7);
	case AiKnightMove::Stand: return finish(e);
	case AiKnightMove::Strike8: act = Action::Strike8; break;
	case AiKnightMove::Strike4: act = Action::Strike4; break;
	case AiKnightMove::Heavy: act = Action::Heavy; break;
	case AiKnightMove::Defend: act = Action::Defend; break;
	case AiKnightMove::Block: act = Action::Block; break;
	case AiKnightMove::Dagger: act = Action::Dagger; break;
	}
	k.uwAction = raw(act);
	v.ulNextScript = tab32(k.ulActionScripts, k.uwAction);
	return finish(e);
}

}  // namespace

HandlerResult fighterAiKnight(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	*e.pSelf = addr(&k);
	v.ulNextScript = k.ulIdleScript;
	k.uwInput = 0;
	*e.pTarget = addr(&k) == *e.pFirst ? e.pActive->ulOpponent : *e.pFirst;
	if(k.ulAttacker != 0) {                                        // LAB_0F14: hit: the knight rule of damage.cpp (parry, damage, or down)
		return knightHurtBy(e, k, *rec(k.ulAttacker), raw(ActorType::KnightFight));
	}
	if(k.ulHitTarget != 0) {                                       // LAB_0F17: it hit something
		const Knight &t = *rec(k.ulHitTarget);
		v.ulNextScript = aiKnightHitReply(t.ubType, k.uwAction, t.uwAction) == FightScript::Stop ? 0xFFFFFFFFu : k.ulScript26;
		return finish(e);
	}
	*e.pAiLastAction = k.uwAction;
	const Approach ap = approach(e);
	uint16_t d1 = ap.d1;
	if(ap.d0 != 0) {
		if(e.pMove->uwNear == 0) {
			return aiMove(e, k, ap.d7);
		}
		bool left;
		d1 = xDelta(k, *rec(*e.pTarget), left);
	}
	return aiDecide(e, k, d1, ap.d7);
}

}}  // namespace ms::game
