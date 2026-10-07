// game/fighters_int - the helpers of src/game/fighters.cpp that src/game/fight_creatures.cpp and src/game/fight_ops.cpp share
// (ROADMAP 7.1h).  Internal: only the three fight*.cpp files include it.  Everything is in ms::game::fi so that it cannot
// be mistaken for the public interface of fighters.hpp; the functions are thin exports of the file-local ones in fighters.cpp.
#pragma once
#include "game/fighters.hpp"

namespace ms { namespace game {

// The registers D6 / D7 the movement helpers leave for LAB_0F32 / LAB_0F20.  (In ms::game, not fi: the argument-dependent
// lookup of the file-local helpers in fighters.cpp must not find the exports of fi.)
struct Dir {
	int32_t d6;
	int32_t d7;
};

// LAB_0F24 result: D0 and the word of D1 the callers read, and D7 (the tunable the last probe used).
struct Approach {
	uint16_t d0;
	uint16_t d1;
	uint16_t d7;
};

namespace fi {

inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }
inline void wr32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}
inline Knight *rec(uint32_t a) { return jobPtr<Knight>(a); }
inline uint32_t addr(const void *p) { return jobAddr(p); }
inline bool bit(uint32_t v, unsigned n) { return ((v >> n) & 1u) != 0; }
inline void subHp(Knight &k, uint16_t d) { k.swHp = (int16_t)((uint16_t)k.swHp - d); }
// MOVE.L 0(A,D.W),X: a long at a base address plus the sign-extended word index.
inline uint32_t tab32(uint32_t ulBase, uint16_t uwIndex) {
	return rd32(jobPtr<const uint8_t>(ulBase + (uint32_t)(int32_t)(int16_t)uwIndex));
}

HandlerResult finish(const FighterEnv &e);                              // LAB_02BA
uint16_t damageOf(const FighterEnv &e, const Knight &attacker);         // JSR LAB_021B -> D0.W
void killCurrent(const FighterEnv &e);                                  // JSR LAB_000D
Approach approach(const FighterEnv &e);                                 // JSR LAB_0F24
int32_t dirSign(const Knight &k);                                       // JSR LAB_0F2E (D7)
void advancePhase(const FighterEnv &e, Knight &k, int32_t d6, int32_t d7);   // JSR LAB_0F32
HandlerResult moveApply(const FighterEnv &e, Knight &k, const Dir &d);  // JMP LAB_0F20
void hitSpark(const FighterEnv &e, const Knight &src);                  // LAB_0F34
void dragonStep(const FighterEnv &e);                                   // LAB_028E
// LAB_01EC.. : the blow of the record att on the knight self, by the row of kHurtRules for the attacker type ubRuleType
// (rules/damage.cpp): block check, effects, hit points, next script.
HandlerResult knightHurtBy(const FighterEnv &e, Knight &self, Knight &att, uint8_t ubRuleType);

}}}  // namespace ms::game::fi
