// game/rules/ai_fight_demon - see include/game/rules/ai_fight.hpp.  The Guardian demon (mog LAB_0ED2): lunge, strike or jump by distance,
// then a grab; a flag state machine (LAB_0EEA).
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

// LAB_0EE1..LAB_0EE5: the tests in the original's order (E1, E3, E9, E7, E5).
DemonState demonState(uint8_t ubFlags) {
	if(ubFlags & DEMON_E1) return DemonState::StageE1;
	if(ubFlags & DEMON_E3) return DemonState::StageE3;
	if(ubFlags & DEMON_E9) return DemonState::StageE9;
	if(ubFlags & DEMON_E7) return DemonState::StageE7;
	if(ubFlags & DEMON_E5) return DemonState::StageE5;
	return DemonState::Think;
}

// LAB_0EDD..LAB_0EDF
DemonPlan demonDecide(int16_t swDist) {
	if(!(swDist > DEMON_LUNGE_RANGE)) return DemonPlan::Lunge;
	if(!(swDist > DEMON_STRIKE8_RANGE)) return DemonPlan::Strike8;
	if(!(swDist > DEMON_STRIKE4_RANGE)) return DemonPlan::Strike4;
	return DemonPlan::Walk;
}

// LAB_0EE7: !(d0 > $96) && !(d0 < $82)
bool demonGrabsAtE7(int16_t swDist) {
	return !(swDist > 0x96) && !(swDist < 0x82);
}

// LAB_0EE5: !(d0 > $8C) && !(d0 < $78)
bool demonGrabsAtE5(int16_t swDist) {
	return !(swDist > 0x8C) && !(swDist < 0x78);
}

// LAB_0EF2: !(d0 > $3C)
bool demonSnapsBack(int16_t swDist) {
	return !(swDist > 0x3C);
}

int16_t demonSnapBackOffset(uint8_t ubFacing) {
	return ubFacing != 3 ? (int16_t)-0x3C : (int16_t)0x3C;
}

}}  // namespace ms::game
