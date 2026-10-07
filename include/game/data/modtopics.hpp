// game/data/modtopics - the topic checks of the mod files that read GameData rows (ROADMAP 9.5b, 9.5c): cross-field and cross-row
// rules the parser cannot see (docs/ARCHITECTURE.md 3.4).  Each is a ModCheck hook (modcheck.hpp): pUser is the scratch GameData
// the file was parsed into, so a hook sees the live values of the other files too (rules.ini loads before shops.ini).  A hook looks at
// the rows its file wrote (ROW_SEEN), and reports at the line of the row's header or of the key the way the parser does.
#pragma once
#include "game/api/data.hpp"
#include "game/data/modcheck.hpp"

namespace ms {
namespace game {

// shops.ini: every price (dagger, market) can be paid with a full purse (<= rules gold_cap); the dice rows list sorted dice, no triple
// twice (the first row wins, a second one could never pay) and a multiplier of at least 1.
void modCheckShops(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser);
// places.ini: node_first <= node_last; map_node positions inside the map.
void modCheckPlaces(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser);
// lairs.ini: the loot bands rise (up_to of band n <= band n+1), the counts a lair can hold at the start.
void modCheckLairs(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser);

// arenas.ini: a row added by a file sits in a free slot of the arena table (10, 11, 13, 15) no other row uses; a slot is moved to
// only from a built-in row to another free slot.  creatures.ini: hp_max is not below hp when both are set.
void modCheckArenas(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser);
void modCheckCreatures(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser);

}  // namespace game
}  // namespace ms
