// rt/scene_menu - asm-callable entries into src/game/scene_menu.cpp (ROADMAP 6.2), patched over the mog menu and the
// knight select (asm/patches/mog.scene_menu.json), plus MenuOps: one small function per primitive the scenes use.
// Rendering, text, blitter, palette, input reading and the timing waits are the typed calls of rt/prim.hpp (ROADMAP 11.1d;
// until then each went through a register-block call helper into the primitive's 68k entry shim).
//
// Variables: the menu state (player count, cursor, gore flag, damage divisor) is copied in before the scene and out
// afterwards.  Nothing else reads them while a scene runs: LAB_06DB/LAB_05D4..LAB_05DA/LAB_06F7..LAB_0703 are used by
// these two scenes only, and LAB_06DC/LAB_06DA/LAB_06DE/LAB_05C5 are read by the main loop after the menu returns.

#include <stdint.h>

#include "game/api/data.hpp"
#include "game/net_knights.hpp"
#include "game/net_find.hpp"
#include "game/net_menu.hpp"
#include "game/options_menu.hpp"
#include "game/party.hpp"
#include "game/scene_lobby.hpp"
#include "game/scene_menu.hpp"
#include "game/state_bind.hpp"
#include "game/version_text.hpp"
#include "engine/nettick.hpp"
#include "engine/util.hpp"
#include "rt/gtext.hpp"
#include "rt/combat_load.hpp"
#include "rt/display_ops.hpp"
#include "rt/abs.h"
#include "rt/file_handle.hpp"
#include "rt/files.hpp"
#include "rt/net_box.hpp"
#include "rt/net_session.hpp"
#include "rt/net_state.hpp"
#include "rt/lobby_draw.hpp"
#include "rt/logwrite.hpp"
#include "rt/mog_display.hpp"
#include "rt/net_stub.hpp"
#include "rt/options_io.hpp"
#include "rt/prim.hpp"

// Labels not bound in state_bind.hpp (ROADMAP 5.1 header is not extended for this task).
extern "C" uint32_t mogDrawScreen;       // draw screen base (LAB_0426+2 / LAB_0D72 argument) (LAB_0D92)
extern "C" uint32_t mogBackground;       // background picture buffer (LAB_05C0)
extern "C" uint32_t mogForeground;       // sprite sheet (LAB_05C1)
extern "C" uint32_t mogSceneSheet;       // sprite sheet the scenes draw from (:= LAB_05C1 by both scenes) (LAB_05C4)
extern "C" uint32_t mogSmallFont[5];    // 16(LAB_05E3) = the long right before LAB_05E4
extern "C" uint32_t mogSelectJob;       // job handle of the knight-select animation (LAB_05A5)
extern "C" uint16_t mogTextFlag;       // text flag (LAB_0D05)
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

inline uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }

// ---- MenuOps ---------------------------------------------------------------------------------------------

