// rt/input - keyboard, mouse and joystick of the original game (ROADMAP 2.3, 7.1d). See input.hpp and docs/IRQ.md.
//
// Keyboard: the level-2 interrupt is ACE's (it owns the CIA-A serial handshake); inputKeyIsr runs ACE's handler and then
// feeds the game's tables through engine/input. Mouse: one sample per VBL from rt/irq. Joystick: mog LAB_00EE and its
// callers, ported to C++ behind asm shims that keep the original register contracts (patches mog.irq2.json).

#include "rt/input.hpp"

#include <ace/managers/key.h>
#include <ace/managers/system.h>
#include <ace/utils/custom.h>
#include <hardware/intbits.h>

#include "engine/input.hpp"
#include "engine/pad.hpp"
#include "game/party.hpp"
#include "game/state_bind.hpp"
#include "rt/abs.h"
#include "rt/autoplay.hpp"
#include "rt/irq.hpp"
#include "rt/sprites.hpp"
#include "rt/stubfn.h"

extern "C" {
// ACE's keyboard CIA callback (ace/managers/key.c): reads SDR, does the handshake, updates g_sKeyManager.
void onKeyInterrupt(volatile tCustom *pCustom, volatile void *pData);

// Overlay cells (asm/program.s, asm/mog.s); the labels are the original's. Types are what the original keeps there.
extern volatile UWORD prgKeyAny, prgFrameOn, prgBlitDone, prgIdleWord;  // SECSTRT_16, LAB_0363, LAB_0367, LAB_036A
extern volatile WORD prgMouseX, prgMouseY, prgButton, prgButton2;  // SECSTRT_17, LAB_0375, LAB_0376, LAB_0377
extern volatile UBYTE prgKeyLast, prgPrevV, prgPrevH, prgKeyDown[];  // LAB_0362, LAB_0368, LAB_0369, LAB_036D
extern volatile ULONG prgIdleLong, prgHooks[], prgFrame;  // LAB_036B, LAB_0372, LAB_0379
extern UBYTE prgKeyXlat[];  // LAB_036C

extern volatile UWORD mogKeyAny, mogFrameOn, mogBlitDone, mogIdleWord;  // SECSTRT_21, LAB_0B87, LAB_0B8B, LAB_0B8E
extern volatile WORD mogMouseX, mogMouseY, mogButton, mogButton2;  // SECSTRT_22, LAB_0B99, LAB_0B9A, LAB_0B9B
extern volatile UBYTE mogKeyLast, mogPrevV, mogPrevH, mogKeyDown[];  // LAB_0B86, LAB_0B8C, LAB_0B8D, LAB_0B91
extern volatile ULONG mogIdleLong, mogHooks[], mogFrame;  // LAB_0B8F, LAB_0B96, LAB_0B9D
extern UBYTE mogKeyXlat[];  // LAB_0B90

// mog joystick / cursor cells and the asm routines the cursor enable/disable call.
extern volatile UWORD mogJoyPort0, mogJoyPort1, mogCursorOn, mogCursorLock;  // LAB_062F, LAB_0630, LAB_097C, LAB_0981
extern volatile ULONG mogCursorData, mogCursorData2, mogCursorHook;  // LAB_097D, LAB_097E, LAB_0982
extern volatile UWORD mogCursorX, mogCursorY;  // LAB_097F, LAB_0980
}

