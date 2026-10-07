// Moonstone game state: the 132-byte actor/knight record and the 24-byte counted inventory (ROADMAP 5.1).
//
// Authority: moonshard/moonstone-main/amiga_asm/mog.asm.  Cross-checks only: DOC_TECHNIQUE.md 10.18,
// moonshard/mechanics.{h,c} and moonshard/ORIGINAL_FOUNDATION.md.  Where they disagree with the asm the asm wins;
// the disagreements are listed in docs/GAME_STATE.md.
//
// Layout rules: the target is a big-endian 68k, so plain integers are right there.  #pragma pack(2) because the
// 68k only needs 2-byte alignment (longs sit at +14, +18, ...).  Pointers are stored as uint32_t so the structs
// have the same size on the host (tests/test_game_state.py compiles them with clang++ on x86-64).
// Naming: ub/uw/ul = unsigned 8/16/32 bit, sw = signed 16 bit, Spare<offset> = a byte the asm never reads or writes (padding); a role that is
// established gets a name.  Every field carries the routine label that fixes its offset and role as evidence.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "game/constants.hpp"

namespace ms { namespace game {

#pragma pack(push, 2)

// ---------------------------------------------------------------------------------------------------------
// Inventory: 24 bytes = 12 two-byte slots, one per item.  A slot's count is the byte at its EVEN offset
// (TST.B 4(A1) in LAB_0013, TST.B 0(A2,D0.W) in LAB_001E); the odd byte of a slot is never read as a byte.
// The item-transfer table LAB_0028 (S_0 DATA, words 0016 0014 0004 0006 000E 0012 0008 000C 0010 0000 0002 000A
// FFFF) lists the slots that move from the loser to the winner after a fight (LAB_001C): 0x14 and 0x16 are merged
// as WORDS with OR (flag masks), every other slot is added as a counted BYTE.
// Instances: BSS LAB_0618 (4 knights; LAB_01C4 hands them out through Knight +96), LAB_0619 (the dragon's, only
// 24 bytes) and 24 lair loot inventories on the heap (LAB_05B9[18], set up in LAB_01B3).
// LAB_01C6 clears all 24 bytes (CLR.B loop, D0=23) when a knight record is initialised.
struct Inventory {
	uint8_t ubCount0;          // +0   slot 0: only evidence is the transfer table LAB_0028; role unknown
	uint8_t ubSpare1;            // +1
	uint8_t ubCount2;          // +2   slot 2: transfer table only
	uint8_t ubSpare3;            // +3
	uint8_t ubSharpSword;      // +4   LAB_0013 / LAB_0026: non-zero forces Knight +88 (sword) to $19 (DOC 10.18: $19 = sword of sharpness)
	uint8_t ubSpare5;            // +5
	uint8_t ubHpItem;          // +6   LAB_0013: MULU #20 -> +20 max HP per count (DOC 10.18: ring of protection)
	uint8_t ubSpare7;            // +7
	uint8_t ubDamageShift;     // +8   LAB_0204: incoming damage is LSR.W'd by this count (foundation notes: "inventory[8]")
	uint8_t ubSpare9;            // +9
	uint8_t ubCount10;         // +10  transfer table only
	uint8_t ubInputPort;           // +11
	uint8_t ubCount12;         // +12  transfer table only
	uint8_t ubSpare13;           // +13
	uint8_t ubCount14;         // +14  transfer table only
	uint8_t ubSpare15;           // +15
	uint8_t ubCount16;         // +16  transfer table only
	uint8_t ubSpare17;           // +17
	uint8_t ubBattleAvoid;     // +18  LAB_0058: TST.B 18(A0) gates the "may use their Scroll of protection to avoid this battle" dialog (strings LAB_06C9..06CC)
	uint8_t ubSpare19;           // +19
	uint8_t ubKeys;            // +20  LAB_009D: CMPI.B #$0F,20(A0) = all four keys; LAB_01B4 writes 8/4/2/1 into four lairs; LAB_00A0 clears it; WORD-merged by LAB_0028
	uint8_t ubSpare21;           // +21
	uint8_t ubMoonstones;      // +22  LAB_021F / LAB_00A1: BTST #0..#3 against the moon frame; LAB_0DCA does BSET rng&3; WORD-merged by LAB_0028
	uint8_t ubSpare23;           // +23
};
static_assert(sizeof(Inventory) == 24, "Inventory is 24 bytes (LAB_0618 DS.L 24 = 4 x 24)");
static_assert(offsetof(Inventory, ubSharpSword) == 4, "");
static_assert(offsetof(Inventory, ubHpItem) == 6, "");
static_assert(offsetof(Inventory, ubDamageShift) == 8, "");
static_assert(offsetof(Inventory, ubBattleAvoid) == 18, "");
static_assert(offsetof(Inventory, ubKeys) == 20, "");
static_assert(offsetof(Inventory, ubMoonstones) == 22, "");

// ---------------------------------------------------------------------------------------------------------
// Knight / actor record, 132 bytes ($84).  Stride proven by ADDA.L #$84 / MULU #$84 (LAB_0011, LAB_01BF, LAB_0DBD).
// The same layout is used by every sprite-driven actor of the combat engine, not only knights:
//   - BSS LAB_0613..LAB_0617: five persistent records.  Records 0..3 are the four knights (LAB_01C4 loop, D7=3);
//     record 4 (LAB_0617) is the dragon (+54 == 5 in LAB_01AE, its inventory is LAB_0619); the daily upkeep
//     LAB_0029 loops D0=4, i.e. over all five.
//   - heap (pointer LAB_05C3): 20 creature/projectile records with the same stride (LAB_000A, LAB_003C).
// Fields +22..+50 and +116..+120 are filled per actor type by the init routines (LAB_0167, LAB_0169, ...).
// LAB_061D is the script interpreter's next-script pointer; the script tables below are what the per-frame
// handlers (LAB_01CA .. LAB_0226) copy into it.
struct Knight {
	uint32_t ulActive;          // +0   TST.L (A0) = slot active (pool slot in use) (LAB_003C, LAB_003B skip empty creature records); LAB_031B clears it
	uint16_t uwX;              // +4   horizontal position: ADD.W D2,4(A0) in LAB_01D5; LAB_0DCB spawns the dragon at 10
	uint16_t uwHeight;         // +6   height above the ground (negative = in the air); the job's +8 (CombatJob::uwY); knights 0, dragon $0C (LAB_0DCB)
	uint16_t uwY;              // +8   vertical position: ADD.W D3,8(A0) in LAB_01D3
	uint8_t  ubFacing;         // +10  1 = right, 3 = left; bit 1 mirrors horizontal motion (BTST #1,10(A1) in LAB_01D9)
	uint8_t  ubInputPort;      // +11  controller port that steers this fighter: PORT_* in constants.hpp (4 = AI, 2 / 1 = the two players; LAB_01AE, LAB_0DBD, LAB_0002, LAB_004F)
	uint8_t  ubAnimPhase;      // +12  walk-cycle phase 0..3 (ADDQ.B #1,12(A1); ANDI.B #3 in LAB_01CE)
	uint8_t  ubAnimTimer;      // +13  countdown reloaded to $1E (LAB_01C8) or 5..20 (LAB_022C)
	uint32_t ulHitTarget;      // +14  record this one has hit: collision code (LAB_03C3) stores the other record; LAB_01CA tests it after +18; LAB_01E0 dispatches on its +77
	uint32_t ulAttacker;       // +18  record that hit this one (back pointer set by LAB_03C3); LAB_01E6 reads its +64 action, LAB_01EC dispatches on its +77 type
	uint32_t ulIdleScript;     // +22  default next script when there is no input (LAB_01CA loads it into LAB_061D)
	uint32_t ulScript26;       // +26  alternate next script (LAB_01E3, LAB_024B); role unknown
	uint32_t ulHurtScripts;    // +30  table of scripts indexed by the attacker's action offset (LAB_020E, LAB_01F3)
	uint32_t ulActionScripts;  // +34  table of scripts indexed by +64 (LAB_01DF, LAB_01F2)
	uint32_t ulJobParam;       // +38  copied into job +28 (LAB_01AC); points at LAB_05E0 / LAB_05E1 style 5-long blocks
	uint32_t ulDamageTable;    // +42  base damage per action offset: LAB_021B MOVE.L 0(A1,D2.W)
	uint32_t ulWalkScripts;    // +46  walk scripts indexed by ubAnimPhase*4 + direction offset (LAB_01D7, LAB_022E)
	uint32_t ulDefenseTable;   // +50  required-defence action per attacker action (LAB_01E6)
	uint32_t ulKind;           // +54  0..3 = the four knights, 4 = AI-controlled knight, 5 = dragon (CMPI.L #4/#5 in LAB_0030, LAB_01AE, LAB_0DB6 area); DOC 10.18 "knight_id"
	uint32_t ulSpare58;        // +58  never accessed as a record field (unused)
	uint16_t uwInput;          // +62  input bits, in byte +63: bit0 right, bit1 left, bit2 down, bit3 up, bit4 fire (LAB_01CE..LAB_01D0, LAB_01DD)
	uint16_t uwAction;         // +64  current action as a byte offset into the script tables: 0/4/8/../$20 (LAB_01DD, LAB_021B)
	uint16_t uwTileX;          // +66  uwMapX >> 3 (LAB_01C5)
	uint16_t uwTileY;          // +68  uwMapY >> 3 (LAB_01C5)
	uint8_t  ubStrength;       // +70  added to melee damage (LAB_021B); init 1 (LAB_01C6); DOC 10.18 "moonstone_strength"
	uint8_t  ubConstitution;   // +71  max HP = this*10 + ... (LAB_0013); init 1
	uint8_t  ubEndurance;      // +72  derived endurance = this*2 + armour bonus + 4 (LAB_0019); init 1
	uint8_t  ubLives;          // +73  SUBI.B #1 when a fight is lost (LAB_000E, LAB_0030); init 5; DOC 10.18 calls it "skill_level" but the asm and the foundation notes agree it is lives
	uint16_t uwGold;           // +74  LAB_046C adds found gold; LAB_0021 transfers half on a win; init 10
	uint8_t  ubDaggers;        // +76  init 10 (LAB_01C6)
	uint8_t  ubType;           // +77  behaviour type: $0C knight in a fight, $10 knight on the map, $14 black-knight/dragon fight type (LAB_001C), $18/$20 creatures, $28 dragon in flight, $34 dagger; indexes the LAB_0621/LAB_0622 handler tables
	uint16_t uwProgress;       // +78  +1 per battle won (LAB_005C), +3 for the guardian (LAB_00A0); foundation "uwProgression"
	int16_t  swHp;             // +80  current HP (TST.W / BGT in LAB_000E); $FFFF marks dead (LAB_000D)
	uint8_t  ubFrogDays;       // +82  frog-curse countdown: skips the turn while non-zero (LAB_0058, LAB_0DBA); LAB_045E sets 3
	uint8_t  ubRecency;        // +83  wizard-visit recency: -10 per day clamped at 0 (LAB_002B); $FF = AI sentinel (LAB_0030); LAB_045D writes $46
	int16_t  swHpMax;          // +84  LAB_0013 stores the recomputed maximum; daily regen heals (max-hp)/4|1 (LAB_002D)
	uint8_t  ubDerivedEnd;     // +86  LAB_0019; the turn budget is this << 4 (LAB_0DBE)
	uint8_t  ubSpare87;          // +87  unused
	uint32_t ulSword;          // +88  $16 long, $17 broad, $18 claymore, $19 sharpness; init $16; damage bonus +0/+2/+3/+5 in LAB_021B
	uint32_t ulArmour;         // +92  $1B padded, $1C mail, $1D plate, $1E battle; init $1B; max-HP bonus +0/+10/+20/+30 in LAB_0013
	uint32_t ulInventory;      // +96  pointer to this record's Inventory (LAB_01C4: LAB_0618 + 24*i; dragon: LAB_0619).  DOC 10.18 calls this "linked_knight": wrong
	uint32_t ulEngagedWith;    // +100 record pointer: the dragon's target knight (LAB_0DB6 compares it with the current knight); LAB_0DBB clears it at turn change; init 0
	uint8_t  ubBehaviourFlags;   // +104 bit 7 = block latch (BTST/BSET #7 in LAB_01E9); other bits used by the script handlers
	uint8_t  ubBehaviourFlags2;  // +105 bits 0 and 2 used by the script handlers
	uint8_t  ubCooldown;         // +106 AI action cooldown (high byte of the word at +106; the low byte is ubCooldownLo): SUBI.B #1, reloaded with $0A / $14 (LAB_023F..LAB_0245)
	uint8_t  ubCooldownLo;       // +107 low byte of the cooldown word (the creature steps write a whole word: cooldownWord() in fighters.cpp)
	uint32_t ulName;           // +108 pointer to the NUL-terminated knight name (LAB_01AE/LAB_01BE: LAB_06B5..06B8, LAB_08C0..08C3; copied by LAB_0059)
	uint8_t  ubSpare112[4];     // +112 unused
	uint16_t uwReachX;         // +116 horizontal reach: the AI stays put when the target is within it ($64 default, LAB_0167, LAB_0242); per-type tunable
	uint16_t uwTooCloseX;      // +118 horizontal distance under which the AI backs away ($50 default); per-type tunable
	uint16_t uwDepthReach;      // +120 depth (y) tolerance for "same lane" (4, 5 or 10); per-type tunable; see depthNear()
	uint16_t uwHitX;           // +122 collision point X (LAB_03C3 stores LAB_0A52)
	uint16_t uwHitY;           // +124 collision point Y (LAB_03C3 stores LAB_0A53)
	uint16_t uwMapX;           // +126 overworld position X: compared with the node table LAB_069F (LAB_0069), +1 per step (LAB_0DA6); DOC 10.18 "spawn_x8"
	uint16_t uwMapY;           // +128 overworld position Y; DOC 10.18 "spawn_y8"
	uint8_t  ubLifeLoss;       // +130 non-zero makes LAB_0030 subtract one life; LAB_01EF sets 1; LAB_01C6 clears it
	uint8_t  ubSpare131;         // +131 unused
};
static_assert(sizeof(Knight) == 132, "Knight is $84 bytes (LAB_0613 DS.L 33)");
static_assert(offsetof(Knight, ulActive) == 0, "");
static_assert(offsetof(Knight, uwX) == 4, "");
static_assert(offsetof(Knight, uwHeight) == 6, "");
static_assert(offsetof(Knight, uwY) == 8, "");
static_assert(offsetof(Knight, ubFacing) == 10, "");
static_assert(offsetof(Knight, ubInputPort) == 11, "");
static_assert(offsetof(Knight, ubAnimPhase) == 12, "");
static_assert(offsetof(Knight, ubAnimTimer) == 13, "");
static_assert(offsetof(Knight, ulHitTarget) == 14, "");
static_assert(offsetof(Knight, ulAttacker) == 18, "");
static_assert(offsetof(Knight, ulIdleScript) == 22, "");
static_assert(offsetof(Knight, ulScript26) == 26, "");
static_assert(offsetof(Knight, ulHurtScripts) == 30, "");
static_assert(offsetof(Knight, ulActionScripts) == 34, "");
static_assert(offsetof(Knight, ulJobParam) == 38, "");
static_assert(offsetof(Knight, ulDamageTable) == 42, "");
static_assert(offsetof(Knight, ulWalkScripts) == 46, "");
static_assert(offsetof(Knight, ulDefenseTable) == 50, "");
static_assert(offsetof(Knight, ulKind) == 54, "");
static_assert(offsetof(Knight, ulSpare58) == 58, "");
static_assert(offsetof(Knight, uwInput) == 62, "");
static_assert(offsetof(Knight, uwAction) == 64, "");
static_assert(offsetof(Knight, uwTileX) == 66, "");
static_assert(offsetof(Knight, uwTileY) == 68, "");
static_assert(offsetof(Knight, ubStrength) == 70, "");
static_assert(offsetof(Knight, ubConstitution) == 71, "");
static_assert(offsetof(Knight, ubEndurance) == 72, "");
static_assert(offsetof(Knight, ubLives) == 73, "");
static_assert(offsetof(Knight, uwGold) == 74, "");
static_assert(offsetof(Knight, ubDaggers) == 76, "");
static_assert(offsetof(Knight, ubType) == 77, "");
static_assert(offsetof(Knight, uwProgress) == 78, "");
static_assert(offsetof(Knight, swHp) == 80, "");
static_assert(offsetof(Knight, ubFrogDays) == 82, "");
static_assert(offsetof(Knight, ubRecency) == 83, "");
static_assert(offsetof(Knight, swHpMax) == 84, "");
static_assert(offsetof(Knight, ubDerivedEnd) == 86, "");
static_assert(offsetof(Knight, ubSpare87) == 87, "");
static_assert(offsetof(Knight, ulSword) == 88, "");
static_assert(offsetof(Knight, ulArmour) == 92, "");
static_assert(offsetof(Knight, ulInventory) == 96, "");
static_assert(offsetof(Knight, ulEngagedWith) == 100, "");
static_assert(offsetof(Knight, ubBehaviourFlags) == 104, "");
static_assert(offsetof(Knight, ubBehaviourFlags2) == 105, "");
static_assert(offsetof(Knight, ubCooldown) == 106, "");
static_assert(offsetof(Knight, ubCooldownLo) == 107, "");
static_assert(offsetof(Knight, ulName) == 108, "");
static_assert(offsetof(Knight, ubSpare112) == 112, "");
static_assert(offsetof(Knight, uwReachX) == 116, "");
static_assert(offsetof(Knight, uwTooCloseX) == 118, "");
static_assert(offsetof(Knight, uwDepthReach) == 120, "");
static_assert(offsetof(Knight, uwHitX) == 122, "");
static_assert(offsetof(Knight, uwHitY) == 124, "");
static_assert(offsetof(Knight, uwMapX) == 126, "");
static_assert(offsetof(Knight, uwMapY) == 128, "");
static_assert(offsetof(Knight, ubLifeLoss) == 130, "");
static_assert(offsetof(Knight, ubSpare131) == 131, "");

// The dragon is record 4 of the same array; the alias only documents intent at call sites.
typedef Knight Dragon;

#pragma pack(pop)

}}  // namespace ms::game