void opIrqOn() { rt_irq_enable(); }                     // LAB_0D7C: INTENA := $C000, no inputs
void opClearHits() { rtDrawBufClear(); }
void opFlip() { rt::displayFlip(); }
void opWait(uint32_t ulTicks) { rt::displayWaitFrames(ulTicks); }
void opSetPlanes() { rt::setDrawTarget(mogDrawScreen); }
void opBlitBackground() { rtMogBlitScreenC(mogBackground, mogDrawScreen); }
void opBlitBackgroundBoth() { rtMogBlitBothC(); }
void opClearBuffer(MenuBuffer eBuf) {                    // A0 = buffer; LAB_0D72 saves every register
	rt::displayClearScreen(reinterpret_cast<void *>((uintptr_t)(eBuf == MENUBUF_BACKGROUND ? mogBackground : mogDrawScreen)));   // LAB_0D72
}
void opSetTextFlag(uint16_t uwValue) { mogTextFlag = uwValue; }
void opDrawSprite(MenuBank eBank, uint16_t uwIndex, uint16_t uwX, uint16_t uwY) {   // frame, x, y; the cel table is the sprite sheet
	uint32_t ulBank = eBank == MENUBANK_BACKDROP ? mogActive.ulSpriteBank : mogSceneSheet;  // LAB_05E4
	rtDrawCel_mog(uwIndex, uwX, uwY, reinterpret_cast<const uint8_t *>(ulBank));
}
void opDrawNumber(uint16_t uwValue) {                    // 0..999 into the 4-byte field of the Players item (LAB_0442)
	ms::formatNumber3(uwValue, reinterpret_cast<char *>(mogMenuPlayersValue));
}
// The text lists of the title menu and the main menu: copies of LAB_06AE in RAM (record = text pointer, x, y, style word, next pointer: 14 bytes)
// with the rows where menuItemY puts them.  The original data stays untouched.
struct TextRec {
	uint32_t ulText;
	uint16_t uwX, uwY, uwStyle;
	uint32_t ulNext;
} __attribute__((packed, aligned(2)));   // 14 bytes on every target (a 32-bit PC aligns the second long to 4 otherwise); aligned(2) keeps the m68k code as before
static_assert(sizeof(TextRec) == 14, "LAB_06AE record size");
const uint8_t kBackText[] = "Back";
const uint8_t kLocalText[] = "Local";
const uint8_t kMultiplayerText[] = "Multiplayer";
const uint8_t kSettingsText[] = "Settings";
constexpr uint16_t LABEL_DY = 2;       // the centred rows' text sits 2 lines below the cursor's y; Players (and its number) 2 lines above
TextRec s_aTitleList[5];
TextRec s_aMainList[MAIN_ITEMS];

// The title menu list: Players with its number, Practice, Select Knight and the Back row.  LAB_06AE holds Players, Gore, Practice, Select Knight, the
// number and the On / Off text; the Gore label and value are left out and the rest follows menuItemY.  Back is a centred text like Practice.
void buildTitleList(bool bBack) {
	const TextRec *pSrc = reinterpret_cast<const TextRec *>(mogMenuItemsMain);
	uint8_t n = 0;
	s_aTitleList[n] = pSrc[0];
	s_aTitleList[n++].uwY = (uint16_t)(menuItemY(ITEM_PLAYERS) - LABEL_DY);
	s_aTitleList[n] = pSrc[2];
	s_aTitleList[n++].uwY = (uint16_t)(menuItemY(ITEM_PRACTICE) + LABEL_DY);
	s_aTitleList[n] = pSrc[3];
	s_aTitleList[n++].uwY = (uint16_t)(menuItemY(ITEM_SELECT) + LABEL_DY);
	if(bBack) {
		s_aTitleList[n] = pSrc[3];
		s_aTitleList[n].ulText = addr(kBackText);
		s_aTitleList[n++].uwY = (uint16_t)(menuItemY(ITEM_BACK) + LABEL_DY);
	}
	s_aTitleList[n] = pSrc[4];                                                  // the number, on the Players line
	s_aTitleList[n++].uwY = s_aTitleList[0].uwY;
	for(uint8_t i = 0; i + 1 < n; ++i) s_aTitleList[i].ulNext = addr(&s_aTitleList[i + 1]);
	s_aTitleList[n - 1].ulNext = 0;
}

// The main menu list: three centred rows in the big font, the style of Practice.
void buildMainList() {
	const TextRec *pSrc = reinterpret_cast<const TextRec *>(mogMenuItemsMain);
	const uint8_t *const apText[MAIN_ITEMS] = {kLocalText, kMultiplayerText, kSettingsText};
	for(uint8_t i = 0; i < MAIN_ITEMS; ++i) {
		s_aMainList[i] = pSrc[3];
		s_aMainList[i].ulText = addr(apText[i]);
		s_aMainList[i].uwY = (uint16_t)(mainMenuItemY(i) + LABEL_DY);
		s_aMainList[i].ulNext = i + 1 < MAIN_ITEMS ? addr(&s_aMainList[i + 1]) : 0;
	}
}

