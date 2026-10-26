// game/scene_menu - the title menu and the knight-select scene of mog.asm in C++ (ROADMAP 6.2).
//
//   SceneMenu    mog.asm 1669-1818 (the code that follows `JMP SECSTRT_9` in LAB_00B4, hunk9.cpp lands there):
//                menu cursor LAB_00BA, option toggle LAB_00C2, player count LAB_00C6, redraw LAB_00C4 and the input
//                loop LAB_00B5.  Items: Players, Practice, Select Knight (the Gore item of the original is gone, owner 2026-10-11:
//                gore is always on).
//   SceneMainMenu (no original, owner 2026-10-11) LOCAL / MULTIPLAYER / SETTINGS in front of SceneMenu; LOCAL opens the title menu above.
//   SceneKnights mog.asm 1905-2048 (LAB_00D3, called by the main loop after the menu): portrait selection LAB_00D4,
//                free-slot search LAB_00DD, redraw LAB_00E0, per-knight setup LAB_00E5 and the name editor
//                LAB_00C9..LAB_00D2.
//
// Pure on purpose: no ACE, no hardware, no game symbols.  The scene state is a struct of its own, every effect on the
// machine goes through MenuOps (one entry per asm primitive, with the call order and the waits of the original), so
// tests/test_scene_menu.py can run the same input script through the C++ and through a Python model of the asm and
// compare the call traces.  src/rt/scene_menu.cpp implements MenuOps by calling the asm primitives (register
// contracts are documented there) and copies the game variables in and out.
//
// Frame pacing is exactly the original's: the input loops spin on LAB_00EE without a wait; the only waits are
// wait(10) after a menu redraw, wait(6) after a knight-select redraw (not while a name is being typed), wait(4) before
// the name editor, wait(2) for the "name too long" flash and the 36-tick palette fade inside palette()/fadeOut().
#pragma once
#include "engine/net_cfg.hpp"
#include <stdint.h>

#include "game/knight.hpp"
#include "game/party.hpp"

