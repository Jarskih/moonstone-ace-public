// rt/combat_load - the C entries of the combat data loaders (src/rt/combat_load.cpp, ROADMAP 7.1f2) that the other rt files call
// directly (ROADMAP 7.1o: the asm labels LAB_00F8 / 0115..0155 are no longer patched into JMP stubs; only LAB_012D keeps its
// rt_cl_select shim because the asm LAB_00B4 jumps to it).  Plain C functions, normal C ABI (callee-saved D2-D7/A2-A6); the
// originals took their input in A0 only for the two message routines, which take it as the argument here.
#pragma once
#include <stdint.h>

extern "C" {
void rtClDriveInit(void);                    // LAB_00F8  drive set-up cells
void rtClKnights(void);                      // LAB_0115  kn1-3.ob, kn4 hit set, blo.cel
void rtClHe(void);                           // LAB_0116  creature set He1-3.ob (kind $0C)
void rtClTroggSpear(void);                   // LAB_0118  kind $20
void rtClTroggAxe(void);                     // LAB_011A  kind $18
void rtClRatmen(void);                       // LAB_011C  kind $24
void rtClMudmen(void);                       // LAB_011E  kind $04
void rtClBalok(void);                        // LAB_011F  kind $30 (+ Kn5.ob)
void rtClDragon(void);                       // LAB_0121  kind $14 (+ Kn5.ob)
// ROADMAP 9.5e3: a creature cel set by id (CELS_*, include/game/api/data.hpp) with an optional sound bank (-1 = the set's own)
void rtClSet(uint8_t ubSet, int8_t sbBank);
void rtClBe(void);                           // LAB_0123  kind $00
void rtClDemon(void);                        // LAB_0125  kind $08
void rtClTroll(void);                        // LAB_0126  kind $40 (+ Kn5.ob)
void rtClKiMi(void);                         // LAB_0128  ki.cel / mi.c into the map slots
void rtClMoon(void);                         // LAB_012B  the moon over ch.piv
void rtClAssets(void);                       // LAB_012C  message.piv, ch.piv, bold.f, Small.font
void rtClSelect(void);                       // LAB_012D  Sel.cel + ch.piv screen (the caller leaves through the hunk-9 exit itself)
void rtClHighWood(void);                     // LAB_012E  place picture HighWood.piv
void rtClWaterDeep(void);                    // LAB_012F  place picture WaterDeep.piv
void rtClWizard(void);                       // LAB_0131  wizard screen (kind $3C)
void rtClMessageNext(void);                  // LAB_0134  next text of the message table
void rtClMessageText(uint32_t ulList);       // LAB_0136  message screen, A0 = the text record list
void rtClMessageRecoloured(uint32_t ulList); // LAB_0137  message screen with the avoid-text colours, A0 = the list
void rtClPack(void);                         // LAB_013A  the arena pack "Test"
void rtClArenaPicture(void);                 // LAB_013C  arena picture and backdrop by LAB_08C4
void rtClTablesClear(void);                  // LAB_0152  clear + fill the fighter script tables
void rtClTables(void);                       // LAB_0155  fill the fighter script tables
}
