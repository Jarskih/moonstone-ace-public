// rt/enhanced - the shims behind asm/patches/{program,mog}.enhanced.json (ROADMAP 4.8a, include/rt/enhanced.hpp).
//
// Every patched 5-plane site of the original asm calls (or jumps to) one of the entries below. Each one reads the cell
// rt_enh_planes: 5 does exactly what the replaced instructions did, 6 does the enhanced thing (a sixth plane, a
// 64-colour palette, bigger buffers). The cells and the asm entries live in the top-level asm blocks; the C++ part
// below them carves the arenas (src/engine/enhcarve.cpp), builds the 7-plane cel temp buffer and walks the arena pack.
//
// Register contract of the entries: the replaced original code clobbered some registers; the entries clobber no
// register beyond that set (documented per entry). Entered by JSR unless the comment says JMP.
#include "rt/enhanced.hpp"

#include <ace/managers/log.h>
#include <ace/managers/memory.h>

#include "engine/enhcarve.hpp"


// ---- cells ----------------------------------------------------------------------------------------------------------
// planes: 5 or 6. scr: bytes of one screen/picture buffer. scr_longs: the same in longwords (CPU copy loops).
// clrcnt: the DBF count of the screen clear (LAB_054D / LAB_0D72: 50 longs x (count + 1) = bytes). temp: the 7-plane
// cel temp buffer (chip). raw_*: the byte counts the game reads / copies raw for message.piv (program, mog) and ch.piv (mog),
// 4326 / 3694 / 9718 originally, 24576 enhanced (the redrawn files are up to twice as large; the files are shorter than
// the count and the read stops at the end of the file).
asm(R"(
	.section .data.rt_enh,"aw",@progbits
	.balign 4
	.globl rt_enh_planes
rt_enh_planes:
	.word 5
	.balign 4
	.globl rt_enh_scr
rt_enh_scr:
	.long 40000
	.globl rt_enh_scr_longs
rt_enh_scr_longs:
	.long 10000
	.globl rt_enh_clrcnt
rt_enh_clrcnt:
	.long 199
	.globl rt_enh_temp
rt_enh_temp:
	.long 0
	.globl rt_enh_raw_prg_msg
rt_enh_raw_prg_msg:
	.long 4326
	.globl rt_enh_raw_mog_msg
rt_enh_raw_mog_msg:
	.long 3694
	.globl rt_enh_raw_mog_ch
rt_enh_raw_mog_ch:
	.long 9718
	.globl rt_enh_raw_mog_pack
rt_enh_raw_mog_pack:
	.long 198745
	.globl rt_enh_pic_sz
rt_enh_pic_sz:
	.long 0x5957, 0x5148, 0x4657, 0x3a54, 0x6394, 0x51c3, 0x4c09, 0x51a4, 0x8a02
	.text
)");

// Hang breadcrumbs (build with -DMS_ENH_TRACE): COLOR00 is written directly (12-bit, bank 0) at the steps of the picture
// loaders and the pack walk; a frozen screen shows the last colour. Off by default.
#ifdef MS_ENH_TRACE
#define TRACE_ASM(COL) "	move.w #" #COL ",0xdff180\n"
#define TRACE_CPP(COL) (*reinterpret_cast<volatile UWORD *>(0xDFF180) = (COL))
#else
#define TRACE_ASM(COL)
#define TRACE_CPP(COL) ((void)0)
#endif

// ---- program ----------------------------------------------------------------------------------------------------------
// LAB_0044 (the arena carve) is rtEnhCarveProgram below; src/rt/progmain.cpp calls it directly (7.1q: rt_prg_carve, the asm entry,
// and rt_prg_0264_init, the pass count of LAB_0264, are gone - src/rt/prg_copy.cpp reads rt_enh_planes itself).
// (The wipe plane-count shims rt_prg_lea_a1_planes / rt_prg_lea_a0_planes / rt_prg_05c5_init went with their dead patches, 7.1
//  cleanup: the wipes are C++ since 7.1e.)
// (The cel mirror loop and the temp-buffer carve of LAB_04E3 left this file with ROADMAP 7.1c: ms::mirrorCelPlanes and
//  ms::carveCelScratch, src/rt/engine_blit.cpp.)

