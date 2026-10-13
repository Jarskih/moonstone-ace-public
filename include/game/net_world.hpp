// game/net_world - the WORLD STATE of a running game as one image, its byte form and its hash (ROADMAP M12, rejoin, docs/NETPLAY.md 4.5).
// Pure: no ACE, no hardware, no globals.  src/rt/net_world.cpp copies the live cells into a NetWorldImage (netWorldCapture) and writes an image back
// into them (netWorldApply); the host sends the bytes of netWorldEncode at a map turn, the rejoining machine decodes them over its own image and applies.
//
// What the image holds (the audit of "everything the deterministic game needs to continue", NETPLAY.md 4.5):
//   st        game/net_hash's NetStateImage: the five knight records, their inventories and names, the 24 lairs with their loot, the turn / day / moon
//             scalars, the RNG state, the party, the logical tick and clock.  The creature pool is sent too (the ACTIVE slots, index + record: slots stay
//             active between fights, the 50 tick hash walks them); the decoder frees every slot of the receiver first.  The host snapshots only
//             when no fight runs (netWorldFightIdle).
//   names     the four 22 byte name buffers of the portraits (the records point at them)
//   x[]       the NetExtra cells: state that rules read across turns and that the hash does not cover (market stock, gift cycles, the dragon's flight, the
//             terrain counter, the map modes, the turn latches, the fight spawn counters ...)
//   party map the co-op PartyMap (size, leader, black knights) and the black-knight pool records 5..8
// Pointers in the records are made address independent (a tag and an offset) on the way out and rebuilt with the receiver's own addresses on the way in:
// the arrays of the image, the pool, the name targets and a table of address REGIONS (the owned data objects: scripts, tables ...).  A pointer into none of
// them is "other": it is not state, the receiver keeps what it has.
//
// The hash (netWorldHash) covers everything of the image except pointers into owned data (scripts, tables: they differ per machine, the receiver keeps its
// own): the standard net_hash walk of st plus the rest.  The periodic 50 tick hash of the session
// stays net_hash's (the extras are only checked when a player rejoins), see NETPLAY.md 4.5.
#pragma once
#include <stdint.h>

#include "game/net_hash.hpp"

