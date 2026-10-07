// game/rules/ai_fight_snatcher - see include/game/rules/ai_fight.hpp.  The Mudmen "snatcher" (mog SECSTRT_40 / LAB_0E98..LAB_0EB6): appears
// beside the first fighter, walks up, grabs and carries it.  A state machine over the flags LAB_0EB6.
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

namespace {

const uint16_t kSnatchStep[8] = {12, 12, 10, 14, 12, 12, 10, 14};   // LAB_0EB6+2: x step per walk phase

}  // namespace

// LAB_0EAC.. : the tests in the original's order (gone, carrying, freed, released).
SnatchState snatcherState(uint8_t ubFlags) {
	if(ubFlags & SNATCH_GONE) return SnatchState::Gone;
	if(ubFlags & SNATCH_CARRY) return SnatchState::Carrying;
	if(ubFlags & SNATCH_FREE) return SnatchState::Freed;
	if(ubFlags & SNATCH_RELEASE) return SnatchState::Released;
	return SnatchState::Approach;
}

// LAB_0E98 (walk when not beyond 0x32), LAB_0EA9 (strike from a distance below 0x4B), LAB_0EA5 (a reach below 0x64 needs the depth).
SnatchPlan snatcherApproach(int16_t swDist, bool bDepthNear) {
	if(!(swDist > SNATCH_WALK_RANGE)) {
		return SnatchPlan::Walk;
	}
	if(swDist < SNATCH_STRIKE_RANGE) {
		return SnatchPlan::StrikeFar;
	}
	if(swDist < SNATCH_REACH_RANGE) {
		return bDepthNear ? SnatchPlan::Reach : SnatchPlan::Walk;
	}
	return SnatchPlan::Walk;
}

// LAB_0EAA: the grab window 0x14..0x50.
bool snatcherCanGrab(int16_t swDist) {
	return !(swDist < SNATCH_GRAB_MIN) && !(swDist > SNATCH_GRAB_MAX);
}

// LAB_0EA6: beside the target, on the side with more room (x below 160: to its right, facing left).
SnatchAppear snatcherAppear(uint16_t uwTargetX) {
	SnatchAppear a;
	a.swOffsetX = SNATCH_APPEAR_OFFSET;
	a.ubFacing = 3;
	if(!((int16_t)uwTargetX < SNATCH_APPEAR_SIDE_X)) {
		a.ubFacing = 1;
		a.swOffsetX = (int16_t)-SNATCH_APPEAR_OFFSET;
	}
	return a;
}

int16_t snatcherStepX(uint8_t ubPhase) {
	return (int16_t)kSnatchStep[ubPhase & 7];
}

}}  // namespace ms::game
