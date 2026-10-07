// Moonstone game state: fight pair / moon clock, lair (encounter) records, map nodes, sprite jobs, scalar slots
// (ROADMAP 5.1).  Same conventions as knight.hpp: asm authority mog.asm, big-endian 68k, #pragma pack(2), pointers
// stored as uint32_t, Spare<offset> for bytes the asm never touches, evidence = routine labels.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ms { namespace game {

#pragma pack(push, 2)

// ---------------------------------------------------------------------------------------------------------
// ActiveKnights: BSS LAB_05E4 (DS.L 5 + DS.W 1 = 22 bytes).  The two knights of the fight / turn in progress and
// the lunar clock.  LAB_05E4 is also the base of the "active knight list" in the foundation notes.
struct ActiveKnights {
	uint32_t ulCurrent;        // +0   Knight* acting / first fighter (LAB_01AE, LAB_0DBD store LAB_0633 here; LAB_000E reads it)
	uint32_t ulOpponent;       // +4   Knight* second fighter (LAB_004F, LAB_000E: MOVEA.L 4(A0),A1)
	uint8_t  ubFightActive;    // +8   1 while the fight loop LAB_0036/LAB_0037 runs; cleared when a fighter dies (LAB_004B, LAB_0005)
	uint8_t  ubSpare9;           // +9   unused
	uint32_t ulSpriteBank;        // +10  receives LAB_05E3[0] (LAB_058A, LAB_045E): the sprite / cel bank the menus draw with (MENUBANK_BACKDROP); meaning inferred from scene_menu.cpp
	uint16_t uwPlayerCount;    // +14  copy of LAB_05C5 (LAB_00B9) / forced 2 (LAB_0002)
	uint8_t  ubEndTimer;       // +16  post-fight delay: SUBI.B #1,16(A2) in LAB_0036; set to $23 / $32 (LAB_0006, LAB_004B)
	uint8_t  ubSpare17;          // +17
	uint16_t uwMoonFrame;      // +18  lunar frame, 45..49: init $2D (LAB_01AE), reloaded from the table LAB_06C2 (LAB_0029); tested against $2D/$2E/$31 (LAB_021F, LAB_00A1)
	uint16_t uwMoonSubcount;   // +20  sub-counter 0..3: ADDI.W #1,20(A0), wrap at >3 advances the day (LAB_0029)
};
static_assert(sizeof(ActiveKnights) == 22, "LAB_05E4: DS.L 5 + DS.W 1");
static_assert(offsetof(ActiveKnights, ulCurrent) == 0, "");
static_assert(offsetof(ActiveKnights, ulOpponent) == 4, "");
static_assert(offsetof(ActiveKnights, ubFightActive) == 8, "");
static_assert(offsetof(ActiveKnights, ulSpriteBank) == 10, "");
static_assert(offsetof(ActiveKnights, uwPlayerCount) == 14, "");
static_assert(offsetof(ActiveKnights, ubEndTimer) == 16, "");
static_assert(offsetof(ActiveKnights, uwMoonFrame) == 18, "");
static_assert(offsetof(ActiveKnights, uwMoonSubcount) == 20, "");

// ---------------------------------------------------------------------------------------------------------
// Lair: the 24 overworld encounter / location records, 20 bytes each.  Heap resident (LAB_05B9[17], filled by
// LAB_01B3 and LAB_01B6); LAB_08C6 points at the current one and advances by $14 (LAB_01B5, LAB_0DA3, LAB_0E04).
// The loot of record i is the 24-byte Inventory at LAB_05B9[18] + 24*i (LAB_01B3).
struct Lair {
	uint32_t ulLoot;           // +0   Inventory* of the loot (LAB_01B3: MOVE.L A1,0(A0), A1 += $18); LAB_005F scans its 24 bytes for "anything left"
	uint16_t uwHandlerOfs;     // +4   high word of LAB_07BD[i]: byte offset into the LAB_08C8 handler table (LAB_0321: MOVE.W 4(A0),D0; 0(A1,D0.L))
	uint16_t uwCreaturesLeft;   // +6   LAB_003A stores LAB_05EC (the remaining creature count) here after a creature fight
	uint16_t uwFlag8;          // +8   cleared at init (LAB_01B3); LAB_005F: non-zero keeps the lair "occupied"
	int16_t  swMapX;           // +10  drawn position (LAB_01B6 from LAB_07BE); LAB_005F stores -1 here to hide the lair, LAB_0DA4 skips it when negative
	int16_t  swMapY;           // +12  drawn position (LAB_01B6 from LAB_07BE, second word)
	uint16_t uwParam14;        // +14  LAB_07BF[i]; LAB_0321 copies it to LAB_08C4
	uint32_t ulParam16;        // +16  LAB_07C0[i]; LAB_0321 copies it to LAB_076E
};
static_assert(sizeof(Lair) == 20, "LAB_08C6 advances by $14");
static_assert(offsetof(Lair, ulLoot) == 0, "");
static_assert(offsetof(Lair, uwHandlerOfs) == 4, "");
static_assert(offsetof(Lair, uwCreaturesLeft) == 6, "");
static_assert(offsetof(Lair, uwFlag8) == 8, "");
static_assert(offsetof(Lair, swMapX) == 10, "");
static_assert(offsetof(Lair, swMapY) == 12, "");
static_assert(offsetof(Lair, uwParam14) == 14, "");
static_assert(offsetof(Lair, ulParam16) == 16, "");

