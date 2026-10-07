// game/combat_load - mog's combat data loading path in C++ (ROADMAP 7.1f2): which files an arena / creature kind / screen needs
// and where in the arenas they go.  The original is mog S_0 LAB_00F8 .. LAB_0155 (the group `mog.combat_load` of
// docs/ASM_REMAINING.md): 26 asm routines that are almost all straight-line `load file N at the running pointer, advance by its
// size, remember the pointer in a slot`.  Pure on purpose, like combat.hpp / overworld.hpp: no ACE, no OS, no globals.  It works
// on the 68k address space of the game through `Mem` and on the asm primitives through `Ops`; src/rt/combat_load.cpp wires
// both to the real game (direct pointers, the file layer of rt/files, one trampoline per asm primitive) and
// tests/test_combat_load.py runs every routine here against the ORIGINAL asm in unicorn (the primitives replaced by
// logging stubs): same calls, same arguments, same memory.
//
// The data model of the original (all in mog S_1 BSS unless noted):
//   LAB_05B8[]   pointer table of the CHIP/FAST arena carve-up (LAB_0004): [1] = knight cel arena, [2] = creature cel arena
//   LAB_05B9[]   second pointer table (25 longs): the nine arena pictures of the pack "Test" ([2] [3] [5] [4] [6] [8] [9] [7],
//                [23]), the raw message.piv / ch.piv ([13] / [14]), cel buffers ([10] [11] [12]), collide.hit text ([21])
//   LAB_05E0..05E3  four arrays of five longs, the "cel slots": where the cel set of a creature / knight / font ended up.  The
//                fighters and the combat screens read them.  05E0 = the creature sets, 05E1 = the knights, 05E2 = the
//                map / practice sets, 05E3 = fonts and the moon
//   LAB_05DF     which creature kind the slots currently hold ($04 mudmen ... $40 trolls, $FF none): a load is skipped when the
//                kind is already there
//   LAB_05C0..05C2  three screen-sized work buffers; LAB_0D92 / SECSTRT_35 the two live screens
// The files are named by the label of their NUL-terminated name in S_4 (kn1.ob, He1.ob, Dragon1.cel, ...); the label numbers
// below are the original's, SECSTRT_n = 0x8000 + n (as in game/mainloop.hpp).
//
// Where the original did a loader call it is an Ops entry named after the asm label it stands for.  Calls to LAB_0100 (the
// "insert disk" prompt, patched to a bare RTS since ROADMAP 2.7: all files live in one tree) are not made.  The nine arena
// pictures are copied out of the pack with DBF loops whose counts the enhanced display patches (rt_enh_pic_sz, rt_enh_raw_*):
// they are `Sizes` here and the copy is `(count & $FFFF) + 1` bytes, like the DBF.
#pragma once
#include <stdint.h>