// The game version (V<major>.<minor>) in the bottom-left corner of the title menu (owner 2026-10-10, no original).  The game's only font in the menu
// is the big one (the small combat font is not loaded here), colours are the glyphs' own, so no palette entry changes.  Drawn from
// opDrawText, which runs once per title-menu redraw into the buffer that the following flip shows, so cursor moves, Players / Gore changes
// and the return from Multiplayer or Practice all keep it.  x = 3 stays left of the cursor arrow (x 50), y = 182 is the last-status-line row
// of the Multiplayer screen (glyph row ends at line 199).
constexpr uint16_t VERSION_X = 3;
constexpr uint16_t VERSION_Y = 182;
void drawVersion() {
	static char acVersion[ms::game::VERSION_TEXT_MAX];
	ms::game::versionText(acVersion);
	rt::mogDrawString(reinterpret_cast<const uint8_t *>(acVersion), VERSION_X, VERSION_Y, 0);
}

void opDrawText(MenuText eText) {                        // LAB_0432: the item list
	if(eText == MENUTEXT_SELECT) {
		rt::mogDrawList(mogTextSelectKnight);
		return;
	}
	if(eText == MENUTEXT_MAIN_MENU) buildMainList();
	else buildTitleList(eText == MENUTEXT_MAIN_BACK);
	rt::mogDrawList(eText == MENUTEXT_MAIN_MENU ? s_aMainList : s_aTitleList);
	drawVersion();
}
void opDrawString(const uint8_t *pText, uint16_t uwX, uint16_t uwY, uint16_t uwStyle) {   // LAB_0431
	rt::mogDrawString(pText, uwX, uwY, uwStyle);
}
void opPalette(MenuPalette ePal) {                       // LAB_03F2: fade to the 32-word palette
	rtMogFadeToC(reinterpret_cast<const uint16_t *>(ePal == MENUPAL_TITLE ? mogPicPalette : mogMenuPalette));
}
void opFadeOut() { rtMogFadeOutC(); }
uint16_t opReadJoy() {                                   // LAB_00EE: port 1 is the low word (LAB_0630; port 0 = LAB_062F is not used by the scenes)
	ms::tickIdle();                                        // deterministic mode (ROADMAP 12.4): these loops poll without a beam wait; a frame boundary per poll
	return (uint16_t)rtMogJoyReadC();
}
uint16_t opKeyWord() { return mogKeyAny; }
void opClearKeyWord() { mogKeyAny = 0; }
void opKeyReset() { rtMogKeyResetC(); }
uint8_t opTranslateKey(uint16_t uwCode) {                // LAB_0D8D: the code (word) in, the game key in the low byte out
	return (uint8_t)rtMogKeyXlatC(uwCode);
}
void opErrorFlash() { rt::displayErrorFlash(); }
void opPublishPlayerCount(uint16_t uwCount) { mogActive.uwPlayerCount = uwCount; }
uint32_t opSpawnJob(uint16_t uwD0, uint16_t uwD1, uint16_t uwD2, uint16_t uwD3) {   // D0-D3 args, D0 = job record
	uint32_t ulJob = rtPaletteRampAdd(1, uwD0, uwD1, uwD2, uwD3);
	if(ulJob == 0) ulJob = uwD0;                           // rt_palette_ramp_add_mog gave the first argument back when every slot was busy
	mogSelectJob = ulJob;
	return ulJob;
}
void opKillJob(uint32_t ulJob) { *(uint32_t *)(uintptr_t)ulJob = 0; }
// Options screen: the font cel table in ActiveKnights +10 (LAB_05E4) that mogDrawString prints from and MENUBANK_BACKDROP draws sprites from.
// LAB_05E3[4] = bold.f, LAB_05E3[0] = Small.font; the boot (LAB_012C) loads both and they stay resident, so this costs no memory.  Should the
// small table be missing the big font stays (the rows then print big and cut, but nothing crashes).
void opUseFont(MenuFont eFont) {
	const uint32_t ulSmall = mogSmallFont[0];
	mogActive.ulSpriteBank = (eFont == MENUFONT_SMALL && ulSmall != 0) ? ulSmall : mogSmallFont[4];
}