// ---------------------------------------------------------------------------------------------------------
// MapNode: overworld location table LAB_069F, S_2 DATA, 9 entries + $FFFF terminator, 6 bytes each (LAB_0069 reads
// three words per entry; negative id ends the walk).  Ids $15..$18 are the four home villages: LAB_006C..LAB_006E
// only accept $15 for Knight +54 == 0, $16 for 1, $17 for 2, $18 for 3.
struct MapNode {
	int16_t  swId;             // +0   location id ($15.. in the table, -1 terminator)
	uint16_t uwX;              // +2   box tested against Knight +126 (LAB_0067: D1 and the sprite width LAB_05B1)
	uint16_t uwY;              // +4   box tested against Knight +128 (LAB_0067: D2)
};
static_assert(sizeof(MapNode) == 6, "");
static_assert(offsetof(MapNode, uwX) == 2, "");
static_assert(offsetof(MapNode, uwY) == 4, "");

// ---------------------------------------------------------------------------------------------------------
// Job: sprite/animation job table LAB_0649, 10 entries of $32 bytes (LAB_0310 searches with ADDA.L #$32 and
// D7=9).  A job runs one script for one record.  36-byte work areas live in LAB_064B (360 bytes), 80-byte
// buffer pairs in LAB_064F / LAB_0650 (LAB_0308 / LAB_0309).
struct Job {
	uint8_t  ubActive;         // +0   LAB_0310 takes the first entry with 0 here and sets it to 1; LAB_031B clears it
	uint8_t  ubRunning;        // +1   set 1 by LAB_0310/LAB_030D, 0 by LAB_030A
	uint32_t ulScript;         // +2   script start pointer (A0 of LAB_0310)
	uint16_t uwX;              // +6   D0 of LAB_0310 = Knight +4
	uint16_t uwHeight;         // +8   D1 of LAB_0310 = Knight +6 (the owner's height)
	uint16_t uwY;              // +10  D2 of LAB_0310 = Knight +8
	uint8_t  ubDrawState[10];  // +12  screen x/y and size of the last draw (CombatJob::uwSx..uwH)
	uint8_t  ubFacing;         // +22  D3 of LAB_0310 = Knight +10
	uint8_t  ubSpare23;          // +23
	uint32_t ulOwner;          // +24  record pointer (A1): LAB_0315 searches the table by it; LAB_031B clears its first long
	uint32_t ulParam;          // +28  A2 of LAB_0310 = Knight +38
	uint8_t  ubType;           // +32  D5 of LAB_0310 = Knight +77
	uint8_t  ubSpare33[3];       // +33
	uint32_t ulWork;           // +36  pointer to this job's 36-byte work area (LAB_0309); LAB_0310 clears 36 bytes through it
	uint32_t ulBuf40;          // +40  pointer to an 80-byte buffer in LAB_064F (LAB_0308)
	uint32_t ulBuf44;          // +44  pointer to an 80-byte buffer in LAB_0650 (LAB_0308)
	uint16_t uwPaused;         // +48  EORI.W #1 toggles it (LAB_0319)
};
static_assert(sizeof(Job) == 50, "LAB_0649 DS.L 125 = 10 x $32");
static_assert(offsetof(Job, ubActive) == 0, "");
static_assert(offsetof(Job, ubRunning) == 1, "");
static_assert(offsetof(Job, ulScript) == 2, "");
static_assert(offsetof(Job, uwX) == 6, "");
static_assert(offsetof(Job, uwHeight) == 8, "");
static_assert(offsetof(Job, uwY) == 10, "");
static_assert(offsetof(Job, ubDrawState) == 12, "");
static_assert(offsetof(Job, ubFacing) == 22, "");
static_assert(offsetof(Job, ubSpare23) == 23, "");
static_assert(offsetof(Job, ulOwner) == 24, "");
static_assert(offsetof(Job, ulParam) == 28, "");
static_assert(offsetof(Job, ubType) == 32, "");
static_assert(offsetof(Job, ubSpare33) == 33, "");
static_assert(offsetof(Job, ulWork) == 36, "");
static_assert(offsetof(Job, ulBuf40) == 40, "");
static_assert(offsetof(Job, ulBuf44) == 44, "");
static_assert(offsetof(Job, uwPaused) == 48, "");

// ---------------------------------------------------------------------------------------------------------
// Scalar slots.  Several single globals are reserved as DS.L / DS.W but the code only ever touches the first
// byte or word (big-endian: the label address is the HIGH half).  These wrappers keep the declared size equal to
// the DS size so the layout test can check it.
struct WordSlot {              // DS.L 1, accessed only as .W at the label (LAB_0665)
	uint16_t uw;
	uint16_t uwSpare;
};
struct ByteSlot {              // DS.W 1, accessed as .B at the label (LAB_05DC, LAB_05DF)
	uint8_t ub;
	uint8_t ubSpare;
};
static_assert(sizeof(WordSlot) == 4 && sizeof(ByteSlot) == 2, "");

#pragma pack(pop)

}}  // namespace ms::game
