// rt/sdl/reg_entries - the register-ABI entries of the mog scene cluster for the PC (ROADMAP 11.10; docs/PC_PORT.md section 6.1, "11.1d part 2").
//
// arena.cpp, combat_ui.cpp (and the rest of the cluster) still call the routines of mog's code by address with a 68k register contract: `call(RT_FN(rt_x),
// d0, a0, ...)` fills a rt::CallRegs and goes through rtAsmCall (src/rt/asmcall.hpp), which on the Amiga loads the registers and JSRs the shim of
// src/rt/amiga/*_entries.cpp.  The PC has no registers: rtAsmCall here calls `rt_x` as `void rt_x(rt::CallRegs &)`, and each entry below does what the
// Amiga shim does with the registers (arguments out of CallRegs, results back into rd0 / rd1 / ra0 / ra1) around the same C++ body.
//
// The entries that a PC run has not needed yet are `unimplemented`: they log "STUB rt_x" once when called (MS_STUB, ROADMAP 11.1d).  Each is the work item
// of its cluster file's conversion; what the scenes do not reach before the title screen stays so.
// TODO(11.1d): typed call tables replace rtAsmCall and this file.
#include <stdint.h>

#include "rt/asmcall.hpp"
#include "rt/display_ops.hpp"
#include "rt/mog_display.hpp"
#include "rt/prim.hpp"
#include "rt/sdl/log.hpp"

extern "C" {
// bodies behind the entries (the files are named in rt/prim.hpp and the Amiga shims)
void rtMogJobsReset(void);                                  // src/rt/mainloop.cpp
uint32_t rtMogJoyReadC(void);                               // src/rt/input.cpp
void rtMogFadeOutC(void);                                   // src/rt/palette_glue.cpp
void rtMogFadeOutSilentC(void);
void rtMogPaletteClearC(void);
void rtMogPaletteSceneC(uint32_t ulScene);
void rtMogBlitBothC(void);
void rtMogSpritePlaceC(uint32_t ulSprite, uint32_t ulX, uint32_t ulY);   // src/rt/sprites.cpp
uint32_t rtMogSpriteBuildC(const void *pRecord, uint32_t ulFrame, uint32_t ulOut, uint32_t *pulSecond);
void rtSynthStartC(uint32_t ulSeq, uint32_t ulChannel);     // src/rt/synth.cpp
void rtKnightRecalcHp(uint32_t ulKnight);                   // src/rt/game_rules.cpp
void rtKnightRecalcEndurance(uint32_t ulKnight);
uint32_t rtMogTextString(const void *pText, uint32_t ulX, uint32_t ulY, uint32_t ulStyle);   // src/rt/gtext.cpp
uint32_t rtMogTextList(const void *pRecords);
uint8_t *rtMogAddRecord(void);
extern uint8_t mogRecordTemplate[];                         // the record template (LAB_0A58 block)
extern uint32_t mogRecordTable;                             // SECSTRT_14
uint32_t rtLoadCel_mog(const char *szName, uint8_t *pBuf);  // src/rt/loaders.cpp
uint32_t rtLoadPicFile_mog(const char *szName, uint8_t *pBuf);
uint32_t rtLoadPicMem_mog(uint8_t *pImage);
uint32_t rtCelSize_mog(const char *szName);
void rtRestorePass_mog(void);                               // src/rt/prims.cpp
void rtCelMirror_mog(int32_t lFrame, uint8_t *pCel);        // src/rt/engine_blit.cpp
void rtPalCopyLive_mog(const uint16_t *puwTable);           // src/rt/palette_glue.cpp
void rtArArena0168(void); void rtArArena016A(void); void rtArArena0175(void); void rtArArena0188(void); void rtArArena018C(void);
void rtArArena0196(void); void rtArArena019A(void); void rtArArena019E(void); void rtArArena01A0(void);
void rtArArenaRow9(void); void rtArArenaRow10(void); void rtArArenaRow11(void); void rtArArenaRow12(void);
void rtArInit0169(uint32_t); void rtArInit0170(uint32_t); void rtArInit0176(uint32_t); void rtArInit018B(uint32_t); void rtArInit018F(uint32_t);
void rtArInit0198(uint32_t); void rtArInit019D(uint32_t); void rtArInit019F(uint32_t);
void rtArInitRow10(uint32_t); void rtArInitRow11(uint32_t); void rtArInitRow12(uint32_t); void rtArInitRow13(uint32_t); void rtArInitRow14(uint32_t);
void rtArInitRow15(uint32_t);
void rtArSwap016B(void); void rtArSwap0189(void); void rtArSwap018D(void); void rtArSwap019B(void); void rtArSwap0197(void);
}