// ---- mog ----------------------------------------------------------------------------------------------------------------
// rt_mog_carve (JMP, replaces LAB_0004).
// (The restore tail rt_{prg,mog}_tail5 and the arena compositor's plane counts rt_mog_0a60_init / rt_mog_0a64_init left this file with
//  ROADMAP 7.1m: ms::runRestore and ms::bgCompose, src/rt/prims.cpp / src/rt/arena.cpp, read rt_enh_planes themselves.)
asm(R"(
	.text
	.globl rt_mog_carve
rt_mog_carve:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtEnhCarveMog
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_pack_done
rt_mog_pack_done:
	jsr rt_mog_file_close
	cmpi.w #6,rt_enh_planes
	bne.s 1f
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtEnhPackDone
	movem.l (%sp)+,%d0-%d1/%a0-%a1
1:	rts
)");

// ---- picture loaders ---------------------------------------------------------------------------------------------------------
// The picture loaders (program LAB_03FC / LAB_0402, mog LAB_0C21 / LAB_0C27) are C++ since ROADMAP 7.1f1 (src/rt/loaders.cpp,
// src/engine/loaders.cpp): one code path, depth from rt_enh_planes (6-plane header = 64 palette words, colours registered with
// rt::enhPictureLoaded, plane 5 zeroed for a picture without one). What stays here is the arena pack walk (rtEnhPackDone).

// ---- palette sites ---------------------------------------------------------------------------------------------------------
//   rt_*_pal_copy_live  JMP, replaces the body of LAB_025D / LAB_03EE (table A0 -> the live palette).
// (rt_mog_pal_clear and rt_*_pal_write went with their dead patches, 7.1 cleanup: the palette machine and the wipes are C++.
//  rt_prg_pal_clear, the end of LAB_0258, went in 7.1q: rtPrgBlackout in src/rt/prg_ops.cpp calls rtEnhPalClear.)

#define PALETTE_SITE_SHIMS(PFX, LIVECELL) \
asm(".text\n" \
	".globl rt_" #PFX "_pal_copy_live\n" \
	"rt_" #PFX "_pal_copy_live:\n" \
	"	movea.l " LIVECELL ",%a1\n" \
	"	moveq #31,%d0\n" \
	"2:	move.w (%a0)+,(%a1)+\n" \
	"	dbf %d0,2b\n" \
	"	cmpi.w #6,rt_enh_planes\n" \
	"	bne.s 3f\n" \
	"	movem.l %d0-%d1/%a0-%a1,-(%sp)\n" \
	"	lea -64(%a0),%a0\n" \
	"	move.l %a0,-(%sp)\n" \
	"	jsr rtEnhPalSetLive\n" \
	"	addq.l #4,%sp\n" \
	"	movem.l (%sp)+,%d0-%d1/%a0-%a1\n" \
	"3:	rts\n");

PALETTE_SITE_SHIMS(prg, "prgLivePalette")
PALETTE_SITE_SHIMS(mog, "mogLivePalette")

