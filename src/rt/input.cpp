// rt/input - keyboard, mouse and joystick of the original game (ROADMAP 2.3, 7.1d). See input.hpp and docs/IRQ.md.
//
// Keyboard: the key events come from the platform (rt/platform/input.hpp: on the Amiga the level-2 interrupt is ACE's, which owns the CIA-A
// serial handshake, rt/amiga/platform_input.cpp; on the PC the SDL events), deliverKey feeds the game's tables through engine/input.
// Mouse: one sample per VBL from rt/irq. Joystick: mog LAB_00EE and its callers, in C++ behind asm shims that keep the original register
// contracts (patches mog.irq2.json; the shims are src/rt/amiga/input_entries.cpp).  Platform-neutral since ROADMAP 11.10: the two joystick
// ports and the fire buttons are read through platform::inputReadPorts.

#include "rt/input.hpp"

#include "engine/endian.hpp"
#include "engine/input.hpp"
#include "engine/nettick.hpp"
#include "engine/pad.hpp"
#include "game/party.hpp"
#include "game/state_bind.hpp"
#include "rt/abs.h"
#include "rt/autoplay.hpp"
#include "rt/irq.hpp"
#include "engine/net_cfg.hpp"
#if MS_NET
#include "rt/net_box.hpp"
#endif
#include "rt/net_tick.hpp"
#include "rt/platform/input.hpp"
#include "rt/sprites.hpp"
#include "rt/stubfn.h"

