// game/rules/waves - see include/game/rules/waves.hpp.  Word arithmetic follows mog.asm LAB_0177..LAB_0187.
#include "game/rules/waves.hpp"

namespace ms { namespace game {

namespace {

// LAB_0185: the bite taken off the creature total, eight creature kinds (LAB_0187 order) by eight damage classes.
const int8_t kCut[64] = {
	2, 2, 1, 1, 0, -1, -1, -2, 5, 4, 3, 2, 0, -1, -2, -4,
	5, 4, 2, 0, 0, -1, -3, -4, 5, 4, 2, 0, 0, -1, -3, -4,
	3, 3, 2, 2, 0, -1, -2, -3, 3, 2, 1, 0, 0, -1, -1, -2,
	3, 2, 1, 0, 0, 0, 0, -1, 2, 1, 0, 0, -1, -1, -2, -3,
};

}  // namespace

void waveScale(WaveState &s, const WaveIn &in, const RulesDef &rules, WaveDamageFn pfnDamage, void *pCtx) {
	const bool bOriginal = rules.ubWaveScaling != WAVE_SCALING_NONE;           // the count grows (original, limited)
	const bool bAlive = rules.ubWaveScaling == WAVE_SCALING_ORIGINAL;          // and the number alive at once (original only)
	s.uwLevel = 0;
	if(bOriginal) {
		if(bAlive && in.sbStrength > 3) {
			s.uwMaxAlive = (uint16_t)(s.uwMaxAlive + 1);
		}
		if(in.swHpMax >= 0x1E) {
			s.uwTotal = (uint16_t)(s.uwTotal + 1);
			s.uwLevel = 1;
		}
		if(in.swHpMax >= 0x3C) {
			if(bAlive) {
				s.uwMaxAlive = (uint16_t)(s.uwMaxAlive + 1);
			}
			s.uwLevel = 2;
		}
		if(in.swHpMax >= 0x5A) {
			s.uwLevel = 3;
			s.uwTotal = (uint16_t)(s.uwTotal + 1);
		}
	}
	if(in.bLair) {
		s.uwTotal = in.uwLairCount;
	}
	if(in.bSingleAlive) {
		s.uwMaxAlive = 1;
	}
	if(in.bAliveCapTwo && (int16_t)s.uwMaxAlive > 2) {
		s.uwMaxAlive = 2;
	}
	if(bOriginal && (int16_t)s.uwTotal > 0) {                        // BLE LAB_0184
		const uint16_t d0 = pfnDamage(pCtx);                         // always asked, also for an unknown kind (it has side effects)
		if(in.ubRow < 8) {
			const int16_t cut = (int16_t)kCut[((uint16_t)in.ubRow << 3) + d0];
			const int32_t left = (int32_t)(int16_t)s.uwTotal - cut;
			if(left > 0) {                                           // SUB.W D0,D1 ; BLE: only a positive result is stored
				s.uwTotal = (uint16_t)left;
			}
		}
	}
	if(in.bCoop) {                                                   // two knights: more at once, more in all
		s.uwMaxAlive = (uint16_t)(s.uwMaxAlive + rules.ubCoopAliveBonus);
		s.uwTotal = (uint16_t)(s.uwTotal * rules.ubCoopTotalFactor);
	}
}

uint16_t waveWriteback(const RulesDef &rules, bool bCoop, uint16_t uwLeft) {
	if(!bCoop || rules.ubCoopWritebackDivisor <= 1) {
		return uwLeft;
	}
	const uint16_t d = rules.ubCoopWritebackDivisor;
	return (uint16_t)((uwLeft + d - 1) / d);
}

}}  // namespace ms::game