// ---- C++ side ----------------------------------------------------------------------------------------------------------------
extern "C" {
// program cells written by the carve (names = the asm labels)
extern ULONG prgChipFree, prgFastFree, prgScreenBufs[3], prgBackground, prgScreenBufA, prgScreenBufB, prgScreenBufC,  // LAB_00C2, LAB_00C4, LAB_00C6 (SECSTRT_3, LAB_00C7, LAB_00C8, LAB_00C9)
    prgScreenBufD, prgModuleBase, prgFastBase, prgFontRecord[6], prgFastBufA, prgFastBufB, prgFastBufC, prgFastBufD,  // LAB_011A (LAB_00CA, LAB_0124, LAB_0045, LAB_00CB, LAB_00CC, LAB_00CD, LAB_00CE)
    prgFastBufE;  // LAB_00CF
// mog
extern ULONG mogChipFree, mogFastFree, mogBufTable[10], mogHeapTable[24], mogBackground, mogBankKnights, mogBankWizard,  // LAB_05BC, LAB_05BE, LAB_05B8, LAB_05B9, LAB_05C0, LAB_05C7, LAB_05CA
    mogBankRatmen, mogBankCreature, mogMapCelTable, mogForeground, mogBankCampaign, mogPicScreen, mogLairs, mogSetValue,  // LAB_05CB, LAB_05C8, LAB_0664, LAB_05C1, LAB_05C9, LAB_05C2, LAB_05C6, LAB_05BB
    mogRecordTable, mogTileScript, mogCreatureHeap;  // SECSTRT_14, LAB_0A83, LAB_05C3

extern UWORD rt_enh_planes_cell asm("rt_enh_planes");
extern ULONG rt_enh_scr_cell asm("rt_enh_scr");
extern ULONG rt_enh_scr_longs_cell asm("rt_enh_scr_longs");
extern ULONG rt_enh_clrcnt_cell asm("rt_enh_clrcnt");
extern ULONG rt_enh_temp_cell asm("rt_enh_temp");
extern ULONG rt_enh_raw_prg_msg_cell asm("rt_enh_raw_prg_msg");
extern ULONG rt_enh_raw_mog_msg_cell asm("rt_enh_raw_mog_msg");
extern ULONG rt_enh_raw_mog_ch_cell asm("rt_enh_raw_mog_ch");
extern ULONG rt_enh_raw_mog_pack_cell asm("rt_enh_raw_mog_pack");
extern ULONG rt_enh_pic_sz_cell[9] asm("rt_enh_pic_sz");
}

namespace {

constexpr ULONG TEMP_PLANES = 7;     // 6 colour planes + the mask plane
constexpr ULONG TEMP_STRIDE = 0x12C0;

bool s_isWanted = MS_ENHANCED != 0;
bool s_isActive;
void *s_pTemp;
UBYTE *s_pCelRead;  // the enhanced carve's packed cel read buffer (ms::kEnhCelReadBufferBytes), null in the original layout

}  // namespace

