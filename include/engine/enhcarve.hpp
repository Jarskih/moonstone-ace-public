// engine/enhcarve - the memory carve-outs of the two overlays for the enhanced 6-plane display (ROADMAP 4.8a).
//
// program LAB_0044 and mog LAB_0004 cut the two arenas handed over by the loader (chip: A1/D1, fast: A0/D0) into the
// game's buffers: full-screen 5-plane picture buffers of $9C40 bytes, cel/object file regions, tables. With a
// 6-plane screen every picture buffer needs 48000 bytes and the (redrawn, up to 6-plane) cel regions grow too, so
// the whole layout is stretched. Both routines are re-implemented here as pure functions that return the new cell
// values; src/rt/enhanced.cpp stores them into the asm's cells (patches {program,mog}.enhanced.json replace the
// first instruction of each routine by a jump to the shim). With 'isEnh' false the result is exactly the original
// layout (tests/test_enh_carve.py checks that against a simulation of the asm, line by line).
//
// Stretch rule (one place, tunable): an original picture chunk ($9C40) becomes kEnhPictureBytes; every other
// increment or offset n becomes n * 3 / 2 (parity kept, so the originally even addresses stay even). 3/2 is the
// largest decoded-size growth measured over build/art/import6 against the original cels (tools: see docs/ART.md).
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t kOrigPictureBytes = 0x9C40;  // 5 x $1F40
constexpr uint32_t kEnhPictureBytes = 48000;    // 6 x $1F40
// message.piv / ch.piv are read raw into fixed regions of the carve (program $10E6 into LAB_011A+20, mog $0E6E and $25F6
// into LAB_05B9+52 / +56) and decoded from there. Redrawn 6-plane files are up to twice the original size, so these
// three regions get a fixed capacity instead of the 3/2 stretch; the read/copy counts at the six asm sites follow
// (cells rt_enh_raw_*, patches enh-raw-*) and rt/files refuses larger overrides (ms::artMaxSize). 32 KB (ROADMAP 4.8b):
// the asm copy loops are DBF loops, 32768 still fits their 16-bit count (the copy runs one byte over, as it always did).
constexpr uint32_t kEnhRawPictureBytes = 32768;
// The arena pack "Test" (nine pictures back to back, 198745 bytes originally, 314261 redrawn at 6 planes) is read raw in
// one piece into mog LAB_05B9+8 (FAST RAM: read and decoded by the CPU only). Its nine increments in the carve (0x5958 ..
// 0x8A03) are stretched by 8/3 (carveSize), which sums to about 530 KB and holds kEnhPackBytes (512 KB, ROADMAP 4.8b).
// rt/enhanced.cpp walks the picture headers after the read and sets the nine pointers (+8 +12 +20 +16 +24
// +32 +36 +28 +92); the nine copy counts of the asm (patches enh-pack-*) come from rt_enh_pic_sz, which are DBF counts, so
// a single picture of the pack stays at most 65536 bytes (the walk refuses larger ones).
constexpr uint32_t kEnhPackBytes = 524288;
// Packed cel body read buffer (the original: 41244 bytes in the BSS section behind program LAB_052A / mog LAB_0D4F, only used
// by the C++ cel loader, rt/loaders.cpp celLoad). In enhanced mode the carve adds a region of this size at the end of each
// overlay's fast chain (PrgCarve/MogCarve::celRead) and the loader reads into it instead. Fast RAM is fine: the file read and
// the LZSS decode are CPU only; the decoded planes go to the (chip) cel regions. Largest redrawn cel so far: au1.cel, 36367.
constexpr uint32_t kEnhCelReadBufferBytes = 65536;

// One increment/offset of the original carve, stretched when 'isEnh'.
uint32_t carveSize(uint32_t n, bool isEnh);

// ---- program LAB_0044 ------------------------------------------------------------------------------------------
// In: the values of the two free-memory cells (program LAB_00C2 = chip, LAB_00C4 = fast) at entry.
struct PrgCarve {
	uint32_t chipNext;   // LAB_00C2 afterwards (entry value + bump)
	uint32_t fastNext;   // LAB_00C4 afterwards
	uint32_t chipBump, fastBump;
	uint32_t secstrt3_0, secstrt3_4, secstrt3_8;   // SECSTRT_3 +0 / +4 / +8
	uint32_t l00c6, l00c7, l00c8, l00c9, l00ca, l0124;
	uint32_t l0045;                                // fast base (the cell sits in the code)
	uint32_t l011a_0, l011a_4, l011a_16, l011a_20; // LAB_011A +0 / +4 / +16 / +20
	uint32_t l00cb, l00cc, l00cd, l00ce, l00cf;
	uint32_t celRead;                              // enhanced only: packed cel read buffer (fast), 0 = the original BSS one
};
void carveProgram(uint32_t chip, uint32_t fast, bool isEnh, PrgCarve &o);

// ---- mog LAB_0004 ----------------------------------------------------------------------------------------------
// In: mog LAB_05BC (chip) and LAB_05BE (fast) at entry.
struct MogCarve {
	uint32_t chipNext, fastNext;       // LAB_05BC / LAB_05BE afterwards
	uint32_t chipBump, fastBump;
	uint32_t b8_0, b8_4, b8_8, b8_12, b8_16, b8_28, b8_36;                   // LAB_05B8 +offset
	uint32_t l05c0, l05c7, l05ca, l05cb, l05c8, l0664, l05c1, l05c9;
	uint32_t b9_0, b9_8, b9_12, b9_16, b9_20, b9_24, b9_28, b9_32, b9_36, b9_40, b9_44, b9_48, b9_52, b9_56, b9_68,
	    b9_72, b9_84, b9_88, b9_92;                                           // LAB_05B9 +offset
	uint32_t l05c2, l05c6, l05bb, secstrt14, l0a83, l05c3;
	uint32_t celRead;                  // enhanced only: packed cel read buffer (fast), 0 = the original BSS one
};
void carveMog(uint32_t chip, uint32_t fast, bool isEnh, MogCarve &o);

// Arena sizes the loader replacement must provide so that both overlays carve inside them (maximum of the two
// bumps) plus the scratch tails the overlays use above their bump (rt/game.cpp).
struct ArenaSizes {
	uint32_t chip, fast;
};
ArenaSizes arenaSizes(bool isEnh, uint32_t scratchChip, uint32_t scratchFast);

}  // namespace ms