void opRestoreBackground() { rtClTitlePicture(); }      // ch.piv decoded again (LAB_05C2 is the arenas' scratch, not the title picture)

const MenuOps g_ops = {
	opIrqOn, opClearHits, opFlip, opWait, opSetPlanes, opBlitBackground, opBlitBackgroundBoth, opClearBuffer,
	opSetTextFlag, opDrawSprite, opDrawNumber, opDrawText, opDrawString, opPalette, opFadeOut,
	opReadJoy, opKeyWord, opClearKeyWord, opKeyReset, opTranslateKey, opErrorFlash, opPublishPlayerCount,
	opSpawnJob, opKillJob, opUseFont, opRestoreBackground,
};

// The lobby's extra primitives (game/scene_lobby.hpp): the name box, the greyed START, the small font and its width.
const LobbyOps g_lobbyOps = {rt::lobbyBar, rt::lobbyRecolour, rt::lobbySmallString, rt::lobbySmallWidth};

#if !defined(MS_COOP)
#define MS_COOP 0
#endif
bool s_bMenuCoop;   // the menu's co-op choice, kept across games (g_party itself is classic while the title menu runs)
uint16_t s_uwMainCursor;   // the main menu's row, kept across the screens it opens (zero = LOCAL)

// ---- Multiplayer screen (ROADMAP 12.8a) ----------------------------------------------------------------------

const char kNetIni[] = "PROGDIR:net.ini";
constexpr uint32_t NET_INI_MAX = 1024;       // net.ini is read whole; longer files are ignored (the defaults stay)
char s_acIniOld[NET_INI_MAX];
char s_acIniNew[NET_INI_MAX + 160];

// The command line (NAME, JOIN <address>, PORT) wins over net.ini: it pre-fills the screen's fields.
void applyCliArgs(NetConfig &cfg) {
	const rt::NetArgs &a = rt::netSessionArgs();
	auto copy = [](char *szDst, uint32_t ulMax, const char *szSrc) {
		uint32_t i = 0;
		while(szSrc[i] && i + 1 < ulMax) { szDst[i] = szSrc[i]; ++i; }
		szDst[i] = 0;
	};
	if(a.szName[0]) copy(cfg.szName, sizeof cfg.szName, a.szName);
	if(a.isJoin) copy(cfg.szAddr, sizeof cfg.szAddr, a.szJoin);
	if(a.uwPort != rt::NET_DEFAULT_PORT) {
		char sz[8];
		uint32_t n = 0;
		uint32_t v = a.uwPort;
		for(uint32_t d = 10000; d > 0; d /= 10) {
			const uint32_t digit = v / d;
			v -= digit * d;
			if(digit != 0 || n != 0 || d == 1) sz[n++] = (char)('0' + digit);
		}
		sz[n] = 0;
		copy(cfg.szPort, sizeof cfg.szPort, sz);
	}
}

// Name, address and port from PROGDIR:net.ini; missing file / keys keep the defaults.
void netConfigRead(NetConfig &cfg) {
	netConfigDefaults(cfg);
	rt::FileHandle sFile(kNetIni, rt::FileHandle::Exact());
	if(!sFile.isOpen()) { applyCliArgs(cfg); return; }
	const uint32_t ulSize = sFile.size();
	if(ulSize == 0 || ulSize > NET_INI_MAX) { applyCliArgs(cfg); return; }
	sFile.read(s_acIniOld, ulSize);
	sFile.close();
	netConfigLoad(cfg, s_acIniOld, ulSize);
	if(cfg.szName[0] == 0) netConfigDefaults(cfg);               // an empty name in the file: back to the defaults
	applyCliArgs(cfg);
	rt::netStatsLineEnable(cfg.bStats);                          // `stats = no`: no ping / loss line in a network game
	rt::netSessionAllowUdp(cfg.bUdp);                            // `udp = no`: the per-tick traffic stays on TCP (the command line's TCP does the same)
}

