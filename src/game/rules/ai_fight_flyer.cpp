// game/rules/ai_fight_flyer - see include/game/rules/ai_fight.hpp.  The Be flyer (mog LAB_0226): crosses the arena at the target's depth.
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

namespace {

// LAB_0234+2: x steps of the flight cycle.
const uint16_t kFlyStep[4] = {0x0021, 0x001B, 0x0011, 0x0021};

}  // namespace

// LAB_0230: only a non-flyer hurts it (type byte != 0), by its contact damage.
HitTaken flyerHitTaken(uint8_t ubAttackerType) {
	HitTaken h;
	h.bHurt = ubAttackerType != raw(ActorType::Be);
	h.bContact = true;
	h.uwFixed = 0;
	return h;
}

// LAB_0231: its hit only counts on a non-flyer.
bool flyerHitsTarget(uint8_t ubTargetType) {
	return ubTargetType != raw(ActorType::Be);
}

// LAB_0231: TST.L 14(A2) ; TST.W 80(A2) / BGT ; TST.L LAB_06DA
bool flyerFinishesTarget(bool bTargetHitting, int16_t swTargetHp, bool bDemo) {
	return !bTargetHitting && swTargetHp <= 0 && !bDemo;
}

// LAB_0228..LAB_0229
FlyerTurn flyerEdgeTurn(uint8_t ubFacing, uint16_t uwX) {
	FlyerTurn t;
	t.bTurned = false;
	t.uwX = uwX;
	t.ubFacing = ubFacing;
	if(ubFacing == 3) {
		if((int16_t)uwX < FLYER_LEFT_EDGE) {                       // off the left edge: come back from the right
			t.bTurned = true;
			t.uwX = FLYER_ENTER_LEFT;
			t.ubFacing = 1;
		}
	} else if((int16_t)uwX >= FLYER_RIGHT_EDGE) {                  // off the right edge
		t.bTurned = true;
		t.uwX = FLYER_ENTER_RIGHT;
		t.ubFacing = 3;
	}
	return t;
}

// LAB_022B: wobble by the beam position (the low three bits times four)
uint16_t flyerLaneY(bool bWobble, uint16_t uwTargetY, uint16_t uwBeam) {
	return bWobble ? (uint16_t)(uwTargetY + (uint16_t)((uwBeam & 7u) << 2)) : uwTargetY;
}

// LAB_022C: (random & 15) | 5, drawn once
uint8_t flyerPauseFrames(uint32_t &ulSeed) {
	return (uint8_t)((uint16_t)((uint16_t)rngDrawSeed(ulSeed) & 0x0F) | 5);
}

// LAB_022E: the step table has four entries; a phase the asm could only reach with a garbage record reads 0.
uint16_t flyerStepX(uint8_t ubPhase, uint8_t ubFacing) {
	uint16_t d1 = ubPhase < 4 ? kFlyStep[ubPhase] : 0;
	if((ubFacing >> 1) & 1u) {
		d1 = (uint16_t)(0u - d1);
	}
	return d1;
}

}}  // namespace ms::game