namespace ms { namespace game {

constexpr uint8_t NW_NAMES = 4;               // name buffers of the portraits
constexpr uint8_t NW_NAME_BYTES = 22;         // bytes of one (game/net_knights NET_KNIGHT_NAME_BYTES)
constexpr uint8_t NW_POOL = 4;                // co-op black-knight pool records 5..8
constexpr uint8_t NW_NAME_TARGETS = 9;        // name pointers: the 4 portrait buffers, the 4 computer knights' names, the pool's group name
constexpr uint8_t NW_REGIONS = 40;            // address regions (owned data objects) pointers may point into
constexpr uint32_t NW_MAGIC = 0x4D535744;     // "MSWD"
constexpr uint8_t NW_VERSION = 3;

// The extra cells.  Every one is a 32 bit value in the image; src/rt/net_world.cpp maps the id to the cell and its width.
enum NetExtra : uint8_t {
	NX_MARKET0, NX_MARKET1, NX_MARKET2, NX_MARKET3, NX_MARKET4, NX_MARKET5,   // the merchant's stock (an Inventory, 24 bytes as six longs)
	NX_GOLD_CYCLE, NX_ITEM_CYCLE, NX_BAD_LUCK, NX_LAST_GIFT,                  // the wizard's gift cycles and the failed-gamble flag
	NX_DRAGON_ACTIVE, NX_DRAGON_TIMER, NX_DRAGON_SPEED_X, NX_DRAGON_SPEED_Y, NX_DRAGON_LAST_ID, NX_DRAGON_LAST_X,
	NX_DRAGON_TOUCH0, NX_DRAGON_TOUCH1, NX_DRAGON_TOUCH2, NX_DRAGON_TOUCH3,   // record pointers
	NX_TERRAIN_COUNTER, NX_TERRAIN_BLOCKED,
	NX_MAP_FORCED, NX_MAP_AMBUSH, NX_MAP_POS_SAVED, NX_MAP_SAVED_X, NX_MAP_SAVED_Y, NX_MAP_LOCKED,
	NX_BUDGET_Q1, NX_BUDGET_Q3, NX_LATCH3, NX_LATCH2, NX_LATCH1, NX_AI_GOAL_XY, NX_AI_WALK0, NX_ROAM_BUILT, NX_HUMANS_SAVED,
	NX_SPAWN_ENTRY, NX_SPAWN_TOGGLE, NX_FLYER_TOGGLE, NX_BAT_COUNTER, NX_AI_LAST_ACTION, NX_HOP_STEP, NX_HOP_DIR, NX_CREATURE_BITS, NX_WAVE_LEVEL,
	NX_ACTIVE_CURRENT,                                                        // ActiveKnights::ulCurrent (a record pointer)
	// What the map's first frame of a turn reads from the turn BEFORE (NW_VERSION 3): mapScreenEnter runs MS_MOVE with LAB_0656, the move word the last
	// frame of the previous turn left behind, so the knight whose turn begins takes one step with a stale word; a rejoiner holding 0 there ends one pixel
	// (and, on a tile edge, one tile) off from the host and the 50 tick hash fails.  The AI's other cells that survive a turn start travel with it.
	NX_AI_MOVE,                                                               // LAB_0656 (mogAiMove)
	NX_AI_WALK1, NX_AI_WALK2, NX_AI_WALK3, NX_AI_WALK4, NX_AI_WALK5, NX_AI_WALK6,   // LAB_0675..LAB_067A (NX_AI_WALK0 is LAB_0674)
	NX_ROAM_LAIR, NX_ROAM_HIDDEN,                                             // LAB_0673 (a lair pointer), LAB_067C
	NX_SHOP_COST, NX_SHOP_KIND, NX_SHOP_ITEM, NX_STEP_GATE, NX_TOWN_DIST1, NX_TOWN_DIST2,   // LAB_08F7, LAB_08F8, LAB_08F9, LAB_066B, LAB_066F, LAB_0670
	NX_OPP_DIST0, NX_OPP_DIST1, NX_OPP_DIST2, NX_OPP_DIST3,                   // LAB_0651: the AI opponent scratch (distances)
	NX_OPP_PTR0, NX_OPP_PTR1, NX_OPP_PTR2, NX_OPP_PTR3,                       // LAB_0652: ... and the record pointers
	NX_COUNT
};

// True for the extras that hold a pointer (normalised on the way out).
bool netExtraIsPtr(uint8_t ubExtra);

struct NetWorldImage {
	NetStateImage st;
	uint8_t aubNames[NW_NAMES][NW_NAME_BYTES];
	uint32_t aulX[NX_COUNT];
	uint8_t aubPartyMap[4];                    // PartyMap: active, size, leader, black-knight count
	Knight aPool[NW_POOL];
	Inventory aPoolInv[NW_POOL];
	// where the things pointers point at live in THIS machine's address space (filled by the capture; the decoder reads them)
	uint32_t ulPoolBase, ulPoolInvBase;
	uint32_t aulNameTarget[NW_NAME_TARGETS];   // addresses of the name texts; 0 = unused
	uint32_t aulRegionBase[NW_REGIONS], aulRegionSize[NW_REGIONS];
	uint8_t ubRegions;
};

// ---- pointers ----------------------------------------------------------------------------------------------------------------

// A pointer as bytes that mean the same on every machine: 0 stays 0; a pointer into the knight / creature / inventory / loot / lair arrays, the pool, the
// name targets or a region becomes (tag << 24) | offset; anything else becomes 0xEE000000 ("other").
uint32_t netWorldNormalise(const NetWorldImage &im, uint32_t ulPtr);
// The reverse with this machine's addresses.  "Other" (and a tag this machine does not know) gives ulKeep: the pointer the machine already has.
uint32_t netWorldDenormalise(const NetWorldImage &im, uint32_t ulNorm, uint32_t ulKeep);

// ---- the byte form -----------------------------------------------------------------------------------------------------------

// Writes the image; false when ulCap is too small.  The image is not changed.
bool netWorldEncode(const NetWorldImage &im, uint8_t *pOut, uint32_t ulCap, uint32_t &ulLen);
// Reads the bytes over im, which must hold THIS machine's capture first (its bases, regions, and the values a field falls back to).  False: wrong magic or
// version, truncated, trailing bytes, a field out of range.  Creatures are marked inactive.
bool netWorldDecode(const uint8_t *pIn, uint32_t ulLen, NetWorldImage &im);

// ---- the hash and the preconditions ------------------------------------------------------------------------------------------

// net_hash's standard hash of st, then the names, the extras (pointers normalised), the party map and the pool: everything the image carries.
uint32_t netWorldHash(const NetWorldImage &im);
// No fight runs: the host snapshots only at a map turn (creature slots left over from earlier fights are part of the image).
bool netWorldFightIdle(const NetWorldImage &im);

}}  // namespace ms::game
