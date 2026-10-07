// rt/combat_load - the wiring of src/game/combat_load.cpp (ROADMAP 7.1f2) to the asm game: the game's address space (direct
// pointers), one thin wrapper per asm primitive the loaders call, the file layer called directly (rt/files: no LAB_0BB5 /
// LAB_0BD7 / LAB_0BFF stubs on this path any more), and the C entries of the 26 original routines of mog S_0 LAB_00F8..LAB_0155.
// 
//
// Entries (ROADMAP 7.1o): the 25 stubs LAB_00F8 / 0115..0155 are gone.  The C++ callers (src/rt/{mainloop,arena,combat,combat_ui,
// overworld,screens}.cpp) call the C functions rtCl* below directly (declared in rt/combat_load.hpp); the originals took their
// input in A0 only for LAB_0136 / LAB_0137 (the text record list), which is the argument of rtClMessageText / rtClMessageRecoloured.
// ROADMAP 7.1q: LAB_012D's patch is gone too (the title step is C++, src/rt/hunk9.cpp: rtTitleStep calls rtClSelect and then does the hunk-9
// return chain itself).  The register-saving asm shims rt_cl_* of the removed patches live on in tests/combat_load_emu_support.cpp, where
// tests/test_combat_load.py still runs every routine against the original asm in unicorn.

#include <stdint.h>

#include <ace/types.h>

#include "game/api/data.hpp"
#include "game/combat_load.hpp"
#include "rt/combat_load.hpp"
#include "rt/files.hpp"
#include "rt/soundbank.hpp"
#include "rt/display_ops.hpp"
#include "rt/stubfn.h"

using namespace ms::game::cl;