// Writes the three fields back, keeping every other line of the file.  A read-only volume is not an error.
void netConfigWrite(const NetConfig &cfg) {
	uint32_t ulOld = 0;
	{
		rt::FileHandle sFile(kNetIni, rt::FileHandle::Exact());
		if(sFile.isOpen() && sFile.size() <= NET_INI_MAX) {
			ulOld = sFile.size();
			sFile.read(s_acIniOld, ulOld);
		}
	}
	const uint32_t ulNew = netConfigSave(cfg, s_acIniOld, ulOld, s_acIniNew, sizeof s_acIniNew);
	if(ulNew == 0) return;
	if(rt_file_write_exact(kNetIni, s_acIniNew, ulNew)) logWrite("net: saved net.ini (%lu bytes)\n", (unsigned long)ulNew);
	else logWrite("net: net.ini not written (volume refuses)\n");
}

// The lobby = the Select a Knight screen (docs/NETPLAY.md 4.2b), entered from the Multiplayer screen with the link open (HOST GAME is listening,
// or the host accepted our connect).  Not in lockstep: picks travel as lobby messages.  Returns how it ended; a left / lost lobby has closed the link.
LobbyResult runLobbyScreen() {
	mogActive.ulSpriteBank = mogSmallFont[4];                  // the same banks as the title menu / the knight screen
	mogSceneSheet = mogForeground;
	const LobbySeam &seam = rt::lobbySeam();
	logWrite("net: lobby open\n");                           // the barrier of the netpair scripts (`wait log lobby_open`)
	SceneLobby sc;
	sc.enter(g_ops, g_lobbyOps, seam);
	while (!sc.update(g_ops, g_lobbyOps, seam)) {
	}
	sc.exit(g_ops, seam);
	logWrite("net: lobby left (%s)\n", sc.eResult == LOBBY_STARTED ? "start" : (sc.eResult == LOBBY_LOST ? "connection lost" : "back"));
	return sc.eResult;
}

// The game browser (FIND GAME, ROADMAP 12.18): the open games of the public lobby; fire on one joins it, directly or through the lobby's relay.  Returns
// NET_LOBBY_CLIENT when a host seated us (on to the lobby), NET_LEAVE for back to the Multiplayer screen.
NetResult runFindScreen(const NetConfig &cfg) {
	mogActive.ulSpriteBank = mogSmallFont[4];                  // the same banks as the title menu
	mogSceneSheet = mogForeground;
	const NetSeam &net = rt::netSeam();
	const FindSeam &find = rt::findSeam();
	if (net.pfnHostOptions) net.pfnHostOptions(cfg.bPublic, cfg.szLobby, cfg.uwLobbyPort);   // the lobby address of net.ini (the options the host start also takes)
	logWrite("net: find screen open\n");                       // the barrier of the netpair scripts (`wait log find_screen_open`)
	SceneFind sc;
	sc.enter(g_ops, g_lobbyOps, cfg.szName, find);
	while (!sc.update(g_ops, g_lobbyOps, find, net)) {
	}
	sc.exit(g_ops, find, net);
	logWrite("net: find screen left (%s)\n", sc.eResult == NET_LOBBY_CLIENT ? "lobby as client" : "back");
	return sc.eResult;
}

