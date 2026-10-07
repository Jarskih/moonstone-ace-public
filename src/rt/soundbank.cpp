// rt/soundbank - mog's sound-bank loaders in C++ (ROADMAP 7.1q, see soundbank.hpp).  What was asm: mog S_16 LAB_0AA7 (start),
// LAB_0AAA..LAB_0AB4 (one bank each: file name, destination buffer cell, byte count) and LAB_0AB5 (the file read), mog.asm 19332-19434.
// The patches inside them (int4-vector, sfx-init-reset) are folded in; the file layer is called directly (rt/files.hpp), not through
// the LAB_0BB5 / LAB_0BD7 / LAB_0BEA / LAB_0BFF stubs.
//
// Differences from the original (all documented, none changes what the game hears or loads):
//   * the VBL hook list gets the address of rt_synth_tick (the entry of the C++ synth tick; with MS_SYNTH_ASM the shim that continues in
//     the original LAB_0F73), not the label LAB_0F73, which is a patch stub of that entry;
//   * with the C++ synth the INT4 handler is rt_synth_int4 from the start (the original installed the asm LAB_0F69 and rt_synth_reloc
//     replaced it after the bank load); INT4 cannot fire in between: audio DMA is off until the first sequence starts.
//     With MS_SYNTH_ASM the original LAB_0F69 is installed as before.
//   * the original ended with `MOVE.W #0,LAB_0AA6` (patched to rt_sfx_reset): rt::sfxReset().
// Contracts: none of the entries is asm-callable any more; callers are C++ (rtSb*).

#include "rt/soundbank.hpp"

#include <ace/types.h>

#include "rt/files.hpp"
#include "rt/guards.hpp"
#include "rt/irq.hpp"
#include "rt/sfx.hpp"

#ifndef MS_SYNTH_ASM
#define MS_SYNTH_ASM 0
#endif

extern "C" {
// file names (NUL-terminated text in the code hunk / SECSTRT_17), the destination buffer cells of the arena carve
extern const char mogBankFileBe[], mogBankFileBalok[], mogBankFileDragon[], mogBankFileTroll[], mogBankFileTroggSpear[], mogBankFileDemon[], mogBankFileWizard[],  // LAB_0AB7, LAB_0AB8, LAB_0AB9, LAB_0ABA, LAB_0ABB, LAB_0ABD, LAB_0ABE
	mogBankFileRatmen[], mogBankFileMudmen[], mogBankFileCampaign[], mogBankFileScreen[], mogBankFileKnights[];  // LAB_0ABF, LAB_0AC0, LAB_0AC1, LAB_0AC2, SECSTRT_17
extern ULONG mogBankKnights, mogBankCreature, mogBankCampaign, mogBankWizard, mogBankRatmen;  // LAB_05C7, LAB_05C8, LAB_05C9, LAB_05CA, LAB_05CB
extern ULONG mogHooks[];                        // the VBL hook list, 0-terminated (LAB_0B96)
extern UWORD mogFileErr[2];                       // the original file layer's error word (+2 = the open result) (LAB_0BA6)
void rt_synth_init(void);                           // src/rt/synth.cpp: LAB_0F89 (every register kept)
void rt_synth_reloc(void);                          // LAB_0FD4
void rt_synth_tick(void);                           // LAB_0F73, the VBL hook
void rt_synth_int4(void);                           // the C++ INT4 handler
#if MS_SYNTH_ASM
void mog_LAB_0F69(void);                            // the original INT4 handler
#endif
}

namespace {

struct Bank {
	const char *szName;
	const ULONG *pulDest;   // the buffer cell: the destination is its value
	ULONG ulSize;
};

// LAB_0AAA..LAB_0AB4 in SoundBank order.
const Bank s_aBanks[SB_COUNT] = {
	{mogBankFileKnights, &mogBankKnights, 0x57F8},   // SB_KNIGHTS
	{mogBankFileBalok, &mogBankCreature, 0xBDCC},     // SB_BALOK
	{mogBankFileDragon, &mogBankCreature, 0xC140},     // SB_DRAGON
	{mogBankFileBe, &mogBankCreature, 0x57BE},     // SB_BE
	{mogBankFileDemon, &mogBankCreature, 0xB27C},     // SB_DEMON
	{mogBankFileTroll, &mogBankCreature, 0xBBC8},     // SB_TROLL
	{mogBankFileScreen, &mogBankCreature, 0x2F78},     // SB_SCREEN
	{mogBankFileTroggSpear, &mogBankCreature, 0xAB28},     // SB_TROGG_SPEAR
	{mogBankFileRatmen, &mogBankRatmen, 0xD508},     // SB_RATMEN
	{mogBankFileMudmen, &mogBankCreature, 0xB690},     // SB_MUDMEN
	{mogBankFileWizard, &mogBankWizard, 0xD6D8},     // SB_WIZARD
};

}  // namespace

extern "C" {

void rtSbFile(const char *szName, ULONG ulDst, ULONG ulSize) {
	rt::FileHandle sFile(szName);
	mogFileErr[1] = sFile.isOpen() ? 0 : static_cast<UWORD>(-1);   // LAB_0BB5: the open result lands in the error word
	sFile.skip(0x20);                                              // LAB_0BEA: the bank file's header
	sFile.read(reinterpret_cast<void *>(static_cast<uintptr_t>(ulDst)), ulSize);   // LAB_0BD7
	sFile.close();                                                 // LAB_0BFF
}

void rtSbLoad(uint32_t ulBank) {
	if(ulBank >= SB_COUNT) {
		return;
	}
	const Bank &sBank = s_aBanks[ulBank];
	rtSbFile(sBank.szName, *sBank.pulDest, sBank.ulSize);
}

void rtSbStart(void) {
	rt_synth_init();                                // JSR LAB_0F89
	ULONG *pulSlot = mogHooks;                  // the first free slot of the hook list gets the synth tick
	while(*pulSlot++ != 0) {
	}
	pulSlot[-1] = reinterpret_cast<ULONG>(rt_synth_tick);
#if MS_SYNTH_ASM
	rt::irqSetInt4(reinterpret_cast<void *>(mog_LAB_0F69));
#else
	rt::irqSetInt4(reinterpret_cast<void *>(rt_synth_int4));
#endif
	rtSbFile(mogBankFileCampaign, mogBankCampaign, 0xD924);   // the campaign bank
	rt_synth_reloc();                               // JSR LAB_0FD4
	rt::sfxReset();                                 // MOVE.W #0,LAB_0AA6
}

}  // extern "C"