extern "C" {

// program LAB_0044 -> ms::carveProgram (enhcarve.cpp, which has the layout and why)
__attribute__((used, externally_visible)) void rtEnhCarveProgram(void) {
	ms::PrgCarve o;
	ms::carveProgram(prgChipFree, prgFastFree, rt_enh_planes_cell == 6, o);
	prgChipFree = o.chipNext;
	prgFastFree = o.fastNext;
	prgScreenBufs[0] = o.secstrt3_0;
	prgScreenBufs[1] = o.secstrt3_4;
	prgScreenBufs[2] = o.secstrt3_8;
	prgBackground = o.l00c6;
	prgScreenBufA = o.l00c7;
	prgScreenBufB = o.l00c8;
	prgScreenBufC = o.l00c9;
	prgScreenBufD = o.l00ca;
	prgModuleBase = o.l0124;
	prgFastBase = o.l0045;
	prgFontRecord[0] = o.l011a_0;
	prgFontRecord[1] = o.l011a_4;
	prgFontRecord[4] = o.l011a_16;
	prgFontRecord[5] = o.l011a_20;
	prgFastBufA = o.l00cb;
	prgFastBufB = o.l00cc;
	prgFastBufC = o.l00cd;
	prgFastBufD = o.l00ce;
	prgFastBufE = o.l00cf;
	s_pCelRead = reinterpret_cast<UBYTE *>(o.celRead);
}

// mog LAB_0004 -> ms::carveMog
__attribute__((used, externally_visible)) void rtEnhCarveMog(void) {
	ms::MogCarve o;
	ms::carveMog(mogChipFree, mogFastFree, rt_enh_planes_cell == 6, o);
	mogChipFree = o.chipNext;
	mogFastFree = o.fastNext;
	mogBufTable[0] = o.b8_0;
	mogBufTable[1] = o.b8_4;
	mogBufTable[2] = o.b8_8;
	mogBufTable[3] = o.b8_12;
	mogBufTable[4] = o.b8_16;
	mogBufTable[7] = o.b8_28;
	mogBufTable[9] = o.b8_36;
	mogBackground = o.l05c0;
	mogBankKnights = o.l05c7;
	mogBankWizard = o.l05ca;
	mogBankRatmen = o.l05cb;
	mogBankCreature = o.l05c8;
	mogMapCelTable = o.l0664;
	mogForeground = o.l05c1;
	mogBankCampaign = o.l05c9;
	mogHeapTable[0] = o.b9_0;
	mogHeapTable[2] = o.b9_8;
	mogHeapTable[3] = o.b9_12;
	mogHeapTable[4] = o.b9_16;
	mogHeapTable[5] = o.b9_20;
	mogHeapTable[6] = o.b9_24;
	mogHeapTable[7] = o.b9_28;
	mogHeapTable[8] = o.b9_32;
	mogHeapTable[9] = o.b9_36;
	mogHeapTable[10] = o.b9_40;
	mogHeapTable[11] = o.b9_44;
	mogHeapTable[12] = o.b9_48;
	mogHeapTable[13] = o.b9_52;
	mogHeapTable[14] = o.b9_56;
	mogHeapTable[17] = o.b9_68;
	mogHeapTable[18] = o.b9_72;
	mogHeapTable[21] = o.b9_84;
	mogHeapTable[22] = o.b9_88;
	mogHeapTable[23] = o.b9_92;
	mogPicScreen = o.l05c2;
	mogLairs = o.l05c6;
	mogSetValue = o.l05bb;
	mogRecordTable = o.secstrt14;
	mogTileScript = o.l0a83;
	mogCreatureHeap = o.l05c3;
	s_pCelRead = reinterpret_cast<UBYTE *>(o.celRead);
}

// The arena pack "Test" was read raw into LAB_05B9+8 (mog LAB_013A): nine pictures back to back, each {BE16 planes,
// BE32 packed, 1 << planes palette words, packed body}. The original code finds them at fixed offsets (the nine
// pointers LAB_05B9 +8 +12 +20 +16 +24 +32 +36 +28 +92 and nine hard-coded copy counts); the redrawn pack has other
// sizes, so walk the headers and set the pointers and the counts (rt_enh_pic_sz, DBF counts = size - 1).
__attribute__((used, externally_visible)) void rtEnhPackDone(void) {
	TRACE_CPP(0x0f0f);
	static const UBYTE s_aubCell[9] = {2, 3, 5, 4, 6, 8, 9, 7, 23};  // index into LAB_05B9 of picture k
	ULONG aulPtr[9], aulCount[9];
	const UBYTE *pPic = reinterpret_cast<const UBYTE *>(mogHeapTable[2]);
	const UBYTE *pStart = pPic;
	for(UBYTE k = 0; k < 9; ++k) {
		const ULONG ulPlanes = (ULONG(pPic[0]) << 8) | pPic[1];
		const ULONG ulPacked = (ULONG(pPic[2]) << 24) | (ULONG(pPic[3]) << 16) | (ULONG(pPic[4]) << 8) | pPic[5];
		const ULONG ulSize = (ulPlanes >= 4 && ulPlanes <= 6) ? 6 + 2 * (1UL << ulPlanes) + ulPacked : 0;
		if(ulSize == 0 || ulSize > 65536 || static_cast<ULONG>(pPic - pStart) + ulSize > ms::kEnhPackBytes) {
			rt::enhLog("ERR enhanced pack picture bad: idx planes size", k, ulPlanes, ulSize);
			return;
		}
		aulPtr[k] = reinterpret_cast<ULONG>(pPic);
		aulCount[k] = ulSize - 1;
		pPic += ulSize;
	}
	for(UBYTE k = 0; k < 9; ++k) {
		mogHeapTable[s_aubCell[k]] = aulPtr[k];
		rt_enh_pic_sz_cell[k] = aulCount[k];
	}
	TRACE_CPP(0x0fff);
	rt::enhLog("enhanced: arena pack walked, 9 pictures, bytes", static_cast<ULONG>(pPic - pStart));
}

__attribute__((used, externally_visible)) void rtEnhPalClear(void) {
	rt::enhPaletteClear();
}

__attribute__((used, externally_visible)) void rtEnhPalSetLive(const UWORD *pTable) {
	rt::enhPaletteSetLive(pTable);
}

}  // extern "C"