// ---- the game's symbols ----------------------------------------------------------------------------------------------------
// Every label of MS_CL_LABELS / MS_CL_SECTIONS (include/game/combat_load.hpp), declared here with the original symbol (tests/test_combat_load.py
// checks that this list and the header list are the same).  Plain mog_ names, like the other rt files, so tools/asm_remaining.py sees the uses.
extern "C" {
extern uint8_t mogBufTable[], mogHeapTable[], mogSetValue[], mogBackground[], mogForeground[], mogPicScreen[];  // LAB_05B8, LAB_05B9, LAB_05BB, LAB_05C0, LAB_05C1, LAB_05C2
extern uint8_t mogTownPalette[], mogCycleLast0[], mogCycleLast1[], mogLocationMode[], mogCelSlotsCreature[], mogCelSlotsKnight[];  // LAB_05DF (LAB_05B7, LAB_05DD, LAB_05DE, LAB_05E0, LAB_05E1)
extern uint8_t mogCelSlotsMap[], mogSmallFont[], mogActive[], mogUiPalette[], mogPicturePalette[], mogCyclePtr[];  // LAB_05E3, LAB_05E4 (LAB_05E2, LAB_05E5, LAB_05E6, LAB_05E7)
extern uint8_t mogCycleStep0[], mogCycleStep1[], mogCycleStep2[], mogCycleStep3[], mogUiStatKnight[], mogMapCelTable[];  // LAB_0632, LAB_0664 (LAB_05E8, LAB_05E9, LAB_05EA, LAB_05EB)
extern uint8_t mogPicNext[], mogPicTable[], mogCelBase[], mogPicBase[], mogPicAnimScript[], mogEncounterKind[];  // LAB_071D, LAB_071E, LAB_0705, LAB_0704, LAB_0713, LAB_076D
extern uint8_t mogLairParam[], mogCycleTab0[], mogCycleTab1[], mogCycleTab2[], mogCycleTab3[], mogArenaRegion[];  // LAB_076E, LAB_08C4 (LAB_07B6, LAB_07B7, LAB_07B8, LAB_07B9)
extern uint8_t mogHitData[], mogHitPairs[], mogHitDataBase[], mogHitPairsBase[], mogTextFlag[], mogPicPalette[];  // LAB_0A4D, LAB_0A4E, LAB_0A4F, LAB_0A50, LAB_0D05, LAB_0D2B
extern uint8_t mogDrawScreen[], mogDrives[], mogDiskUnit0[], mogDiskUnit1[], mogDiskUnit2[], mogDiskUnit3[];  // LAB_0D92 (LAB_05D0, LAB_05CC, LAB_05CD, LAB_05CE, LAB_05CF)
extern uint8_t mogUnitDisk1[], mogUnitDisk2[], mogUnitDisk3[], mogUnitDisk4[], mogUnitSel[], mogDiskFlag[];  // LAB_06FE, LAB_06FF, LAB_0700, LAB_0701, LAB_0B35, LAB_0D4D
extern uint8_t mogKnightActionScripts[], mogKnightHurtScripts[], mogKnightDamageTable[], mogKnightDefenseTable[], mogTroggAxeWalkScripts[], mogKnightWalkScripts[];  // LAB_05F5, LAB_05F6, LAB_05F7, LAB_05F8, LAB_060C, LAB_0610
extern uint8_t mogTextListCreatedBy[], mogTextListNextDay[], mogTextListWelcome[], mogTextHighwood[], mogTextWaterdeep[], mogFileKn1Ob[];  // LAB_0706, LAB_0709, LAB_070F, LAB_071B, LAB_071C, LAB_076F
extern uint8_t mogFileKn2Ob[], mogFileKn3Ob[], mogFileKn4Ob[], mogFileKn5Ob[], mogFileBloCel[], mogFileHe1Ob[];  // LAB_0770, LAB_0771, LAB_0772, LAB_0773, LAB_0774, LAB_0775
extern uint8_t mogFileHe2Ob[], mogFileHe3Ob[], mogFileTroggAxe1Cel[], mogFileTroggAxe2Cel[], mogFileTroggSpear1Cel[], mogFileTroggSpear2Cel[];  // LAB_0776, LAB_0777, LAB_0778, LAB_0779, LAB_077A, LAB_077B
extern uint8_t mogFileRatmen1Cel[], mogFileRatmen2Cel[], mogFileBe1C[], mogFileBe2C[], mogFileDragon1Cel[], mogFileDragon2Cel[];  // LAB_077C, LAB_077D, LAB_077E, LAB_077F, LAB_0780, LAB_0781
extern uint8_t mogFileMudmen1Cel[], mogFileMudmen2Cel[], mogFileKiCel[], mogFileBalok1Cel[], mogFileBalok3Cel[], mogFileBalok2Cel[];  // LAB_0782, LAB_0783, LAB_0784, LAB_0785, LAB_0786, LAB_0787
extern uint8_t mogFileWi1C[], mogFileWi1P[], mogFileWi2P[], mogFileBoldF[], mogFileSmallFont[], mogFileSelCel[];  // LAB_0788, LAB_0789, LAB_078A, LAB_078B, LAB_078C, LAB_07AD
extern uint8_t mogFileMessagePiv[], mogFileChPiv[], mogFileDemon1Cel[], mogFileDemon2Cel[], mogFileDemon3Cel[], mogFileDemon4Cel[];  // LAB_07AE, LAB_07AF, LAB_07B0, LAB_07B1, LAB_07B2, LAB_07B3
extern uint8_t mogFileTroll1Cel[], mogFileTroll2Cel[], mogFileMiC[], mogFileHighWoodPiv[], mogFileWaterDeepPiv[], mogFileDragon5[];  // LAB_07B4, LAB_07B5, LAB_070E, LAB_0715, LAB_0716, LAB_0122
extern uint8_t mogFilePackTest[], mogKnightIdleScript[], mogKnightAltScript[], mogKnightWalk00[], mogKnightWalk01[], mogKnightWalk02[];  // LAB_013B, LAB_07DB, LAB_07DC, LAB_07DD, LAB_07DE, LAB_07DF
extern uint8_t mogKnightWalk03[], mogKnightWalk10[], mogKnightWalk11[], mogKnightWalk12[], mogKnightWalk13[], mogKnightWalk20[];  // LAB_07E0, LAB_07E1, LAB_07E2, LAB_07E3, LAB_07E4, LAB_07E5
extern uint8_t mogKnightWalk21[], mogKnightWalk22[], mogKnightWalk23[], mogKnightActionScript5[], mogKnightActionScript3[], mogKnightActionScript2[];  // LAB_07E6, LAB_07E7, LAB_07E8, LAB_07E9, LAB_07EA, LAB_07ED
extern uint8_t mogKnightActionScript8[], mogKnightActionScript1[], mogKnightActionScript6[], mogKnightActionScript7[], mogKnightActionScript4[], mogKnightHurtScriptB[];  // LAB_07EE, LAB_07EF, LAB_07F1, LAB_07F3, LAB_07F4, LAB_07F5
extern uint8_t mogKnightHurtScriptA[], mogShownScreen[];  // SECSTRT_35 (LAB_07F6)

// asm primitives (called through rtClAsm)

// the enhanced display's byte counts (src/rt/enhanced.cpp)
extern ULONG rt_enh_raw_mog_msg, rt_enh_raw_mog_ch, rt_enh_raw_mog_pack;   // message.piv / ch.piv / pack byte counts
extern ULONG rt_enh_pic_sz[9];                                             // DBF counts of the nine pack pictures
// the original file layer's error word (open result), kept for the readers that remain
extern uint16_t rtClFileErr[2] asm("mogFileErr");

void rt_mog_pack_done(void);   // src/rt/enhanced.cpp: the close after the arena pack read (+ the nine picture headers at 6 planes)
void rtSfxSynth(ULONG ulSeq, ULONG ulChannel);   // src/rt/sfx.cpp: the synth start LAB_0F8C, any C caller
}

