"""Host test of the co-op input layer (ROADMAP 8.2): engine/pad.cpp (controllers: two joystick ports, two keyboard sets, the
adapter stub), game/party.cpp (g_party, controller binding, the "player stick" routing) and the co-op steps of the title menu
and the knight screen (src/game/scene_menu.cpp).  The classic menu / knight-screen traces are tests/test_scene_menu.py (against
the asm model); here: a classic run with an inactive party gives exactly the trace of a run without one.

The C++ driver is compiled with clang++ (no STL, no exceptions) and prints one line per check; the Python side compares.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "engine/pad.hpp"
#include "game/party.hpp"
#include "game/scene_menu.hpp"
using namespace ms;
using namespace ms::game;

// ---- scripted MenuOps: every call goes into a text trace ---------------------------------------------------------------
static char g_trace[65536];
static size_t g_len;
static void tr(const char *s) {
	size_t n = strlen(s);
	if (g_len + n + 1 < sizeof(g_trace)) { memcpy(g_trace + g_len, s, n); g_len += n; g_trace[g_len++] = '|'; g_trace[g_len] = 0; }
}
static char g_buf[128];
static const uint16_t *g_joy;
static int g_nJoy, g_pos;
static bool g_eos;
static void oNop() {}
static void oFlip() { tr("flip"); }
static void oWait(uint32_t n) { snprintf(g_buf, sizeof g_buf, "wait %u", (unsigned)n); tr(g_buf); }
static void oClear(MenuBuffer) {}
static void oTFlag(uint16_t) {}
static void oSprite(MenuBank b, uint16_t i, uint16_t x, uint16_t y) { snprintf(g_buf, sizeof g_buf, "spr %d %u %u %u", (int)b, i, x, y); tr(g_buf); }
static void oNum(uint16_t v) { snprintf(g_buf, sizeof g_buf, "num %u", v); tr(g_buf); }
static void oParty(uint16_t v) { snprintf(g_buf, sizeof g_buf, "coop %u", v); tr(g_buf); }
static void oGore(bool) {}
static void oText(MenuText t) { snprintf(g_buf, sizeof g_buf, "text %d", (int)t); tr(g_buf); }
static void oStr(const uint8_t *p, uint16_t x, uint16_t y, uint16_t) { snprintf(g_buf, sizeof g_buf, "str %s %u %u", (const char *)p, x, y); tr(g_buf); }
static void oPal(MenuPalette) {}
static uint16_t oJoy() {
	if (g_pos >= g_nJoy) { g_eos = true; return 0x10; }   // the script ran out: keep pressing fire so every loop ends
	return g_joy[g_pos++];
}
static uint16_t oKey() { return 0; }
static void oKeyNop() {}
static uint8_t oXlat(uint16_t) { return 0; }
static void oPub(uint16_t) {}
static uint32_t oSpawn(uint16_t, uint16_t, uint16_t, uint16_t) { return 1; }
static void oKill(uint32_t) {}
static const MenuOps g_ops = {oNop, oNop, oFlip, oWait, oNop, oNop, oNop, oClear, oTFlag, oSprite, oNum, oGore, oText, oStr,
	oPal, oNop, oJoy, oKey, oKeyNop, oKeyNop, oXlat, oNop, oPub, oSpawn, oKill, oParty};

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if (!ok) { ++g_fail; printf("FAIL %s\n", what); }
}

static void run(const uint16_t *joy, int n) { g_joy = joy; g_nJoy = n; g_pos = 0; g_eos = false; g_len = 0; g_trace[0] = 0; }

static SceneMenu menuRun(const uint16_t *joy, int n, uint16_t players, bool allowed, bool coop) {
	SceneMenu m;
	m.state.uwPlayers = players; m.state.uwCursor = 0; m.state.uwMoved = 0; m.state.ulGore = 0; m.state.uwDamageDiv = 3;
	m.state.bCoopAllowed = allowed; m.state.bCoop = coop;
	run(joy, n);
	m.enter(g_ops);
	while (!m.update(g_ops)) {}
	m.exit(g_ops);
	return m;
}

static uint8_t g_names[4][22];
static Knight g_knights[5];
static SceneKnights knightsRun(const uint16_t *joy, int n, uint16_t players, PartyConfig *pParty) {
	static const char *kNames[4] = {"SIR_GODBER", "SIR_RICHARD", "SIR_JEFFREY", "SIR_EDWARD"};
	for (int i = 0; i < 4; ++i) { memset(g_names[i], ' ', 21); memcpy(g_names[i], kNames[i], strlen(kNames[i])); g_names[i][21] = 0; }
	memset(g_knights, 0, sizeof g_knights);
	SceneKnights k;
	k.env.uwPlayers = players;
	k.env.aKnights = g_knights;
	for (int i = 0; i < 4; ++i) k.env.apNames[i] = g_names[i];
	k.env.pParty = pParty;
	run(joy, n);
	k.enter(g_ops);
	while (!k.update(g_ops)) {}
	k.exit(g_ops);
	return k;
}

int main() {
	// ---- engine/pad: keyboard sets -------------------------------------------------------------------------------------
	volatile uint8_t keys[128];
	memset((void *)keys, 0, sizeof keys);
	check(padKeyBits(kPadKeysArrows, keys) == 0, "no key");
	keys[0x48] = 1; keys[0x4D] = 1;                              // up + right (translated codes of the cursor keys)
	check(padKeyBits(kPadKeysArrows, keys) == (kJoyUp | kJoyRight), "arrows up+right");
	check(padKeyBits(kPadKeysWasd, keys) == 0, "arrows are not WASD");
	keys[0x1D] = 1;                                              // Ctrl
	check(padKeyBits(kPadKeysArrows, keys) == (kJoyUp | kJoyRight | kJoyFire), "arrows fire = Ctrl");
	keys[0x1D] = 0; keys[0x36] = 1;                              // Right Shift
	check(padKeyBits(kPadKeysArrows, keys) == (kJoyUp | kJoyRight | kJoyFire), "arrows fire = RShift");
	keys[0x4B] = 1;                                              // left as well: left+right cancel
	check(padKeyBits(kPadKeysArrows, keys) == (kJoyUp | kJoyFire), "left+right cancel");
	memset((void *)keys, 0, sizeof keys);
	keys[0x11] = 1; keys[0x1E] = 1; keys[0x38] = 1;              // W A Alt
	check(padKeyBits(kPadKeysWasd, keys) == (kJoyUp | kJoyLeft | kJoyFire), "WASD up+left+Alt");
	keys[0x1F] = 1;                                              // S: up+down cancel
	check(padKeyBits(kPadKeysWasd, keys) == (kJoyLeft | kJoyFire), "up+down cancel");
	check(padKeyBits(kPadKeysArrows, keys) == 0, "WASD is not arrows");
	keys[0x20] = 1; keys[0x1E] = 0; keys[0x11] = 0; keys[0x1F] = 0; keys[0x38] = 0;   // D
	check(padKeyBits(kPadKeysWasd, keys) == kJoyRight, "D = right");
	check(padKeyBits(kPadKeysWasd, nullptr) == 0, "no keyboard");

	// ---- engine/pad: the frame ------------------------------------------------------------------------------------------
	PadFrame f;
	PadInputs in = {{0x0002, 0x0011}, keys, 0, 0};               // port 0 left, port 1 right+fire, D held
	padsFill(f, in);
	check(padBits(f, PAD_JOY1) == 0x11, "joy1 = port 1");
	check(padBits(f, PAD_JOY0) == 0x02, "joy0 = port 0");
	check(padBits(f, PAD_KEYS_WASD) == kJoyRight, "frame WASD");
	check(padBits(f, PAD_KEYS_ARROWS) == 0, "frame arrows");
	check(padBits(f, PAD_ADAPTER3) == 0 && padBits(f, PAD_ADAPTER4) == 0, "adapter stub");
	check(padBits(f, PAD_NONE) == 0 && padBits(f, 17) == 0, "unknown source");
	check(padsAny(f) == 0x13, "any");
	PadAdapterBits ad = padAdapterDecode(0x00, 0x00);
	check(ad.uwJoy3 == 0 && ad.uwJoy4 == 0, "adapter decode stub");
	check(!strcmp(padName(PAD_JOY1), "Joystick 1") && !strcmp(padName(PAD_JOY0), "Joystick 2"), "pad names");
	check(!strcmp(padName(PAD_KEYS_ARROWS), "Keys arrows") && !strcmp(padName(PAD_KEYS_WASD), "Keys WASD"), "key set names");

	// ---- game/party ------------------------------------------------------------------------------------------------------
	check(!g_party.active && g_party.n == 0, "g_party starts classic (zero)");
	PartyConfig p;
	partyStart(p, 2);
	check(p.active && p.n == 2 && p.aubPad[0] == PAD_JOY1 && p.aubPad[1] == PAD_JOY0 && p.aubPad[2] == PAD_NONE, "start 2");
	check(p.ubFocus == PARTY_FOCUS_ANY, "start focus any");
	PartyConfig p4; partyStart(p4, 9);
	check(p4.n == 4 && p4.aubPad[3] == PAD_KEYS_WASD, "start clamps to 4");
	PartyConfig p0; partyStart(p0, 0);
	check(p0.n == 1, "start clamps to 1");
	check(partyPadTaken(p, 1, PAD_JOY1) && !partyPadTaken(p, 1, PAD_JOY0) && partyPadTaken(p, 2, PAD_JOY0), "taken");
	check(partyNextPad(p, 1, PAD_JOY0, 1) == PAD_KEYS_ARROWS, "next skips nothing taken");
	check(partyNextPad(p, 1, PAD_KEYS_WASD, 1) == PAD_JOY0, "next wraps past the taken joy1");
	check(partyNextPad(p, 1, PAD_JOY0, -1) == PAD_KEYS_WASD, "previous wraps past the taken joy1");
	p.aubPad[0] = PAD_JOY0;
	check(partyFirstFreePad(p, 1) == PAD_KEYS_ARROWS, "first free when the default is taken");
	check(partyFirstFreePad(p, 0) == PAD_JOY1, "first free = default");
	p.aubPad[0] = PAD_JOY1;
	// routing of the player stick
	PartyConfig r; partyStart(r, 2);
	r.aubPad[1] = PAD_KEYS_WASD;
	check(partyStick(r, f, 0) == 0x13, "focus any = every controller");
	r.ubFocus = PARTY_FOCUS_TURN;
	check(partyStick(r, f, 0) == 0x11, "turn: knight 0 = joystick 1");
	check(partyStick(r, f, 1) == kJoyRight, "turn: knight 1 = WASD");
	check(partyStick(r, f, PARTY_MEMBER_NONE) == 0x11 && partyStick(r, f, 2) == 0x11, "turn of a non-member: port 1");
	r.ubFocus = 1;
	check(partyStick(r, f, 0) == kJoyRight, "fixed focus member 1");
	check(partyPadBits(r, f, 3) == 0, "no member 3");
	partyReset(r);
	check(!r.active && r.n == 0 && r.aubPad[0] == PAD_NONE, "reset");

	// ---- title menu: Players past 4 -> Coop 2 ---------------------------------------------------------------------------
	{
		const uint16_t s[] = {1, 1, 1, 4, 4, 4, 0x10};          // right x3 (1 -> 4), down x3 to Select Knight, fire
		SceneMenu m = menuRun(s, 7, 1, false, false);
		check(m.state.uwPlayers == 4 && !m.state.bCoop && m.eResult == MENU_SELECT, "classic clamps at 4");
		check(!strstr(g_trace, "coop"), "classic never draws coop");
	}
	{
		const uint16_t s[] = {1, 1, 1, 1, 4, 4, 4, 0x10};       // right x4: 4 -> Coop 2
		SceneMenu m = menuRun(s, 8, 1, true, false);
		check(m.state.bCoop && m.state.uwPlayers == 2 && m.state.uwDamageDiv == 2 && m.eResult == MENU_SELECT, "coop 2");
		check(strstr(g_trace, "num 4|") && strstr(g_trace, "coop 2|"), "coop text drawn");
	}
	{
		const uint16_t s[] = {1, 1, 4, 4, 4, 0x10};             // already Coop 2: right stays at PARTY_COOP_MAX
		SceneMenu m = menuRun(s, 6, 2, true, true);
		check(m.state.bCoop && m.state.uwPlayers == PARTY_COOP_MAX, "coop max");
	}
	{
		const uint16_t s[] = {2, 4, 4, 4, 0x10};                // Coop 2, left: back to 4 players
		SceneMenu m = menuRun(s, 5, 2, true, true);
		check(!m.state.bCoop && m.state.uwPlayers == 4 && m.state.uwDamageDiv == 1, "coop left -> 4");
		check(strstr(g_trace, "coop 2|") && strstr(g_trace, "num 4|"), "coop then number");
	}
	{
		const uint16_t s[] = {1, 4, 4, 4, 0x10};                // 1 player, right: 2 (no co-op below 4)
		SceneMenu m = menuRun(s, 5, 1, true, false);
		check(!m.state.bCoop && m.state.uwPlayers == 2, "coop only past 4");
	}

	// ---- knight screen ---------------------------------------------------------------------------------------------------
	// classic with an inactive party: the same trace and records as without one
	{
		const uint16_t s[] = {0x10, 0, 0x10, 0, 1, 0x10, 0, 0x10};
		knightsRun(s, 8, 2, nullptr);
		static char want[65536]; strcpy(want, g_trace);
		Knight wantK[5]; memcpy(wantK, g_knights, sizeof wantK);
		PartyConfig off; partyReset(off);
		knightsRun(s, 8, 2, &off);
		check(!strcmp(want, g_trace), "inactive party = classic trace");
		check(!memcmp(wantK, g_knights, sizeof wantK), "inactive party = classic records");
		check(!strstr(g_trace, "Control"), "classic never shows a controller");
	}
	// co-op 2: knight 1 takes portrait 0 and steps its controller right (joystick 1 -> joystick 2); knight 2 takes the next
	// portrait and keeps what is offered (its default joystick 2 is taken: arrows)
	{
		PartyConfig party; partyStart(party, 2);
		const uint16_t s[] = {
			0x10, 0, 0x10,       // portrait 0: fire, release, fire = keep the name
			0x10, 0,             // pad choice: still held (wait), released
			1,                   // right: Joystick 2
			0x10,                // take it
			0x10, 0, 0x10,       // knight 2: portrait 1, keep the name
			0,                   // released
			0x10                 // take the offer
		};
		SceneKnights k = knightsRun(s, 12, 2, &party);
		check(!g_eos, "co-op script not exhausted");
		check(party.aubPad[0] == PAD_JOY0 && party.aubPad[1] == PAD_KEYS_ARROWS, "co-op pads bound");
		check(g_knights[0].ulKind == 0 && g_knights[1].ulKind == 1, "co-op portraits");
		check(g_knights[0].ubInputPort == 2 && g_knights[1].ubInputPort == 2, "co-op keeps +11 = 2");
		check(k.ubFree == 0x0C && k.uwRemaining == 0, "co-op picks");
		check(strstr(g_trace, "str Control  Joystick 1 50 168|") != nullptr, "offer joystick 1");
		check(strstr(g_trace, "str Control  Joystick 2 50 168|") != nullptr, "offer joystick 2");
		check(strstr(g_trace, "str Control  Keys arrows 50 168|") != nullptr, "offer arrows");
		check(strstr(g_trace, "str SIR GODBER") == nullptr && strstr(g_trace, "str SIR_GODBER 50 50|") != nullptr, "name shown");
		check(strstr(g_trace, "spr 1 2 12 80|spr 1 1 12 80|str SIR_GODBER") != nullptr, "taken portrait drawn under the frame");
	}
	printf("checks %d failed %d\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
'''


@unittest.skipUnless(CXX, 'needs clang++')
class Coop(unittest.TestCase):
    def test_pads_party_and_coop_screens(self):
        with tempfile.TemporaryDirectory() as tmp:
            p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
            with open(p, 'w') as f:
                f.write(DRIVER)
            cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti',
                   '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), p,
                   os.path.join(ROOT, 'src', 'game', 'scene_menu.cpp'), os.path.join(ROOT, 'src', 'game', 'party.cpp'),
                   os.path.join(ROOT, 'src', 'engine', 'pad.cpp'), '-o', exe]
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            out = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout)
            self.assertIn('failed 0', out.stdout)


if __name__ == '__main__':
    unittest.main()
