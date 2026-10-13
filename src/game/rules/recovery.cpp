#include "game/rules/recovery.hpp"

namespace ms { namespace game {

uint8_t knightRecoveryRepeats(const Knight &k, bool ownRecoveryHold, uint8_t repeats, const RulesDef &rules) {
	if(!rules.ubDexterityEnabled || k.ubDexterity <= 1 || repeats <= 1 || k.swHp <= 0 ||
	   !ownRecoveryHold ||
	   (k.ubType != raw(ActorType::KnightFight) && k.ubType != raw(ActorType::KnightMap))) return repeats;
	switch(k.uwAction) {
	case raw(Action::Strike4): case raw(Action::Strike8): case raw(Action::Strike20):
	case raw(Action::Strike24): case raw(Action::Heavy): break;
	default: return repeats;
	}
	return dexterityRecoveryRepeats(k, repeats, rules);
}

}} // namespace ms::game
