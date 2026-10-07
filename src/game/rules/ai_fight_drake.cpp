// game/rules/ai_fight_drake.cpp - see include/game/rules/ai_fight.hpp.  Balok, the "drake" (mog LAB_029F): pounces in a dagger-style arc,
// grabs, flings.  Flags: LAB_0627.
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

// LAB_029F..LAB_02A6: by the x distance when the target is within depth reach.  The JumpClose case also clears the second flag byte.
DrakeMelee drakeMelee(int16_t swDist, uint16_t uwAction, uint8_t ubFirstDaggers) {
	if(swDist <= DRAKE_CLOSE) {
		return DrakeMelee::JumpClose;
	}
	if(swDist <= DRAKE_HOP) {                                               // LAB_02A2
		return uwAction != raw(Action::Strike8) ? DrakeMelee::HopStrike : DrakeMelee::Heavy;
	}
	if(swDist <= DRAKE_HEAVY) {                                             // LAB_02A3
		return DrakeMelee::Heavy;
	}
	if(ubFirstDaggers == 0 && swDist <= DRAKE_WAIT) {                       // LAB_02A5: the first fighter has no daggers: let it come
		return DrakeMelee::Stand;
	}
	return DrakeMelee::Jump;
}

// LAB_02A6: a jump of 3 steps or fewer is not worth it.
bool drakeCanJump(uint16_t uwAimSteps) {
	return !((int16_t)uwAimSteps <= (int16_t)DRAKE_MIN_STEPS);
}

// LAB_02A7: at most 20 steps.
uint16_t drakeJumpSteps(uint16_t uwAimSteps) {
	return (int16_t)uwAimSteps > (int16_t)DRAKE_MAX_STEPS ? (uint16_t)DRAKE_MAX_STEPS : uwAimSteps;
}

// LAB_02A9
bool drakeLandsOnTarget(uint16_t uwAimSteps, uint16_t uwStepsLeft, int16_t swHeight, int16_t swDist) {
	const uint16_t uwHalf = (uint16_t)(uwAimSteps >> 1);
	if((int16_t)uwHalf < (int16_t)uwStepsLeft) {                            // not yet past half way
		return false;
	}
	if(swHeight < (int16_t)0xFFD8) {                                        // still too high
		return false;
	}
	return !(swDist > DRAKE_LAND_DIST);
}

// LAB_02AA..LAB_02B0
bool drakeLandsHard(int16_t swAimArc) {
	return !(swAimArc < 8);
}

// LAB_02B2..LAB_02B4
DrakeHit drakeHitReply(uint16_t uwAction) {
	if(uwAction == raw(Action::Heavy)) {
		return DrakeHit::Grab;
	}
	if(uwAction == raw(Action::Strike8)) {
		return DrakeHit::Strike;
	}
	return DrakeHit::Alternate;
}

}}  // namespace ms::game