namespace ms { namespace game {

// Game data the asm owns, named instead of addressed (the shim maps them to labels).
enum MenuPalette : uint8_t { MENUPAL_TITLE = 0, MENUPAL_KNIGHTS };    // LAB_0D2B, LAB_06FD
// The title list (Players / Practice / Select Knight) is built by the shim from LAB_06AE without the Gore lines, rows at menuItemY; the Mode row's
// line is drawn with pfnDrawString, _BACK adds the Back row.  MENUTEXT_MAIN_MENU is the three rows of the main menu (LOCAL / MULTIPLAYER / SETTINGS).
enum MenuText : uint8_t { MENUTEXT_MAIN = 0, MENUTEXT_SELECT, MENUTEXT_MAIN_BACK, MENUTEXT_MAIN_MENU };
enum MenuBank : uint8_t { MENUBANK_BACKDROP = 0, MENUBANK_SPRITES };  // ActiveKnights +10 (= LAB_05E3+16), LAB_05C4
enum MenuFont : uint8_t { MENUFONT_BIG = 0, MENUFONT_SMALL };          // the font cel table text is printed in (ActiveKnights +10): bold.f / Small.font
enum MenuBuffer : uint8_t { MENUBUF_BACKGROUND = 0, MENUBUF_SCREEN }; // the buffers LAB_05C0 / LAB_0D92 point to

// One entry per asm primitive.  All are called with the exact argument values the original loads into registers.
struct MenuOps {
	void (*pfnIrqOn)();                                   // LAB_0D7C (INTENA master on)
	void (*pfnClearHits)();                               // LAB_03A7 ($FF fill of LAB_064D..)
	void (*pfnFlip)();                                    // LAB_0416 (show the drawn screen, swap)
	void (*pfnWait)(uint32_t ulTicks);                    // LAB_0D74
	void (*pfnSetPlanes)();                               // MOVE.L LAB_0D92,D0 ; JSR LAB_0426+2 (blitter plane pointers)
	void (*pfnBlitBackground)();                          // LAB_0419 LAB_05C0 -> LAB_0D92
	void (*pfnBlitBackgroundBoth)();                      // LAB_0418 LAB_05C0 -> SECSTRT_35 and LAB_0D92
	void (*pfnClearBuffer)(MenuBuffer eBuf);              // LAB_0D72 with A0 = the buffer
	void (*pfnSetTextFlag)(uint16_t uwValue);             // MOVE.W #n,LAB_0D05
	void (*pfnDrawSprite)(MenuBank eBank, uint16_t uwIndex, uint16_t uwX, uint16_t uwY);  // LAB_0CDA
	void (*pfnDrawNumber)(uint16_t uwValue);              // LAB_0442 into the LAB_06B9 text buffer
	void (*pfnDrawText)(MenuText eText);                  // LAB_0432
	void (*pfnDrawString)(const uint8_t *pText, uint16_t uwX, uint16_t uwY, uint16_t uwStyle);  // LAB_0431
	void (*pfnPalette)(MenuPalette ePal);                 // LAB_03F2 (palette + 36 tick fade)
	void (*pfnFadeOut)();                                 // LAB_03F0
	uint16_t (*pfnReadJoy)();                             // LAB_00EE, returns D1 (= LAB_0630: bit0 right, 1 left, 2 down, 3 up, 4 fire)
	uint16_t (*pfnKeyWord)();                             // SECSTRT_21 (last key code, 0 = none)
	void (*pfnClearKeyWord)();                            // MOVE.W #0,SECSTRT_21
	void (*pfnKeyReset)();                                // LAB_0B82 (key table + SECSTRT_21 cleared)
	uint8_t (*pfnTranslateKey)(uint16_t uwCode);          // LAB_0D8D (0 = no printable character)
	void (*pfnErrorFlash)();                              // LAB_0D77 ; COLOR00 = $0C00 ; wait 2 ; COLOR00 = 0
	void (*pfnPublishPlayerCount)(uint16_t uwCount);      // ActiveKnights +14 := count (LAB_00B9)
	uint32_t (*pfnSpawnJob)(uint16_t uwD0, uint16_t uwD1, uint16_t uwD2, uint16_t uwD3);  // LAB_0E5A, result into LAB_05A5
	void (*pfnKillJob)(uint32_t ulJob);                   // MOVEA.L LAB_05A5,A0 ; CLR.L (A0)
	// Options screen (ROADMAP 9.9), no original: switches the font text and the backdrop sprites are drawn from (the banner is a cel of the
	// big font's table, so sprites need MENUFONT_BIG).  MENUFONT_SMALL = Small.font, resident since the boot loads (LAB_012C).  Not called
	// by the title menu, the knight select or the Multiplayer screen (nullptr is fine for them).
	void (*pfnUseFont)(MenuFont eFont);
	// No original: the title picture (ch.piv, from the raw copy kept since the boot) decoded again into the background buffer (LAB_05C0) and its
	// palette (LAB_0D2B).  The lobby draws the knight select over its own background and returns to the Multiplayer screen / main menu, which
	// blit the title picture from there.  nullptr for screens that never overwrite the background.
	void (*pfnRestoreBackground)();
};

// ---------------------------------------------------------------------------------------------------------
// Title menu

// The cursor's items in list order (LAB_06DC): the settings group Players and Mode, then (after an empty line the cursor skips) Practice and
// Select Knight, which keep the numbers 2 and 3 of the original; Mode takes the index of the original's Gore row.  ITEM_MODE only exists with
// bCoopAllowed, ITEM_BACK with bBack.
enum MenuItem : uint16_t { ITEM_PLAYERS = 0, ITEM_MODE = 1, ITEM_PRACTICE = 2, ITEM_SELECT = 3, ITEM_BACK = 4 };

// The Esc key's code in the key word (SECSTRT_21): leaves the title menu for the main menu (bBack).
constexpr uint16_t MENUKEY_ESC = 0x45;

// LAB_06DA, the gore cell: 0 = gore on (the cell's start value, shown as "On" by the original menu), non-zero = off (the original's menu toggled
// it; combat reads it as the `ifnogore` flag).  The title menu no longer offers the choice, the shim writes MENU_GORE_ON before the menu.
constexpr uint32_t MENU_GORE_ON = 0;

// The game variables the menu reads and writes (copied in and out by the shim; nothing else reads them while the
// menu runs).
struct MenuState {
	uint16_t uwPlayers;     // LAB_05C5 (.W) number of human players 1..4
	uint16_t uwCursor;      // LAB_06DC item under the cursor; the main loop tests it == ITEM_PRACTICE afterwards
	uint16_t uwMoved;       // LAB_06DB set to 1 when the cursor hit an end stop (never read)
	uint16_t uwDamageDiv;   // LAB_06DE 3,2,1,1 for 1..4 players (table SECSTRT_4); subtracted from a knight's gold-like word in LAB_0545
	// The four costs for 1..4 players ([temple] stat_cost of shops.ini, ROADMAP 9.5b); nullptr = the original 3,2,1,1.
	const uint16_t *pStatCost = nullptr;
	// Co-op (ROADMAP 8.2): the Mode row under Players (left / right / fire) selects Classic or Coop and never changes the count, except that co-op needs at
	// least two knights: entering it with one player makes two, and in co-op Players stays within 2..PARTY_COOP_MAX (uwPlayers = the party size).
	// bCoopAllowed = false (MS_COOP off, the host traces) keeps the original 1..4 and hides Mode.
	bool bCoopAllowed = false;
	bool bCoop = false;
	// Main menu (owner 2026-10-11, no original): the title menu is the LOCAL screen of the main menu.  bBack adds the Back row after the last item
	// and makes Esc leave to it (MENU_BACK).  false (host traces, a running network game where the replicated menu has nothing to go back to).
	bool bBack = false;
};

enum MenuAction : uint8_t {
	MENU_IDLE = 0,          // nothing to do, poll again
	MENU_REDRAW,            // something changed: LAB_00C4
	MENU_PRACTICE,          // fire on Practice: leave (LAB_00B8)
	MENU_SELECT,            // fire on Select Knight: leave (LAB_00B9)
	MENU_BACK               // fire on Back or Esc (bBack only): leave for the main menu
};

// LAB_00C6: players += delta, then 0 -> 1 / (signed) > 4 -> 4, and uwDamageDiv from the table.  In co-op the count is kept within
// 2..PARTY_COOP_MAX instead (also with delta 0: the way into co-op raises a single player to two).
void menuSetPlayers(MenuState &s, int16_t swDelta);
// LAB_00BA: cursor movement / player count / mode with the joystick word; returns true when a redraw is needed.
bool menuMove(MenuState &s, uint16_t uwJoy);
// One pass of the LAB_00B5 loop body for a joystick word (D1 of LAB_00EE).
MenuAction menuInput(MenuState &s, uint16_t uwJoy);
// y of the cursor of an item (LAB_06DD; LAB_05D9 in the asm): Players 66, Mode 86, an empty line, Practice 126, Select Knight 146, Back 166
// (20 lines apart, the picture has 200).
uint16_t menuItemY(uint16_t uwCursor);

// ---------------------------------------------------------------------------------------------------------
// Main menu (owner 2026-10-11, no original): LOCAL / MULTIPLAYER / SETTINGS in front of the title menu, on the same title picture.

enum MainItem : uint16_t { MAIN_LOCAL = 0, MAIN_MULTIPLAYER, MAIN_SETTINGS, MAIN_ITEMS };
// The rows shown.  MS_NET off (the stock Amiga build, docs/MEMORY.md) has no network play: the MULTIPLAYER row is not offered, the cursor
// is a row number (LOCAL = 0, SETTINGS = 1) and mainMenuItem() maps it to the MainItem (and so the MainAction) it stands for.
constexpr uint16_t MAIN_ROWS = MS_NET ? MAIN_ITEMS : MAIN_ITEMS - 1;
constexpr uint16_t mainMenuItem(uint16_t uwRow) { return (MS_NET || uwRow == 0) ? uwRow : static_cast<uint16_t>(MAIN_SETTINGS); }
struct MainMenuState {
	uint16_t uwCursor;      // the row (== the MainItem with MS_NET on)
};
enum MainAction : uint8_t {
	MAINACT_IDLE = 0,       // nothing to do, poll again
	MAINACT_REDRAW,         // the cursor moved
	MAINACT_LOCAL,          // fire on a row: leave for the title menu / the Multiplayer screen / the Options screen (these three follow MainItem)
	MAINACT_MULTIPLAYER,
	MAINACT_SETTINGS
};
// One pass of the loop body for a joystick word: up / down move the cursor (wrapping), fire takes the row.
MainAction mainMenuInput(MainMenuState &s, uint16_t uwJoy);
// y of the cursor of a row.
uint16_t mainMenuItemY(uint16_t uwCursor);

struct SceneMainMenu {
	MainMenuState state;
	MainAction eResult;     // the row taken once update() returned true

