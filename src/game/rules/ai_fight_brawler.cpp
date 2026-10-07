// game/rules/ai_fight_brawler - see include/game/rules/ai_fight.hpp.  The Troggs (mog LAB_0236): walk up to the target and pick one of three
// attacks by the distance; the spear type thrusts instead.  After a kill it gloats once.
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

// LAB_0246..LAB_024A
HitTaken brawlerHitTaken(uint8_t ubAttackerType) {
	HitTaken h;
	h.bHurt = false;
	h.bContact = false;
	h.uwFixed = 0;
	if(ubAttackerType == raw(ActorType::KnightFight) || ubAttackerType == raw(ActorType::TroggSpear)) {
		h.bHurt = true;
		h.bContact = true;
	} else if(ubAttackerType == raw(ActorType::Dagger)) {
		h.bHurt = true;
		h.uwFixed = DAMAGE_THROWN_DAGGER;
	}
	return h;
}

// LAB_023E..LAB_0245 (mog.asm).  The decision tree, in the original's order:
//   too close                          -> walk (it backs off)
//   target down:  demo                 -> stand;  beyond 100 -> walk;  gloated already -> stand;  cool-down -> count down;  else gloat once
//   cool-down running                  -> count down
//   spear: beyond its reach            -> walk;  else thrust
//   within 100: one percent draw: above 30 gloats (strike, action 8); otherwise it gloats unless the first fighter is defending
//   not gloating: beyond 120           -> walk;  else the heavy swing (action $20)
BrawlerPlan brawlerDecide(const BrawlerFacts &f, uint32_t &ulSeed) {
	if(f.swDist <= f.swTooClose) {
		return BrawlerPlan::Walk;
	}
	if(f.swTargetHp <= 0) {                                                 // LAB_023F: the target is down
		if(f.bDemo) {
			return BrawlerPlan::Stand;
		}
		if(f.swDist > BRAWLER_CLOSE_RANGE) {
			return BrawlerPlan::Walk;
		}
		if(f.bGloated) {
			return BrawlerPlan::Stand;
		}
		if(f.ubCooldown != 0) {
			return BrawlerPlan::CountDown;
		}
		return BrawlerPlan::GloatOnBody;
	}
	if(f.ubCooldown != 0) {                                                 // LAB_0241
		return BrawlerPlan::CountDown;
	}
	if(f.bSpear) {                                                          // LAB_0242
		return f.swDist > f.swReach ? BrawlerPlan::Walk : BrawlerPlan::Thrust;
	}
	bool bGloat = false;
	if(f.swDist <= BRAWLER_CLOSE_RANGE) {                                   // LAB_0243
		const uint32_t roll = rngDrawPercentSeed(ulSeed);
		if((int16_t)roll > BRAWLER_GLOAT_ODDS) {
			bGloat = true;
		} else if(!f.bFirstDefends) {
			bGloat = true;
		}
	}
	if(!bGloat) {                                                           // LAB_0245
		return f.swDist > BRAWLER_HEAVY_RANGE ? BrawlerPlan::Walk : BrawlerPlan::Heavy;
	}
	return BrawlerPlan::Strike;                                             // LAB_0244
}

Action brawlerActionOf(BrawlerPlan plan) {
	switch(plan) {
	case BrawlerPlan::Thrust: return Action::Strike4;
	case BrawlerPlan::GloatOnBody:
	case BrawlerPlan::Strike: return Action::Strike8;
	case BrawlerPlan::Heavy: return Action::Heavy;
	default: return Action::Idle;
	}
}

uint8_t brawlerCooldownOf(BrawlerPlan plan) {
	switch(plan) {
	case BrawlerPlan::Thrust: return BRAWLER_COOLDOWN_THRUST;
	case BrawlerPlan::GloatOnBody:
	case BrawlerPlan::Strike: return BRAWLER_COOLDOWN_STRIKE;
	case BrawlerPlan::Heavy: return BRAWLER_COOLDOWN_HEAVY;
	default: return 0;
	}
}

// LAB_024B
BrawlerAfterHit brawlerAfterHit(bool bTargetIsKnight, bool bTargetHitting, int16_t swTargetHp, bool bSpear, bool bDemo) {
	if(!bTargetIsKnight) {
		return BrawlerAfterHit::Think;
	}
	if(!bTargetHitting && swTargetHp <= 0) {                                // the target is out: the spear finishes it, else stop
		if(bSpear && !bDemo) {
			return BrawlerAfterHit::FinishOff;
		}
		return bSpear ? BrawlerAfterHit::Idle : BrawlerAfterHit::Stop;
	}
	return bSpear ? BrawlerAfterHit::Idle : BrawlerAfterHit::Alternate;     // LAB_024C
}

}}  // namespace ms::game