namespace {

// ---- register trampoline into the asm primitives ----------------------------------------------------------------------------------
struct AsmRegs {
	uint32_t d0, d1, d2, a0, a1, a2;
	uint32_t rd0;   // out: D0
};

}  // namespace

extern "C" void rtClAsm(uint32_t ulFn, AsmRegs *pRegs);

// rtClAsm(fn, regs): loads D0-D2/A0-A2 from regs, JSRs fn, stores D0 back; D2-D7/A2-A6 are preserved for the C++ side (the
// primitives clobber freely: LAB_0CDA uses D0-D7/A0-A5).
asm(R"(
	.text
	.globl rtClAsm
rtClAsm:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	move.l %a6,-(%sp)
	movem.l (%a6),%d0-%d2/%a0-%a2
	jsr (%a5)
	move.l (%sp)+,%a6
	move.l %d0,24(%a6)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

namespace {

inline uint32_t addrOfSym(const void *p) { return (uint32_t)(uintptr_t)p; }

uint32_t call(const void *pFn, uint32_t d0 = 0, uint32_t d1 = 0, uint32_t d2 = 0, uint32_t a0 = 0, uint32_t a1 = 0) {
	AsmRegs r = {d0, d1, d2, a0, a1, 0, 0};
	rtClAsm(addrOfSym(pFn), &r);
	return r.rd0;
}

// ---- Label -> address ---------------------------------------------------------------------------------------------------------------
uint32_t labelAddr(uint16_t uwLabel) {
	switch(uwLabel) {
		case 0x05B8: return addrOfSym(mogBufTable);
		case 0x05B9: return addrOfSym(mogHeapTable);
		case 0x05BB: return addrOfSym(mogSetValue);
		case 0x05C0: return addrOfSym(mogBackground);
		case 0x05C1: return addrOfSym(mogForeground);
		case 0x05C2: return addrOfSym(mogPicScreen);
		case 0x05B7: return addrOfSym(mogTownPalette);
		case 0x05DD: return addrOfSym(mogCycleLast0);
		case 0x05DE: return addrOfSym(mogCycleLast1);
		case 0x05DF: return addrOfSym(mogLocationMode);
		case 0x05E0: return addrOfSym(mogCelSlotsCreature);
		case 0x05E1: return addrOfSym(mogCelSlotsKnight);
		case 0x05E2: return addrOfSym(mogCelSlotsMap);
		case 0x05E3: return addrOfSym(mogSmallFont);
		case 0x05E4: return addrOfSym(mogActive);
		case 0x05E5: return addrOfSym(mogUiPalette);
		case 0x05E6: return addrOfSym(mogPicturePalette);
		case 0x05E7: return addrOfSym(mogCyclePtr);
		case 0x05E8: return addrOfSym(mogCycleStep0);
		case 0x05E9: return addrOfSym(mogCycleStep1);
		case 0x05EA: return addrOfSym(mogCycleStep2);
		case 0x05EB: return addrOfSym(mogCycleStep3);
		case 0x0632: return addrOfSym(mogUiStatKnight);
		case 0x0664: return addrOfSym(mogMapCelTable);
		case 0x071D: return addrOfSym(mogPicNext);
		case 0x071E: return addrOfSym(mogPicTable);
		case 0x0705: return addrOfSym(mogCelBase);
		case 0x0704: return addrOfSym(mogPicBase);
		case 0x0713: return addrOfSym(mogPicAnimScript);
		case 0x076D: return addrOfSym(mogEncounterKind);
		case 0x076E: return addrOfSym(mogLairParam);
		case 0x07B6: return addrOfSym(mogCycleTab0);
		case 0x07B7: return addrOfSym(mogCycleTab1);
		case 0x07B8: return addrOfSym(mogCycleTab2);
		case 0x07B9: return addrOfSym(mogCycleTab3);
		case 0x08C4: return addrOfSym(mogArenaRegion);
		case 0x0A4D: return addrOfSym(mogHitData);
		case 0x0A4E: return addrOfSym(mogHitPairs);
		case 0x0A4F: return addrOfSym(mogHitDataBase);
		case 0x0A50: return addrOfSym(mogHitPairsBase);
		case 0x0D05: return addrOfSym(mogTextFlag);
		case 0x0D2B: return addrOfSym(mogPicPalette);
		case 0x0D92: return addrOfSym(mogDrawScreen);
		case 0x05D0: return addrOfSym(mogDrives);
		case 0x05CC: return addrOfSym(mogDiskUnit0);
		case 0x05CD: return addrOfSym(mogDiskUnit1);
		case 0x05CE: return addrOfSym(mogDiskUnit2);
		case 0x05CF: return addrOfSym(mogDiskUnit3);
		case 0x06FE: return addrOfSym(mogUnitDisk1);
		case 0x06FF: return addrOfSym(mogUnitDisk2);
		case 0x0700: return addrOfSym(mogUnitDisk3);
		case 0x0701: return addrOfSym(mogUnitDisk4);
		case 0x0B35: return addrOfSym(mogUnitSel);
		case 0x0D4D: return addrOfSym(mogDiskFlag);
		case 0x05F5: return addrOfSym(mogKnightActionScripts);
		case 0x05F6: return addrOfSym(mogKnightHurtScripts);
		case 0x05F7: return addrOfSym(mogKnightDamageTable);
		case 0x05F8: return addrOfSym(mogKnightDefenseTable);
		case 0x060C: return addrOfSym(mogTroggAxeWalkScripts);
		case 0x0610: return addrOfSym(mogKnightWalkScripts);
		case 0x0706: return addrOfSym(mogTextListCreatedBy);
		case 0x0709: return addrOfSym(mogTextListNextDay);
		case 0x070F: return addrOfSym(mogTextListWelcome);
		case 0x071B: return addrOfSym(mogTextHighwood);
		case 0x071C: return addrOfSym(mogTextWaterdeep);
		case 0x076F: return addrOfSym(mogFileKn1Ob);
		case 0x0770: return addrOfSym(mogFileKn2Ob);
		case 0x0771: return addrOfSym(mogFileKn3Ob);
		case 0x0772: return addrOfSym(mogFileKn4Ob);
		case 0x0773: return addrOfSym(mogFileKn5Ob);
		case 0x0774: return addrOfSym(mogFileBloCel);
		case 0x0775: return addrOfSym(mogFileHe1Ob);
		case 0x0776: return addrOfSym(mogFileHe2Ob);
		case 0x0777: return addrOfSym(mogFileHe3Ob);
		case 0x0778: return addrOfSym(mogFileTroggAxe1Cel);
		case 0x0779: return addrOfSym(mogFileTroggAxe2Cel);
		case 0x077A: return addrOfSym(mogFileTroggSpear1Cel);
		case 0x077B: return addrOfSym(mogFileTroggSpear2Cel);
		case 0x077C: return addrOfSym(mogFileRatmen1Cel);
		case 0x077D: return addrOfSym(mogFileRatmen2Cel);
		case 0x077E: return addrOfSym(mogFileBe1C);
		case 0x077F: return addrOfSym(mogFileBe2C);
		case 0x0780: return addrOfSym(mogFileDragon1Cel);
		case 0x0781: return addrOfSym(mogFileDragon2Cel);
		case 0x0782: return addrOfSym(mogFileMudmen1Cel);
		case 0x0783: return addrOfSym(mogFileMudmen2Cel);
		case 0x0784: return addrOfSym(mogFileKiCel);
		case 0x0785: return addrOfSym(mogFileBalok1Cel);
		case 0x0786: return addrOfSym(mogFileBalok3Cel);
		case 0x0787: return addrOfSym(mogFileBalok2Cel);
		case 0x0788: return addrOfSym(mogFileWi1C);
		case 0x0789: return addrOfSym(mogFileWi1P);
		case 0x078A: return addrOfSym(mogFileWi2P);
		case 0x078B: return addrOfSym(mogFileBoldF);
		case 0x078C: return addrOfSym(mogFileSmallFont);
		case 0x07AD: return addrOfSym(mogFileSelCel);
		case 0x07AE: return addrOfSym(mogFileMessagePiv);
		case 0x07AF: return addrOfSym(mogFileChPiv);
		case 0x07B0: return addrOfSym(mogFileDemon1Cel);
		case 0x07B1: return addrOfSym(mogFileDemon2Cel);
		case 0x07B2: return addrOfSym(mogFileDemon3Cel);
		case 0x07B3: return addrOfSym(mogFileDemon4Cel);
		case 0x07B4: return addrOfSym(mogFileTroll1Cel);
		case 0x07B5: return addrOfSym(mogFileTroll2Cel);
		case 0x070E: return addrOfSym(mogFileMiC);
		case 0x0715: return addrOfSym(mogFileHighWoodPiv);
		case 0x0716: return addrOfSym(mogFileWaterDeepPiv);
		case 0x0122: return addrOfSym(mogFileDragon5);
		case 0x013B: return addrOfSym(mogFilePackTest);
		case 0x07DB: return addrOfSym(mogKnightIdleScript);
		case 0x07DC: return addrOfSym(mogKnightAltScript);
		case 0x07DD: return addrOfSym(mogKnightWalk00);
		case 0x07DE: return addrOfSym(mogKnightWalk01);
		case 0x07DF: return addrOfSym(mogKnightWalk02);
		case 0x07E0: return addrOfSym(mogKnightWalk03);
		case 0x07E1: return addrOfSym(mogKnightWalk10);
		case 0x07E2: return addrOfSym(mogKnightWalk11);
		case 0x07E3: return addrOfSym(mogKnightWalk12);
		case 0x07E4: return addrOfSym(mogKnightWalk13);
		case 0x07E5: return addrOfSym(mogKnightWalk20);
		case 0x07E6: return addrOfSym(mogKnightWalk21);
		case 0x07E7: return addrOfSym(mogKnightWalk22);
		case 0x07E8: return addrOfSym(mogKnightWalk23);
		case 0x07E9: return addrOfSym(mogKnightActionScript5);
		case 0x07EA: return addrOfSym(mogKnightActionScript3);
		case 0x07ED: return addrOfSym(mogKnightActionScript2);
		case 0x07EE: return addrOfSym(mogKnightActionScript8);
		case 0x07EF: return addrOfSym(mogKnightActionScript1);
		case 0x07F1: return addrOfSym(mogKnightActionScript6);
		case 0x07F3: return addrOfSym(mogKnightActionScript7);
		case 0x07F4: return addrOfSym(mogKnightActionScript4);
		case 0x07F5: return addrOfSym(mogKnightHurtScriptB);
		case 0x07F6: return addrOfSym(mogKnightHurtScriptA);
		case 0x8023: return addrOfSym(mogShownScreen);
		default:
			return 0;
	}
}

// ---- Mem: the address space is the machine's ------------------------------------------------------------------------------------
uint8_t rd8(uint32_t a) { return *reinterpret_cast<const volatile uint8_t *>((uintptr_t)a); }
uint16_t rd16(uint32_t a) { return *reinterpret_cast<const volatile uint16_t *>((uintptr_t)a); }
uint32_t rd32(uint32_t a) { return *reinterpret_cast<const volatile uint32_t *>((uintptr_t)a); }
void wr8(uint32_t a, uint8_t v) { *reinterpret_cast<volatile uint8_t *>((uintptr_t)a) = v; }
void wr16(uint32_t a, uint16_t v) { *reinterpret_cast<volatile uint16_t *>((uintptr_t)a) = v; }
void wr32(uint32_t a, uint32_t v) { *reinterpret_cast<volatile uint32_t *>((uintptr_t)a) = v; }
void copyBytes(uint32_t ulDst, uint32_t ulSrc, uint32_t ulCount) {
	uint8_t *pD = reinterpret_cast<uint8_t *>((uintptr_t)ulDst);
	const uint8_t *pS = reinterpret_cast<const uint8_t *>((uintptr_t)ulSrc);
	for(uint32_t i = 0; i < ulCount; ++i) {
		pD[i] = pS[i];
	}
}
void fillBytes(uint32_t ulDst, uint8_t ub, uint32_t ulCount) {
	uint8_t *pD = reinterpret_cast<uint8_t *>((uintptr_t)ulDst);
	for(uint32_t i = 0; i < ulCount; ++i) {
		pD[i] = ub;
	}
}

// ---- Ops ------------------------------------------------------------------------------------------------------------------------------
uint32_t opCelSize(uint16_t n) { return call(RT_FN(rt_mog_cel_size), 0, 0, 0, labelAddr(n)); }
void opCelLoad(uint16_t n, uint32_t d) { call(RT_FN(rt_mog_cel_load), 0, 0, 0, labelAddr(n), d); }
void opHitLoad(uint16_t n, uint32_t d) { call(RT_FN(rt_mog_hit_load), 0, 0, 0, labelAddr(n), d); }
void opFileOpen(uint16_t n) { rtClFileErr[1] = (uint16_t)rt_file_open(reinterpret_cast<const char *>((uintptr_t)labelAddr(n))); }
void opFileRead(uint32_t d, uint32_t c) { rt_file_read(reinterpret_cast<void *>((uintptr_t)d), c); }
void opFileClose() { rt_file_close(); }
void opPackDone() { rt_mog_pack_done(); }
void opPlanes(uint32_t b) { call(RT_FN(rt_mog_set_planes), b); }
void opUnpack(uint32_t p) { call(RT_FN(rt_mog_pic_mem), 0, 0, 0, p); }
void opPictureLoad(uint16_t n, uint32_t b) { call(RT_FN(rt_mog_pic_file), 0, 0, 0, labelAddr(n), b); }
void opPalette(uint32_t p) { call(RT_FN(rt_mog_fade_to), 0, 0, 0, p); }
void opBlank() { call(RT_FN(rt_mog_palette_clear)); }
void opFadeOut() { call(RT_FN(rt_mog_fade_out)); }
void opClear(uint32_t s) { rt::displayClearScreen(reinterpret_cast<void *>((uintptr_t)s)); }   // LAB_0D72
void opCopyScreen(uint32_t s, uint32_t d) { call(RT_FN(rt_mog_blit_screen), 0, 0, 0, s, d); }
void opCopyScreens() { call(RT_FN(rt_mog_blit_both)); }
void opText(uint32_t l) { call(RT_FN(rt_mog_text_list), 0, 0, 0, l); }
void opDrawCel(uint32_t s, uint16_t cel, uint16_t x, uint16_t y) { call(RT_FN(rt_mog_draw_cel), cel, x, y, s); }
void opSynth(uint16_t seq, uint16_t ch) { rtSfxSynth(seq, ch); }
void opMusic(uint16_t e) {
	switch(e) {
		case MUSIC_CAMPAIGN: rtSbStart(); break;                      // LAB_0AA7 and LAB_0AAA..LAB_0AB4: src/rt/soundbank.cpp (7.1q)
		case MUSIC_KNIGHTS: rtSbLoad(SB_KNIGHTS); break;
		case MUSIC_BALOK: rtSbLoad(SB_BALOK); break;
		case MUSIC_DRAGON: rtSbLoad(SB_DRAGON); break;
		case MUSIC_BE: rtSbLoad(SB_BE); break;
		case MUSIC_DEMON: rtSbLoad(SB_DEMON); break;
		case MUSIC_TROLL: rtSbLoad(SB_TROLL); break;
		case MUSIC_TROGG_SPEAR: rtSbLoad(SB_TROGG_SPEAR); break;
		case MUSIC_RATMEN: rtSbLoad(SB_RATMEN); break;
		case MUSIC_MUDMEN: rtSbLoad(SB_MUDMEN); break;
		case MUSIC_WIZARD: rtSbLoad(SB_WIZARD); break;
		default: break;
	}
}
void opBackdrop(uint32_t s) { call(RT_FN(rt_mog_arena_load), 0, 0, 0, s); }
void opBackdropReset() { call(RT_FN(rt_mog_arena_default)); }
void opHunk9() {}   // LAB_012D's tail jump to SECSTRT_9 (the protection stub): rtTitleStep does the stub's return chain (src/rt/hunk9.cpp)

const Mem kMem = {rd8, rd16, rd32, wr8, wr16, wr32, copyBytes, fillBytes};
const Ops kOps = {opCelSize, opCelLoad, opHitLoad, opFileOpen, opFileRead, opFileClose, opPackDone, opPlanes, opUnpack,
                  opPictureLoad, opPalette, opBlank, opFadeOut, opClear, opCopyScreen, opCopyScreens, opText, opDrawCel,
                  opSynth, opMusic, opBackdrop, opBackdropReset, opHunk9};

// The byte counts are cells the enhanced display rewrites, so the Env is filled per entry.
Env makeEnv() {
	Env e;
	e.m = kMem;
	e.o = kOps;
	e.addr = labelAddr;
	e.sz.ulRawMessage = rt_enh_raw_mog_msg;
	e.sz.ulRawChar = rt_enh_raw_mog_ch;
	e.sz.ulRawPack = rt_enh_raw_mog_pack;
	for(uint8_t i = 0; i < 9; ++i) {
		e.sz.aulPic[i] = rt_enh_pic_sz[i];
	}
	return e;
}

}  // namespace

// ---- C entries (called directly by the rt callers; rt_cl_select below is the one asm shim) -------------------------------------------------------------------------------------
#define RT_CL_ENTRY(CNAME, FN) \
	extern "C" __attribute__((used, externally_visible, noinline)) void CNAME(void) { \
		const Env e = makeEnv(); \
		FN(e); \
	}

RT_CL_ENTRY(rtClDriveInit, driveInit)
RT_CL_ENTRY(rtClKnights, loadKnights)
RT_CL_ENTRY(rtClHe, loadHe)
RT_CL_ENTRY(rtClTroggSpear, loadTroggSpear)
RT_CL_ENTRY(rtClTroggAxe, loadTroggAxe)
RT_CL_ENTRY(rtClRatmen, loadRatmen)
RT_CL_ENTRY(rtClMudmen, loadMudmen)
RT_CL_ENTRY(rtClBalok, loadBalok)
RT_CL_ENTRY(rtClDragon, loadDragon)
RT_CL_ENTRY(rtClBe, loadBe)
RT_CL_ENTRY(rtClDemon, loadDemon)
RT_CL_ENTRY(rtClTroll, loadTroll)
RT_CL_ENTRY(rtClKiMi, loadKiMi)
RT_CL_ENTRY(rtClMoon, drawMoon)
RT_CL_ENTRY(rtClAssets, loadAssets)
RT_CL_ENTRY(rtClSelect, selectScreen)
RT_CL_ENTRY(rtClHighWood, placeHighWood)
RT_CL_ENTRY(rtClWaterDeep, placeWaterDeep)
RT_CL_ENTRY(rtClWizard, loadWizard)
RT_CL_ENTRY(rtClMessageNext, messageNext)
RT_CL_ENTRY(rtClPack, loadPack)
RT_CL_ENTRY(rtClArenaPicture, arenaPicture)
RT_CL_ENTRY(rtClTablesClear, clearTables)
RT_CL_ENTRY(rtClTables, fillTables)

// ROADMAP 9.5e3: the cel set and the sound bank of a creature arena by name (arenas.ini `cels` / `sounds`; ids of the schema's
// enums, tools/mod_schema/arenas.yaml).  A sound bank < 0 keeps the set's own.
namespace {
const uint16_t kBankMusic[] = {MUSIC_BALOK, MUSIC_DRAGON, MUSIC_BE, MUSIC_DEMON, MUSIC_TROLL, MUSIC_TROGG_SPEAR, MUSIC_RATMEN, MUSIC_MUDMEN};
}
extern "C" __attribute__((used, externally_visible, noinline)) void rtClSet(uint8_t ubSet, int8_t sbBank) {
	const Env e = makeEnv();
	const bool b = sbBank >= 0 && (uint8_t)sbBank < sizeof(kBankMusic) / sizeof(kBankMusic[0]);
	const uint16_t m = b ? kBankMusic[sbBank] : 0;
	switch(ubSet) {
		case ms::game::CELS_TROGG_AXE: b ? loadTroggAxe(e, m) : loadTroggAxe(e); break;
		case ms::game::CELS_TROGG_SPEAR: b ? loadTroggSpear(e, m) : loadTroggSpear(e); break;
		case ms::game::CELS_RATMEN: b ? loadRatmen(e, m) : loadRatmen(e); break;
		case ms::game::CELS_MUDMEN: b ? loadMudmen(e, m) : loadMudmen(e); break;
		case ms::game::CELS_BALOK: b ? loadBalok(e, m) : loadBalok(e); break;
		case ms::game::CELS_BE: b ? loadBe(e, m) : loadBe(e); break;
		case ms::game::CELS_TROLL: b ? loadTroll(e, m) : loadTroll(e); break;
		case ms::game::CELS_DRAGON: b ? loadDragon(e, m) : loadDragon(e); break;
		case ms::game::CELS_DEMON: b ? loadDemon(e, m) : loadDemon(e); break;
		default: break;
	}
}

extern "C" __attribute__((used, externally_visible, noinline)) void rtClMessageText(uint32_t ulList) {
	const Env e = makeEnv();
	messageText(e, ulList);
}

extern "C" __attribute__((used, externally_visible, noinline)) void rtClMessageRecoloured(uint32_t ulList) {
	const Env e = makeEnv();
	messageTextRecoloured(e, ulList);
}

