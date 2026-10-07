// rt/scene_menu - asm-callable entries into src/game/scene_menu.cpp (ROADMAP 6.2), patched over the mog menu and the
// knight select (asm/patches/mog.scene_menu.json), plus MenuOps: one small trampoline per asm primitive the scenes
// use.  Rendering, text, blitter, palette, input reading and the timing waits stay in asm and are called with the
// register contract the original code used.  
//
// Call helper: rtSceneCall(fn, regs) loads D0-D3/A0-A2 from `regs`, JSRs `fn` and stores D0/D1 back.  D2-D7/A2-A6 are
// saved around the call (the primitives clobber a lot: LAB_0CDA uses D0-D7/A0-A5), so the C++ above sees the normal
// m68k-gcc calling convention.  The asm primitives never need a register the caller left behind: each one gets all
// its inputs from the Regs block or from memory (verified per op below).
//
// Variables: the menu state (player count, cursor, gore flag, damage divisor) is copied in before the scene and out
// afterwards.  Nothing else reads them while a scene runs: LAB_06DB/LAB_05D4..LAB_05DA/LAB_06F7..LAB_0703 are used by
// these two scenes only, and LAB_06DC/LAB_06DA/LAB_06DE/LAB_05C5 are read by the main loop after the menu returns.

#include <stdint.h>

#include <ace/managers/log.h>

#include "game/api/data.hpp"
#include "game/party.hpp"
#include "game/scene_menu.hpp"
#include "game/state_bind.hpp"
#include "engine/util.hpp"
#include "rt/gtext.hpp"
#include "rt/display_ops.hpp"
#include "rt/stubfn.h"

// Labels not bound in state_bind.hpp (ROADMAP 5.1 header is not extended for this task).
extern "C" uint32_t mogDrawScreen;       // draw screen base (LAB_0426+2 / LAB_0D72 argument) (LAB_0D92)
extern "C" uint32_t mogBackground;       // background picture buffer (LAB_05C0)
extern "C" uint32_t mogForeground;       // sprite sheet (LAB_05C1)
extern "C" uint32_t mogSceneSheet;       // sprite sheet the scenes draw from (:= LAB_05C1 by both scenes) (LAB_05C4)
extern "C" uint32_t mogSmallFont[5];    // 16(LAB_05E3) = the long right before LAB_05E4
extern "C" uint32_t mogSelectJob;       // job handle of the knight-select animation (LAB_05A5)
extern "C" uint16_t mogTextFlag;       // text flag (LAB_0D05)
extern "C" uint32_t mogMenuGoreItem;       // first long of the Gore On/Off text item: the text pointer (LAB_06B3)
extern "C" uint8_t mogMenuGoreOn[], mogMenuGoreOff[];   // gore texts (LAB_06BA, LAB_06BB)
extern "C" uint8_t mogMenuPlayersValue[];      // 4-byte number field of the Players item (LAB_06B9)
extern "C" uint8_t mogMenuItemsMain[], mogTextSelectKnight[];   // text item lists (LAB_06AE, LAB_06E7)
extern "C" uint8_t mogPicPalette[], mogMenuPalette[];   // palettes (LAB_0D2B) (LAB_06FD)
extern "C" uint8_t mogKnightName1[], mogKnightName2[], mogKnightName3[], mogKnightName4[];  // knight name buffers (22 bytes) (LAB_06B5, LAB_06B6, LAB_06B7, LAB_06B8)
extern "C" uint16_t mogMenuMoved, mogMenuCursor, mogDamageDiv;  // LAB_06DB, LAB_06DC, LAB_06DE
extern "C" uint32_t mogGore;  // LAB_06DA
extern "C" uint32_t mogMenuPopCell;  // LAB_0714
extern "C" volatile uint16_t mogKeyAny;         // last key code (SECSTRT_21)

namespace {

using namespace ms::game;

struct Regs {
	uint32_t d0, d1, d2, d3, a0, a1, a2;
};

}  // namespace

extern "C" void rtSceneCall(uint32_t ulFn, Regs *pRegs);