// ---- the call helper ------------------------------------------------------------------------------------------------------------------

extern "C" void rtAsmCall(uint32_t ulFn, rt::CallRegs *pRegs) {
	reinterpret_cast<void (*)(rt::CallRegs &)>(static_cast<uintptr_t>(ulFn))(*pRegs);
}

namespace {

constexpr uint32_t kWordMask = 0xFFFF;   // the original left D0 = $0000FFFF where a DBF loop ended

}  // namespace

// An entry that is not wired yet: reports itself once (rt/sdl/log.hpp MS_STUB) and leaves the registers as they are.
#define MS_UNIMPLEMENTED_ENTRY(name) extern "C" void name(rt::CallRegs &) { MS_STUB("11.1d"); }   // TODO(11.1d): the cluster file that owns the routine

// ---- display, palette, blitter, input ---------------------------------------------------------------------------------------------------

extern "C" {

void rt_mog_display_flip(rt::CallRegs &) { rt::displayFlip(); }
void rt_mog_blit_both(rt::CallRegs &) { rtMogBlitBothC(); }
void rt_display_wait_frames(rt::CallRegs &r) { rt::displayWaitFrames(r.d0); }
void rt_display_palette_write(rt::CallRegs &r) { rt::displayWritePalette(reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(r.a0))); }
void rt_mog_pal_copy_live(rt::CallRegs &r) { rtPalCopyLive_mog(reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(r.a0))); }
void rt_mog_palette_clear(rt::CallRegs &r) { rtMogPaletteClearC(); r.rd0 = kWordMask; }
void rt_mog_fade_out(rt::CallRegs &) { rtMogFadeOutC(); }
void rt_mog_fade_out_silent(rt::CallRegs &) { rtMogFadeOutSilentC(); }
void rt_mog_palette_scene(rt::CallRegs &r) { rtMogPaletteSceneC(r.d0); }
void rt_mog_restore_pass(rt::CallRegs &) { rtRestorePass_mog(); }

// LAB_0426+2: D0 = the screen base; out A1..A5 = the five plane bases, D0 = A5
void rt_mog_set_planes(rt::CallRegs &r) {
	uint32_t aulPlanes[5];
	rtSetPlanes(r.d0, aulPlanes);
	r.ra1 = aulPlanes[0];
	r.rd0 = aulPlanes[4];
}

// LAB_00EE: D0.w = port 0 bits, D1.w = port 1 bits
void rt_mog_joy_read(rt::CallRegs &r) {
	const uint32_t ulPorts = rtMogJoyReadC();
	r.rd0 = ulPorts >> 16;
	r.rd1 = ulPorts & kWordMask;
}

void rt_mog_draw_cel(rt::CallRegs &r) { rtDrawCel_mog(static_cast<int32_t>(r.d0), static_cast<int32_t>(r.d1), static_cast<int32_t>(r.d2), reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(r.a0))); }
void rt_mog_cel_mirror(rt::CallRegs &r) { rtCelMirror_mog(static_cast<int32_t>(r.d0), reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(r.a0))); }
void rt_mog_sprite_place(rt::CallRegs &r) { rtMogSpritePlaceC(r.d0, r.d1, r.d2); }