	// Interrupts on, first draw, palette (bFade: after a screen that faded out; from the title menu the title palette stands).
	void enter(const MenuOps &ops, bool bFade);
	// One pass of the loop.  Returns true when a row was taken.
	bool update(const MenuOps &ops);
	// Fades out unless the title menu follows (same picture); returns true when it did.
	bool exit(const MenuOps &ops);

private:
	void draw(const MenuOps &ops);
};

struct SceneMenu {
	MenuState state;
	MenuAction eResult;     // MENU_PRACTICE / MENU_SELECT / MENU_BACK once update() returned true

	// LAB_00B4+12 up to the first poll: interrupts on, hit list cleared, player count normalised, first draw,
	// palette (bFade false: no fade-in, coming from the main menu on the same picture).  `state` must be filled by the caller.
	void enter(const MenuOps &ops, bool bFade = true);
	// One pass of the LAB_00B5 loop (read the joystick, act).  Returns true when the menu is finished.
	bool update(const MenuOps &ops);
	// LAB_00B8 / LAB_00B9: publish the player count (Select Knight only) and fade out (not for Back); returns true when it faded.
	bool exit(const MenuOps &ops);

private:
	void draw(const MenuOps &ops);    // LAB_00C4
};

// ---------------------------------------------------------------------------------------------------------
// Knight select

// The environment of the scene: where the four knights' records and name buffers are.
struct KnightsEnv {
	uint16_t uwPlayers;         // LAB_05C5 (.W): how many knights get picked
	Knight *aKnights;           // LAB_0613 (stride $84): the record the next pick fills in is aKnights[n]
	uint8_t *apNames[4];        // name buffer (22 bytes, space padded) of the knight with portrait index 0..3: LAB_06B6, LAB_06B5, LAB_06B7, LAB_06B8
	// Co-op (ROADMAP 8.2): the active party (g_party): after each name a controller choice for that knight, written to
	// pParty->aubPad[slot].  nullptr or inactive = the original screen.
	PartyConfig *pParty = nullptr;
	// Network game (docs/NETPLAY.md 3.2): pParty->isNet.  Member i sits at node pParty->aubNode[i]; its Control line ("Player N  <pad>") is
	// steered by that node only (the screen asks the merge through pParty->ubFocus = i) and offers the pads of that node.
};

enum KnightsPhase : uint8_t {
	KP_SELECT = 0,              // LAB_00D4: joystick moves the portrait cursor, fire takes the knight
	KP_NAME_RELEASE,            // LAB_00C9 first loop: waiting for fire to be released
	KP_NAME_EDIT,               // LAB_00CC: typing
	KP_PAD_RELEASE,             // co-op: the name was taken with fire, wait for its release before the controller choice
	KP_PAD_CHOICE               // co-op: left / right picks this knight's controller, fire takes it
};

// Co-op controller line of the knight screen (below the portraits; the knight's name stays at the name entry's place).
constexpr uint16_t KNIGHTS_PAD_X = 0x32;
constexpr uint16_t KNIGHTS_PAD_Y = 0xA8;

struct SceneKnights {
	KnightsEnv env;
	uint16_t uwRemaining;       // LAB_06F8 knights still to pick
	uint8_t ubFree;             // LAB_06F9 bit n = portrait n is still available
	uint16_t uwSel;             // LAB_0703 portrait under the cursor
	uint16_t uwSlot;            // (LAB_06F7 - LAB_0613) / $84: record the next pick fills
	uint16_t uwEditing;         // LAB_05D7 non-zero while the name is edited (draws the name, no wait)
	uint16_t uwNamePos;         // LAB_05D6 caret position
	uint8_t *pName;             // LAB_06B4 name being edited
	uint32_t ulJob;             // LAB_05A5 job from LAB_0E5A
	KnightsPhase ePhase;
	uint8_t ubPad;              // co-op: the controller on offer for the knight just named (ms::PadSource)
	uint8_t aubPadText[24];     // co-op: "Control  <name>" for pfnDrawString

