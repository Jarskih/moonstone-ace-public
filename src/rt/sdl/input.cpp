// rt/sdl/input - rt::platform::input for the PC (ROADMAP 11.9).  See input.hpp for the model; the pure mapping is engine/pcinput.
#include "rt/sdl/input.hpp"
#include "rt/sdl/game.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/pad.hpp"
#include "engine/planar.hpp"
#include "game/party.hpp"
#include "rt/platform/display.hpp"
#include "rt/platform/log.hpp"
#include "rt/sdl/display.hpp"
#include "rt/sdl/timer.hpp"

// The engine tables are keyed by USB HID usage; SDL's scancodes are those numbers.  Spot-check the ones the table leans on.
static_assert(SDL_SCANCODE_A == 4 && SDL_SCANCODE_Z == 29 && SDL_SCANCODE_1 == 30 && SDL_SCANCODE_0 == 39, "SDL scancodes are HID usages");
static_assert(SDL_SCANCODE_RETURN == 40 && SDL_SCANCODE_SPACE == 44 && SDL_SCANCODE_GRAVE == 53 && SDL_SCANCODE_F1 == 58, "HID usage");
static_assert(SDL_SCANCODE_RIGHT == 79 && SDL_SCANCODE_UP == 82 && SDL_SCANCODE_KP_0 == 98 && SDL_SCANCODE_NONUSBACKSLASH == 100, "HID usage");
static_assert(SDL_SCANCODE_LCTRL == 224 && SDL_SCANCODE_RCTRL == 228 && SDL_SCANCODE_RGUI == 231 && SDL_SCANCODE_HELP == 117, "HID usage");