// SECSTRT_37: A0 = sprite record, D0.w = frame, A1 = output buffer; out A1 = end of the data, A0 = the partner sprite of an attached frame (else unchanged)
void rt_mog_sprite_build(rt::CallRegs &r) {
	uint32_t ulSecond = 0;
	r.ra1 = rtMogSpriteBuildC(reinterpret_cast<const void *>(static_cast<uintptr_t>(r.a0)), r.d0, r.a1, &ulSecond);
	r.ra0 = ulSecond != 0 ? ulSecond : r.a0;
}

// LAB_0CBB / LAB_0C27 / LAB_0C21 / LAB_0CB6: A0 = name (or the image), A1 = destination; out D0 = the size
void rt_mog_cel_load(rt::CallRegs &r) { r.rd0 = rtLoadCel_mog(reinterpret_cast<const char *>(static_cast<uintptr_t>(r.a0)), reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(r.a1))); }
void rt_mog_pic_file(rt::CallRegs &r) { r.rd0 = rtLoadPicFile_mog(reinterpret_cast<const char *>(static_cast<uintptr_t>(r.a0)), reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(r.a1))); }
void rt_mog_pic_mem(rt::CallRegs &r) { r.rd0 = rtLoadPicMem_mog(reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(r.a0))); }
void rt_mog_cel_size(rt::CallRegs &r) { r.rd0 = rtCelSize_mog(reinterpret_cast<const char *>(static_cast<uintptr_t>(r.a0))); }

// the text drawers: the extent comes back as D1 = width (high word), D0 = height (low word)
void rt_mog_text_string(rt::CallRegs &r) {
	const uint32_t ulExtent = rtMogTextString(reinterpret_cast<const void *>(static_cast<uintptr_t>(r.a0)), r.d0 & kWordMask, r.d1 & kWordMask, r.d2 & kWordMask);
	r.rd1 = (ulExtent >> 16) & kWordMask;
	r.rd0 = ulExtent & kWordMask;
}
void rt_mog_text_list(rt::CallRegs &r) {
	const uint32_t ulExtent = rtMogTextList(reinterpret_cast<const void *>(static_cast<uintptr_t>(r.a0)));
	r.rd1 = (ulExtent >> 16) & kWordMask;
	r.rd0 = ulExtent & kWordMask;
}

// LAB_0448: a record into the pool; out A0 = the record + 24, A1 = the template + 24, D0 = $FFFF; the pool full: A0 = table + 2352, D0 = 1
void rt_mog_add_record(rt::CallRegs &r) {
	const uint32_t ulRecord = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rtMogAddRecord()));
	if(ulRecord != 0) {
		r.ra0 = ulRecord + 24;
		r.ra1 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mogRecordTemplate)) + 24;
		r.rd0 = kWordMask;
	}
	else {
		r.ra0 = mogRecordTable + 2352;
		r.rd0 = 1;
	}
}

// ---- sound, jobs, creatures ---------------------------------------------------------------------------------------------------------------

void rt_synth_start(rt::CallRegs &r) { rtSynthStartC(r.d0 & kWordMask, r.d1 & kWordMask); }
void rt_mog_jobs_reset(rt::CallRegs &) { rtMogJobsReset(); }
void rt_mog_creature_clear(rt::CallRegs &r) { rtCreatureClear(); r.rd0 = kWordMask; }
void rt_mog_dagger_clear(rt::CallRegs &r) { rtDaggerClear(); r.rd0 = kWordMask; }
void rt_knight_recalc_hp(rt::CallRegs &r) { rtKnightRecalcHp(r.a0); }
void rt_knight_recalc_endurance(rt::CallRegs &r) { rtKnightRecalcEndurance(r.a0); }

// ---- the creature tables of arena.cpp (src/rt/amiga/arena_entries.cpp): plain calls / the record in A1 -----------------------------------------

