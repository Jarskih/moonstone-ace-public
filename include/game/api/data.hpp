// game/api/data - GameData: the rule data the mod files override (docs/ARCHITECTURE.md 2, 3; ROADMAP 9.4b).
// MINIMAL for now: RulesDef and the item tables (WeaponDef, ArmourDef, ItemDef; ROADMAP 9.5d).  `kDefaults` is generated (build/gen/mod_defaults.cpp, tools/gen_moddata.py from
// tools/mod_schema/*.yaml); the members listed in a schema's `layout:` must be declared here in exactly that order
// (the generator emits a positional initialiser plus static_asserts that check it).  POD, constant-initialised.
#pragma once
#include <stdint.h>

namespace ms {
namespace game {

enum { WAVE_SCALING_ORIGINAL = 0, WAVE_SCALING_NONE = 1, WAVE_SCALING_LIMITED = 2 };

// tools/mod_schema/rules.yaml: [waves] [coop] [limits] of rules.ini.
struct RulesDef {
	uint16_t uwGoldCap;               // [limits] gold_cap: a knight's gold is cut to this (signed word compare)
	uint8_t ubDaggerCap;              // [limits] dagger_cap: daggers a knight can carry
	uint8_t ubWaveScaling;            // [waves] scaling: WAVE_SCALING_*
	uint8_t ubCoopAliveBonus;         // [coop] alive_bonus: extra monsters alive at once with two knights
	uint8_t ubCoopTotalFactor;        // [coop] total_factor: monster total multiplier with two knights
	uint8_t ubCoopWritebackDivisor;   // [coop] writeback_divisor: the lair count written back is divided by this, rounded up
	uint8_t ubCoopEnabled;            // [coop] enabled: the co-op wave rules apply (no co-op mode yet: false)
};

// ---- items (tools/mod_schema/items.yaml, mods/items.ini; ROADMAP 9.5d) ----------------------------------------------------
// Three tables of rows.  The rows are the Type Objects of the equipment: rules look a row up by the item code the knight
// carries and read its numbers, so a new row (a mod, or ROADMAP 8.8 / 8.9) is behaviour without a new if-chain.  A row whose
// code is 0 is unused and inert: the tables keep their built-in rows first, the rest of the pool is zero.
enum { WEAPON_ROWS = 8, ARMOUR_ROWS = 8, ITEM_ROWS = 8 };
enum { WEAPON_KIND_MELEE = 0 };   // 8.9 adds the bow here (the dagger / projectile path); 8.8 adds reach, speed, attack moves

// A weapon: what Knight::ulSword (+88) selects.  contactDamage adds ubDamageBonus to the hit.
struct WeaponDef {
	uint16_t uwCode;               // the Knight::ulSword value that selects this row; 0 = unused row
	uint16_t uwPrice;              // smith price in gold, 0 = not sold (read by the shop rules, ROADMAP 9.5c)
	uint8_t ubDamageBonus;         // added to the damage of every hit of a knight wielding it (word arithmetic)
	uint8_t ubRank;                // order of strength: the smith offers a weapon only above the one the knight has
	uint8_t ubKind;                // WEAPON_KIND_*
};

// An armour: what Knight::ulArmour (+92) selects.  The stats rules add the bonuses to maximum HP and derived endurance.
struct ArmourDef {
	uint16_t uwCode;               // Knight::ulArmour value; 0 = unused row
	uint16_t uwPrice;              // smith price in gold, 0 = not sold
	uint8_t ubHpBonus;             // added to the knight's maximum HP
	uint8_t ubEnduranceBonus;      // added to the derived endurance (byte arithmetic)
	uint8_t ubRank;                // order of strength
};

// An inventory item that changes the knight's stats (a slot of the Inventory, ItemSlot): maximum HP per item held, and a
// weapon that holding it forces the knight to wield.  A row with neither effect is inert.
struct ItemDef {
	uint16_t uwForcesWeapon;       // weapon code (WeaponDef::uwCode) the knight wields while the count is non-zero; 0 = none
	uint8_t ubSlot;                // the Inventory byte offset of the item's count (ItemSlot, even)
	uint8_t ubHpBonus;             // maximum HP added per item held
};

// ---- shops, places, lairs, encounters (shops.yaml, places.yaml, lairs.yaml, encounters.yaml; ROADMAP 9.5b, 9.5c) -------------
// The smith's armour and sword prices are the rows of the item tables above (uwPrice, 0 = not sold).  A pool row that is not in use
// is zero: a dice row without a multiplier, a place row 0..0 of kind none.
enum { MARKET_SLOTS = 12, DICE_ROWS_MAX = 16, TEMPLE_PLAYER_ROWS = 4 };
struct SmithDef { uint16_t uwDaggerPrice; };     // [smith] dagger_price
struct MarketDef {                               // [market]
	uint16_t auwPrice[MARKET_SLOTS];             // prices: inventory slot offset / 2 (the game's price list)
	uint8_t ubSellShift;                         // sell_shift: a sale pays price >> shift
	uint8_t ubStockRolls;                        // stock_rolls: item rolls that stock the merchant at a new game
	uint8_t ubStockExtra;                        // stock_extra: extra items of slot 8 added after the rolls
};
struct DiceRowDef {                              // [dice_row <name>]: the three sorted dice that win, and the multiplier (0 = unused row)
	uint8_t aDice[3];
	uint8_t ubMultiplier;
};
struct TempleDef {                               // [temple]
	uint16_t auwStatCost[TEMPLE_PLAYER_ROWS];    // stat_cost: progress points a stat costs with 1..4 human players
	uint8_t ubCastleLifeCap;                     // castle_life_cap: a castle gives a life below this many
};
struct HealerDef {                               // [healer]
	uint16_t uwHealPrice;                        // heal_price: the smallest donation taken, and the price of full hp
	uint16_t uwLifePrice;                        // life_price: the price of one life
	uint8_t ubLifeCap;                           // life_cap: the healer stops at this many lives
};

// -1 in the lair / node / loot-odds rows means "keep what the original data says" (those numbers are not in this tree; the game
// reads them from the disks at start-up), so unmodded values are exactly the original ones.
enum { PLACE_ROWS_MAX = 12, MAP_NODE_ROWS = 9, LAIR_ROWS = 24, LOOT_ODDS_ROWS = 4 };
struct PlaceDef {                                // [place <name>]: the map nodes ubNodeFirst..ubNodeLast are a place of this kind
	uint8_t ubNodeFirst;
	uint8_t ubNodeLast;
	uint8_t ubKind;                              // PlaceKind (game/scene_places.hpp): 0 none, 1 village, ... 7 duel
};
struct MapNodeDef { int16_t swX, swY; };         // [map_node <id>]: x, y of the node (-1 = original)
struct LairDef {                                 // [lair <name>]: one of the 24 overworld encounters (-1 = original)
	int8_t sbArena;                              // arena: which arena handler (index into the arena table)
	int16_t swCount;                             // count: monsters in the lair
	int16_t swX, swY;                            // x, y: position on the map
	int16_t swRegion;                            // region: the map region word
};
struct LootOddsDef {                             // [loot_odds <band1..band4>]: percentile up to which the loot kind applies
	int16_t swUpTo;                              // up_to (-1 = original)
	int8_t sbKind;                               // loot: LOOT_* (-1 = original)
};
enum { LOOT_ORIGINAL = -1, LOOT_GOLD = 1, LOOT_ITEMS = 2, LOOT_MIXED = 3, LOOT_NONE = 4 };
struct EncountersDef {                           // [encounters]
	uint8_t ubDragonDay;                         // dragon_day: the dragon appears from this day on
	uint8_t ubAiEngageOdds;                      // ai_engage_odds: an AI knight picks a fight on a draw 0..127 up to this
};

// ---- creatures (tools/mod_schema/creatures.yaml, mods/creatures.ini; ROADMAP 9.5e1) --------------------------------------------
// A creature row is a Type Object of OVERRIDES: the built-in numbers of the original creatures live in the game code (the built-in
// records `ubLike` selects, src/rt/arena.cpp), the row only holds what a mod changes, so the defaults are all "original" (-1 for
// numbers, 0 for script names, 255 for the AI) and no original value is in this tree.  `creatureApply` (game/api/creatures.hpp)
// writes a row's overrides into a creature record.  Script and table names are ScriptRef ids of the generated name tables
// (build/gen/mod_names.cpp: id -> the original's cell); the data never holds an address.
enum { CREATURE_ROWS = 16, CREATURE_BUILTIN = 10, CREATURE_DAMAGE_SLOTS = 9 };
enum { CREATURE_AI_ORIGINAL = 255 };
typedef uint16_t ScriptRef;    // 0 = original; 1.. = the enum value of the name tables (idle scripts, hurt/walk/action/damage tables)

struct CreatureDef {
	uint8_t ubLike;                // the built-in record the row starts from (0 .. CREATURE_BUILTIN-1)
	uint8_t ubAi;                  // ActorType value, CREATURE_AI_ORIGINAL = the built-in one
	int16_t swHp, swHpMax;         // -1 = original; hp_max -1 follows hp when hp is set
	int16_t swReach, swTooClose, swDepth;   // -1 = original (Knight +116 / +118 / +120)
	ScriptRef uwIdle, uwAlt;       // standing scripts (+22 / +26); alt 0 follows a set idle
	ScriptRef uwHurtTable, uwWalkTable, uwActionTable, uwDamageTable;   // the tables the record points at (+30 / +34 / ...)
	uint8_t ubDamageCount;         // entries of aDamage in use (0 = none)
	int16_t aDamage[CREATURE_DAMAGE_SLOTS];   // damage per slot written into the damage table at game start (-1 keep)
};

// ---- arenas (tools/mod_schema/arenas.yaml, mods/arenas.ini; ROADMAP 9.5e2) -----------------------------------------------------
// An arena row, like a creature row, holds overrides over the built-in arena it names (`ubLike`); -1 = the original.  The creature
// arenas are entries of the arena table (the lairs' `arena` is a slot of it, lairs.ini): a row with `slot` set is stored there, so a
// new row plus a lair row that names its slot puts a new fight on the map.
// The creature cel sets the loaders know (arenas.ini `cels`) and the sound banks (`sounds`, an index into the creature banks).
enum { CELS_TROGG_AXE = 0, CELS_TROGG_SPEAR, CELS_RATMEN, CELS_MUDMEN, CELS_BALOK, CELS_BE, CELS_TROLL, CELS_DRAGON, CELS_DEMON };
enum { ARENA_ROWS = 13, ARENA_BUILTIN = 9, ARENA_SLOTS = 17 };
struct ArenaDef {
	uint8_t ubLike;                // the built-in arena the row starts from (0 .. ARENA_BUILTIN-1)
	int8_t sbSlot;                 // the arena-table slot the row is stored in; -1 = the built-in's own slot (new rows need one)
	int8_t sbCreature;             // the creature row that spawns (creatures.ini row number); -1 = the built-in's
	int16_t swAliveMax, swTotal;   // creatures alive at once / in all before the wave scaling; -1 = the built-in's
	int16_t swPalette;             // the palette kind of the fight screen; -1 = the built-in's
	int8_t sbCels;                 // CELS_*: the creature cel set the arena loads; -1 = the built-in's
	int8_t sbSounds;               // the sound bank started with it (index of the creature banks); -1 = the cel set's own
};

// The members are declared in the order of the schema files (sorted by file name), the sections of a file in their order.
struct GameData {
	ArenaDef aArenas[ARENA_ROWS];         // arenas.ini
	CreatureDef aCreatures[CREATURE_ROWS];   // creatures.ini
	EncountersDef encounters;          // encounters.ini
	WeaponDef aWeapons[WEAPON_ROWS];   // items.ini [weapon <name>]
	ArmourDef aArmours[ARMOUR_ROWS];   // items.ini [armour <name>]
	ItemDef aItems[ITEM_ROWS];         // items.ini [item <name>]
	LairDef aLairs[LAIR_ROWS];         // lairs.ini
	LootOddsDef aLootOdds[LOOT_ODDS_ROWS];
	PlaceDef aPlaces[PLACE_ROWS_MAX];  // places.ini
	MapNodeDef aMapNodes[MAP_NODE_ROWS];
	RulesDef rules;                    // rules.ini
	MarketDef market;                  // shops.ini
	DiceRowDef aDice[DICE_ROWS_MAX];
	TempleDef temple;
	HealerDef healer;
	SmithDef smith;
};

extern const GameData kDefaults;   // generated
extern GameData g_gameData;        // the live data: kDefaults plus PROGDIR:mods/ (rt/modload.cpp, ROADMAP 9.4c)

// Row lookup by item code over the live data; nullptr when no row has the code (the original gives an unknown item no bonus).
inline const WeaponDef *weaponDefFind(uint32_t ulCode) {
	for(uint8_t i = 0; i < WEAPON_ROWS; ++i) {
		const WeaponDef &d = g_gameData.aWeapons[i];
		if(d.uwCode != 0 && d.uwCode == ulCode) return &d;
	}
	return 0;
}
inline const ArmourDef *armourDefFind(uint32_t ulCode) {
	for(uint8_t i = 0; i < ARMOUR_ROWS; ++i) {
		const ArmourDef &d = g_gameData.aArmours[i];
		if(d.uwCode != 0 && d.uwCode == ulCode) return &d;
	}
	return 0;
}
// The smith's price of an item code over data `d` (the uwPrice of its row in items.ini; 0 = not sold, also for an unknown code).
inline uint16_t smithArmourPrice(const GameData &d, uint32_t ulCode) {
	for(uint8_t i = 0; i < ARMOUR_ROWS; ++i)
		if(d.aArmours[i].uwCode != 0 && d.aArmours[i].uwCode == ulCode) return d.aArmours[i].uwPrice;
	return 0;
}
inline uint16_t smithSwordPrice(const GameData &d, uint32_t ulCode) {
	for(uint8_t i = 0; i < WEAPON_ROWS; ++i)
		if(d.aWeapons[i].uwCode != 0 && d.aWeapons[i].uwCode == ulCode) return d.aWeapons[i].uwPrice;
	return 0;
}
inline uint16_t weaponDamageBonus(uint32_t ulCode) {
	const WeaponDef *p = weaponDefFind(ulCode);
	return p ? p->ubDamageBonus : 0;
}

}  // namespace game
}  // namespace ms
