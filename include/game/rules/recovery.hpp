// Optional Dexterity mod (ROADMAP 8.8a). One owner for the bounded recovery-hold rule.
#pragma once
#include "game/api/data.hpp"
#include "game/knight.hpp"

namespace ms { namespace game {

inline uint8_t dexterityRecoveryRepeats(const Knight &k, uint8_t repeats, const RulesDef &rules) {
	if(!rules.ubDexterityEnabled || k.ubDexterity <= 1 || repeats <= 1) return repeats;
	const uint8_t dex = k.ubDexterity > 5 ? 5 : k.ubDexterity;
	const uint16_t reduction = (uint16_t)(dex - 1) * rules.ubRecoveryFramesPerPoint;
	return reduction >= repeats ? 1 : (uint8_t)(repeats - reduction);
}

// Only a living knight's own melee-action tail receives the bonus (not hurt, defence, dagger or monster scripts).
uint8_t knightRecoveryRepeats(const Knight &k, bool ownRecoveryHold, uint8_t repeats, const RulesDef &rules);

}} // namespace ms::game