namespace rt {
namespace sdl {

namespace {

constexpr int kDefaultWinW = 640, kDefaultWinH = 400;   // used when an event has no window (2x, the smallest window main.cpp opens)
constexpr uint32_t kMaxScript = 32768;   // events of one --keys / --keys-file script (a soak: ~6000 key taps)
constexpr unsigned kPads = 2;                           // slot 0 -> port 1, slot 1 -> port 0

enum ScriptKind : uint8_t { SK_KEY_DOWN, SK_KEY_UP, SK_KEY_REPEAT, SK_MOUSE, SK_LMB, SK_RMB };
struct ScriptEvent {
	uint32_t ulFrame;
	uint8_t ubKind;
	int32_t lA, lB;                 // key: scancode / lA = 1 for "down" of a mouse button; mouse: dx, dy counts
};

struct State {
	platform::KeyEventFn pfnKey;
	ms::KeyboardState sKeys;
	ms::JoyKeyState sJoyKeys;
	const ms::JoyKeySet *pJoyKeySet;
	ms::MouseCounters sMouse;
	bool isLeft, isRight;
	bool isGrabWanted, isFocused, isGrabbed;
	SDL_Window *pWindow;            // the window that last had the focus (relative mouse mode is per window)
	float fRemX, fRemY;             // fractional mouse motion not yet handed on (high-DPI devices report non-integers)
	SDL_Gamepad *apPad[kPads];
	SDL_JoystickID aPadId[kPads];
	ScriptEvent aScript[kMaxScript];
	uint32_t ulScript;
	bool isProbe;
	platform::PortState sLastPorts;
	bool isLastValid;
};
State s_in;

uint32_t frameNow() {
	return timerFrames();
}

void setGrab(bool isOn) {
	if(isOn == s_in.isGrabbed || s_in.pWindow == nullptr) {
		return;
	}
	s_in.isGrabbed = SDL_SetWindowRelativeMouseMode(s_in.pWindow, isOn) ? isOn : false;
}

void deliver(uint8_t ubRaw, bool isDown) {
	if(s_in.isProbe) {
		platform::logFormat("input: frame %u key %s 0x%02X\n", static_cast<unsigned>(frameNow()), isDown ? "down" : "up", ubRaw);
	}
	if(s_in.pfnKey != nullptr) {
		s_in.pfnKey(ubRaw, isDown);
	}
}

void releaseEverything() {
	uint8_t ubRaw = 0;
	while(ms::keyboardNextHeld(s_in.sKeys, &ubRaw)) {
		deliver(ubRaw, false);
	}
	s_in.sJoyKeys.ubHeld = 0;
	s_in.isLeft = s_in.isRight = false;
}

void keyEdge(uint16_t uwUsage, bool isDown, bool isRepeat, bool isGrabChord) {
	if(isRepeat) {
		return;                       // the Amiga keyboard sends no repeats
	}
	if(isDown && isGrabChord) {
		s_in.isGrabWanted = !s_in.isGrabWanted;
		setGrab(s_in.isGrabWanted && s_in.isFocused);
		return;
	}
	ms::joyKeyEvent(s_in.sJoyKeys, *s_in.pJoyKeySet, uwUsage, isDown);
	uint8_t ubRaw = 0;
	const ms::KeyAction eAction = ms::keyboardEvent(s_in.sKeys, uwUsage, isDown, &ubRaw);
	if(eAction != ms::KEY_IGNORE) {
		deliver(ubRaw, eAction == ms::KEY_PRESS);
	}
}

void mouseMotion(const SDL_MouseMotionEvent &sMotion) {
	int wWidth = kDefaultWinW, wHeight = kDefaultWinH;
	SDL_Window *pWindow = SDL_GetWindowFromID(sMotion.windowID);
	if(pWindow != nullptr) {
		SDL_GetWindowSize(pWindow, &wWidth, &wHeight);
	}
	const ms::planar::Rect sRect = displayFit(wWidth, wHeight);   // the picture as rt/sdl/display draws it (pixel-perfect or --aspect)
	if(sRect.w <= 0 || sRect.h <= 0) {
		return;
	}
	// window pixels -> game counts: the picture is platform::kDisplayWidth counts wide and sRect.w pixels wide
	const int32_t lScaleX = static_cast<int32_t>((static_cast<int64_t>(platform::kDisplayWidth) << 16) / sRect.w);
	const int32_t lScaleY = static_cast<int32_t>((static_cast<int64_t>(platform::kDisplayHeight) << 16) / sRect.h);
	const float fX = sMotion.xrel + s_in.fRemX;
	const float fY = sMotion.yrel + s_in.fRemY;
	const int32_t lDx = static_cast<int32_t>(fX);   // truncates toward zero; the remainder keeps its sign and is added to the next event
	const int32_t lDy = static_cast<int32_t>(fY);
	s_in.fRemX = fX - static_cast<float>(lDx);
	s_in.fRemY = fY - static_cast<float>(lDy);
	ms::mouseMove(s_in.sMouse, lDx, lDy, lScaleX, lScaleY);
}

void padOpen(SDL_JoystickID lId) {   // SDL3: the id of the device (= its instance id once opened), not an index
	for(unsigned i = 0; i < kPads; ++i) {
		if(s_in.apPad[i] != nullptr && s_in.aPadId[i] == lId) {
			return;                                 // already open
		}
	}
	SDL_Gamepad *pPad = SDL_OpenGamepad(lId);
	if(pPad == nullptr) {
		return;
	}
	for(unsigned i = 0; i < kPads; ++i) {
		if(s_in.apPad[i] == nullptr) {
			s_in.apPad[i] = pPad;
			s_in.aPadId[i] = lId;
			platform::logFormat("input: pad %u (%s) -> port %u\n", i + 1, SDL_GetGamepadName(pPad), i == 0 ? 1u : 0u);
			return;
		}
	}
	SDL_CloseGamepad(pPad);                         // a third pad is not used
}

void padClose(SDL_JoystickID lId) {
	for(unsigned i = 0; i < kPads; ++i) {
		if(s_in.apPad[i] != nullptr && s_in.aPadId[i] == lId) {
			SDL_CloseGamepad(s_in.apPad[i]);
			s_in.apPad[i] = nullptr;
		}
	}
}

// The joystick word of one pad: d-pad and left stick for the directions, A / X / right shoulder for fire.
uint16_t padBitsOf(SDL_Gamepad *pPad, bool *pIsSecond) {
	*pIsSecond = false;
	if(pPad == nullptr) {
		return 0;
	}
	uint16_t uwBits = ms::padStickBits(SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTY));
	if(SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_UP)) uwBits |= ms::kJoyUp;
	if(SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) uwBits |= ms::kJoyDown;
	if(SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) uwBits |= ms::kJoyLeft;
	if(SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) uwBits |= ms::kJoyRight;
	if(SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_WEST)
		|| SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
		uwBits |= ms::kJoyFire;
	}
	*pIsSecond = SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_EAST) || SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_NORTH);
	return ms::joyCancelOpposites(uwBits);
}

