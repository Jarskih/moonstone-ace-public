// Test support for tests/test_arena_emu.py (ROADMAP 7.1j): linked with src/rt/arena.cpp, src/game/creatures.cpp and
// src/engine/util.cpp into one flat 68020 blob that runs next to the ORIGINAL routines of the reassembled mog image.
//   * rtFightTablesInit / rtKnightDefaults are the two C entries src/rt/fighters.cpp provides in the game (7.1h).  Here the
//     first stores the sixteen handler identities the original LAB_01AE stores inline, the second runs the original
//     LAB_01C6 of the image.
//   * rt_ar_tables / rt_ar_clear_links / rt_ar_reset / rt_ar_knights are the register-saving entries the game no longer links (ROADMAP
//     7.1o: LAB_0156 / 0161 / 01AE / 01BE are called as rtArTables ... directly); the test enters them in place of the original labels.
//   * rtKnightTables (src/rt/combat.cpp in the game, LAB_0167) is the original's A1 contract here: the test's logging stub of LAB_0167 reads A1.
//     The creature loaders rtCl* (src/rt/combat_load.cpp) are the logging stubs of the original labels (test_arena_emu.CL_LABELS).
//   * t_* are register entries for the C++ helpers arena.cpp offers (rt::arena*), with the register contract of the
//     original routine named next to each, so the test can call the original and the C++ with the same registers.
#include <stdint.h>

#include "rt/arena.hpp"

extern "C" {
extern uint8_t mogHandlerTable[], mog_LAB_0251[], mog_LAB_0226[], mog_LAB_0EFF[], mog_LAB_01CA[], mog_LAB_027A[], mog_LAB_0ED2[],  // LAB_08C7
	mog_LAB_0236[], mog_LAB_02D2[], mog_LAB_0298[], mog_LAB_02CB[], mog_LAB_029F[], mog_LAB_0EC2[], mog_SECSTRT_40[];

void rtFightTablesInit(void) {
	uint32_t *t = reinterpret_cast<uint32_t *>(mogHandlerTable);
	t[9] = (uint32_t)(uintptr_t)mog_LAB_0251;      // +36
	t[1] = (uint32_t)(uintptr_t)mog_SECSTRT_40;    // +4
	t[0] = (uint32_t)(uintptr_t)mog_LAB_0226;
	t[4] = (uint32_t)(uintptr_t)mog_LAB_0EFF;      // +16
	t[14] = (uint32_t)(uintptr_t)mog_LAB_01CA;     // +56
	t[5] = (uint32_t)(uintptr_t)mog_LAB_027A;      // +20
	t[2] = (uint32_t)(uintptr_t)mog_LAB_0ED2;      // +8
	t[3] = (uint32_t)(uintptr_t)mog_LAB_01CA;      // +12
	t[6] = (uint32_t)(uintptr_t)mog_LAB_0236;      // +24
	t[7] = (uint32_t)(uintptr_t)mog_LAB_0236;      // +28
	t[8] = (uint32_t)(uintptr_t)mog_LAB_0236;      // +32
	t[10] = (uint32_t)(uintptr_t)mog_LAB_02D2;     // +40
	t[11] = (uint32_t)(uintptr_t)mog_LAB_0298;     // +44
	t[13] = (uint32_t)(uintptr_t)mog_LAB_02CB;     // +52
	t[12] = (uint32_t)(uintptr_t)mog_LAB_029F;     // +48
	t[16] = (uint32_t)(uintptr_t)mog_LAB_0EC2;     // +64
}

// rtClSet (src/rt/combat_load.cpp in the game): the test's loaders are the logging stubs of the original labels
void rtClTroggAxe(void); void rtClTroggSpear(void); void rtClRatmen(void); void rtClMudmen(void); void rtClBalok(void); void rtClBe(void);
void rtClTroll(void); void rtClDragon(void); void rtClDemon(void);
void rtClSet(uint8_t ubSet, int8_t) {
	static void (*const aLoad[])(void) = {rtClTroggAxe, rtClTroggSpear, rtClRatmen, rtClMudmen, rtClBalok, rtClBe, rtClTroll, rtClDragon, rtClDemon};
	aLoad[ubSet]();
}

uint32_t rtTAlloc(void) { return rt::arenaAlloc(); }
void rtTSpawn(uint32_t ulRecord, uint32_t ulScript) { rt::arenaSpawn(ulRecord, ulScript); }
void rtTClear(void) { rt::arenaClearKnights(); }
void rtTCommon(uint32_t ulMode) { rt::arenaCommonSetup(ulMode); }
void rtTPlace(void) { rt::arenaPlaceAttacker(); }
}

asm(R"(
	.text
	.globl rt_ar_tables
rt_ar_tables:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtArTables
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_ar_clear_links
rt_ar_clear_links:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtArClearLinks
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_ar_reset
rt_ar_reset:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtArReset
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rt_ar_knights
rt_ar_knights:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	jsr rtArKnights
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts

	.globl rtKnightTables
rtKnightTables:
	move.l 4(%sp),%a1
	jsr mog_LAB_0167
	rts

	.globl rtKnightDefaults
rtKnightDefaults:
	move.l 4(%sp),%a1
	jsr mog_LAB_01C6
	rts

	| LAB_0171: no input, A1 = the record
	.globl t_alloc
t_alloc:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTAlloc
	move.l %d0,%a1
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	| LAB_01A9: A1 = record, A0 = script
	.globl t_spawn
t_spawn:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l %a0,-(%sp)
	move.l %a1,-(%sp)
	jsr rtTSpawn
	addq.l #8,%sp
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	| LAB_015F
	.globl t_clear
t_clear:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTClear
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	| LAB_016F: D0 = mode
	.globl t_common
t_common:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l %d0,-(%sp)
	jsr rtTCommon
	addq.l #4,%sp
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	| LAB_01A4
	.globl t_place
t_place:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtTPlace
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

// ROADMAP 9.5a: the arena's scaleWave reads the rule data (game/rules/waves); g_gameData = kDefaults comes from GAMEDATA_REL (tests/moddata_lib.py).
namespace rt {
void waveLog(unsigned, unsigned, unsigned, unsigned, unsigned) {}   // the serial log line of rt/modload.cpp
void creatureLog(unsigned, unsigned, int, int) {}   // the serial log line of rt/modload.cpp (ROADMAP 9.7)
}