namespace rt {

namespace {

// One row per overlay; the two copies of the engine are identical (program.asm:6245 / mog.asm:20421).
const IrqCells s_pOverlays[] = {
	{
		"program", &prgKeyAny, &prgKeyLast, &prgFrameOn, &prgBlitDone, &prgPrevV, &prgPrevH, &prgIdleWord,
		&prgIdleLong, prgKeyXlat, prgKeyDown, prgHooks, &prgMouseX, &prgMouseY,
		&prgButton, &prgButton2, &prgFrame,
	},
	{
		"mog", &mogKeyAny, &mogKeyLast, &mogFrameOn, &mogBlitDone, &mogPrevV, &mogPrevH, &mogIdleWord,
		&mogIdleLong, mogKeyXlat, mogKeyDown, mogHooks, &mogMouseX, &mogMouseY,
		&mogButton, &mogButton2, &mogFrame,
	},
};

const IrqCells *volatile s_pCells;
volatile ULONG s_ulEvents;
volatile ULONG s_ulSkipPresses;

bool isFireDown(UBYTE ubBit) {
	return !(g_pCia[CIA_A]->pra & ubBit);   // CIAA PRA bit 6 = port 0 fire / left mouse button, bit 7 = port 1 fire; active low
}

// Co-op (ROADMAP 8.2): the record index of the current knight (LAB_0633) when it is a party member.
UBYTE currentMember() {
	const ULONG ulFirst = reinterpret_cast<ULONG>(&mogKnights[0]);
	const ULONG ulCur = mogCurKnight;
	if(ulCur < ulFirst || (ulCur - ulFirst) % sizeof(ms::game::Knight) != 0) {
		return ms::game::PARTY_MEMBER_NONE;
	}
	const ULONG ulIdx = (ulCur - ulFirst) / sizeof(ms::game::Knight);
	return ulIdx < ms::game::g_party.n ? static_cast<UBYTE>(ulIdx) : ms::game::PARTY_MEMBER_NONE;
}

// Co-op: the "player stick" (the port 1 word every menu, the map and the first fighter read) comes from the party's
// controllers (game/party.hpp partyStick); isAny = every controller (the "press fire" waits). The port 0 word stays the raw
// port 0 joystick (the second fighter of a duel). The parallel adapter is not read yet (engine/pad.hpp padAdapterDecode).
UWORD coopStick(const ms::JoyBits &sBits, bool isAny) {
	ms::PadFrame sFrame;
	const ms::PadInputs sIn = {sBits, mogKeyDown, 0, 0};
	ms::padsFill(sFrame, sIn);
	if(isAny) {
		return ms::padsAny(sFrame);
	}
	return ms::game::partyStick(ms::game::g_party, sFrame, currentMember());
}

ms::JoyBits joyPoll(bool isAny = false) {
	const UWORD uwJoy1 = g_pCustom->joy1dat;
	const UWORD uwJoy0 = g_pCustom->joy0dat;
	ms::JoyBits sBits = ms::joyRead(uwJoy0, uwJoy1, isFireDown(0x40), isFireDown(0x80));
	autoplayJoy(sBits);   // MS_AUTOPLAY: scripted joystick (rt/autoplay), a no-op otherwise
	if(ms::game::g_party.active) {   // co-op only: classic reads stay exactly the two ports
		sBits.uwPort1 = coopStick(sBits, isAny);
	}
	mogJoyPort1 = sBits.uwPort1;
	mogJoyPort0 = sBits.uwPort0;
	return sBits;
}

ULONG packJoy(const ms::JoyBits &sBits) {
	return (static_cast<ULONG>(sBits.uwPort0) << 16) | sBits.uwPort1;
}

// Same effect as the original INT2 handler's tail (program.asm:6258-6271), see ms::keyDecode / keyApply.
void feedGame(const IrqCells &sCells, UBYTE ubAceCode) {
	const UBYTE ubSdr = static_cast<UBYTE>(~ubAceCode);  // what CIAA_SDR held
	const ms::KeyEvent sEvent = ms::keyDecode(ubSdr, sCells.pubXlat);
	ms::keyApply(sEvent, sCells.pubKeyDown, sCells.pubKeyLast);
}

}  // namespace

const IrqCells &inputProgramCells() {
	return s_pOverlays[0];
}

const IrqCells &inputMogCells() {
	return s_pOverlays[1];
}

void inputBind(const IrqCells *pCells) {
	s_pCells = pCells;
}

const IrqCells *inputCells() {
	return s_pCells;
}

void inputPointerInit(const IrqCells &sCells) {
	const UWORD uwJoy = g_pCustom->joy0dat;
	*sCells.pubPrevV = static_cast<UBYTE>(uwJoy >> 8);
	*sCells.pubPrevH = static_cast<UBYTE>(uwJoy);
}

void inputPointerTick(const IrqCells &sCells) {
	ms::MouseState sMouse;
	sMouse.ubPrevV = *sCells.pubPrevV;
	sMouse.ubPrevH = *sCells.pubPrevH;
	sMouse.wX = *sCells.pwMouseX;
	sMouse.wY = *sCells.pwMouseY;
	sMouse.wButton = *sCells.pwButton;
	sMouse.wButton2 = *sCells.pwButton2;
	sMouse.uwIdleWord = *sCells.puwIdleWord;
	sMouse.ulIdleLong = 0;
	const bool isActive = ms::mouseStep(sMouse, g_pCustom->joy0dat, isFireDown(0x40));
	*sCells.pwMouseY = sMouse.wY;
	*sCells.pwMouseX = sMouse.wX;
	if(isActive) {
		*sCells.pulIdleLong = sMouse.ulIdleLong;  // the original's MOVE.L #$10 (also reaches the key table, see engine/input.hpp)
		*sCells.puwIdleWord = sMouse.uwIdleWord;
	}
	*sCells.pwButton = sMouse.wButton;
	*sCells.pwButton2 = sMouse.wButton2;
	*sCells.pubPrevV = sMouse.ubPrevV;
	*sCells.pubPrevH = sMouse.ubPrevH;
}

// What the keyboard ISR does with a key once ACE's handler is through: count, skip latch, feed the game's tables.
static void deliverKey(UBYTE ubAceCode) {
	++s_ulEvents;
	const UBYTE ubKey = ubAceCode >> 1;
	if(!(ubAceCode & 1) && (ubKey == KEY_SPACE || ubKey == KEY_RETURN)) {
		++s_ulSkipPresses;
	}
	const IrqCells *pCells = s_pCells;
	if(pCells) {
		feedGame(*pCells, ubAceCode);
#ifdef MS_IRQ_TRACE
		const ms::KeyEvent sEvent = ms::keyDecode(static_cast<UBYTE>(~ubAceCode), pCells->pubXlat);
		traceEvent(1, ubAceCode, sEvent.ubKey, *pCells->pubKeyLast);
#endif
	}
}

void inputKeyIsr(volatile void *pCustom, volatile void *pData) {
	const UBYTE ubAceCode = static_cast<UBYTE>(~g_pCia[CIA_A]->sdr);  // (key << 1) | released
	onKeyInterrupt(static_cast<volatile tCustom *>(pCustom), pData);
	deliverKey(ubAceCode);
}

void inputInjectKey(UBYTE ubKey, bool isDown) {
	deliverKey(static_cast<UBYTE>((ubKey << 1) | (isDown ? 0 : 1)));
}

void inputInstall() {
	systemSetCiaInt(
		CIA_A, CIAICRB_SERIAL, reinterpret_cast<tAceIntHandler>(inputKeyIsr), &g_sKeyManager
	);
}

void inputRemove() {
	systemSetCiaInt(CIA_A, CIAICRB_SERIAL, nullptr, nullptr);
}

ULONG inputEventCount() {
	return s_ulEvents;
}

ULONG inputSkipPressCount() {
	return s_ulSkipPresses;
}

}  // namespace rt

