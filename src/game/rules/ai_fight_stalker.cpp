// game/rules/ai_fight_stalker - see include/game/rules/ai_fight.hpp.  The Troll "stalker" (mog LAB_0EC2): follows the first fighter and strikes
// when in reach.
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

namespace {

// LAB_0ECE+2: {dx, dy} per walk phase (4 bytes each).  Phases 4..7 read the code-hunk bytes that follow the table (LAB_0ECF: the sound id
// list of LAB_0ED0 and an SBCD opcode), which is what the asm reads too.
const uint16_t kStalkStep[16] = {0x0010, 0x0000, 0x001A, 0x0000, 0x000D, 0x0000, 0x001A, 0x0000,
                                 0x0000, 0x8889, 0x8A8B, 0x8C8D, 0x8E89, 0x8C8D, 0x8B8C, 0x8900};

}  // namespace

// LAB_0EC8..LAB_0ECA
StalkerPlan stalkerDecide(int16_t swDist, uint16_t uwLastAction) {
	bool bPlain = swDist < STALKER_STRIKE_NEAR || !(swDist < STALKER_STRIKE_FAR);
	if(!bPlain && uwLastAction == raw(Action::Heavy)) {                     // LAB_0ECA
		bPlain = true;
	}
	return bPlain ? StalkerPlan::Strike8 : StalkerPlan::Heavy;              // LAB_0EC9 : the heavy swing
}

// LAB_0EC6 / LAB_0EC7: MOVE.B 12(A0),D0 ; LSL.W #2,D0 (phases are 0..7)
int16_t stalkerStepX(uint8_t ubPhase) {
	return (int16_t)kStalkStep[(ubPhase & 7) * 2];
}

int16_t stalkerStepDy(uint8_t ubPhase) {
	return (int16_t)kStalkStep[(ubPhase & 7) * 2 + 1];
}

}}  // namespace ms::game