// Local co-op on one PC (docs/PC_PORT.md "Local co-op"): which of the game's four controllers can be driven right now.  Joystick 1 is the
// keyboard joystick (arrows) or gamepad 1, Joystick 2 is only gamepad 2 (without one port 0 is the mouse), Keys arrows share their keys
// with the keyboard joystick (usable with --joykeys none), Keys WASD (W A S D + Alt) is always there.  The knight screen offers the usable
// ones first, so two keyboard players are Joystick 1 and Keys WASD without touching the Control line.
bool padUsable(uint8_t ubPad) {
	switch(ubPad) {
		case ms::PAD_JOY1: return s_in.pJoyKeySet != &ms::kJoyKeysNone || s_in.apPad[0] != nullptr;
		case ms::PAD_JOY0: return s_in.apPad[1] != nullptr;
		case ms::PAD_KEYS_ARROWS: return s_in.pJoyKeySet == &ms::kJoyKeysNone;
		default: return true;
	}
}

// ---- script ------------------------------------------------------------------------------------------------------------------------

bool scriptAdd(uint32_t ulFrame, ScriptKind eKind, int32_t lA, int32_t lB) {
	if(s_in.ulScript >= kMaxScript) {
		platform::logFormat("input: --keys script is longer than %u events\n", static_cast<unsigned>(kMaxScript));
		return false;
	}
	ScriptEvent &sEvent = s_in.aScript[s_in.ulScript++];
	sEvent.ulFrame = ulFrame;
	sEvent.ubKind = eKind;
	sEvent.lA = lA;
	sEvent.lB = lB;
	return true;
}

SDL_Scancode scancodeOf(const char *szName) {
	char szBuf[48];
	size_t n = 0;
	for(; szName[n] != 0 && n < sizeof(szBuf) - 1; ++n) {
		szBuf[n] = szName[n] == '_' ? ' ' : szName[n];
	}
	szBuf[n] = 0;
	return SDL_GetScancodeFromName(szBuf);
}

void runScript(uint32_t ulFrame) {
	for(uint32_t i = 0; i < s_in.ulScript; ++i) {
		const ScriptEvent &sEvent = s_in.aScript[i];
		if(sEvent.ulFrame != ulFrame) {
			continue;
		}
		SDL_Event sSdl;
		memset(&sSdl, 0, sizeof(sSdl));
		switch(sEvent.ubKind) {
			case SK_KEY_DOWN:
			case SK_KEY_UP:
			case SK_KEY_REPEAT:
				sSdl.type = sEvent.ubKind == SK_KEY_UP ? SDL_EVENT_KEY_UP : SDL_EVENT_KEY_DOWN;
				sSdl.key.down = sEvent.ubKind != SK_KEY_UP;
				sSdl.key.repeat = sEvent.ubKind == SK_KEY_REPEAT;
				sSdl.key.scancode = static_cast<SDL_Scancode>(sEvent.lA);
				inputHandleEvent(sSdl);
				break;
			case SK_MOUSE:
				ms::mouseMove(s_in.sMouse, sEvent.lA, sEvent.lB, 65536, 65536);
				break;
			case SK_LMB:
				s_in.isLeft = sEvent.lA != 0;
				break;
			case SK_RMB:
				s_in.isRight = sEvent.lA != 0;
				break;
			default:
				break;
		}
	}
}

}  // namespace

InputOptions inputDefaultOptions() {
	InputOptions sOptions;
	sOptions.pJoyKeys = &ms::kJoyKeysArrows;
	sOptions.isGrabMouse = true;
	return sOptions;
}

