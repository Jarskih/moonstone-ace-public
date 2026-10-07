// game/api/world - the World view: everything a rule may see of the running game, without addresses (docs/ARCHITECTURE.md
// 2; ROADMAP 9.3a).  Rules and scenes take a `World &` and use the accessors of party.hpp, items.hpp, clock.hpp, fight.hpp,
// places.hpp and cues.hpp; they do not touch the records' pointer fields or the owned cells.
//
// The World only holds pointers to storage somebody else owns.  On the Amiga src/rt/world_bind.cpp builds it from the owned
// cells (`worldGet()`, ROADMAP 9.3b); a host test builds one over local arrays.  Values that change while the game runs
// (humans, day, seed, counters) are held as pointers to the live cell, never as a copy.
//
// Not pure-API: the record types come from game/state.hpp (the layout of the original structs).
#pragma once
#include <stdint.h>

#include "game/api/cues.hpp"
#include "game/api/ids.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

struct GameData;   // game/api/data.hpp

// The persistent knight records: four knights and the dragon (LAB_0613..LAB_0617) with their inventories (LAB_0618/0619).
struct Party {
	Knight *aRecords;               // KNIGHT_SLOTS records
	Inventory *aInv;                // KNIGHT_SLOTS inventories, aInv[i] belongs to aRecords[i]
	uint8_t ubCount;                // knights in the party (PLAYER_KNIGHTS; the dragon is not counted)
	uint16_t *puwHumans;            // live cell: human players 1..4 (LAB_05C5)
	const uint32_t *pulCurrent;     // live cell: the current knight, as the 32-bit address the game stores (LAB_0633)
	uint32_t ulRecordsAddr;         // the 32-bit address of aRecords[0] in the same units as *pulCurrent
};

// The lunar clock and the day count (ActiveKnights::uwMoonFrame, LAB_06C1, LAB_06C0).
struct Clock {
	uint16_t *puwDay;               // live cell: day counter (LAB_06C0)
	uint16_t *puwMoonIndex;         // live cell: lunar index 0..7 (LAB_06C1)
	uint16_t *puwMoonFrame;         // live cell: current lunar image 45..49 (ActiveKnights +18)
};

// The game's random generator (LAB_0973); only the original draw shapes are offered so a moved rule keeps the sequence.
struct Rng {
	uint32_t *pulSeed;
};

// The fight in progress: the creature pool and its counters (LAB_05C3, LAB_05EC, LAB_05ED, LAB_05EE).
struct Fight {
	Knight *aCreatures;             // CREATURE_POOL_SIZE records; ulActive != 0 = slot in use
	uint16_t *puwTotal;             // live cell: creatures still to come in this fight (LAB_05EC)
	uint16_t *puwMaxAlive;          // live cell: most creatures alive at once (LAB_05ED)
	uint16_t *puwAlive;             // live cell: creatures alive now (LAB_05EE)
};

struct World {
	Party party;
	Fight fight;
	Lair *aLairs;                   // LAIR_COUNT records (heap, LAB_05B9[17])
	Inventory *aLairLoot;           // LAIR_COUNT loot inventories (heap, LAB_05B9[18])
	Clock clock;
	Rng rng;
	const GameData *pData;          // the rule data (defaults, later overridden by mods/*.ini); may be null in tests
	CueQueue *pCues;
};

// The game's World, rebuilt at every overlay entry.  Platform side: src/rt/world_bind.cpp (Amiga only; not linked on the host).
World &worldGet();

}}  // namespace ms::game