namespace rt {

namespace {
constexpr unsigned LOG_LINES = 8;
constexpr unsigned LOG_LEN = 100;
char s_aaLog[LOG_LINES][LOG_LEN];
unsigned s_uLogCount;

// decimal digits of v appended at pos (no printf: this runs with the OS gone)
unsigned putNum(char *pOut, unsigned uPos, ULONG ulV) {
	char aDigits[12];
	unsigned n = 0;
	do {
		aDigits[n++] = static_cast<char>('0' + ulV % 10);
		ulV /= 10;
	} while(ulV);
	while(n && uPos < LOG_LEN - 1) {
		pOut[uPos++] = aDigits[--n];
	}
	return uPos;
}
}  // namespace

void enhLog(const char *sz, ULONG ulA, ULONG ulB, ULONG ulC) {
	if(s_uLogCount >= LOG_LINES) {
		return;
	}
	char *pOut = s_aaLog[s_uLogCount];
	unsigned uPos = 0;
	for(; *sz && uPos < LOG_LEN - 24; ++sz) {
		pOut[uPos++] = *sz;
	}
	const ULONG aulNum[3] = {ulA, ulB, ulC};
	for(unsigned i = 0; i < 3; ++i) {
		pOut[uPos++] = ' ';
		uPos = putNum(pOut, uPos, aulNum[i]);
	}
	pOut[uPos] = 0;
	++s_uLogCount;
}

void enhLogFlush(EnhLogSink pfnSink) {
	for(unsigned i = 0; i < s_uLogCount; ++i) {
		pfnSink(s_aaLog[i], "", 0);
	}
	s_uLogCount = 0;
}

uint8_t *enhCelReadBuf() {
	return s_pCelRead;
}

bool enhancedWanted() {
	return s_isWanted;
}

void enhancedSetWanted(bool isWanted) {
	s_isWanted = isWanted && MS_ENHANCED;
}

bool enhancedActive() {
	return s_isActive;
}

bool enhancedEnable() {
	if(s_isActive) {
		return true;
	}
	if(!s_pTemp) {
		s_pTemp = memAllocChipClear(TEMP_PLANES * TEMP_STRIDE);
	}
	if(!s_pTemp) {
		logWrite("ERR: enhancedEnable: no chip memory for the 7-plane cel temp buffer\n");
		return false;
	}
	rt_enh_temp_cell = reinterpret_cast<ULONG>(s_pTemp);
	rt_enh_scr_cell = ms::kEnhPictureBytes;
	rt_enh_scr_longs_cell = ms::kEnhPictureBytes / 4;
	rt_enh_clrcnt_cell = ms::kEnhPictureBytes / 200 - 1;  // 50 longs (200 bytes) per pass
	rt_enh_raw_prg_msg_cell = rt_enh_raw_mog_msg_cell = rt_enh_raw_mog_ch_cell = ms::kEnhRawPictureBytes;
	rt_enh_raw_mog_pack_cell = ms::kEnhPackBytes;
	rt_enh_planes_cell = 6;
	s_isActive = true;
	logWrite("enhanced: 6 planes, screen %lu bytes, temp buffer %p\n", static_cast<unsigned long>(ms::kEnhPictureBytes), s_pTemp);
	return true;
}

void enhancedDisable() {
	s_pCelRead = nullptr;
	rt_enh_planes_cell = 5;
	rt_enh_scr_cell = ms::kOrigPictureBytes;
	rt_enh_scr_longs_cell = ms::kOrigPictureBytes / 4;
	rt_enh_clrcnt_cell = ms::kOrigPictureBytes / 200 - 1;
	rt_enh_raw_prg_msg_cell = 4326;
	rt_enh_raw_mog_msg_cell = 3694;
	rt_enh_raw_mog_ch_cell = 9718;
	rt_enh_raw_mog_pack_cell = 198745;
	static const ULONG s_aulOrigSz[9] = {0x5957, 0x5148, 0x4657, 0x3a54, 0x6394, 0x51c3, 0x4c09, 0x51a4, 0x8a02};
	for(UBYTE k = 0; k < 9; ++k) {
		rt_enh_pic_sz_cell[k] = s_aulOrigSz[k];
	}
	s_isActive = false;
	if(s_pTemp) {
		memFree(s_pTemp, TEMP_PLANES * TEMP_STRIDE);
		s_pTemp = nullptr;
	}
	rt_enh_temp_cell = 0;
}

void enhancedOverlayEnter() {
	enhPaletteReset();
}

}  // namespace rt

