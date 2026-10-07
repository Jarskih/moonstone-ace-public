// game/rules/ai_fight_knight - see include/game/rules/ai_fight.hpp.  The computer's knight (mog LAB_0EFF): the choice between guarding and
// attacking, by the distance to the opponent, its last action and the odds of the difficulty.
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

namespace {

AiKnightPlan plan(AiKnightMove move, bool bClearCooldown) {
	AiKnightPlan p;
	p.move = move;
	p.bClearCooldown = bClearCooldown;
	return p;
}

}  // namespace

// LAB_0F08 (mog.asm).  The order of the tests and of the random draws is the original's.
AiKnightPlan aiKnightDecide(const AiKnightFacts &f, uint32_t &ulSeed) {
	int16_t swDist = f.swDist;
	if(!(f.swOppHp > 0)) {                                                  // the opponent is down: finish it or walk up
		if(swDist > AIK_STRIKE8_RANGE) {
			return plan(AiKnightMove::Walk, true);
		}
		return plan(f.bGloated ? AiKnightMove::Stand : AiKnightMove::Strike8, true);
	}
	if(!(f.ubFlags & 0x80)) {                                               // LAB_0F0A: try to block
		const uint32_t roll = rngDrawPercentSeed(ulSeed);                   // JSR LAB_04A3
		if(!((int8_t)roll < (int8_t)f.ubOdds) && f.ubOppFacing != f.ubFacing) {
			if(f.uwOppAction == raw(Action::Strike8)) {
				if(!(f.swOppDist > AIK_GUARD_RANGE)) {
					return plan(AiKnightMove::Defend, false);
				}
				swDist = f.bOppLeft ? 1 : 0;                                // QUIRK: LAB_02C2 leaves D1 = its 'negative' flag
			} else if(f.uwOppAction == raw(Action::Heavy) || f.uwOppAction == raw(Action::Strike4)) {   // LAB_0F0B / LAB_0F0C
				if(!(f.swOppDist > AIK_GUARD_RANGE)) {
					return plan(AiKnightMove::Block, false);
				}
				swDist = f.bOppLeft ? 1 : 0;                                // (the attack choice below then sees 0 / 1, not the distance)
			}
		}
	}
	const uint32_t roll = rngDrawPercentSeed(ulSeed);                       // LAB_0F0D
	if(!((int8_t)roll > (int8_t)f.ubOdds)) {                                // the " Stupid " taunt (a no-op here): a wasted draw
		rngDrawSeed(ulSeed);
		return plan(AiKnightMove::Walk, false);
	}
	const uint16_t uwLast = f.uwLastAction;
	if(!(swDist > AIK_STRIKE8_RANGE) && uwLast != raw(Action::Strike8)) {   // LAB_0F0E
		return plan(AiKnightMove::Strike8, false);
	}
	if(!(swDist > AIK_HEAVY_RANGE) && uwLast != raw(Action::Heavy)) {       // LAB_0F0F
		return plan(AiKnightMove::Heavy, false);
	}
	if(!(swDist > AIK_STRIKE4_RANGE) && (f.ubDaggers == 0 || uwLast != raw(Action::Strike4))) {   // LAB_0F10
		return plan(AiKnightMove::Strike4, false);
	}
	if(f.ubDaggers != 0) {                                                  // LAB_0F12
		return plan(AiKnightMove::Dagger, false);
	}
	return plan(AiKnightMove::Walk, false);                                 // LAB_0F13
}

// LAB_0F17
FightScript aiKnightHitReply(uint8_t ubTargetType, uint16_t uwOwnAction, uint16_t uwTargetAction) {
	const bool bDefends = ubTargetType == raw(ActorType::KnightFight) || uwOwnAction == raw(Action::Heavy) || uwOwnAction == raw(Action::Strike4);
	return (bDefends && uwTargetAction == raw(Action::Block)) ? FightScript::Stop : FightScript::Alternate;
}

}}  // namespace ms::game
