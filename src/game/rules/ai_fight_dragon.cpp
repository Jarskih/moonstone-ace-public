// game/rules/ai_fight_dragon - see include/game/rules/ai_fight.hpp.  The dragon (mog LAB_027A) and its object (LAB_0298): a walk towards the
// first fighter, then either a melee / breath attack or a flight (a dagger-style arc), in two phases.  Flags: LAB_0623.
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

// LAB_0287..LAB_028B: hurt by a knight (contact damage) or a thrown dagger (3); anything else does not hurt it.
HitTaken dragonHitTaken(uint8_t ubAttackerType) {
	HitTaken h;
	h.bHurt = false;
	h.bContact = false;
	h.uwFixed = 0;
	if(ubAttackerType == raw(ActorType::KnightFight)) {
		h.bHurt = true;
		h.bContact = true;
	} else if(ubAttackerType == raw(ActorType::Dagger)) {
		h.bHurt = true;
		h.uwFixed = DAMAGE_THROWN_DAGGER;
	}
	return h;
}

// LAB_028E: x kept between 30 and 100 (a step that would reach the bound is not taken).
uint16_t dragonStepX(uint16_t uwX, uint16_t uwInput) {
	if(uwInput & 1u) {
		const uint16_t d1 = (uint16_t)(uwX + DRAGON_STEP);
		if(!((int16_t)d1 >= (int16_t)DRAGON_X_MAX)) {
			uwX = d1;
		}
	}
	if(uwInput & 2u) {
		const uint16_t d1 = (uint16_t)(uwX - DRAGON_STEP);
		if(!((int16_t)d1 <= (int16_t)DRAGON_X_MIN)) {
			uwX = d1;
		}
	}
	return uwX;
}

uint16_t dragonStepY(uint16_t uwY, uint16_t uwInput) {
	if(uwInput & 8u) {
		uwY = (uint16_t)(uwY - DRAGON_STEP);
	}
	if(uwInput & 4u) {
		uwY = (uint16_t)(uwY + DRAGON_STEP);
	}
	return uwY;
}

// LAB_027B / LAB_027C: near (below 140) in the first phase and far in the second pick an attack; the other combination starts a flight.
DragonThink dragonNextPhase(uint8_t ubFlags, int16_t swDist) {
	const bool bFar = swDist >= DRAGON_FAR;
	const bool bPick = bFar ? !(ubFlags & DRAGON_PHASE2) : (ubFlags & DRAGON_PHASE2) != 0;
	return bPick ? DragonThink::PickAttack : DragonThink::StartFlight;
}

// LAB_0283..LAB_0286
DragonAttack dragonPickAttack(uint8_t &ubFlags, int16_t swDist, int16_t swTargetHp, bool bDepthNear) {
	ubFlags &= (uint8_t)~DRAGON_REDUCED;
	if(swTargetHp <= 0 || !bDepthNear) {
		return DragonAttack::None;
	}
	if(!(ubFlags & DRAGON_PHASE2)) {                                        // LAB_0286: the first phase strikes
		return DragonAttack::Strike8;
	}
	if(!(swDist > DRAGON_BREATH_RANGE) && !(ubFlags & DRAGON_HIT)) {        // LAB_0285: second phase, close and not just hit: the strike
		ubFlags &= (uint8_t)~DRAGON_HIT;
		return DragonAttack::Strike4;
	}
	ubFlags &= (uint8_t)~DRAGON_HIT;                                        // far, or just hit: the breath (the next walk step is short)
	ubFlags |= DRAGON_REDUCED;
	return DragonAttack::Breath;
}

// LAB_027B (near, first phase) / LAB_027C (far, second phase)
DragonFlightPlan dragonFlightPlan(bool bFar) {
	DragonFlightPlan p;
	p.ubFlagsSet = bFar ? (uint8_t)DRAGON_FLIGHT : (uint8_t)(DRAGON_FLIGHT | DRAGON_PHASE2);
	p.ubFlagsClear = bFar ? (uint8_t)DRAGON_PHASE2 : (uint8_t)0;
	p.uwWalkOffset = bFar ? (uint16_t)64 : (uint16_t)32;
	p.uwSteps = bFar ? (uint16_t)9 : (uint16_t)0x0D;
	p.uwArc = bFar ? (uint16_t)0x78 : (uint16_t)0x50;
	p.uwTargetX = 0x64;
	p.uwTargetHeight = bFar ? (uint16_t)0xFFE2 : (uint16_t)0xFFBA;
	return p;
}

uint16_t dragonFlightWalkOffset(uint8_t ubFlags) {
	return (ubFlags & DRAGON_PHASE2) ? (uint16_t)32 : (uint16_t)64;
}

// LAB_0299
bool dragonPartStrikes(int16_t swTargetHp, bool bDepthNear, int16_t swTargetX) {
	return swTargetHp > 0 && bDepthNear && swTargetX <= 0x64;
}

// LAB_029C: it hit anything but the dragon: it hops.
bool dragonPartHopsOnHit(uint8_t ubTargetType) {
	return ubTargetType != raw(ActorType::Dragon);
}

}}  // namespace ms::game
