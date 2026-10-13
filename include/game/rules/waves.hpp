// game/rules/waves - how big a creature fight is (mog LAB_0177 scale routine, ROADMAP 9.5a).  Pure: the caller (src/rt/arena.cpp
// scaleWave) gathers the facts, the rules come from `[waves]` / `[coop]` of mods/rules.ini (RulesDef).  The default path
// (`scaling = original`, co-op off) is word-for-word the original arithmetic.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"

namespace ms { namespace game {

// The fight's three numbers that the scaling changes.
struct WaveState {
	uint16_t uwMaxAlive;       // LAB_05ED  most creatures alive at once
	uint16_t uwTotal;          // LAB_05EC  creatures in all
	uint16_t uwLevel;          // wave level 0..3 (mogFightWaveLevel: the knight's HP band)
};

// What the scaling looks at.
struct WaveIn {
	int8_t sbStrength;         // the fighter's strength (signed byte)
	int16_t swHpMax;           // the fighter's maximum HP
	bool bLair;                // a lair fight: the lair's own count replaces the total
	uint16_t uwLairCount;      // the lair's creature count (LAB_08C6) when bLair
	bool bSingleAlive;         // the spawn routine allows one creature at a time (LAB_0197 / LAB_019B)
	bool bAliveCapTwo;         // the creature initialiser caps the alive count at two (LAB_019F)
	uint8_t ubRow;             // the creature's row in the cut table (0..7), 8 = not one of the known kinds
	bool bCoop;                // two or more knights play together ([coop] enabled, or a party fight)
	uint8_t ubParty = 2;       // Moonstone 2 (ROADMAP 8.4b): knights on the party's side (2..4); the [coop] numbers are for two and grow per extra knight
};

// The fighter's damage class 0..7 (the column of the cut table); called only when the original would compute it.
typedef uint16_t (*WaveDamageFn)(void *pCtx);

// LAB_0177: scales the fight.  `pfnDamage` is only called on the `original` path, when the total is still positive.
void waveScale(WaveState &s, const WaveIn &in, const RulesDef &rules, WaveDamageFn pfnDamage, void *pCtx);

// The value written back to the lair after a fight with `uwLeft` creatures still to come.  Single knight: unchanged; co-op:
// divided by `[coop] writeback_divisor`, rounded up.
uint16_t waveWriteback(const RulesDef &rules, bool bCoop, uint16_t uwLeft, uint8_t ubParty = 2);

// The [coop] numbers are given for TWO knights; every knight beyond the second adds the same step again: alive at once +alive_bonus per
// extra knight (2: +1, 3: +2, 4: +3), the total * (1 + (total_factor - 1) * (n - 1)) (2: x2, 3: x3, 4: x4), the write-back divisor likewise.
uint16_t coopAliveBonus(const RulesDef &rules, uint8_t ubParty);
uint16_t coopTotalFactor(const RulesDef &rules, uint8_t ubParty);
uint16_t coopWritebackDivisor(const RulesDef &rules, uint8_t ubParty);

}}  // namespace ms::game