// The Multiplayer screen on top of the title picture, then the lobby.  Returns true when the lobby started a network game (the lockstep runs; the
// caller leaves the title menu for the game), false in every other case (back at the title menu).
bool runNetScreen() {
	if (rt::netSessionRunning()) {                             // the title menu of a running lockstep game: a second HOST / JOIN would tear the session down
		logWrite("net: multiplayer screen ignored, a session is running\n");
		return false;
	}
	NetRow eStart = NETROW_HOST;                               // the game browser returns to its own row
	for (;;) {
		NetConfig cfg;
		netConfigRead(cfg);
		mogActive.ulSpriteBank = mogSmallFont[4];              // the same banks as the title menu
		mogSceneSheet = mogForeground;
		const NetSeam &seam = rt::netSeam();
		logWrite("net: screen open\n");                      // the barrier of the netpair scripts (`wait log screen_open`)
		SceneNet sc;
		sc.enter(g_ops, cfg, eStart);
		eStart = NETROW_HOST;
		while (!sc.update(g_ops, seam)) {
		}
		sc.exit(g_ops, seam);
		if (sc.menu.bDirty) netConfigWrite(sc.menu.cfg);
		logWrite("net: screen left (%s)\n", sc.eResult == NET_LOBBY_HOST ? "lobby as host" : (sc.eResult == NET_LOBBY_CLIENT ? "lobby as client" : (sc.eResult == NET_FIND ? "game browser" : (sc.eResult == NET_REJOIN ? "rejoin" : "back"))));
		NetResult eWhere = sc.eResult;
		if (eWhere == NET_FIND) {                              // FIND GAME: the list of open games; a join ends in the same lobby as a typed address
			eStart = NETROW_FIND;
			eWhere = runFindScreen(sc.menu.cfg);
			if (eWhere != NET_LOBBY_CLIENT && eWhere != NET_REJOIN) continue;   // back from the list: the Multiplayer screen again

		}
		if (eWhere == NET_REJOIN) {                            // REJOIN: the running game's world is here: on to the game, no lobby (ROADMAP M12, docs/NETPLAY.md 4.5)
			if (rt::netSessionBeginRejoin()) return true;
			continue;
		}
		if (eWhere != NET_LOBBY_HOST && eWhere != NET_LOBBY_CLIENT) return false;
		const LobbyResult eLobby = runLobbyScreen();

		if (eLobby == LOBBY_STARTED) {
			if (rt::netSessionBegin()) return true;            // all nodes switch the deterministic mode on at tick 0 (ROADMAP 12.8b)
			logWrite("net: no session, playing locally\n");
			return false;
		}
		// the player left the lobby (or lost the connection): the Multiplayer screen again
	}
}

// ---- Options screen (ROADMAP 9.9) ----------------------------------------------------------------------------------------

SceneOptions s_sceneOptions;   // about 7 KB of pack rows: static, zero-initialised (no constructor runs)

// The Options screen on top of the title picture; returns to the title menu.  Read-only inside a network session.  The choices are saved and
// the live data rebuilt after the screen has faded out (the next game uses them; the title menu is the only place this runs).
void runOptionsScreen() {
	const bool bReadOnly = rt::netSessionRunning();
	const OptionsSeam &seam = rt::optionsSeam();
	mogActive.ulSpriteBank = mogSmallFont[4];                  // the same banks as the title menu
	mogSceneSheet = mogForeground;
	s_sceneOptions.menu.init(bReadOnly);
	seam.pfnLoad(s_sceneOptions.menu);
	logWrite("options: screen open (%u packs%s)\n", (unsigned)s_sceneOptions.menu.ubPacks, bReadOnly ? ", read only" : "");
	s_sceneOptions.enter(g_ops);
	while (!s_sceneOptions.update(g_ops)) {
	}
	s_sceneOptions.exit(g_ops);
	if (s_sceneOptions.menu.bChanged && !bReadOnly) {
		seam.pfnSave(s_sceneOptions.menu);
		logWrite("options: screen left, saved\n");
	} else {
		logWrite("options: screen left\n");
	}
}

}  // namespace

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
namespace rt {
volatile uint8_t g_ubMenuAction;   // `poke menuact N`: the main menu / the title menu takes that row as if fire were pressed on it (docs/AUTOPLAY.md)
bool menuActionPoke(ULONG ulValue) {
	if(ulValue > MENUACT_PRACTICE) return false;
	g_ubMenuAction = static_cast<uint8_t>(ulValue);
	return true;
}
}  // namespace rt
#endif

