// rt/fighters - the wiring of src/game/fighters.cpp (ROADMAP 6.4a) to the asm game: the FighterEnv that points at the
// game's own cells, one trampoline per asm routine the handlers call out to (joystick LAB_00EE, wall probe LAB_0A71, sound
// LAB_0AA2, copper effect LAB_0427), rtFighterRun (called by the creature dispatcher in src/rt/creatures.cpp for every
// handler address it knows).  (The patched script-called sound pickers LAB_02DC..LAB_02E9 went in the 7.1 cleanup: their patches were dead, the
// scripts reach soundPick through rtFightOpRun.)
// 
//
// Handler dispatch.  LAB_0322 (rtCreatureDispatch) looks the job's type up in the handler table LAB_08C7 and calls the
// entry.  When the entry is one of the thirteen handlers ported here (fighters.hpp: LAB_01CA, 0226, 0236, 0251, 027A, 0298,
// 029F, 02CB, 02D2 and, ROADMAP 7.1h, SECSTRT_40, LAB_0ED2, LAB_0EFF, LAB_0EC2) rtFighterRun runs the C++ handler and fills
// the HandlerResult the dispatcher expects; every other entry (the map handlers LAB_04AC, LAB_04C4, LAB_0DCF) is still the
// asm, reached through rtCreatureCall.  The table's identities are written by fightTablesInit (called by src/rt/arena.cpp; the
// LAB_01AE patch of asm/patches/mog.fight_ops.json was dead and is gone); the asm bodies of the thirteen handlers are dead code, nothing jumps to them.
//
// ROADMAP 7.1h: rtFightOpRun runs the routines the combat scripts call through opcode $B0 (the operand is a tag, see
// fighters.hpp; src/rt/combat_script.cpp offers every operand to it first), rtFightTablesInit / rtKnightDefaults are the two
// C entries src/rt/arena.cpp calls (their asm entries rt_fight_tables_init / rt_knight_init_defaults went with their dead patches).
//
// There are no register entries for the handlers any more (the rt_fighter_<name> shims and rtFighterEntry were dead since the
// creature dispatcher calls rtFighterRun directly; ROADMAP 7.1f2).  tests/test_fighters_emu.py defines them in its own
// link unit to run the original handler and rtFighterRun on the same registers.  Contract of the originals: A0 = the owner
// record; result A0 = next script (0 kills the creature, -1 keeps the running one), D0 / D1 / D2 = x / y / z words, D3.b = facing.
//
// rt_sound_<label>: LAB_02DC..LAB_02E9 (the pickers opcode $B0 calls): no input, no output, the originals end in
// JMP LAB_0AA2 and clobber D0, D1, A0, A1 (and whatever LAB_0AA2 does); the shims clobber D0, D1, A0, A1 too.

#include <stdint.h>

#include "engine/util.hpp"
#include "game/fighters.hpp"
#include "game/state_bind.hpp"
#include "rt/abs.h"
#include "rt/sfx.hpp"