namespace ms { namespace game { namespace cl {

// X(name, label-number-without-0x).  Everything the routines address.
#define MS_CL_LABELS(X) \
	X(HEAP8, 05B8) X(HEAP9, 05B9) X(HEAP_05BB, 05BB) X(BUF_A, 05C0) X(BUF_B, 05C1) X(BUF_C, 05C2) X(PAL_05B7, 05B7) \
	X(MODE_05DD, 05DD) X(MODE_05DE, 05DE) X(MODE, 05DF) X(SLOTS0, 05E0) X(SLOTS1, 05E1) X(SLOTS2, 05E2) X(SLOTS3, 05E3) \
	X(ACTIVE, 05E4) X(PAL_05E5, 05E5) X(PAL_05E6, 05E6) X(TMP_05E7, 05E7) X(CYCLE_05E8, 05E8) X(CYCLE_05E9, 05E9) \
	X(CYCLE_05EA, 05EA) X(CYCLE_05EB, 05EB) X(TMP_0632, 0632) X(CELS_0664, 0664) X(PIC_NEXT, 071D) X(PIC_TABLE, 071E) \
	X(CELBASE_0705, 0705) X(PICBASE_0704, 0704) X(SCRIPT_0713, 0713) X(SCENE_076D, 076D) X(PARAM_076E, 076E) \
	X(CYCLE_TAB_B6, 07B6) X(CYCLE_TAB_B7, 07B7) X(CYCLE_TAB_B8, 07B8) X(CYCLE_TAB_B9, 07B9) X(ARENA_08C4, 08C4) \
	X(HIT_0A4D, 0A4D) X(HIT_0A4E, 0A4E) X(HIT_0A4F, 0A4F) X(HIT_0A50, 0A50) X(FLAG_0D05, 0D05) X(PALETTE, 0D2B) \
	X(SCREEN_B, 0D92) X(DRIVES, 05D0) X(DISK_UNIT0, 05CC) X(DISK_UNIT1, 05CD) X(DISK_UNIT2, 05CE) X(DISK_UNIT3, 05CF) \
	X(UNIT_DISK1, 06FE) X(UNIT_DISK2, 06FF) X(UNIT_DISK3, 0700) X(UNIT_DISK4, 0701) X(UNIT_SEL, 0B35) X(DISK_FLAG, 0D4D) \
	X(TBL_05F5, 05F5) X(TBL_05F6, 05F6) X(TBL_05F7, 05F7) X(TBL_05F8, 05F8) X(TBL_060C, 060C) X(TBL_0610, 0610) \
	/* texts (record lists of LAB_0432) */ \
	X(TEXT_0706, 0706) X(TEXT_0709, 0709) X(TEXT_070F, 070F) X(TEXT_071B, 071B) X(TEXT_071C, 071C) \
	/* file names (S_4 / S_0) */ \
	X(N_KN1, 076F) X(N_KN2, 0770) X(N_KN3, 0771) X(N_KN4, 0772) X(N_KN5, 0773) X(N_BLO, 0774) X(N_HE1, 0775) \
	X(N_HE2, 0776) X(N_HE3, 0777) X(N_TROGGAXE1, 0778) X(N_TROGGAXE2, 0779) X(N_TROGGSPEAR1, 077A) \
	X(N_TROGGSPEAR2, 077B) X(N_RATMEN1, 077C) X(N_RATMEN2, 077D) X(N_BE1, 077E) X(N_BE2, 077F) X(N_DRAGON1, 0780) \
	X(N_DRAGON2, 0781) X(N_MUDMEN1, 0782) X(N_MUDMEN2, 0783) X(N_KI, 0784) X(N_BALOK1, 0785) X(N_BALOK3, 0786) \
	X(N_BALOK2, 0787) X(N_WI1C, 0788) X(N_WI1P, 0789) X(N_WI2P, 078A) X(N_BOLD, 078B) X(N_SMALL, 078C) \
	X(N_SEL, 07AD) X(N_MESSAGE, 07AE) X(N_CH, 07AF) X(N_DEMON1, 07B0) X(N_DEMON2, 07B1) X(N_DEMON3, 07B2) \
	X(N_DEMON4, 07B3) X(N_TROLL1, 07B4) X(N_TROLL2, 07B5) X(N_MI, 070E) X(N_HIGHWOOD, 0715) X(N_WATERDEEP, 0716) \
	X(N_DRAGON5, 0122) X(N_PACK, 013B) \
	/* data the script tables of LAB_0155 point at */ \
	X(D_07DB, 07DB) X(D_07DC, 07DC) X(D_07DD, 07DD) X(D_07DE, 07DE) X(D_07DF, 07DF) X(D_07E0, 07E0) X(D_07E1, 07E1) \
	X(D_07E2, 07E2) X(D_07E3, 07E3) X(D_07E4, 07E4) X(D_07E5, 07E5) X(D_07E6, 07E6) X(D_07E7, 07E7) X(D_07E8, 07E8) \
	X(D_07E9, 07E9) X(D_07EA, 07EA) X(D_07ED, 07ED) X(D_07EE, 07EE) X(D_07EF, 07EF) X(D_07F1, 07F1) X(D_07F3, 07F3) \
	X(D_07F4, 07F4) X(D_07F5, 07F5) X(D_07F6, 07F6)
// Section starts (SECSTRT_n): X(name, n).
#define MS_CL_SECTIONS(X) X(SCREEN_A, 35)

enum Label : uint16_t {
#define MS_CL_ENUM(n, l) n = 0x##l,
	MS_CL_LABELS(MS_CL_ENUM)
#undef MS_CL_ENUM
#define MS_CL_ENUM(n, s) n = 0x8000 + s,
	MS_CL_SECTIONS(MS_CL_ENUM)
#undef MS_CL_ENUM
};

// Fixed sizes of the original's `mog_LAB_0426 + 2` target, music entries and the like are Ops, not labels.  The music entries:
enum Music : uint16_t {
	MUSIC_CAMPAIGN = 0x0AA7,  // LAB_0AA7 start of the combat module / reset
	MUSIC_KNIGHTS = 0x0AAA,   // LAB_0AAA  (loads the knights' arena module)
	MUSIC_TROGG_SPEAR = 0x0AB1,
	MUSIC_RATMEN = 0x0AB2,
	MUSIC_MUDMEN = 0x0AB3,
	MUSIC_BALOK = 0x0AAB,
	MUSIC_DRAGON = 0x0AAC,
	MUSIC_BE = 0x0AAD,
	MUSIC_DEMON = 0x0AAE,
	MUSIC_TROLL = 0x0AAF,
	MUSIC_WIZARD = 0x0AB4,
};

// The 68k address space: the game's own cells and buffers.  rd/wr are big-endian, any (even) alignment; copy is a forward byte copy, fill a byte fill.
struct Mem {
	uint8_t (*rd8)(uint32_t ulAddr);
	uint16_t (*rd16)(uint32_t ulAddr);
	uint32_t (*rd32)(uint32_t ulAddr);
	void (*wr8)(uint32_t ulAddr, uint8_t ub);
	void (*wr16)(uint32_t ulAddr, uint16_t uw);
	void (*wr32)(uint32_t ulAddr, uint32_t ul);
	void (*copy)(uint32_t ulDst, uint32_t ulSrc, uint32_t ulCount);
	void (*fill)(uint32_t ulDst, uint8_t ub, uint32_t ulCount);
};

// The DBF counts / read sizes the enhanced display changes (rt_enh_raw_mog_*, rt_enh_pic_sz).  Original values in the comments.
struct Sizes {
	uint32_t ulRawMessage;   // 3694 ($0E6E): message.piv read and copy
	uint32_t ulRawChar;      // 9718 ($25F6): ch.piv read and copy
	uint32_t ulRawPack;      // 198745 ($30859): the pack "Test"
	uint32_t aulPic[9];      // DBF counts (size - 1) of the pack pictures [0..8]: 5957 5148 4657 3A54 6394 51C3 4C09 51A4 8A02
};

// The asm primitives.  Each comment names the original routine and its register contract.
struct Ops {
	uint32_t (*celSize)(uint16_t uwName);                         // LAB_0CB6, A0 = name -> D0 = bytes the cel set needs
	void (*celLoad)(uint16_t uwName, uint32_t ulDest);            // LAB_0CBB, A0 = name, A1 = destination
	void (*hitLoad)(uint16_t uwName, uint32_t ulDest);            // LAB_03CE, A0 = name, A1 = the cel set's destination
	void (*fileOpen)(uint16_t uwName);                            // LAB_0BB5
	void (*fileRead)(uint32_t ulDest, uint32_t ulCount);          // LAB_0BD7
	void (*fileClose)();                                          // LAB_0BFF
	void (*packDone)();                                           // rt_mog_pack_done: the close that ends the pack read (+ the 6-plane walk)
	void (*planes)(uint32_t ulBase);                              // LAB_0426+2, D0 = base of five planes (draw target)
	void (*unpack)(uint32_t ulPicture);                           // LAB_0C21, A0 = picture file image in memory
	void (*pictureLoad)(uint16_t uwName, uint32_t ulBuffer);      // LAB_0C27, A0 = name, A1 = scratch buffer
	void (*palette)(uint32_t ulPalette);                          // LAB_03F2, A0 = 32 colour words
	void (*blank)();                                              // LAB_03EB: palette to black
	void (*fadeOut)();                                            // LAB_03F0
	void (*clear)(uint32_t ulScreen);                             // LAB_0D72, A0 = screen
	void (*copyScreen)(uint32_t ulSrc, uint32_t ulDst);           // LAB_0419, A0 = source, A1 = destination
	void (*copyScreens)();                                        // LAB_0418
	void (*text)(uint32_t ulList);                                // LAB_0432, A0 = record list
	void (*drawCel)(uint32_t ulSet, uint16_t uwCel, uint16_t uwX, uint16_t uwY);   // LAB_0CDA, A0 = set, D0 = cel, D1 = x, D2 = y
	void (*synth)(uint16_t uwSeq, uint16_t uwChannel);            // LAB_0F8C, D0 = sequence, D1 = channel
	void (*music)(uint16_t uwEntry);                              // Music: LAB_0AA7 / LAB_0AAA..LAB_0AB4
	void (*backdrop)(uint32_t ulScript);                          // LAB_0A6D, A0 = script / table entry
	void (*backdropReset)();                                      // LAB_0A6C
	void (*hunk9)();                                              // SECSTRT_9: the tail jump of LAB_012D (never returns in the original)
};

struct Env {
	Mem m;
	Ops o;
	uint32_t (*addr)(uint16_t uwLabel);   // the 68k address of a Label
	Sizes sz;
};

// ---- LAB_00F8 ---------------------------------------------------------------------------------------------------------------
// Drive set-up: with the drive probe (LAB_0B18) patched to "no extra drive" the original ends with these cells and nothing else:
// the drive count LAB_05D0 = 1, the unit table LAB_05CC.. / LAB_06FE.. (disk A on unit 0), LAB_0B35 = 0 (selected unit),
// LAB_0D4D = 1 (the loader's "disk present" flag).  Written for the readers that remain (the dead prompt code).
void driveInit(const Env &e);

// ---- the knights and the creature sets (the cel slots) -----------------------------------------------------------------------
// LAB_0115: kn1.ob, kn2.ob, kn3.ob into LAB_05E1[0..2] (knight arena LAB_05B8[1]), kn4's hit set, blo.cel into LAB_05BB; the hit
// cursors LAB_0A4D/0A4E are saved to LAB_0A4F/0A50; music LAB_0AAA.
void loadKnights(const Env &e);
// The creature sets, each into LAB_05E0[] at LAB_05B8[2] (the creature arena) and tagged in LAB_05DF; skipped when the tag is
// already there (except mudmen, dragon, demons: always).  `LAB_0116 He` also copies the knights' slots 3/4.
// The creature loaders take the sound bank to start (a MUSIC_* event) as a second argument; the default is the creature's own.
void loadHe(const Env &e);           // LAB_0116  He1-3.ob            kind $0C, (kept: the 0100 disk prompt is a no-op)
void loadTroggSpear(const Env &e, uint16_t uwMusic = MUSIC_TROGG_SPEAR);   // LAB_0118  TroggSpear1/2.cel   kind $20, music LAB_0AB1
void loadTroggAxe(const Env &e, uint16_t uwMusic = MUSIC_TROGG_SPEAR);     // LAB_011A  TroggAxe1/2.cel     kind $18, music LAB_0AB1
void loadRatmen(const Env &e, uint16_t uwMusic = MUSIC_RATMEN);       // LAB_011C  Ratmen1/2.cel       kind $24, music LAB_0AB2
void loadMudmen(const Env &e, uint16_t uwMusic = MUSIC_MUDMEN);       // LAB_011E  Mudmen1/2.cel       kind $04, music LAB_0AB3
void loadBalok(const Env &e, uint16_t uwMusic = MUSIC_BALOK);        // LAB_011F  Balok1/3/2.cel + Kn5.ob  kind $30, music LAB_0AAB
void loadDragon(const Env &e, uint16_t uwMusic = MUSIC_DRAGON);       // LAB_0121  Dragon1/2/5.cel + Kn5.ob  kind $14, music LAB_0AAC
void loadBe(const Env &e, uint16_t uwMusic = MUSIC_BE);           // LAB_0123  be1.c, be2.c        kind $00, music LAB_0AAD
void loadDemon(const Env &e, uint16_t uwMusic = MUSIC_DEMON);        // LAB_0125  Demon1-4.cel        kind $08, music LAB_0AAE
void loadTroll(const Env &e, uint16_t uwMusic = MUSIC_TROLL);        // LAB_0126  Troll1/2.cel + Kn5.ob  kind $40, music LAB_0AAF
void loadKiMi(const Env &e);         // LAB_0128  ki.cel, mi.c into LAB_05E2[]
void loadWizard(const Env &e);       // LAB_0131  wi1.c, wi1.p, wi2.p screens, kind $3C, music LAB_0AB4

// ---- screens and assets --------------------------------------------------------------------------------------------------------
// LAB_0129: ch.piv (raw, LAB_05B9[14]) as the screen.  LAB_0138: message.piv (raw, LAB_05B9[13]) as the screen.
void screenFromChar(const Env &e);
void screenFromMessage(const Env &e);
// LAB_012B: the moon (LAB_05E2[1] cel LAB_05E4+18) over ch.piv.
void drawMoon(const Env &e);
// LAB_012C combat_assets_load: message.piv + ch.piv raw, bold.f and Small.font into LAB_05E3[], the three corner cels, music LAB_0AA7.
void loadAssets(const Env &e);
// LAB_012D: Sel.cel, ch.piv screen, two screen copies, then SECSTRT_9 (Ops::hunk9, a tail jump: nothing after it runs).
void selectScreen(const Env &e);
// LAB_012E / LAB_012F: the place pictures HighWood.piv / WaterDeep.piv.
void placeHighWood(const Env &e);
void placeWaterDeep(const Env &e);
// LAB_0133: four synth starts (sequences $6E..$71 on channels 0..3).
void startSynths(const Env &e);
// LAB_0134: synths, message.piv screen, the next text of the table LAB_071E (cycles 14 entries), palette.
void messageNext(const Env &e);
// LAB_0136 / LAB_0137: message screen with the record list ulList (LAB_0137 additionally rewrites palette entries 1..6).
void messageText(const Env &e, uint32_t ulList);
void messageTextRecoloured(const Env &e, uint32_t ulList);
// LAB_013A: clear the cycle counters, mark every kind "none" ($FF), read the pack "Test" (nine arena pictures) into LAB_05B9[2].
void loadPack(const Env &e);
// LAB_013C: the arena picture by LAB_08C4 (0 / 4 / 8 / $C; anything else does nothing) and the backdrop script.
void arenaPicture(const Env &e);
// LAB_0152 / LAB_0155: clear / fill the fighter script tables LAB_05F5.. LAB_0610.
void clearTables(const Env &e);
void fillTables(const Env &e);

}}}  // namespace ms::game::cl
