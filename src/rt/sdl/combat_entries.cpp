// rt/sdl/combat_entries - the PC side of src/rt/combat.cpp (ROADMAP 11.1d part 2): the register entries of the fight and screen loops (the patch targets of
// src/rt/amiga/combat_entries.cpp) and the call helper rtFightAsm, which on the PC calls a `void entry(rt::CallRegs &)` (rt/asmcall.hpp) instead of JSRing
// an address with loaded registers.  The entries are the Amiga shims with the registers read from / written to the CallRegs.
#include <stdint.h>

#include "rt/asmcall.hpp"

extern "C" {

// bodies (src/rt/combat.cpp)
void rtFightRun(void);
void rtFightMeet(uint32_t ulFirst, uint32_t ulSecond);
void rtFightCreature(uint32_t ulLair);
void rtFightDragon(void);
void rtArenaMeet(void);
void rtArenaDragon(void);
void rtScreenRun(uint32_t ulScene);
void rtScreenRedraw(void);

// The register block of combat.cpp's `call` (layout shared with it): inputs D0-D3, A0-A2, outputs D0 / D1 / A0 / A1.
struct FightAsmRegs {
	uint32_t d0, d1, d2, d3, a0, a1, a2;
	uint32_t rd0, rd1, ra0, ra1;
};

void rtFightAsm(uint32_t ulFn, FightAsmRegs *pRegs) {
	rt::CallRegs r = {pRegs->d0, pRegs->d1, pRegs->d2, pRegs->d3, 0, pRegs->a0, pRegs->a1, pRegs->a2, 0, 0, 0, 0};
	rtAsmCall(ulFn, &r);
	pRegs->rd0 = r.rd0;
	pRegs->rd1 = r.rd1;
	pRegs->ra0 = r.ra0;
	pRegs->ra1 = r.ra1;
}

void rt_fight_run(rt::CallRegs &) { rtFightRun(); }                                                   // LAB_0036
void rt_fight_meet(rt::CallRegs &r) { rtFightMeet(r.a0, r.a1); r.rd0 = 1; }                           // LAB_004F: A0 = first knight, A1 = second knight
void rt_fight_creature(rt::CallRegs &r) { rtFightCreature(r.a1); r.rd0 = 1; }                         // LAB_005B: A1 = lair record
void rt_fight_dragon(rt::CallRegs &) { rtFightDragon(); }                                             // LAB_0083
void rt_arena_meet(rt::CallRegs &) { rtArenaMeet(); }                                                 // LAB_0164 (also the creature table LAB_08C8)
void rt_arena_dragon(rt::CallRegs &) { rtArenaDragon(); }                                             // LAB_0192
void rt_screen_run(rt::CallRegs &r) { rtScreenRun(r.d0); }                                            // LAB_04CF: D0 = scene id
void rt_screen_redraw(rt::CallRegs &) { rtScreenRedraw(); }                                           // LAB_04D4

}  // extern "C"