extern "C" {
extern ms::game::FightVars mogFightVars;             // LAB_061D..LAB_062D: the handlers' working cells
extern ms::game::MoveVars mogMoveVars;              // LAB_0F3A..LAB_0F3F
extern uint16_t mogFlyerToggle;                        // the flyer's alternation word (in the code hunk, written by the asm) (LAB_0234)
extern uint16_t mogDaggerAimDist;                        // dagger aim distance (LAB_02DA)
extern uint16_t mogSoundStep[2];                     // the sound pickers' counters (code hunk, written by the asm) (LAB_02EC)
extern ms::game::DaggerBlock mogDaggerBlock;  // LAB_062E
extern ms::game::DaggerSlot mogDaggerSlots[ms::game::DAGGER_SLOTS];  // LAB_0301
extern uint32_t mogHandlerTable[];                      // the handler table (longs; the C++ writes them bytewise) (LAB_08C7)
// ROADMAP 7.1h: the cells of the S_40 handlers / script ops (code hunk and S_41 data, written by C++ only now)
extern uint8_t mogSnatchFlags[], mogDemonFlags[];       // flag bytes (the first byte of each is the state) (LAB_0EB6) (LAB_0EEA)
extern uint16_t mogBatCounter, mogDayCounter, mogAiLastAction, mogHopStep, mogScenePalette[];  // LAB_06C0, LAB_08D9 (LAB_0EC1, LAB_0F38, LAB_08CF)
extern uint32_t mogBodyA, mogBodyB;  // LAB_01A1, LAB_01A2
extern const uint8_t mogAiOdds[], mogAiSteps[];  // LAB_097B, SECSTRT_41
extern uint32_t mogFightSecond, mogConfusedKnight, mogGore, mogRandomSeed, mogBatLinkA, mogBatLinkB, mogHopTable,  // LAB_05F4, LAB_06DA, LAB_0973, LAB_0193, LAB_0194 (LAB_05D1, LAB_08CE)
	mogFightTarget;  // LAB_0634
extern uint16_t mogBadLuck;  // LAB_05D3
extern uint8_t mogHopDir;                         // DS.L 1, only the byte at the label is used (LAB_08D0)
#define MS_X(n) extern const uint8_t mog_LAB_##n[];
MS_FIGHT_SCRIPTS(MS_X)
#undef MS_X
extern const uint8_t mogDaggerScript[], mogDaggerDamage[], mogSetValues[], mogCelSlotsCreature[], mogHopScriptA[],  // LAB_07EB, LAB_0302, LAB_0648 (LAB_05E0) (LAB_08CC)
	mogHopScriptB[];  // LAB_08CD

// asm trampolines below
uint32_t rtFighterJoystick(void);
void rtFighterObstacles(uint32_t ulRecord, uint32_t ulDx, uint32_t ulDy);
void rtFighterEffect(void);
void rtFightDied(void);                              // rt/combat.cpp: LAB_0005
void rtFightEnd(void);                               // LAB_0006
void rtFightPauseAll(void);                          // rt/overworld.cpp: LAB_000A
void rtFightKillCurrent(void);                       // LAB_000D
uint32_t rtRngPercent(uint32_t *pSeed);               // src/rt/engine_util.cpp: LAB_04A3, the percent roll 0..100
void rtFighterFadeA(void);
void rtFighterFadeB(void);
}

// The identities of the thirteen handlers the creature dispatcher runs in C++ (ROADMAP 7.1p): the handler table LAB_08C7 and the job records
// hold the address of one byte of rtFighterTag where the original had the label of the asm handler body (tag index: idle LAB_02D2 0, knight
// LAB_01CA 1, flyer 0226 2, brawler 0236 3, caster 0251 4, dragon 027A 5, dragon part 0298 6, drake 029F 7, dagger 02CB 8, snatcher SECSTRT_40 9,
// demon 0ED2 10, AI knight 0EFF 11, stalker 0EC2 12).  rtFighterRun compares the table entry with them; nothing ever jumps to one.
// Global so that tests/test_fighters_emu.py can name them.
extern "C" {
extern const uint8_t rtFighterTag[13];
const uint8_t rtFighterTag[13] = {};
}

