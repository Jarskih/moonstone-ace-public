// game/net_hash - the state hash of the network desync check (ROADMAP 12.5 minimal / 12.8b, docs/NETPLAY.md section 1 "Desync check", 1.1a M2).
// Pure: no ACE, no hardware, no globals.  The runtime (src/rt/net_state.cpp) copies the live game state into a NetStateImage; this file walks
// the image field by field, feeds every value into the CRC of engine/statehash.hpp and can print the same walk as text.
//
// What is in the walk and what is not:
//   * the five knight records and the 20 creature records (creatures only while active), the inventories, the 24 lairs and their loot,
//     the turn / day / moon / fight scalars, the RNG state, the party, the logical tick and clock;
//   * 68k POINTERS are never hashed as addresses (the same game lives at different addresses on two machines): a record pointer becomes
//     (array tag, byte offset from the array's base), the inventory pointer the same, a name pointer is replaced by the name text, and
//     the script / table pointers (a function of the creature kind) and spare bytes are left out;
//   * display and interrupt state (palette ramps, cursor sprite, DIW shake, frame pacing counter) is not game state and stays out.
// The walk order is the hash: both nodes run one build, so the order only has to be the same in that build.
#pragma once
#include <stdint.h>

#include "game/knight.hpp"
#include "game/world.hpp"
#include "game/party.hpp"

namespace ms { namespace game {

constexpr uint8_t NETHASH_KNIGHTS = 5;      // mogKnights[0..3] the knights, [4] the dragon
constexpr uint8_t NETHASH_CREATURES = 20;   // the heap creature / projectile records (mogCreatureHeap)
constexpr uint8_t NETHASH_LAIRS = 24;
constexpr uint8_t NETHASH_NAME = 16;        // bytes of a knight name that are hashed

struct NetStateImage {
	// where the arrays live in the 68k address space (what the pointers in the records are relative to)
	uint32_t ulKnightsBase, ulInventoriesBase, ulCreaturesBase, ulLootBase, ulLairBase;
	Knight aKnights[NETHASH_KNIGHTS];
	Inventory aInventories[NETHASH_KNIGHTS];
	char aacName[NETHASH_KNIGHTS][NETHASH_NAME];   // the text ulName points to (zero-filled where it points nowhere)
	Knight aCreatures[NETHASH_CREATURES];
	Lair aLairs[NETHASH_LAIRS];
	Inventory aLoot[NETHASH_LAIRS];
	// scalars
	uint32_t ulTick, ulClock, ulSeed, ulScene, ulGore;
	uint32_t ulCurKnight, ulFirstFighter, ulOpponent, ulCurLair;   // 68k addresses (normalised in the walk)
	uint16_t uwHumans, uwTurnCursor, uwMoveSpent, uwRoundDone, uwTurnBudget, uwDay, uwLunar, uwMoonFrame, uwMoonSub;
	uint8_t ubDefeat, ubLocationMode, ubFightActive;
	PartyConfig party;
};

// The consumer of a walk.  begin() opens a named group (a record), put() is one value of it.
struct StateSink {
	void (*pfnBegin)(void *pCtx, const char *szGroup);
	void (*pfnPut)(void *pCtx, const char *szName, uint32_t ulValue);
	void *pCtx;
};

// Visits every hashed value in order.
void netStateWalk(const NetStateImage &im, const StateSink &sink);
// CRC-32 over the values of the walk (each as four big-endian bytes).
uint32_t netStateHash(const NetStateImage &im);
// The walk as text lines ("group: name=hex name=hex ...", at most about 180 characters each), for the serial log of a desync.
typedef void (*NetLineFn)(void *pCtx, const char *szLine);
void netStateDump(const NetStateImage &im, NetLineFn pfnLine, void *pCtx);

// A pointer made address independent: 0 stays 0, a pointer into one of the image's arrays becomes (tag << 24) | byte offset, any other
// non-zero pointer becomes 0xEE000000 (it points at data whose address differs per machine and is not state).
uint32_t netNormalisePtr(const NetStateImage &im, uint32_t ulPtr);

}}  // namespace ms::game
