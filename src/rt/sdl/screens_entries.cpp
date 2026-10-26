// rt/sdl/screens_entries - the PC side of src/rt/screens.cpp (ROADMAP 11.1d part 2): the register entries rt_scr_* (the patch targets
// of src/rt/amiga/screens_entries.cpp; contracts: the comment there), as `void entry(rt::CallRegs &)` functions around the same C++ bodies.
#include <stdint.h>

#include "rt/asmcall.hpp"
#include "rt/sdl/handlers.hpp"

extern "C" {

// bodies (src/rt/screens.cpp)
void rtScrMystic(void);
void rtScrHealer(void);
void rtScrDice(void);
void rtScrRitual(void);
void rtScrDiceIdle(uint32_t ulRecord);
void rtScrRitualDone(uint32_t ulRecord);
const uint8_t *rtScrLines(const uint8_t *pText, uint32_t ulX, uint32_t ulY, uint32_t ulStyle);
uint8_t *rtScrRamp5(uint8_t *pBase, uint32_t ulOffset);

extern uint32_t mogCurKnight;             // LAB_0633

void rt_scr_mystic(rt::CallRegs &) { rtScrMystic(); }       // LAB_047C
void rt_scr_healer(rt::CallRegs &) { rtScrHealer(); }       // LAB_048E
void rt_scr_dice(rt::CallRegs &) { rtScrDice(); }           // LAB_04A6
void rt_scr_ritual(rt::CallRegs &) { rtScrRitual(); }       // LAB_04BF

// LAB_049E: A0 = list of lines, D0 = x, D1 = y, D2 = style (all registers kept)
void rt_scr_lines(rt::CallRegs &r) { rtScrLines(reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(r.a0)), r.d0, r.d1, r.d2); }

// LAB_04C5: A0 = base, D0 = byte offset.  Out A0 = the last word written, A1 = the knight LAB_0633
void rt_scr_ramp5(rt::CallRegs &r) {
	r.ra0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rtScrRamp5(reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(r.a0)), r.d0)));
	r.ra1 = mogCurKnight;
}

// LAB_04AC / LAB_04C4: the script handlers (typed rt::sdl::HandlerEntry: the owner in, the HandlerResult out); they leave through LAB_02BA like the originals
void rt_scr_dice_idle(uint32_t ulOwner, ms::game::HandlerResult *pOut) {
	rtScrDiceIdle(ulOwner);
	rt::sdl::handlerExit(pOut);
}
void rt_scr_ritual_done(uint32_t ulOwner, ms::game::HandlerResult *pOut) {
	rtScrRitualDone(ulOwner);
	rt::sdl::handlerExit(pOut);
}

}  // extern "C"
