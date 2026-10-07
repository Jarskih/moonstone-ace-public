// game/data/modtopics - see modtopics.hpp.
#include "game/data/modtopics.hpp"

namespace ms {
namespace game {

namespace {

const IniSlice kNone = {0, 0};

void putU(char *p, uint16_t &n, uint32_t v) {
	char tmp[11];
	uint8_t k = 0;
	do {
		tmp[k++] = (char)('0' + v % 10);
		v /= 10;
	} while(v);
	while(k) p[n++] = tmp[--k];
}
void putS(char *p, uint16_t &n, const char *s) {
	while(*s) p[n++] = *s++;
}

// A reason of the form "<head><number><tail>".
void reportNum(ModReport &rep, const ModTable &t, uint8_t row, const char *pKey, const char *pHead, uint32_t ulNum, const char *pTail) {
	char why[96];
	uint16_t n = 0;
	putS(why, n, pHead);
	putU(why, n, ulNum);
	putS(why, n, pTail);
	why[n] = 0;
	modReportAdd(rep, t.aInfo[row].uwLine, modLit(t.pDesc->pKind), t.aNames ? modLit(t.aNames[row]) : kNone, modLit(pKey), kNone, why);
}

inline bool seen(const ModTable &t, uint8_t row) { return (t.aInfo[row].ubFlags & ROW_SEEN) != 0; }

// price <= gold_cap, for the rows a file wrote.
void checkPrice(ModReport &rep, const ModTable &t, uint8_t row, uint16_t uwPrice, const char *pKey, uint16_t uwCap) {
	if(uwPrice > uwCap) reportNum(rep, t, row, pKey, "above gold_cap (", uwCap, " in rules.ini): nobody could pay it");
}

}  // namespace

void modCheckShops(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser) {
	const GameData &d = *static_cast<const GameData *>(pUser);
	const uint16_t uwCap = d.rules.uwGoldCap;
	if(const ModTable *t = modTableOf(aTables, ubTables, "smith"))
		if(seen(*t, 0)) checkPrice(rep, *t, 0, d.smith.uwDaggerPrice, "dagger_price", uwCap);
	if(const ModTable *t = modTableOf(aTables, ubTables, "market"))
		if(seen(*t, 0))
			for(uint8_t i = 0; i < MARKET_SLOTS; ++i)
				if(d.market.auwPrice[i] > uwCap) {
					reportNum(rep, *t, 0, "prices", "entry above gold_cap (", uwCap, " in rules.ini): nobody could pay it");
					break;
				}
	if(const ModTable *t = modTableOf(aTables, ubTables, "dice_row")) {
		for(uint8_t r = 0; r < t->ubUsed; ++r) {
			if(!seen(*t, r)) continue;
			const uint8_t *a = d.aDice[r].aDice;
			if(a[0] > a[1] || a[1] > a[2])
				modReportAdd(rep, t->aInfo[r].uwLine, modLit("dice_row"), modLit(t->aNames[r]), modLit("dice"), kNone,
				             "the dice must be sorted from the lowest to the highest (the game sorts every throw)");
			for(uint8_t o = 0; o < r; ++o)
				if(d.aDice[o].aDice[0] == a[0] && d.aDice[o].aDice[1] == a[1] && d.aDice[o].aDice[2] == a[2]) {
					modReportAdd(rep, t->aInfo[r].uwLine, modLit("dice_row"), modLit(t->aNames[r]), modLit("dice"), kNone,
					             "this throw is already paid by an earlier row");
					break;
				}
		}
	}
}

void modCheckPlaces(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser) {
	const GameData &d = *static_cast<const GameData *>(pUser);
	if(const ModTable *t = modTableOf(aTables, ubTables, "place"))
		for(uint8_t r = 0; r < t->ubUsed; ++r)
			if(seen(*t, r) && d.aPlaces[r].ubNodeFirst > d.aPlaces[r].ubNodeLast)
				modReportAdd(rep, t->aInfo[r].uwLine, modLit("place"), modLit(t->aNames[r]), modLit("node_first"), kNone,
				             "must not be above node_last");
}

void modCheckLairs(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser) {
	const GameData &d = *static_cast<const GameData *>(pUser);
	if(const ModTable *t = modTableOf(aTables, ubTables, "loot_odds"))
		for(uint8_t r = 1; r < t->ubUsed; ++r)
			if((seen(*t, r) || seen(*t, r - 1)) && d.aLootOdds[r].swUpTo >= 0 && d.aLootOdds[r - 1].swUpTo > d.aLootOdds[r].swUpTo)
				modReportAdd(rep, t->aInfo[seen(*t, r) ? r : r - 1].uwLine, modLit("loot_odds"), modLit(t->aNames[r]), modLit("up_to"), kNone,
				             "the bands must rise: this one is below the band before it");
}

void modCheckArenas(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser) {
	const GameData &d = *static_cast<const GameData *>(pUser);
	const ModTable *t = modTableOf(aTables, ubTables, "arena");
	if(!t) return;
	for(uint8_t r = 0; r < t->ubUsed; ++r) {
		if(!seen(*t, r)) continue;
		const int8_t slot = d.aArenas[r].sbSlot;
		const bool bFree = slot == 10 || slot == 11 || slot == 13 || slot == 15;
		if(r >= ARENA_BUILTIN && !bFree)
			modReportAdd(rep, t->aInfo[r].uwLine, modLit("arena"), modLit(t->aNames[r]), modLit("slot"), kNone,
			             "a new row needs a free slot of the arena table: 10, 11, 13 or 15");
		else if(r < ARENA_BUILTIN && slot >= 0 && !bFree)
			modReportAdd(rep, t->aInfo[r].uwLine, modLit("arena"), modLit(t->aNames[r]), modLit("slot"), kNone,
			             "a built-in arena keeps its slot or moves to a free one (10, 11, 13, 15)");
		for(uint8_t o = 0; o < r; ++o)
			if(slot >= 0 && d.aArenas[o].sbSlot == slot) {
				modReportAdd(rep, t->aInfo[r].uwLine, modLit("arena"), modLit(t->aNames[r]), modLit("slot"), kNone,
				             "another row uses this slot");
				break;
			}
	}
}

void modCheckCreatures(const ModTable *aTables, uint8_t ubTables, ModReport &rep, void *pUser) {
	const GameData &d = *static_cast<const GameData *>(pUser);
	const ModTable *t = modTableOf(aTables, ubTables, "creature");
	if(!t) return;
	for(uint8_t r = 0; r < t->ubUsed; ++r)
		if(seen(*t, r) && d.aCreatures[r].swHp >= 0 && d.aCreatures[r].swHpMax >= 0 && d.aCreatures[r].swHpMax < d.aCreatures[r].swHp)
			modReportAdd(rep, t->aInfo[r].uwLine, modLit("creature"), modLit(t->aNames[r]), modLit("hp_max"), kNone,
			             "must not be below hp");
}

}  // namespace game
}  // namespace ms
