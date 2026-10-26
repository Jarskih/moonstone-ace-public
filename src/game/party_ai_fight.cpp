// game/party_ai_fight - see include/game/party_ai_fight.hpp.  New rules (ROADMAP 8.3b), no original label.
#include "game/party_ai_fight.hpp"

#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

namespace {

uint16_t absDiff(uint16_t a, uint16_t b) { return a > b ? static_cast<uint16_t>(a - b) : static_cast<uint16_t>(b - a); }

uint16_t distance(const Knight &a, const Knight &b) { return static_cast<uint16_t>(absDiff(a.uwX, b.uwX) + absDiff(a.uwY, b.uwY)); }

// A record is a candidate when it is a creature or a black knight that still stands.  (Foe knights are KnightMap / KnightFight records, which
// have an AI row too, so the type test only rules out the helpers: daggers, effect objects, body parts.)
bool candidate(const Knight &k) {
	return k.swHp > 0 && (partyAiIsFoeType(k.ubType) || k.ubType == raw(ActorType::KnightMap) || k.ubType == raw(ActorType::KnightFight));
}

}  // namespace

bool partyAiIsFoeType(uint8_t ubType) {
	const FightAi *pAi = fightAiByType(ubType);
	if(pAi == 0) {
		return false;
	}
	switch(pAi->kind) {
	case AiKind::Dagger:
	case AiKind::Idle:
	case AiKind::DragonPart:
	case AiKind::Knight:
	case AiKind::AiKnight:
		return false;
	default:
		return true;
	}
}

uint8_t partyAiOdds(uint8_t ubTableOdds) {
	return ubTableOdds < PARTY_AI_ODDS_CAP ? ubTableOdds : PARTY_AI_ODDS_CAP;
}

Knight *partyAiPickFoe(
const Knight &self, const Knight *pHeld, Knight *const *apFoe, uint8_t ubCount) {
	Knight *pBest = 0;
	uint16_t uwBest = 0xFFFF;
	for(uint8_t i = 0; i < ubCount; ++i) {
		if(apFoe[i] == 0 || !candidate(*apFoe[i])) {
			continue;
		}
		const uint16_t d = distance(self, *apFoe[i]);
		if(pBest == 0 || d < uwBest) {
			pBest = apFoe[i];
			uwBest = d;
		}
	}
	if(pBest != 0 && pHeld != 0 && pHeld != pBest && candidate(*pHeld) && distance(self, *pHeld) <= static_cast<uint16_t>(uwBest + PARTY_AI_STICKY)) {
		for(uint8_t i = 0; i < ubCount; ++i) {
			if(apFoe[i] == pHeld) {
				return apFoe[i];          // (only a record that is on the list stays the target)
			}
		}
	}
	return pBest;
}

}}  // namespace ms::game
