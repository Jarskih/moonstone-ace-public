// game/api/creatures - the creature type objects (docs/ARCHITECTURE.md 2; ROADMAP 9.5e1).  A CreatureDef row (game/api/data.hpp,
// mods/creatures.ini) holds the OVERRIDES of a creature: `creatureApply` writes the ones that are set into a creature record, after
// the built-in initialiser (src/rt/arena.cpp) has filled it.  A row left at its defaults changes nothing, so the game plays as the
// original.  Script and table names are ScriptRef ids; the platform maps an id to the address of the original's cell (CreatureEnv),
// the data and this code never hold an address of their own.  Implementation: src/game/api/creatures.cpp (pure, host-testable).
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

enum ScriptKind : uint8_t { SCRIPT_IDLE = 0, SCRIPT_HURT_TABLE, SCRIPT_WALK_TABLE, SCRIPT_ACTION_TABLE, SCRIPT_DAMAGE_TABLE };

// What the platform provides: the address of the cell a name stands for (0 for none / unknown), and a long store into the game's
// memory (the damage tables live in the original's data).
struct CreatureEnv {
	uint32_t (*pfnAddr)(ScriptKind kind, ScriptRef ref);
	void (*pfnStore32)(uint32_t ulAddr, uint32_t ulValue);
};

// Writes the overrides of `def` into the record: AI type, hit points, the three tunables, the idle / alt scripts and the
// table pointers.  Fields the row leaves at "original" are not touched.
void creatureApply(const CreatureDef &def, Knight &rec, const CreatureEnv &env);

// Writes the row's `damage` list into its damage table (the table `damage_table` names, else ulBuiltinTable; 0 = none: nothing
// is written).  Called once per game start (the fighters' tables are filled then), not per spawn.
void creatureDamageApply(const CreatureDef &def, const CreatureEnv &env, uint32_t ulBuiltinTable);

}}  // namespace ms::game