namespace {

using namespace ms;
using namespace ms::game;

// The identities of the fourteen handlers the creature dispatcher runs in C++ (ROADMAP 7.1p): the handler table LAB_08C7 and the job records
// hold these addresses where the original had the labels of the asm handler bodies (idle LAB_02D2, knight LAB_01CA, flyer 0226, brawler 0236,
// caster 0251, dragon 027A, dragon part 0298, drake 029F, dagger 02CB, snatcher SECSTRT_40, demon 0ED2, AI knight 0EFF, stalker 0EC2; the
// order of the tag indices).  rtFighterRun compares the table entry with them; nothing ever jumps to one.

Joystick cbJoystick() {
	const uint32_t v = rtFighterJoystick();
	Joystick j;
	j.uwPort0 = (uint16_t)(v >> 16);
	j.uwPort1 = (uint16_t)v;
	return j;
}
void cbObstacles(Knight &k, int16_t swDx, int16_t swDy) { rtFighterObstacles(jobAddr(&k), (uint16_t)swDx, (uint16_t)swDy); }
void cbSound(uint16_t uwId) { rt::sfxRequest(uwId); }      // LAB_0AA2, now C++ (src/rt/sfx.cpp)
void cbEffect() { rtFighterEffect(); }
void cbSfxFixed(uint8_t ubChannel, uint16_t uwSeq) { rt::sfxStartFixed(ubChannel, uwSeq); }   // SECSTRT_16 / 0A9B..0A9D
void cbFadeA() { rtFighterFadeA(); }
void cbFadeB() { rtFighterFadeB(); }
uint16_t cbBeam() { return *(volatile uint16_t *)0x00DFF006; }       // VHPOSR

// The environment: pointers into the game's cells (constant for the life of the program) and the script addresses.  Built
// once; only the creature heap base (LAB_05C3's value) is read again every call.
FighterEnv g_env;
bool g_ready;

const FighterEnv &env() {
	if(!g_ready) {
		g_ready = true;
		FighterEnv &e = g_env;
		e.pVars = &mogFightVars;
		e.pMove = &mogMoveVars;
		e.pToggle = &mogFlyerToggle;
		e.pAimDist = &mogDaggerAimDist;
		e.pJobs = reinterpret_cast<CombatJob *>(mogJobs);  // LAB_0649
		e.pKnights = mogKnights;  // LAB_0613
		e.pSlots = mogDaggerSlots;
		e.pBlock = &mogDaggerBlock;
		e.pHandlerTable = reinterpret_cast<uint8_t *>(mogHandlerTable);
		e.pActive = &mogActive;  // LAB_05E4
		e.pFirst = &mogFirstFighter;  // LAB_05F2
		e.pSecond = &mogFightSecond;
		e.pConfused = &mogConfusedKnight;
		e.pConfusedOn = &mogBadLuck;
		e.pHopTable = &mogHopTable;
		e.pHopDir = &mogHopDir;
		e.pSelf = &mogCurKnight;  // LAB_0633
		e.pTarget = &mogFightTarget;
		e.pDemoFlag = &mogGore;
		e.pSeed = &mogRandomSeed;
		e.pLinkA = &mogBatLinkA;
		e.pLinkB = &mogBatLinkB;
		e.pSnatchFlags = mogSnatchFlags;
		e.pDemonFlags = mogDemonFlags;
		e.pBatCounter = &mogBatCounter;
		e.pBodyA = &mogBodyA;
		e.pBodyB = &mogBodyB;
		e.pDifficulty = &mogDayCounter;
		e.pAiOdds = mogAiOdds;
		e.pAiLastAction = &mogAiLastAction;
		e.pAiSteps = mogAiSteps;
		e.pHopStep = &mogHopStep;
		e.pFade = mogScenePalette;
		e.pPickCycle = reinterpret_cast<SoundCycle *>(mogSoundStep);
		FightScripts &s = e.scripts;
#define MS_X(n) s.s##n = jobAddr(mog_LAB_##n);
		MS_FIGHT_SCRIPTS(MS_X)
#undef MS_X
		s.sDaggerScript = jobAddr(mogDaggerScript);
		s.tDaggerDamage = jobAddr(mogDaggerDamage);
		s.fFrames0648 = jobAddr(mogSetValues);
		s.fFrames05E0 = jobAddr(mogCelSlotsCreature);
		s.hIdle = jobAddr(&rtFighterTag[0]);
		s.tHop = jobAddr(mogHopScriptA);
		s.tHop2 = jobAddr(mogHopScriptB);
		s.hKnight = jobAddr(&rtFighterTag[1]);
		s.hFlyer = jobAddr(&rtFighterTag[2]);
		s.hBrawler = jobAddr(&rtFighterTag[3]);
		s.hCaster = jobAddr(&rtFighterTag[4]);
		s.hDragon = jobAddr(&rtFighterTag[5]);
		s.hDragonPart = jobAddr(&rtFighterTag[6]);
		s.hDrake = jobAddr(&rtFighterTag[7]);
		s.hDagger = jobAddr(&rtFighterTag[8]);
		s.hSnatcher = jobAddr(&rtFighterTag[9]);
		s.hDemon = jobAddr(&rtFighterTag[10]);
		s.hAiKnight = jobAddr(&rtFighterTag[11]);
		s.hStalker = jobAddr(&rtFighterTag[12]);
		e.joystick = cbJoystick;
		e.obstacles = cbObstacles;
		e.sound = cbSound;
		e.screenEffect = cbEffect;
		e.beamPosition = cbBeam;
		e.sfxFixed = cbSfxFixed;
		e.fadeA = cbFadeA;
		e.fadeB = cbFadeB;
	}
	g_env.pCreatures = jobPtr<Knight>(mogCreatureHeap);  // LAB_05C3
	g_env.uwJunkA = rt_abs_a;
	g_env.uwJunkC = rt_abs_c;
	return g_env;
}

}  // namespace