InputSession::InputSession(const InputOptions &sOptions) : m_isPadSystem(SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
	// a key handler registered before the session and a script parsed before it survive; everything else starts clean
	ms::keyboardReset(s_in.sKeys);
	ms::mouseReset(s_in.sMouse);
	s_in.sJoyKeys.ubHeld = 0;
	s_in.isLeft = s_in.isRight = s_in.isFocused = s_in.isGrabbed = false;
	s_in.pWindow = nullptr;
	s_in.fRemX = s_in.fRemY = 0.0f;
	s_in.isProbe = s_in.isLastValid = false;
	for(unsigned i = 0; i < kPads; ++i) {
		s_in.apPad[i] = nullptr;
	}
	s_in.pJoyKeySet = sOptions.pJoyKeys != nullptr ? sOptions.pJoyKeys : &ms::kJoyKeysNone;
	s_in.isGrabWanted = sOptions.isGrabMouse;
	ms::game::g_pfnPadUsable = padUsable;
	if(!m_isPadSystem) {
		platform::logFormat("input: game controllers unavailable: %s\n", SDL_GetError());
	}
}

InputSession::~InputSession() {
	ms::game::g_pfnPadUsable = nullptr;
	setGrab(false);
	for(unsigned i = 0; i < kPads; ++i) {
		if(s_in.apPad[i] != nullptr) {
			SDL_CloseGamepad(s_in.apPad[i]);
			s_in.apPad[i] = nullptr;
		}
	}
	s_in.pWindow = nullptr;
	if(m_isPadSystem) {
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
	}
}

void inputFrameBegin() {
	ms::mouseBeginFrame(s_in.sMouse);
	runScript(frameNow());
}

bool inputHandleEvent(const SDL_Event &sEvent) {
	switch(sEvent.type) {
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP: {
			const SDL_KeyboardEvent &sKey = sEvent.key;
			const bool isChord = sKey.scancode == SDL_SCANCODE_G && (sKey.mod & SDL_KMOD_CTRL) && (sKey.mod & SDL_KMOD_ALT);
			keyEdge(static_cast<uint16_t>(sKey.scancode), sKey.down, sKey.repeat, isChord);
			return true;
		}
		case SDL_EVENT_MOUSE_MOTION:
			mouseMotion(sEvent.motion);
			return true;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if(sEvent.button.button == SDL_BUTTON_LEFT) {
				s_in.isLeft = sEvent.button.down;
			}
			else if(sEvent.button.button == SDL_BUTTON_RIGHT) {
				s_in.isRight = sEvent.button.down;
			}
			return true;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			s_in.isFocused = false;
			setGrab(false);
			releaseEverything();
			return true;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			s_in.isFocused = true;
			s_in.pWindow = SDL_GetWindowFromID(sEvent.window.windowID);
			setGrab(s_in.isGrabWanted);
			return true;
		case SDL_EVENT_GAMEPAD_ADDED:
			padOpen(sEvent.gdevice.which);
			return true;
		case SDL_EVENT_GAMEPAD_REMOVED:
			padClose(sEvent.gdevice.which);
			return true;
		default:
			return false;
	}
}

void inputFrameEnd() {
	if(!s_in.isProbe) {
		return;
	}
	platform::PortState sNow;
	platform::inputReadPorts(sNow);
	if(s_in.isLastValid && memcmp(&sNow, &s_in.sLastPorts, sizeof(sNow)) == 0) {
		return;
	}
	s_in.sLastPorts = sNow;
	s_in.isLastValid = true;
	platform::logFormat("input: frame %u ports joy0=0x%04X joy1=0x%04X fire0=%d fire1=%d right0=%d right1=%d\n", static_cast<unsigned>(frameNow()),
		sNow.uwJoy0Dat, sNow.uwJoy1Dat, sNow.isFire0, sNow.isFire1, sNow.isRight0, sNow.isRight1);
}

bool inputHasKeyHandler() {
	return s_in.pfnKey != nullptr;
}