asm(R"(
	.text
	.globl rtSceneCall
rtSceneCall:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a5
	move.l 52(%sp),%a6
	move.l %a6,-(%sp)
	movem.l (%a6),%d0-%d3/%a0-%a2
	jsr (%a5)
	move.l (%sp)+,%a6
	movem.l %d0-%d1,(%a6)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");

namespace {

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

uint32_t call(const void *pFn, Regs &r) {
	rtSceneCall(addr(pFn), &r);
	return r.d0;
}

uint32_t call0(const void *pFn) {
	Regs r = {0, 0, 0, 0, 0, 0, 0};
	return call(pFn, r);
}

// ---- MenuOps ---------------------------------------------------------------------------------------------

void opIrqOn() { rt_irq_enable(); }                     // LAB_0D7C: INTENA := $C000, no inputs
void opClearHits() { call0(RT_FN(rt_mog_draw_buf_clear)); }              // sets A0/D0 itself
void opFlip() { call0(RT_FN(rt_mog_display_flip)); }
void opWait(uint32_t ulTicks) { Regs r = {ulTicks, 0, 0, 0, 0, 0, 0}; call(RT_FN(rt_display_wait_frames), r); }   // D0 = ticks (long)
void opSetPlanes() { Regs r = {mogDrawScreen, 0, 0, 0, 0, 0, 0}; call(RT_FN(rt_mog_set_planes), r); }     // D0 = screen; sets A1-A5 itself
void opBlitBackground() { Regs r = {0, 0, 0, 0, mogBackground, mogDrawScreen, 0}; call(RT_FN(rt_mog_blit_screen), r); }  // A0 src, A1 dst
void opBlitBackgroundBoth() { call0(RT_FN(rt_mog_blit_both)); }     // loads A0/A1 itself
void opClearBuffer(MenuBuffer eBuf) {                    // A0 = buffer; LAB_0D72 saves every register
	rt::displayClearScreen(reinterpret_cast<void *>((uintptr_t)(eBuf == MENUBUF_BACKGROUND ? mogBackground : mogDrawScreen)));   // LAB_0D72
}
void opSetTextFlag(uint16_t uwValue) { mogTextFlag = uwValue; }
void opDrawSprite(MenuBank eBank, uint16_t uwIndex, uint16_t uwX, uint16_t uwY) {   // A0 = sprite sheet, D0 = frame, D1 = x, D2 = y
	uint32_t ulBank = eBank == MENUBANK_BACKDROP ? mogActive.ulSpriteBank : mogSceneSheet;  // LAB_05E4
	Regs r = {uwIndex, uwX, uwY, 0, ulBank, 0, 0};
	call(RT_FN(rt_mog_draw_cel), r);
}
void opDrawNumber(uint16_t uwValue) {                    // 0..999 into the 4-byte field of the Players item (LAB_0442)
	ms::formatNumber3(uwValue, reinterpret_cast<char *>(mogMenuPlayersValue));
}
void opDrawPartyText(uint16_t uwKnights) {             // co-op (ROADMAP 8.2): "Coop N" in the 12-byte Players field (LAB_06B9)
	static const char kText[] = "Coop ";
	uint8_t i = 0;
	for(const char *p = kText; *p; ++p) mogMenuPlayersValue[i++] = (uint8_t)*p;
	mogMenuPlayersValue[i++] = (uint8_t)('0' + (uwKnights % 10));
	mogMenuPlayersValue[i] = 0;
}
void opSetGoreText(bool bAlternate) { mogMenuGoreItem = addr(bAlternate ? mogMenuGoreOff : mogMenuGoreOn); }
void opDrawText(MenuText eText) {                        // LAB_0432: the item list
	rt::mogDrawList(eText == MENUTEXT_MAIN ? mogMenuItemsMain : mogTextSelectKnight);
}
void opDrawString(const uint8_t *pText, uint16_t uwX, uint16_t uwY, uint16_t uwStyle) {   // LAB_0431
	rt::mogDrawString(pText, uwX, uwY, uwStyle);
}
void opPalette(MenuPalette ePal) {                       // A0 = palette; LAB_03F2 sets D0/D1 itself
	Regs r = {0, 0, 0, 0, addr(ePal == MENUPAL_TITLE ? mogPicPalette : mogMenuPalette), 0, 0};
	call(RT_FN(rt_mog_fade_to), r);
}
void opFadeOut() { call0(RT_FN(rt_mog_fade_out)); }                // saves/restores A0 itself
uint16_t opReadJoy() {                                   // D1 = LAB_0630 (D0 = LAB_062F is not used by the scenes)
	Regs r = {0, 0, 0, 0, 0, 0, 0};
	call(RT_FN(rt_mog_joy_read), r);
	return (uint16_t)r.d1;
}
uint16_t opKeyWord() { return mogKeyAny; }
void opClearKeyWord() { mogKeyAny = 0; }
void opKeyReset() { call0(RT_FN(rt_mog_key_reset)); }
uint8_t opTranslateKey(uint16_t uwCode) {                // D0 = code (word); result in the low byte of D0
	Regs r = {uwCode, 0, 0, 0, 0, 0, 0};
	call(RT_FN(rt_mog_key_xlat), r);
	return (uint8_t)r.d0;
}
void opErrorFlash() {
	rt::displayWaitBeam();                                 // wait for the next frame line, then flash the border
	*(volatile uint16_t *)0xDFF180 = 0x0C00;             // COLOR00
	opWait(2);
	*(volatile uint16_t *)0xDFF180 = 0x0000;
}
void opPublishPlayerCount(uint16_t uwCount) { mogActive.uwPlayerCount = uwCount; }
uint32_t opSpawnJob(uint16_t uwD0, uint16_t uwD1, uint16_t uwD2, uint16_t uwD3) {   // D0-D3 args, D0 = job record
	Regs r = {uwD0, uwD1, uwD2, uwD3, 0, 0, 0};
	uint32_t ulJob = call(RT_FN(rt_palette_ramp_add_mog), r);
	mogSelectJob = ulJob;
	return ulJob;
}
void opKillJob(uint32_t ulJob) { *(uint32_t *)(uintptr_t)ulJob = 0; }

const MenuOps g_ops = {
	opIrqOn, opClearHits, opFlip, opWait, opSetPlanes, opBlitBackground, opBlitBackgroundBoth, opClearBuffer,
	opSetTextFlag, opDrawSprite, opDrawNumber, opSetGoreText, opDrawText, opDrawString, opPalette, opFadeOut,
	opReadJoy, opKeyWord, opClearKeyWord, opKeyReset, opTranslateKey, opErrorFlash, opPublishPlayerCount,
	opSpawnJob, opKillJob, opDrawPartyText,
};

#if !defined(MS_COOP)
#define MS_COOP 0
#endif
bool s_bMenuCoop;   // the menu's co-op choice, kept across games (g_party itself is classic while the title menu runs)

}  // namespace

// ---- scene entries -----------------------------------------------------------------------------------------

extern "C" __attribute__((used, externally_visible)) void rtSceneMenuRun() {
	SceneMenu m;
	m.state.uwPlayers = mogHumanPlayers.uw;  // LAB_05C5
	m.state.uwCursor = mogMenuCursor;
	m.state.uwMoved = mogMenuMoved;
	m.state.ulGore = mogGore;
	m.state.uwDamageDiv = mogDamageDiv;
	m.state.pStatCost = ms::game::g_gameData.temple.auwStatCost;   // [temple] stat_cost (ROADMAP 9.5b)
	ms::game::partyReset(ms::game::g_party);                   // co-op (ROADMAP 8.2): classic input while the menu runs
	m.state.bCoopAllowed = MS_COOP != 0;
	m.state.bCoop = m.state.bCoopAllowed && s_bMenuCoop;
	mogActive.ulSpriteBank = mogSmallFont[4];                  // MOVE.L 16(A1),10(A0) with A1 = LAB_05E3
	mogSceneSheet = mogForeground;                             // MOVE.L LAB_05C1,LAB_05C4
	m.enter(g_ops);
	while (!m.update(g_ops)) {
	}
	m.exit(g_ops);
	mogHumanPlayers.uw = m.state.uwPlayers;
	mogMenuCursor = m.state.uwCursor;
	mogMenuMoved = m.state.uwMoved;
	mogGore = m.state.ulGore;
	mogDamageDiv = m.state.uwDamageDiv;
	s_bMenuCoop = m.state.bCoop;
	if(m.eResult == MENU_SELECT && m.state.bCoop) {           // a co-op campaign: the knight screen binds the controllers
		ms::game::partyStart(ms::game::g_party, (uint8_t)m.state.uwPlayers);
		logWrite("party: co-op, %u knights\n", (unsigned)ms::game::g_party.n);
	}
}

extern "C" __attribute__((used, externally_visible)) void rtSceneKnightsRun() {
	SceneKnights k;
	k.env.uwPlayers = mogHumanPlayers.uw;
	k.env.aKnights = mogKnights;  // LAB_0613
	k.env.apNames[0] = mogKnightName2;                         // portrait 0 = Sir Godber (LAB_00E5: D0 = 0)
	k.env.apNames[1] = mogKnightName1;                         // 1 = Sir Richard
	k.env.apNames[2] = mogKnightName3;                         // 2 = Sir Jeffrey
	k.env.apNames[3] = mogKnightName4;                         // 3 = Sir Edward
	mogSceneSheet = mogForeground;                             // MOVE.L LAB_05C1,LAB_05C4
	ms::game::PartyConfig &party = ms::game::g_party;
	if(party.active) {                                         // co-op (ROADMAP 8.2): any controller drives the screen
		party.ubFocus = ms::game::PARTY_FOCUS_ANY;
		k.env.pParty = &party;
	}
	k.enter(g_ops);
	while (!k.update(g_ops)) {
	}
	k.exit(g_ops);
	if(party.active) {                                         // from here on each knight's turn reads its own controller
		party.ubFocus = ms::game::PARTY_FOCUS_TURN;
		for(uint8_t i = 0; i < party.n; ++i) {
			logWrite("party: knight %u kind %lu controller %u %s\n", (unsigned)i, (unsigned long)mogKnights[i].ulKind,
			         (unsigned)party.aubPad[i], ms::padName(party.aubPad[i]));
		}
	}
}

// ROADMAP 7.1q: the asm entries rt_scene_menu_run (LAB_00B4+12, the menu behind the hunk-9 return chain) and rt_scene_knights_run (LAB_00D3) are gone: the
// main loop calls rtTitleStep (src/rt/hunk9.cpp, which ends in rtSceneMenuRun after storing the dummy long the menu popped into LAB_0714) and
// rtSceneKnightsRun directly.  Both return to the main loop like LAB_00B8 / LAB_00B9 / LAB_00DA did; D2-D7/A2-A6 are preserved (C ABI).