	// LAB_00D3 up to the first poll.  `env` must be filled by the caller.
	void enter(const MenuOps &ops);
	// One pass of the current loop.  Returns true when the last knight has been taken (LAB_00DA reached).
	bool update(const MenuOps &ops);
	// LAB_00DA tail: release the job and fade out.
	void exit(const MenuOps &ops);

private:
	void draw(const MenuOps &ops);          // LAB_00E0
	void selectFirstFree();                 // LAB_00DD
	void beginNameEntry(const MenuOps &ops);// LAB_00E5 head: wait(4), pick the name buffer
	void nameRelease(const MenuOps &ops);   // LAB_00C9 head / LAB_00CE
	bool nameEdit(const MenuOps &ops);      // LAB_00CC
	void refresh(const MenuOps &ops);       // LAB_00CE
	bool finishName(const MenuOps &ops);    // LAB_00D2 + the rest of LAB_00E5 and of the LAB_00D4 fire branch
	bool nextPick(const MenuOps &ops);      // the LAB_00D4 fire branch's tail: next pick or done
	bool coop() const;                      // env.pParty is an active party
	void padText();                         // aubPadText for ubPad
	bool padChoice(const MenuOps &ops);     // KP_PAD_RELEASE / KP_PAD_CHOICE
};

}}  // namespace ms::game