extern "C" {

// Runs the C++ handler for a handler-table entry.  Returns false when the entry is not one of the ported handlers.
__attribute__((used, externally_visible)) bool rtFighterRun(uint32_t ulHandler, uint32_t ulOwner, HandlerResult *pOut) {
	return fighterRun(env(), ulHandler, ulOwner, pOut);
}

// ROADMAP 7.1h: the script opcode $B0 (src/rt/combat_script.cpp calls this first; 0 = not a routine of ours, run the asm).
//
// ROADMAP 7.1p: the operands that named asm routines are tags too (asm/patches/mog.fight_tags.json), run here because they are runtime glue,
// not game rules: $0005 / $0006 (a fighter died / the fight ends, rt/combat.cpp), $000A / $000D (pause all / kill the current knight,
// rt/overworld.cpp), $0A9E / $0A9F (release sound channel 0 / 1), $0427 (the fight-hit screen shake) and the two ritual-screen sounds:
// $04BA (LAB_04BA: one random draw whose value is dropped, then sequence $99 on channel 3: the original forces the table index to 0) and
// $04C2 (LAB_04C2: a percent draw, up to 50 plays sequence $9D on the next free channel).  The registers the originals left are not read by the
// script engine (rtCombatCall saves what it keeps).
__attribute__((used, externally_visible)) uint32_t rtFightOpRun(uint32_t ulFn, uint32_t ulOwner, uint32_t ulFrames,
                                                                uint32_t ulX, uint32_t ulY, uint32_t ulZ, uint32_t ulFacing) {
	if((ulFn & 0xFFFF0000u) == ms::game::FIGHT_OP_TAG) {
		switch(ulFn & 0xFFFFu) {
			case 0x0005: rtFightDied(); return 1;
			case 0x0006: rtFightEnd(); return 1;
			case 0x000A: rtFightPauseAll(); return 1;
			case 0x000D: rtFightKillCurrent(); return 1;
			case 0x0A9E: rt::sfxRelease(0); return 1;
			case 0x0A9F: rt::sfxRelease(1); return 1;
			case 0x0427: rtFighterEffect(); return 1;
			case 0x04BA:
				ms::rngNext(mogRandomSeed);                                  // JSR LAB_04A1 (the value is overwritten: MOVEQ #0,D0)
				rt::sfxStartFixed(3, 0x99);                               // MOVE.B LAB_04BB+0,D0 (the table bytes $99 $9A..: index 0) ; JSR LAB_0A9D
				return 1;
			case 0x04C2:
				if(rtRngPercent(&mogRandomSeed) <= 0x32) rt::sfxRequest(0x9D);          // JSR LAB_04A3 ; CMP.W #$32,D0 ; BGT.S ; MOVE.W #$9D,D0 ; JSR LAB_0AA2
				return 1;
			default: break;
		}
	}
	return fightOpRun(env(), ulFn, ulOwner, ulFrames, (uint16_t)ulX, (uint16_t)ulY, (uint16_t)ulZ, (uint8_t)ulFacing) ? 1u : 0u;
}

// Patched into LAB_01AE (asm/patches/mog.fight_ops.json): the handler table fill.
__attribute__((used, externally_visible)) void rtFightTablesInit(void) { fightTablesInit(env()); }

// Patched into LAB_01C4: LAB_01C6 (A1 = the knight record).
__attribute__((used, externally_visible)) void rtKnightDefaults(uint32_t ulRecord) { knightDefaults(*jobPtr<Knight>(ulRecord)); }

}  // extern "C"

// Trampolines.  The asm routines clobber freely, so each saves D2-D7/A2-A6 (the C ABI's callee-saved registers).
//   LAB_00EE  no input; D0.w = port 0 bits, D1.w = port 1 bits  -> returned as (port0 << 16) | port1
//   LAB_0A71  A0 = record, D0.w = x step, D1.w = depth step (clears the blocked direction bits of 63(A0))
//   LAB_0427  none
asm(R"(
	.text

	.globl rtFighterJoystick
rtFighterJoystick:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rt_mog_joy_read
	swap %d0
	move.w %d1,%d0
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtFighterObstacles
rtFighterObstacles:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%d0
	move.l 56(%sp),%d1
	jsr rt_mog_obstacles
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtFighterEffect
rtFighterEffect:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rt_mog_diw_shake_start
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	| LAB_0D8A / LAB_03EE with A0 = LAB_08D9 (the demon's death fade, LAB_0EF7)
	.globl rtFighterFadeA
rtFighterFadeA:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	lea mogScenePalette,%a0
	jsr rt_display_palette_write
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rtFighterFadeB
rtFighterFadeB:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	lea mogScenePalette,%a0
	jsr rt_mog_pal_copy_live
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

)");

