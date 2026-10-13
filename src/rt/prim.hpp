// rt/prim - the primitives of mog's code as typed C++ calls (ROADMAP 11.1d).
//
// Until 7.1 the game's scenes called small routines of the original (set the draw target, fade, blit, read the joystick, ...) by
// address with a 68k register contract (`RT_FN(rt_mog_x)` + a `jsr (An)` call helper, docs/PC_PORT.md section 4).  Every one of
// those routines is a C++ function by now; its `rt_mog_x` entry is a register shim in src/rt/amiga/*_entries.cpp that loads the
// registers into C arguments, calls the body and puts the register results back.  C++ callers do not need the shim: they call the
// body, named here.  The Amiga code gets shorter (no register block, no indirect jsr) and behaves the same, because the shim
// only translated the registers; the PC build links the same bodies.
//
// What this header lists is only what the already converted callers use; a primitive is added here when its last `RT_FN` caller
// goes.  Each declaration names the shim it replaces and the file that defines the body.
#pragma once
#include <stdint.h>

extern "C" {

// ---- src/rt/prims.cpp -------------------------------------------------------------------------------------------------------
void rtDrawBufClear(void);                                  // rt_mog_draw_buf_clear, LAB_03A7: the draw-event buffer := $FF
void rtCreatureClear(void);                                 // rt_mog_creature_clear, LAB_02CE: the 20 creature records := 0
void rtDaggerClear(void);                                   // rt_mog_dagger_clear, LAB_02F2: the six dagger trajectories := 0
void rtSetPlanes(uint32_t ulBase, uint32_t *pulPlanes);     // rt_mog_set_planes, LAB_0426+2 (plane bases out: [0..4])

// ---- src/rt/mog_display.cpp -------------------------------------------------------------------------------------------------
void rtMogBlitBothC(void);                                  // rt_mog_blit_both, LAB_0418 (also rt::displayBlitBackgroundBoth)
void rtMogBlitScreenC(uint32_t ulSrc, uint32_t ulDst);      // rt_mog_blit_screen, LAB_0419 (also rt::displayBlitScreen)

// ---- src/rt/palette_glue.cpp ------------------------------------------------------------------------------------------------
void rtMogFadeOutC(void);                                   // rt_mog_fade_out, LAB_03F0
void rtMogFadeOutSilentC(void);                             // rt_mog_fade_out_silent, LAB_03F1
void rtMogFadeToC(const uint16_t *puwTable);                // rt_mog_fade_to, LAB_03F2 (A0 = the 32-word target)

// ---- src/rt/input.cpp, src/rt/display_ops.cpp --------------------------------------------------------------------------------
uint32_t rtMogJoyReadC(void);                               // rt_mog_joy_read, LAB_00EE: both ports, port 0 << 16 | port 1
void rtMogKeyResetC(void);                                  // rt_mog_key_reset
uint32_t rtMogKeyXlatC(uint32_t ulIndex);                   // rt_mog_key_xlat, LAB_0D8D: (index << 16) | the game key (D0)

// ---- src/rt/engine_palette.cpp ----------------------------------------------------------------------------------------------
// The colour-ramp slot allocator (rt_palette_ramp_add_mog with ulMog = 1): the slot address or 0 when every slot is busy.  The register
// entry gave D0 back unchanged in that case; a caller that read it as the job handle does the same (rt/scene_menu.cpp).
uint32_t rtPaletteRampAdd(uint32_t ulMog, uint32_t ulD0, uint32_t ulD1, uint32_t ulD2, uint32_t ulD3);

// ---- src/rt/synth.cpp ------------------------------------------------------------------------------------------------------
void rtSynthStartC(uint32_t ulSeq, uint32_t ulChannel);     // rt_synth_start, LAB_0F8C: start sound sequence ulSeq on channel ulChannel
void rtSynthFadeC(void);                                    // rt_synth_fade, LAB_0FC2: the music fade step (reads mogMusicFade)

// ---- src/rt/engine_blit.cpp -------------------------------------------------------------------------------------------------
void rtDrawCel_mog(int32_t lFrame, int32_t lX, int32_t lY, const uint8_t *pCel);   // rt_mog_draw_cel, LAB_0CDA (pCel = A0)
void rtDrawCel_prg(int32_t lFrame, int32_t lX, int32_t lY, const uint8_t *pCel);   // rt_prg_draw_cel, LAB_04B4

// ---- the program overlay's primitives (rt_prg_* / the *_prg twins), used by the scene host src/rt/engine_scenes.cpp -----------------------
void rtRestorePass_prg(void);                               // rt_prg_restore_pass, LAB_0242 (src/rt/prims.cpp)
void rtPrgDisplaySwapC(void);                               // rt_prg_display_swap, LAB_054C (src/rt/display_ops.cpp)
void rtPrgDisplayInitC(void);                               // rt_prg_display_init, SECSTRT_29
void rtDisplayPaletteWriteC(const uint16_t *puwTable);      // rt_display_palette_write, LAB_0565: wait for the beam, write 32 colours
void rtPrgKeyResetC(void);                                  // rt_prg_key_reset, LAB_035E (src/rt/input.cpp)
void rtPrgPaletteHookAddC(uint32_t ulLive);                 // rt_prg_palette_hook_add, SECSTRT_31 (src/rt/palette_glue.cpp)
void rtPaletteSlotFreeC(uint32_t ulSlot);                   // rt_palette_slot_free, LAB_0579 / LAB_0E59: clear a colour cycle / ramp slot
void rtPalCopyLive_prg(const uint16_t *puwTable);           // rt_prg_pal_copy_live, LAB_025D: table -> the live palette
void rtPalCopyLive_mog(const uint16_t *puwTable);           // rt_mog_pal_copy_live, LAB_03EE
void rtPaletteSetTarget(uint32_t ulMog, uint32_t ulTarget, uint32_t ulPeriod);   // rt_palette_set_target_{prg,mog} (src/rt/engine_palette.cpp)
void rtMusicStartC(void);                                   // rt_music_start, SECSTRT_1 (src/rt/audio.cpp)
void rtMusicStopC(void);                                    // rt_music_stop, LAB_005B
void rtPrgTextList(const void *pRecords);                   // rt_prg_text_list, LAB_028F (src/rt/gtext.cpp)
uint32_t rtRncDecode(uint8_t *pBuf);                        // rt_rnc_decode, LAB_0190: unpacked size (src/rt/engine_rnc.cpp)
void rtJobResetAll(void);                                   // rt_job_reset, LAB_01E4: the job pool and the draw lists (src/rt/engine_jobs.cpp)
uint32_t rtLoadPicMem_prg(uint8_t *pImage);                 // rt_prg_pic_mem, LAB_03FC (src/rt/loaders.cpp)
uint32_t rtLoadPicFile_prg(const char *szName, uint8_t *pBuf);   // rt_prg_pic_file, LAB_0402
uint32_t rtLoadCel_prg(const char *szName, uint8_t *pBuf);  // rt_prg_cel_load, LAB_0496
uint32_t rtCelSize_prg(const char *szName);                 // rt_prg_cel_size, LAB_0491
void rtCelInit_prg(uint32_t ulPlanes);                      // rt_prg_cel_init, SECSTRT_23: D7 = screen planes (src/rt/engine_blit.cpp)
void rtCelClip_prg(uint32_t ulLeft, uint32_t ulTop, uint32_t ulRight, uint32_t ulBottom);   // rt_prg_cel_clip, LAB_04A7
void rtCelMirror_prg(int32_t lFrame, uint8_t *pCel);        // rt_prg_cel_mirror, LAB_04A8: flip a frame in place
uint32_t rtCopyRect(uint32_t ulSrc, uint32_t ulDst, uint32_t ulModA, uint32_t ulModD, uint32_t ulWords, uint32_t ulRows);   // rt_prg_copy_rect, LAB_04E1: A to D copy, BLTSIZE out (src/rt/engine_blit.cpp)
void rtCelScratch_prg(void);                                // rt_prg_cel_scratch, SECSTRT_25
uint32_t rtCopyRectDesc(uint32_t ulSrc, uint32_t ulDst, uint32_t ulModA, uint32_t ulModD, uint32_t ulWords, uint32_t ulRows);   // rt_prg_copy_rect_desc, LAB_04E2

}  // extern "C"

namespace rt {

// mog LAB_0426+2: the draw target base; the five plane bases and the cel renderer's destination cells follow from it.
inline void setDrawTarget(uint32_t ulScreen) {
	uint32_t aulPlanes[5];
	rtSetPlanes(ulScreen, aulPlanes);
}

}  // namespace rt
