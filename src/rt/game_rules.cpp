// rt/game_rules - asm-callable entries into src/game/rules.cpp, patched over the mog routines they replace
// (asm/patches/mog.rules.json, ROADMAP 5.3).  Each shim keeps the original register contract.  The Knight record's
// +96 pointer is a real address on the Amiga, so the shim turns it into the Inventory reference the pure code wants.

#include <stdint.h>

#include "game/rules.hpp"

extern "C" __attribute__((used, externally_visible)) void rtKnightRecalcHp(ms::game::Knight *pKnight) {
	ms::game::knightRecalcHp(*pKnight, *reinterpret_cast<const ms::game::Inventory *>((uintptr_t)pKnight->ulInventory));
}

extern "C" __attribute__((used, externally_visible)) void rtKnightRecalcEndurance(ms::game::Knight *pKnight) {
	ms::game::knightRecalcEndurance(*pKnight);
}
