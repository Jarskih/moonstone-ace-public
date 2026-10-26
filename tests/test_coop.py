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
static const MenuOps g_ops = {oNop, oNop, oFlip, oWait, oNop, oNop, oNop, oClear, oTFlag, oSprite, oNum, oText, oStr,
	oPal, oNop, oJoy, oKey, oKeyNop, oKeyNop, oXlat, oNop, oPub, oSpawn, oKill, nullptr, nullptr};

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if (!ok) { ++g_fail; printf("FAIL %s\n", what); }
}

static void run(const uint16_t *joy, int n) { g_joy = joy; g_nJoy = n; g_pos = 0; g_eos = false; g_len = 0; g_trace[0] = 0; }

static SceneMenu menuRun(const uint16_t *joy, int n, uint16_t players, bool allowed, bool coop) {
	SceneMenu m;
	m.state.uwPlayers = players; m.state.uwCursor = 0; m.state.uwMoved = 0; m.state.uwDamageDiv = 3;
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
	check(partyNextPad(p, 1, PAD_KEYS_WASD, 1) == PAD_COMPUTER && partyNextPad(p, 1, PAD_COMPUTER, 1) == PAD_JOY0, "next: Computer follows the last controller, then wraps past the taken joy1");
	check(partyNextPad(p, 1, PAD_JOY0, -1) == PAD_COMPUTER && partyNextPad(p, 1, PAD_COMPUTER, -1) == PAD_KEYS_WASD, "previous wraps past the taken joy1 to Computer");
	check(partyNextPad(p, 0, PAD_KEYS_WASD, 1) == PAD_JOY1 && partyNextPad(p, 0, PAD_JOY1, -1) == PAD_KEYS_WASD, "the first knight is never offered Computer (ROADMAP 8.3b)");
	p.aubPad[0] = PAD_JOY0;
	check(partyFirstFreePad(p, 1) == PAD_KEYS_ARROWS, "first free when the default is taken");
	check(partyFirstFreePad(p, 0) == PAD_JOY1, "first free = default");
	p.aubPad[0] = PAD_JOY1;
	// the platform hint (PC: no second gamepad, keyboard joystick on): Joystick 2 and Keys arrows are skipped, nothing else changes
	{
		static bool (*const usable)(uint8_t) = [](uint8_t pad) { return pad == PAD_JOY1 || pad == PAD_KEYS_WASD; };
		PartyConfig h; partyStart(h, 4);
		g_pfnPadUsable = usable;
		check(partyFirstFreePad(h, 0) == PAD_JOY1 && partyFirstFreePad(h, 1) == PAD_KEYS_WASD, "hint: knight 2 gets WASD, not Joystick 2");
		check(partyNextPad(h, 1, PAD_KEYS_WASD, 1) == PAD_COMPUTER && partyNextPad(h, 1, PAD_KEYS_WASD, -1) == PAD_COMPUTER && partyNextPad(h, 1, PAD_COMPUTER, 1) == PAD_KEYS_WASD, "hint: cycling stays on the only usable free pad and Computer");
		h.aubPad[1] = PAD_KEYS_WASD;
		check(partyFirstFreePad(h, 2) == PAD_JOY0, "hint: nothing usable left, an unusable free pad is still offered");
		check(partyNextPad(h, 2, PAD_JOY0, 1) == PAD_KEYS_ARROWS, "hint: nothing usable left, cycling reaches every free pad");
		h.aubPad[2] = PAD_JOY0;
		check(partyFirstFreePad(h, 3) == PAD_KEYS_ARROWS && partyNextPad(h, 3, PAD_KEYS_WASD, 1) == PAD_COMPUTER, "hint: the fourth knight never gets the pad the second holds");
		g_pfnPadUsable = nullptr;
		PartyConfig q2; partyStart(q2, 2);
		check(partyFirstFreePad(q2, 1) == PAD_JOY0, "no hint: knight 2 defaults to Joystick 2");
	}
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
	// ---- network game: the binding is (node, pad); a pad is unique per node, the merged slot is the member index -----------
	{
		PartyConfig q; partyStart(q, 4);
		const uint8_t nodes[4] = {0, 1, 0, 1};
		partyNetBind(q, nodes);
		check(q.active && q.n == 4 && q.isNet && q.aubNode[1] == 1 && q.aubNode[2] == 0 && q.aubPad[0] == PAD_NONE && q.aubPad[3] == PAD_NONE, "net bind: nodes set, nothing chosen");
		check(partySlot(q, 0) == 0 && partySlot(q, 3) == 3 && partySlot(q, 4) == PAD_NONE, "net slot = member index");
		check(partyFirstFreePad(q, 0) == PAD_JOY1 && partyFirstFreePad(q, 1) == PAD_JOY1, "net default is Joystick 1 for everybody");
		q.aubPad[0] = PAD_JOY1;
		check(partyFirstFreePad(q, 1) == PAD_JOY1, "member on another node may pick the same pad");
		check(!partyPadTakenFor(q, 1, PAD_JOY1) && partyPadTaken(q, 1, PAD_JOY1), "taken per node");
		q.aubPad[1] = PAD_JOY1;
		check(partyPadTakenFor(q, 2, PAD_JOY1) && !partyPadTakenFor(q, 3, PAD_JOY0), "member 2 shares node 0 with member 0, member 3 sees only node 1");
		check(partyFirstFreePad(q, 2) == PAD_JOY0, "same node: the default is taken, next free");
		check(partyNextPad(q, 2, PAD_JOY0, 1) == PAD_KEYS_ARROWS && partyNextPad(q, 2, PAD_JOY0, -1) == PAD_KEYS_WASD, "same node: cycling skips the taken pad");
		check(partyNextPad(q, 3, PAD_JOY0, -1) == PAD_KEYS_WASD && partyNextPad(q, 3, PAD_KEYS_WASD, 1) == PAD_JOY0, "node 1: member 1 holds Joystick 1");
		q.aubPad[2] = PAD_KEYS_WASD; q.aubPad[3] = PAD_KEYS_WASD;   // WASD on node 0 and on node 1: fine
		PadFrame mf;
		for(int i = 0; i < PAD_SOURCE_COUNT; ++i) mf.auwBits[i] = (uint16_t)(0x10 + i);
		check(partyPadBits(q, mf, 1) == 0x11 && partyPadBits(q, mf, 3) == 0x13, "net: a member reads its merged slot, not its controller");
		q.ubFocus = PARTY_FOCUS_TURN;
		check(partyStick(q, mf, 2) == 0x12, "net: stick of member 2 = slot 2");
		partyLocalize(q);
		check(!q.isNet && partySlot(q, 3) == PAD_KEYS_WASD && partyPadBits(q, mf, 3) == 0x13 && partyPadBits(q, mf, 0) == 0x10, "localize: members read their picked pads");
		partyStart(q, 2);
		check(!q.isNet && q.aubPad[1] == PAD_JOY0, "partyStart is local again");
		PartyConfig z; memset(&z, 0, sizeof z);
		check(partySlot(z, 0) == 0 && !z.isNet, "zero config = local");
	}
	partyReset(r);
	check(!r.active && r.n == 0 && r.aubPad[0] == PAD_NONE, "reset");

	// ---- title menu: Players is the count, Mode (under it) is Classic / Coop (owner 2026-10-11; Players, Mode, an empty line, Practice, Select Knight) --------
	{
		const uint16_t s[] = {1, 1, 1, 4, 4, 0x10};             // right x3 (1 -> 4), down x2 (no Mode row) to Select Knight, fire
		SceneMenu m = menuRun(s, 6, 1, false, false);
		check(m.state.uwPlayers == 4 && !m.state.bCoop && m.eResult == MENU_SELECT, "classic clamps at 4");
		check(!strstr(g_trace, "coop") && strstr(g_trace, "num 4|"), "classic draws the number");
	}
	{
		const uint16_t s[] = {1, 1, 1, 1, 4, 4, 4, 0x10};       // right x4 on Players: 4 stays 4 (no Coop beyond), still classic; down x3 over Mode
		SceneMenu m = menuRun(s, 8, 1, true, false);
		check(!m.state.bCoop && m.state.uwPlayers == 4 && m.state.uwDamageDiv == 1 && m.eResult == MENU_SELECT, "players past 4 stay 4, classic");
		check(!strstr(g_trace, "Coop"), "no Coop N text");
	}
	{
		const uint16_t s[] = {4, 1, 4, 4, 0x10};                // down to Mode, right = Coop (1 player -> 2), down x2 to Select Knight, fire
		SceneMenu m = menuRun(s, 5, 1, true, false);
		check(m.state.bCoop && m.state.uwPlayers == 2 && m.state.uwDamageDiv == 2 && m.eResult == MENU_SELECT, "mode right: coop, 1 -> 2");
		check(strstr(g_trace, "str Mode  Classic 88 86|") && strstr(g_trace, "str Mode  Coop 88 86|"), "mode labels drawn under Players, no number");
		check(strstr(g_trace, "spr 1 0 50 86|"), "mode cursor position");
		check(menuItemY(ITEM_PLAYERS) == 66 && menuItemY(ITEM_MODE) == 86 && menuItemY(ITEM_PRACTICE) == 126 && menuItemY(ITEM_SELECT) == 146,
		      "Players, Mode, one empty line, Practice, Select Knight");
	}
	{
		const uint16_t s[] = {4, 1, 2, 4, 4, 0x10};             // 3 players: Coop keeps 3, back to Classic keeps 3
		SceneMenu m = menuRun(s, 6, 3, true, false);
		check(!m.state.bCoop && m.state.uwPlayers == 3 && m.eResult == MENU_SELECT, "classic -> coop -> classic keeps the count");
	}
	{
		const uint16_t s[] = {2, 2, 2, 4, 4, 4, 0x10};          // coop, Players left x3 from 3: 2, 2, 2 (never out of co-op, never 1), down x3, fire
		SceneMenu m = menuRun(s, 7, 3, true, true);
		check(m.state.bCoop && m.state.uwPlayers == 2 && m.state.uwDamageDiv == 2, "coop players clamp at 2");
	}
	{
		const uint16_t s[] = {1, 1, 1, 1, 4, 4, 4, 0x10};       // coop, right x4 from 2: 3, 4, 4, 4, then down x3, fire
		SceneMenu m = menuRun(s, 8, 2, true, true);
		check(m.state.bCoop && m.state.uwPlayers == PARTY_COOP_MAX && PARTY_COOP_MAX == 4 && m.state.uwDamageDiv == 1, "coop players clamp at PARTY_COOP_MAX");
	}
	{
		MenuState s = {};
		s.uwPlayers = 1; s.bCoopAllowed = true; s.uwCursor = ITEM_PLAYERS;
		check(menuInput(s, 4) == MENU_REDRAW && s.uwCursor == ITEM_MODE, "mode follows Players");
		check(menuInput(s, 4) == MENU_REDRAW && s.uwCursor == ITEM_PRACTICE, "the cursor skips the empty line");
		check(menuInput(s, 4) == MENU_REDRAW && s.uwCursor == ITEM_SELECT, "Select Knight follows Practice");
		check(menuInput(s, 4) == MENU_REDRAW && s.uwCursor == ITEM_SELECT && s.uwMoved == 1, "no Back row without bBack: Select Knight is the end stop");
		check(menuInput(s, 8) == MENU_REDRAW && menuInput(s, 8) == MENU_REDRAW && s.uwCursor == ITEM_MODE, "up over the empty line to Mode");
	}
	{
		const uint16_t s[] = {4, 0x10, 2, 4, 4, 0x10};          // on Mode: fire = Coop, left = Classic, then down x2 to Select Knight
		SceneMenu m = menuRun(s, 6, 1, true, false);
		check(!m.state.bCoop && m.state.uwPlayers == 2 && m.eResult == MENU_SELECT, "mode fire enables coop and left restores classic");
	}
	{
		const uint16_t s[] = {4, 4, 0x10};
		SceneMenu m = menuRun(s, 3, 1, false, false);
		check(m.state.uwCursor == ITEM_SELECT && !strstr(g_trace, "str Mode  "), "mode absent when disabled");
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