extern "C" {
void rtCuiCursorTick(void);   // the joystick cursor's VBL hook (src/rt/combat_ui.cpp; rt_cui_cursor_tick)

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

// The "last key" byte is the LOW byte of the "any key" word (LAB_0B86 = SECSTRT_21 + 1 on the 68k, where the low byte is the odd one).  On a
// little-endian CPU the odd byte is the HIGH byte of the native word, so the word read as 0x0200 for key 2 and indexed the key table at +512
// (docs/PC_ENDIAN.md 1: a ByteSlot inside a native cell).  The PC stores the byte at the word's own low byte.
#if MS_BIG_ENDIAN
#define MS_KEY_LAST(ANY, LAST) (&(LAST))
#else
#define MS_KEY_LAST(ANY, LAST) (reinterpret_cast<volatile UBYTE *>(&(ANY)))
#endif

// One row per overlay; the two copies of the engine are identical (program.asm:6245 / mog.asm:20421).
const IrqCells s_pOverlays[] = {
	{
		"program", &prgKeyAny, MS_KEY_LAST(prgKeyAny, prgKeyLast), &prgFrameOn, &prgBlitDone, &prgPrevV, &prgPrevH, &prgIdleWord,
		&prgIdleLong, prgKeyXlat, prgKeyDown, prgHooks, &prgMouseX, &prgMouseY,
		&prgButton, &prgButton2, &prgFrame,
	},
	{
		"mog", &mogKeyAny, MS_KEY_LAST(mogKeyAny, mogKeyLast), &mogFrameOn, &mogBlitDone, &mogPrevV, &mogPrevH, &mogIdleWord,
		&mogIdleLong, mogKeyXlat, mogKeyDown, mogHooks, &mogMouseX, &mogMouseY,
		&mogButton, &mogButton2, &mogFrame,
	},
};

const IrqCells *volatile s_pCells;
volatile ULONG s_ulEvents;
volatile ULONG s_ulSkipPresses;

constexpr UBYTE kRawSpace = 0x40;    // Amiga raw key codes (ACE's KEY_SPACE / KEY_RETURN / KEY_ESCAPE)
constexpr UBYTE kRawReturn = 0x44;
constexpr UBYTE kRawEscape = 0x45;
constexpr UBYTE kRawF10 = 0x59;      // toggles the network link line (rt/net_box); the game does not use it, a network game swallows it

// Both ports as the hardware shows them now: JOY0DAT / JOY1DAT and the fire buttons (CIA-A PRA bit 6 = port 0 fire / left mouse button,
// bit 7 = port 1 fire; active low on the chip, true here).
platform::PortState readPorts() {
	platform::PortState sPorts;
	platform::inputReadPorts(sPorts);
	return sPorts;
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
	if(netDeterministic()) {   // ROADMAP 12.4: the controllers of the tick (the network layer may have replaced them)
		sFrame = netLatch().sPads;
	}
	else {
		const ms::PadInputs sIn = {sBits, mogKeyDown, 0, 0};
		ms::padsFill(sFrame, sIn);
	}
	if(isAny) {
		return ms::padsAny(sFrame);
	}
	return ms::game::partyStick(ms::game::g_party, sFrame, currentMember());
}

ms::JoyBits joyPoll(bool isAny = false) {
	ms::JoyBits sBits;
	if(netDeterministic()) {   // ROADMAP 12.4: every poll of a tick returns the frame netTick latched
		autoplayNotePoll();
		sBits = netLatch().sFrame.joy;
	}
	else {
		const platform::PortState sPorts = readPorts();   // JOY1DAT first, then JOY0DAT, then the fire bits
		sBits = ms::joyRead(sPorts.uwJoy0Dat, sPorts.uwJoy1Dat, sPorts.isFire0, sPorts.isFire1);
		autoplayJoy(sBits);   // MS_AUTOPLAY: scripted joystick (rt/autoplay), a no-op otherwise
	}
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
	const UWORD uwJoy = readPorts().uwJoy0Dat;
	*sCells.pubPrevV = static_cast<UBYTE>(uwJoy >> 8);
	*sCells.pubPrevH = static_cast<UBYTE>(uwJoy);
}

void inputPointerTick(const IrqCells &sCells) {
	const platform::PortState sPorts = readPorts();
	inputPointerStep(sCells, sPorts.uwJoy0Dat, sPorts.isFire0);
}

void inputPointerStep(const IrqCells &sCells, UWORD uwJoy0Dat, bool isLeftDown) {
	ms::MouseState sMouse;
	sMouse.ubPrevV = *sCells.pubPrevV;
	sMouse.ubPrevH = *sCells.pubPrevH;
	sMouse.wX = *sCells.pwMouseX;
	sMouse.wY = *sCells.pwMouseY;
	sMouse.wButton = *sCells.pwButton;
	sMouse.wButton2 = *sCells.pwButton2;
	sMouse.uwIdleWord = *sCells.puwIdleWord;
	sMouse.ulIdleLong = 0;
	const bool isActive = ms::mouseStep(sMouse, uwJoy0Dat, isLeftDown);
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
	if(!(ubAceCode & 1) && (ubKey == kRawSpace || ubKey == kRawReturn)) {
		++s_ulSkipPresses;
	}
	const IrqCells *pCells = s_pCells;
	if(netDeterministic()) {   // ROADMAP 12.4: queued, applied to the game's tables at the next netTick
		ms::keyQueuePush(netKeyQueue(), ubAceCode);
		return;
	}
	if(pCells) {
		feedGame(*pCells, ubAceCode);
#ifdef MS_IRQ_TRACE
		const ms::KeyEvent sEvent = ms::keyDecode(static_cast<UBYTE>(~ubAceCode), pCells->pubXlat);
		traceEvent(1, ubAceCode, sEvent.ubKey, *pCells->pubKeyLast);
#endif
	}
}

// The platform's key event (rt::platform::inputSetKeyHandler): ubRawCode = the Amiga raw key, the keyboard ISR's (key << 1) | released encoding follows.
static void onKeyEvent(UBYTE ubRawCode, bool isDown) {
#if MS_NET
	if(ubRawCode == kRawF10 && netDeterministic()) {   // a network game: F10 shows / hides the ping and loss line on THIS machine; it is not an input of the lockstep
		static bool s_isF10Down;   // a held key (repeat events) toggles once
		if(isDown && !s_isF10Down) {
			netStatsLineToggle();
		}
		s_isF10Down = isDown;
		return;
	}
#endif
	deliverKey(static_cast<UBYTE>((ubRawCode << 1) | (isDown ? 0 : 1)));
}

void inputInjectKey(UBYTE ubKey, bool isDown) {
	deliverKey(static_cast<UBYTE>((ubKey << 1) | (isDown ? 0 : 1)));
}

void inputInstall() {
	platform::inputSetKeyHandler(onKeyEvent);
}

void inputRemove() {
	platform::inputSetKeyHandler(nullptr);
}

void inputCaptureLocal(ms::LocalFrame &sFrame) {
	ms::localFrameClear(sFrame);
	const platform::PortState sPorts = readPorts();
	sFrame.uwMouseDat = sPorts.uwJoy0Dat;
	sFrame.ubMouseButton = sPorts.isFire0 ? 1 : 0;
	sFrame.joy = ms::joyRead(sPorts.uwJoy0Dat, sPorts.uwJoy1Dat, sPorts.isFire0, sPorts.isFire1);
	autoplayJoyOverride(sFrame.joy);   // MS_AUTOPLAY: scripted joystick, a no-op otherwise
	ms::localFrameTakeKeys(sFrame, netKeyQueue());
}

bool inputConfirmDown() {
	const platform::PortState sPorts = readPorts();
	return sPorts.isFire0 || sPorts.isFire1 || platform::inputKeyHeld(kRawEscape) || autoplayFireHeld();   // MS_AUTOPLAY: a scripted fire counts (WinUAE cannot press the hardware button)
}

void inputApplyLocal(const ms::LocalFrame &sFrame) {
	const IrqCells *pCells = s_pCells;
	if(!pCells) {
		return;
	}
	inputPointerStep(*pCells, sFrame.uwMouseDat, sFrame.ubMouseButton != 0);
	for(UBYTE i = 0; i < sFrame.ubKeys; ++i) {
		feedGame(*pCells, sFrame.aubKey[i]);
	}
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
		ms::tickIdle();   // deterministic mode: a frame wait + tick, else the key never arrives (NETPLAY 1.2)
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
		ms::tickIdle();   // deterministic mode: the poll returns the latched frame, so a frame boundary per poll
		sBits = rt::joyPoll(true);
	} while(!(sBits.uwPort1 & ms::kJoyFire));
	do {
		ms::tickIdle();
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
	*pSlot = reinterpret_cast<ULONG>(RT_HOOK(rt_cui_cursor_tick, rtCuiCursorTick));
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