bool inputScriptParse(const char *szScript) {
	s_in.ulScript = 0;
	uint32_t ulAt = 1;
	const char *p = szScript;
	while(*p != 0) {
		while(*p == ' ' || *p == '\t' || *p == '\n') {
			++p;
		}
		char szTok[64];
		size_t n = 0;
		while(*p != 0 && *p != ' ' && *p != '\t' && *p != '\n' && n < sizeof(szTok) - 1) {
			szTok[n++] = *p++;
		}
		szTok[n] = 0;
		if(n == 0) {
			break;
		}
		bool isOk = true;
		if(szTok[0] == '@') {
			ulAt += static_cast<uint32_t>(atoi(szTok + 1));
		}
		else if(strcmp(szTok, "lmb+") == 0 || strcmp(szTok, "lmb-") == 0) {
			isOk = scriptAdd(ulAt, SK_LMB, szTok[3] == '+', 0);
		}
		else if(strcmp(szTok, "rmb+") == 0 || strcmp(szTok, "rmb-") == 0) {
			isOk = scriptAdd(ulAt, SK_RMB, szTok[3] == '+', 0);
		}
		else if(strncmp(szTok, "mouse=", 6) == 0) {
			int lDx = 0, lDy = 0;
			isOk = sscanf(szTok + 6, "%d,%d", &lDx, &lDy) == 2 && scriptAdd(ulAt, SK_MOUSE, lDx, lDy);
		}
		else {
			const char cMode = szTok[0];
			const bool isPrefixed = cMode == '+' || cMode == '-' || cMode == '~';
			const SDL_Scancode eCode = scancodeOf(isPrefixed ? szTok + 1 : szTok);
			if(eCode == SDL_SCANCODE_UNKNOWN) {
				platform::logFormat("input: unknown key name in --keys: %s\n", szTok);
				return false;
			}
			if(cMode == '+') {
				isOk = scriptAdd(ulAt, SK_KEY_DOWN, eCode, 0);
			}
			else if(cMode == '-') {
				isOk = scriptAdd(ulAt, SK_KEY_UP, eCode, 0);
			}
			else if(cMode == '~') {
				isOk = scriptAdd(ulAt, SK_KEY_REPEAT, eCode, 0);
			}
			else {
				isOk = scriptAdd(ulAt, SK_KEY_DOWN, eCode, 0) && scriptAdd(ulAt + 1, SK_KEY_UP, eCode, 0);
			}
		}
		if(!isOk) {
			return false;
		}
	}
	return true;
}

InputProbe::InputProbe(bool isActive) : m_isActive(isActive) {
	if(!m_isActive) {
		return;
	}
	s_in.isProbe = true;
	s_in.isLastValid = false;
	platform::inputSetKeyHandler([](uint8_t, bool) {});   // a registered (silent) handler: deliver() logs the event itself
}

InputProbe::~InputProbe() {
	if(!m_isActive) {
		return;
	}
	platform::inputSetKeyHandler(nullptr);
	s_in.isProbe = false;
}

}  // namespace sdl

namespace platform {

void inputReadPorts(PortState &sOut) {
	bool isSecond1 = false, isSecond2 = false;
	const uint16_t uwPad1 = sdl::padBitsOf(sdl::s_in.apPad[0], &isSecond1);
	const uint16_t uwPad2 = sdl::padBitsOf(sdl::s_in.apPad[1], &isSecond2);
	const uint16_t uwPort1 = ms::joyCancelOpposites(static_cast<uint16_t>(ms::joyKeyBits(sdl::s_in.sJoyKeys) | uwPad1));
	sOut.uwJoy1Dat = ms::joyEncode(uwPort1);
	sOut.isFire1 = (uwPort1 & ms::kJoyFire) != 0;
	sOut.isRight1 = isSecond1;
	if(sdl::s_in.apPad[1] != nullptr) {           // a joystick in port 0 replaces the mouse
		sOut.uwJoy0Dat = ms::joyEncode(uwPad2);
		sOut.isFire0 = (uwPad2 & ms::kJoyFire) != 0 || sdl::s_in.isLeft;
	}
	else {
		sOut.uwJoy0Dat = ms::mouseDat(sdl::s_in.sMouse);
		sOut.isFire0 = sdl::s_in.isLeft;
	}
	sOut.isRight0 = sdl::s_in.isRight || isSecond2;
}

void inputArmPots() {
	// nothing to do on the PC: the pot counters do not exist
}

void inputSetKeyHandler(KeyEventFn pfnHandler) {
	sdl::s_in.pfnKey = pfnHandler;
}

bool inputKeyHeld(uint8_t ubRawCode) {
	return ubRawCode < 128 && sdl::s_in.sKeys.aubCount[ubRawCode] != 0;
}

bool inputSkipRequested() {
	return sdl::gameSkipIntro();
}

}  // namespace platform
}  // namespace rt
