// rt/amiga/platform_input - rt::platform::input for the Amiga (ROADMAP 11.1b / 11.10): the two joystick ports and the fire bits from the custom
// chips and CIA-A, the keyboard through ACE's level-2 handler (which owns the CIA-A serial handshake).
#include "rt/platform/input.hpp"

#include <ace/managers/key.h>
#include <ace/managers/system.h>
#include <ace/utils/custom.h>

extern "C" {
// ACE's keyboard CIA callback (ace/managers/key.c): reads SDR, does the handshake, updates g_sKeyManager.
void onKeyInterrupt(volatile tCustom *pCustom, volatile void *pData);
}

namespace {

rt::platform::KeyEventFn s_pfnKey;

// ACE's handler first (SDR read, handshake, g_sKeyManager state), then the game's callback with the decoded event.
void keyIsr(volatile void *pCustom, volatile void *pData) {
	const UBYTE ubAceCode = static_cast<UBYTE>(~g_pCia[CIA_A]->sdr);  // (key << 1) | released
	onKeyInterrupt(static_cast<volatile tCustom *>(pCustom), pData);
	if(s_pfnKey != nullptr) {
		s_pfnKey(static_cast<UBYTE>(ubAceCode >> 1), !(ubAceCode & 1));
	}
}

}  // namespace

namespace rt {
namespace platform {

void inputReadPorts(PortState &sOut) {
	sOut.uwJoy1Dat = g_pCustom->joy1dat;
	sOut.uwJoy0Dat = g_pCustom->joy0dat;
	const UBYTE ubPra = g_pCia[CIA_A]->pra;
	sOut.isFire0 = !(ubPra & 0x40);   // CIAA PRA bit 6 = port 0 fire / left mouse button, bit 7 = port 1 fire; active low
	sOut.isFire1 = !(ubPra & 0x80);
	const UWORD uwPot = *reinterpret_cast<volatile UWORD *>(0xDFF016);   // POTGOR
	sOut.isRight0 = !(uwPot & (1 << 10));
	sOut.isRight1 = !(uwPot & (1 << 14));
}

void inputArmPots() {
	g_pCustom->potgo = 1;
}

void inputSetKeyHandler(KeyEventFn pfnHandler) {
	s_pfnKey = pfnHandler;
	if(pfnHandler != nullptr) {
		systemSetCiaInt(CIA_A, CIAICRB_SERIAL, reinterpret_cast<tAceIntHandler>(keyIsr), &g_sKeyManager);
	}
	else {
		systemSetCiaInt(CIA_A, CIAICRB_SERIAL, nullptr, nullptr);
	}
}

bool inputKeyHeld(uint8_t ubRawCode) {
	return keyCheck(ubRawCode) != 0;
}

}  // namespace platform
}  // namespace rt