namespace {

// ---- the screens of the title ---------------------------------------------------------------------------------

// The press that took a row must not take the next screen's row as well (a cursor can stand on Practice from the last game).
void waitFireReleased() {
	while(opReadJoy() & 0x10) {
	}
}

// The main menu: returns the row taken.  bFaded: the screen before faded out, so the picture fades in (in: whether to fade in, out: whether this one faded out).
MainAction runMainMenu(SceneMainMenu &sMain, bool &bFaded) {
	sMain.enter(g_ops, bFaded);
	while(!sMain.update(g_ops)) {
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
		const uint8_t ubAct = rt::g_ubMenuAction;              // autoplay `poke menuact N` (no cursor counting in the scripts)
		if(ubAct) {
			if(ubAct == rt::MENUACT_MULTIPLAYER || ubAct == rt::MENUACT_SETTINGS) rt::g_ubMenuAction = 0;
			sMain.eResult = ubAct == rt::MENUACT_MULTIPLAYER ? MAINACT_MULTIPLAYER : (ubAct == rt::MENUACT_SETTINGS ? MAINACT_SETTINGS : MAINACT_LOCAL);
			break;                                             // Select Knight / Practice stay poked: the title menu takes them next
		}
#endif
	}
	bFaded = sMain.exit(g_ops);
	return sMain.eResult;
}

// The title menu (Players / Mode / Practice / Select Knight).  The result is in m.eResult; MENU_BACK only with m.state.bBack.
void runTitleMenu(SceneMenu &m, bool &bFaded) {
	m.enter(g_ops, bFaded);
	while(!m.update(g_ops)) {
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
		const uint8_t ubAct = rt::g_ubMenuAction;
		if(ubAct == rt::MENUACT_SELECT || ubAct == rt::MENUACT_PRACTICE) {
			rt::g_ubMenuAction = 0;
			const bool bSelect = ubAct == rt::MENUACT_SELECT;
			m.state.uwCursor = bSelect ? ITEM_SELECT : ITEM_PRACTICE;
			m.eResult = bSelect ? MENU_SELECT : MENU_PRACTICE;
			break;
		}
		if(ubAct && m.state.bBack) {                           // Multiplayer / Settings belong to the main menu: back to it, the poke stays
			m.eResult = MENU_BACK;
			break;
		}
		if(ubAct) rt::g_ubMenuAction = 0;                      // a running network game has no such rows
#endif
	}
	bFaded = m.exit(g_ops);
}

}  // namespace

// ---- scene entries -----------------------------------------------------------------------------------------

// The main menu is the first screen (owner 2026-10-11): LOCAL opens the title menu, MULTIPLAYER the network screen, SETTINGS the Options screen;
// the title menu goes back to it with Back / Esc and the other two return to it.  A running network game has no main menu: its replicated
// post-game menu is the title menu alone (no Back row, nothing to leave for that would break the lockstep).
extern "C" __attribute__((used, externally_visible)) void rtSceneMenuRun() {
	rt::NetMenuScope sNetMenu;                                 // the merge treats these ticks as the host's menu (ROADMAP 12.8b)
	mogGore = MENU_GORE_ON;                                    // gore is always on (owner 2026-10-11); the cell stays what combat and the HELLO rules read
	SceneMenu m;
	m.eResult = MENU_IDLE;
	m.state.uwPlayers = mogHumanPlayers.uw;  // LAB_05C5
	m.state.uwCursor = mogMenuCursor;
	m.state.uwMoved = mogMenuMoved;
	m.state.uwDamageDiv = mogDamageDiv;
	m.state.pStatCost = ms::game::g_gameData.temple.auwStatCost;   // [temple] stat_cost (ROADMAP 9.5b)
	ms::game::partyReset(ms::game::g_party);                   // co-op (ROADMAP 8.2): classic input while the menu runs
	m.state.bCoopAllowed = MS_COOP != 0;
	m.state.bCoop = m.state.bCoopAllowed && s_bMenuCoop;
	m.state.bBack = !rt::netSessionRunning();
	mogActive.ulSpriteBank = mogSmallFont[4];                  // MOVE.L 16(A1),10(A0) with A1 = LAB_05E3
	mogSceneSheet = mogForeground;                             // MOVE.L LAB_05C1,LAB_05C4
	bool bFaded = true;                                        // the picture fades in at the first draw, and after every screen that faded out
	if(!m.state.bBack) {
		runTitleMenu(m, bFaded);
	} else {
		SceneMainMenu sMain;
		sMain.state.uwCursor = s_uwMainCursor;
		for(;;) {
			waitFireReleased();                                // the press that left the last screen must not take a row of this one
			const MainAction eAct = runMainMenu(sMain, bFaded);
			s_uwMainCursor = sMain.state.uwCursor;
			if(eAct == MAINACT_SETTINGS) {
				runOptionsScreen();                            // the mods screen, then the main menu again
				bFaded = true;
				continue;
			}
			if(eAct == MAINACT_MULTIPLAYER) {
				if(runNetScreen()) {                           // the screen and the lobby; a started network game goes straight on:
					m.state.uwCursor = ITEM_SELECT;            // the main flow takes "Select Knight" (Campaign); rtSceneKnightsRun builds the knights from
					break;                                     // the CONFIG instead of showing the screen.  No title menu, no knight screen in a network game.
				}
				bFaded = true;
				continue;
			}
			waitFireReleased();
			runTitleMenu(m, bFaded);
			if(m.eResult != MENU_BACK) break;
			m.state.uwCursor = ITEM_PLAYERS;                   // the cursor of a Back is not worth keeping
		}
	}
	mogHumanPlayers.uw = m.state.uwPlayers;
	mogMenuCursor = m.state.uwCursor;
	mogMenuMoved = m.state.uwMoved;
	mogDamageDiv = m.state.uwDamageDiv;
	s_bMenuCoop = m.state.bCoop;
	if(m.eResult == MENU_SELECT && m.state.bCoop) {           // a co-op campaign: the knight screen binds the controllers
		ms::game::partyStart(ms::game::g_party, (uint8_t)m.state.uwPlayers);
		logWrite("party: co-op, %u knights\n", (unsigned)ms::game::g_party.n);
	}
}