void rt_ar_arena_0168(rt::CallRegs &) { rtArArena0168(); }
void rt_ar_arena_016a(rt::CallRegs &) { rtArArena016A(); }
void rt_ar_arena_0175(rt::CallRegs &) { rtArArena0175(); }
void rt_ar_arena_0188(rt::CallRegs &) { rtArArena0188(); }
void rt_ar_arena_018c(rt::CallRegs &) { rtArArena018C(); }
void rt_ar_arena_0196(rt::CallRegs &) { rtArArena0196(); }
void rt_ar_arena_019a(rt::CallRegs &) { rtArArena019A(); }
void rt_ar_arena_019e(rt::CallRegs &) { rtArArena019E(); }
void rt_ar_arena_01a0(rt::CallRegs &) { rtArArena01A0(); }
void rt_ar_arena_row9(rt::CallRegs &) { rtArArenaRow9(); }
void rt_ar_arena_row10(rt::CallRegs &) { rtArArenaRow10(); }
void rt_ar_arena_row11(rt::CallRegs &) { rtArArenaRow11(); }
void rt_ar_arena_row12(rt::CallRegs &) { rtArArenaRow12(); }
void rt_ar_init_0169(rt::CallRegs &r) { rtArInit0169(r.a1); }
void rt_ar_init_0170(rt::CallRegs &r) { rtArInit0170(r.a1); }
void rt_ar_init_0176(rt::CallRegs &r) { rtArInit0176(r.a1); }
void rt_ar_init_018b(rt::CallRegs &r) { rtArInit018B(r.a1); }
void rt_ar_init_018f(rt::CallRegs &r) { rtArInit018F(r.a1); }
void rt_ar_init_0198(rt::CallRegs &r) { rtArInit0198(r.a1); }
void rt_ar_init_019d(rt::CallRegs &r) { rtArInit019D(r.a1); }
void rt_ar_init_019f(rt::CallRegs &r) { rtArInit019F(r.a1); }
void rt_ar_init_row10(rt::CallRegs &r) { rtArInitRow10(r.a1); }
void rt_ar_init_row11(rt::CallRegs &r) { rtArInitRow11(r.a1); }
void rt_ar_init_row12(rt::CallRegs &r) { rtArInitRow12(r.a1); }
void rt_ar_init_row13(rt::CallRegs &r) { rtArInitRow13(r.a1); }
void rt_ar_init_row14(rt::CallRegs &r) { rtArInitRow14(r.a1); }
void rt_ar_init_row15(rt::CallRegs &r) { rtArInitRow15(r.a1); }
void rt_ar_swap_016b(rt::CallRegs &) { rtArSwap016B(); }
void rt_ar_swap_0189(rt::CallRegs &) { rtArSwap0189(); }
void rt_ar_swap_018d(rt::CallRegs &) { rtArSwap018D(); }
void rt_ar_swap_019b(rt::CallRegs &) { rtArSwap019B(); }
void rt_ar_swap_0197(rt::CallRegs &) { rtArSwap0197(); }

}  // extern "C"

// ---- not wired yet: the rest of the cluster ---------------------------------------------------------------------------------------------------

MS_UNIMPLEMENTED_ENTRY(rt_creature_spawn)         // creatures.cpp (rtCreatureSpawn)
MS_UNIMPLEMENTED_ENTRY(rt_job_create)             // creatures.cpp (rtJobCreate)
MS_UNIMPLEMENTED_ENTRY(rt_arena_meet)             // combat.cpp
MS_UNIMPLEMENTED_ENTRY(rt_arena_dragon)           // combat.cpp
MS_UNIMPLEMENTED_ENTRY(rt_combat_tick)            // combat_script.cpp
MS_UNIMPLEMENTED_ENTRY(rt_mog_arena_default)      // arena_bg.cpp
MS_UNIMPLEMENTED_ENTRY(rt_palette_set_target_mog) // engine_palette_entries (rtPaletteSetTarget)
MS_UNIMPLEMENTED_ENTRY(rt_places_gift_gold)       // scene_places_entries
MS_UNIMPLEMENTED_ENTRY(rt_places_gift_item)
MS_UNIMPLEMENTED_ENTRY(rt_places_math_roll)
MS_UNIMPLEMENTED_ENTRY(rt_scr_dice_idle)          // screens.cpp
MS_UNIMPLEMENTED_ENTRY(rt_scr_lines)
MS_UNIMPLEMENTED_ENTRY(rt_scr_ramp5)
