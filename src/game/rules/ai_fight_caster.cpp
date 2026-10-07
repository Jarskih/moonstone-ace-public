// game/rules/ai_fight_caster - see include/game/rules/ai_fight.hpp.  The Ratmen "caster" (mog LAB_0251): keeps its distance, throws a dagger,
// grabs the first fighter with it and drops or slams it.  A state machine over the record flags +104 / +105 and the shared flags LAB_062B.
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

// LAB_0252: the original tests the flags in this order; the first that is set wins.
CasterState casterState(uint8_t ubFlags, uint8_t ubFlags2) {
	if(ubFlags & CASTER_QUICK) return CasterState::Flight;
	if(ubFlags & CASTER_DAGGER_OUT) return CasterState::Flight;
	if(ubFlags & CASTER_GRABBED) return CasterState::Grabbed;
	if(ubFlags2 & CASTER2_RELEASE) return CasterState::Release;
	if(ubFlags & CASTER_THROW) return CasterState::Throw;
	if(ubFlags & CASTER_HOLD) return CasterState::Hold;
	if(ubFlags2 & CASTER2_SLAM) return CasterState::Slam;
	return CasterState::Think;
}

// LAB_0252..LAB_0255: within depth reach: wait while the first fighter is grabbed or slammed, while it is down and between actions; strike
// at 40 (action 8) or 50 (action 4); further off, or out of depth reach, start a throw.
CasterMelee casterMelee(bool bDepthNear, uint8_t ubSharedFlags, int16_t swTargetHp, uint16_t uwWait, int16_t swDist) {
	if(bDepthNear) {
		if((ubSharedFlags & CASTER_SHARED_GRABBED) || (ubSharedFlags & CASTER_SHARED_SLAM)) {
			return CasterMelee::Stand;
		}
		if(swTargetHp <= 0) {
			return CasterMelee::Stand;
		}
		if(uwWait != 0) {
			return CasterMelee::Wait;
		}
		if(swDist <= CASTER_STRIKE8_RANGE) {
			return CasterMelee::Strike8;
		}
		if(swDist <= CASTER_STRIKE4_RANGE) {                                // LAB_0254
			return CasterMelee::Strike4;
		}
	}
	return CasterMelee::StartThrow;
}

// LAB_0256: a target within 40 gets the quick underhand throw.
bool casterThrowsClose(int16_t swAimDist) {
	return swAimDist <= CASTER_CLOSE_THROW;
}

// LAB_0256: half the aimed steps, at least 8.
uint16_t casterThrowSteps(uint16_t uwAimSteps) {
	uint16_t d0 = (uint16_t)(uwAimSteps >> 1);
	if((int16_t)d0 < 8) {
		d0 = 8;
	}
	return d0;
}

// LAB_025E: below 60 the near script (LAB_0864), else the far one (LAB_0863).
bool casterTargetNear(int16_t swDist) {
	return swDist < CASTER_HOLD_NEAR;
}

// LAB_026B..LAB_0270
CasterHurt casterHurtBy(uint8_t ubAttackerType, uint8_t ubFlags, uint16_t uwAttackerAction) {
	if(ubAttackerType != raw(ActorType::KnightFight)) {
		return CasterHurt::Think;
	}
	if(!(ubFlags & CASTER_DAGGER_OUT)) {                                    // LAB_026C
		if(uwAttackerAction == raw(Action::Defend) || uwAttackerAction == raw(Action::Block)) {   // LAB_026D: finished off
			return CasterHurt::FinishedOff;
		}
		return CasterHurt::Wound;                                           // LAB_026E
	}
	if(uwAttackerAction == raw(Action::Strike24)) {                         // LAB_026F / LAB_0270
		return CasterHurt::Killed;
	}
	return CasterHurt::InFlight;
}

// LAB_0273..LAB_0279.  QUIRK: the Ratmen type is tested first, so the "t.type == Ratmen" in the dagger-out test below can never be true.
CasterHit casterHitReply(uint8_t ubTargetType, uint8_t ubFlags, uint8_t ubSharedFlags) {
	if(ubTargetType == raw(ActorType::Ratmen)) {
		return CasterHit::Think;
	}
	if(ubFlags & CASTER_DAGGER_OUT) {                                       // LAB_0278
		if(ubTargetType == raw(ActorType::Ratmen) || (ubSharedFlags & CASTER_SHARED_GRABBED)) {
			return CasterHit::Flight;
		}
		return CasterHit::Grab;
	}
	if(ubFlags & CASTER_HOLD) {                                             // LAB_0279
		return CasterHit::SlamStart;
	}
	return CasterHit::Counter;
}

}}  // namespace ms::game