// A network game (the lobby just started it): the knights are the players, built from the CONFIG on every machine (game/net_knights.hpp) - the same
// fields the knight screen and the name entry set.  The player count, the damage divisor and the published count follow, as after the title menu.
void startNetGame(const ms::NetConfig &cfg) {
	NetKnightsEnv env;
	env.aKnights = mogKnights;
	env.apNames[0] = mogKnightName2;                           // portrait 0 = Sir Godber (as rtSceneKnightsRun's env)
	env.apNames[1] = mogKnightName1;                           // 1 = Sir Richard
	env.apNames[2] = mogKnightName3;                           // 2 = Sir Jeffrey
	env.apNames[3] = mogKnightName4;                           // 3 = Sir Edward
	env.pParty = &ms::game::g_party;
	const uint8_t ubKnights = netKnightsApply(cfg, env);
	MenuState sMenu;
	sMenu.uwPlayers = ubKnights;
	sMenu.pStatCost = ms::game::g_gameData.temple.auwStatCost;
	menuSetPlayers(sMenu, 0);                                  // the damage divisor for this many players (LAB_06DE)
	mogHumanPlayers.uw = ubKnights;
	mogDamageDiv = sMenu.uwDamageDiv;
	opPublishPlayerCount(ubKnights);
	logWrite("net: game starts, %s, %u knights\n", cfg.ubMode == ms::NET_MODE_COOP ? "co-op" : "classic", (unsigned)ubKnights);
	for (uint8_t i = 0; i < ubKnights; ++i) {
		logWrite("net: knight %u kind %lu name %s\n", (unsigned)i, (unsigned long)mogKnights[i].ulKind, cfg.acNames[i]);
	}
	if (ms::game::g_party.active) {
		for (uint8_t i = 0; i < ms::game::g_party.n; ++i) {
			logWrite("party: knight %u kind %lu controller %u %s\n", (unsigned)i, (unsigned long)mogKnights[i].ulKind,
			         (unsigned)ms::game::g_party.aubPad[i], ms::padName(ms::game::g_party.aubPad[i]));
		}
	}
}

extern "C" __attribute__((used, externally_visible)) void rtSceneKnightsRun() {
	ms::NetConfig sNetCfg;
	if (rt::netSessionGameConfig(sNetCfg)) {                   // the lobby started a network game: no knight screen
		startNetGame(sNetCfg);
		return;
	}
	rt::NetMenuScope sNetMenu;
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