// ---- C bodies of the asm shims (called through the register-preserving wrappers below) ---------------------------------

extern "C" {

// program LAB_035E / mog LAB_0B82: clear the 128 down flags and the "any key" word.
__attribute__((used, externally_visible)) void rtPrgKeyResetC(void) {
	const rt::IrqCells &sCells = rt::inputProgramCells();
	ms::keyClearAll(sCells.pubKeyDown);
	*sCells.puwKeyAny = 0;
}

__attribute__((used, externally_visible)) void rtMogKeyResetC(void) {
	const rt::IrqCells &sCells = rt::inputMogCells();
	ms::keyClearAll(sCells.pubKeyDown);
	*sCells.puwKeyAny = 0;
}

// mog LAB_0B46: clear the "any key" word, then wait until the keyboard handler stores a non-zero key in it.
__attribute__((used, externally_visible)) void rtMogKeyWaitC(void) {
	volatile UWORD *pAny = rt::inputMogCells().puwKeyAny;
	*pAny = 0;
	while(*pAny == 0) {
	}
}

// mog LAB_00EE: read both ports into LAB_062F / LAB_0630. Returns (port 0 << 16) | port 1.
__attribute__((used, externally_visible)) ULONG rtMogJoyReadC(void) {
	return rt::packJoy(rt::joyPoll());
}

// mog LAB_00EC: wait for a fire press and release on port 1 (co-op: on any controller). Returns the last reading.
__attribute__((used, externally_visible)) ULONG rtMogWaitFireC(void) {
	ms::JoyBits sBits;
	do {
		sBits = rt::joyPoll(true);
	} while(!(sBits.uwPort1 & ms::kJoyFire));
	do {
		sBits = rt::joyPoll(true);
	} while(sBits.uwPort1 & ms::kJoyFire);
	return rt::packJoy(sBits);
}

// mog LAB_0575: joystick cursor on. Sprite DMA on, install the cursor sprite (built by LAB_0572), place it, hook LAB_057D into
// the VBL list. The sprite routines are C++ now (rt/sprites.cpp, ROADMAP 7.1e); the original passed the copper list in A1, the
// sprite pointers live in ACE's stub.
__attribute__((used, externally_visible)) void rtMogCursorOnC(void) {
	if(mogCursorOn) {
		return;
	}
	mogCursorOn = 1;
	rt::spriteDmaOn();
	rt::spriteInstall(0, mogCursorData, mogCursorData2);
	rt::spritePlace(0, static_cast<WORD>(mogCursorX), static_cast<WORD>(mogCursorY));
	mogCursorLock = 1;
	volatile ULONG *pSlot = mogHooks;
	while(*pSlot++ != 0) {                                   // LAB_0579: first free slot of the hook list
	}
	--pSlot;
	*pSlot = reinterpret_cast<ULONG>(RT_FN(rt_cui_cursor_tick));
	mogCursorHook = reinterpret_cast<ULONG>(pSlot);
}

// mog LAB_057B: joystick cursor off.
__attribute__((used, externally_visible)) void rtMogCursorOffC(void) {
	if(!mogCursorOn) {
		return;
	}
	mogCursorOn = 0;
	rt::spriteOff(0);
	*reinterpret_cast<volatile ULONG *>(mogCursorHook) = 0;   // CLR.L (A0): take LAB_057D out of the hook list
}

}  // extern "C"

// ---- asm: register-contract wrappers --------------------------------------------------------------------------------
// The C++ ABI clobbers D0/D1/A0/A1 only; the original routines kept even those, so every wrapper saves them.
//   rt_prg_key_reset / rt_mog_key_reset / rt_mog_key_wait / rt_mog_cursor_on / rt_mog_cursor_off: no register changes.
//   rt_mog_joy_read  (LAB_00EE): D0.w = port 0 bits, D1.w = port 1 bits (the upper words are the caller's), nothing else.
//   rt_mog_wait_fire (LAB_00EC): as LAB_00EE with the last reading.
asm(R"(
	.text
	.macro RT_INPUT_PLAIN cfn
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm

	.macro RT_INPUT_JOY cfn, tail
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	swap %d0
	move.w %d0,2(%sp)
	swap %d0
	move.w %d0,6(%sp)
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	\tail
	rts
	.endm

	.globl rt_prg_key_reset
rt_prg_key_reset:
	RT_INPUT_PLAIN rtPrgKeyResetC
	.globl rt_mog_key_reset
rt_mog_key_reset:
	RT_INPUT_PLAIN rtMogKeyResetC
	.globl rt_mog_key_wait
rt_mog_key_wait:
	RT_INPUT_PLAIN rtMogKeyWaitC
	.globl rt_mog_cursor_on
rt_mog_cursor_on:
	RT_INPUT_PLAIN rtMogCursorOnC
	.globl rt_mog_cursor_off
rt_mog_cursor_off:
	RT_INPUT_PLAIN rtMogCursorOffC
	.globl rt_mog_joy_read
rt_mog_joy_read:
	RT_INPUT_JOY rtMogJoyReadC, "tst.w %d1"
	.globl rt_mog_wait_fire
rt_mog_wait_fire:
	RT_INPUT_JOY rtMogWaitFireC, "btst #4,%d1"
)");

