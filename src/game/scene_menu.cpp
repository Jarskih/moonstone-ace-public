// game/scene_menu - see include/game/scene_menu.hpp.  Every function cites the mog.asm labels it transcribes; the
// call order into MenuOps is the call order of the original (tests/test_scene_menu.py compares the traces).
#include "game/scene_menu.hpp"

namespace ms { namespace game {

namespace {

// y of the cursor by item (the title list is built from these, src/rt/scene_menu.cpp).  The original's four rows were Players (0x55), Gore (0x6E),
// Practice (0x94), Select Knight (0xA8) in LAB_06DD.  Now (owner 2026-10-11) the settings group Players and Mode comes first, then an empty line,
// then Practice and Select Knight, then Back (below a main menu only): six lines of 20, from 66 so that the last one ends at line 184 of 200.
// The cursor has no item for the empty line.  Without Mode (no co-op build, host traces) its line stays empty too.
const uint16_t kItemY[5] = {66, 86, 126, 146, 166};
// The main menu (LOCAL / MULTIPLAYER / SETTINGS): three rows, 25 lines apart.
const uint16_t kMainRowY[MAIN_ITEMS] = {90, 115, 140};

// SECSTRT_4 (DC.L $00030002,$00010001): LAB_06DE for 1, 2, 3, 4 players.
const uint16_t kDamageDiv[4] = {3, 2, 1, 1};

// LAB_0702: x of the four knight portraits.
const uint16_t kPortraitX[4] = {0x000C, 0x0058, 0x00A4, 0x00F0};

const uint16_t kMenuCursorX = 0x0032;     // LAB_05D8 (written once at the head of the menu, only read by LAB_00C4)
const uint8_t kCaretChar = 0x5C;          // MOVE.B #$5C,LAB_05D4: the caret glyph
const uint16_t kNameMax = 13;             // CMPI.W #$D,LAB_05D6 ; BLT: at most 13 characters
const uint16_t COOP_PLAYERS_MIN = 2;      // a co-op party has at least two knights (the Players row in co-op)

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Pure menu logic

// LAB_00C6 (mog.asm 1815)
void menuSetPlayers(MenuState &s, int16_t swDelta) {
	s.uwPlayers = (uint16_t)(s.uwPlayers + (uint16_t)swDelta);          // ADD.W D0,LAB_05C5 ; BNE
	if (s.uwPlayers == 0) s.uwPlayers = 1;                              // MOVE.W #1,LAB_05C5
	else if ((int16_t)s.uwPlayers > 4) s.uwPlayers = 4;                 // CMPI.W #4 ; BLE skip ; MOVE.W #4
	if (s.bCoop) {                                                      // co-op (no original): two knights at least, PARTY_COOP_MAX at most
		if (s.uwPlayers < COOP_PLAYERS_MIN) s.uwPlayers = COOP_PLAYERS_MIN;
		else if (s.uwPlayers > PARTY_COOP_MAX) s.uwPlayers = PARTY_COOP_MAX;
	}
	s.uwDamageDiv = (s.pStatCost ? s.pStatCost : kDamageDiv)[(uint16_t)(s.uwPlayers - 1) & 3];   // SECSTRT_4[(players - 1) * 2]
}

// Co-op (ROADMAP 8.2, no original): the Mode row chooses Classic or Coop.  The count is the Players row's own: menuSetPlayers only raises a
// single player to the two knights co-op needs, so Classic afterwards keeps whatever the count was.
static bool menuSetMode(MenuState &s, bool bCoop) {
	if (!s.bCoopAllowed || s.uwCursor != ITEM_MODE) return false;
	s.bCoop = bCoop;
	menuSetPlayers(s, 0);
	return true;
}

// LAB_00BA (mog.asm 1715)
bool menuMove(MenuState &s, uint16_t uwJoy) {
	if (uwJoy == 0) return false;                                       // MOVE.W LAB_0630,D1 ; BNE
	if (uwJoy & 8) {                                                    // up
		s.uwCursor = (uint16_t)(s.uwCursor - 1);                        // SUBI.W #1,LAB_06DC ; BGE
		if (!s.bCoopAllowed && s.uwCursor == ITEM_MODE) s.uwCursor = ITEM_PLAYERS;   // no Mode row: Practice -> Players
		if ((int16_t)s.uwCursor < 0) { s.uwCursor = 0; s.uwMoved = 1; }
		return true;
	}
	if (uwJoy & 4) {                                                    // down
		s.uwCursor = (uint16_t)(s.uwCursor + 1);                        // ADDI.W #1 ; CMPI.W #3 ; BLE
		if (!s.bCoopAllowed && s.uwCursor == ITEM_MODE) s.uwCursor = ITEM_PRACTICE;   // no Mode row: Players -> Practice
		const int16_t swLast = s.bBack ? ITEM_BACK : ITEM_SELECT;
		if ((int16_t)s.uwCursor > swLast) { s.uwCursor = (uint16_t)swLast; s.uwMoved = 1; }
		return true;
	}
	if (uwJoy & 2) {                                                    // left
		if (menuSetMode(s, false)) return true;
		if (s.uwCursor != ITEM_PLAYERS) return false;                   // BNE.W LAB_00C2 (its Gore toggle is gone)
		menuSetPlayers(s, -1);                                          // MOVE.W #$FFFF,D0 ; JSR LAB_00C6
		return true;
	}
	if (uwJoy & 1) {                                                    // right
		if (menuSetMode(s, true)) return true;
		if (s.uwCursor != ITEM_PLAYERS) return false;                   // BNE.S LAB_00C2
		menuSetPlayers(s, 1);
		return true;
	}
	return false;                                                       // only fire (or other bits): D0 = 0
}

// LAB_00B5 loop body (mog.asm 1695-1713)
MenuAction menuInput(MenuState &s, uint16_t uwJoy) {
	if (uwJoy == 0) return MENU_IDLE;                                   // TST.W D1 ; BEQ LAB_00B5
	if (uwJoy & 0x10) {                                                 // BTST #4,D1 (fire)
		if (menuSetMode(s, !s.bCoop)) return MENU_REDRAW;
		if (s.bBack && s.uwCursor == ITEM_BACK) return MENU_BACK;       // no original
		if (s.uwCursor == ITEM_SELECT) return MENU_SELECT;              // CMPI.W #3 ; BEQ LAB_00B9
		if (s.uwCursor == ITEM_PRACTICE) return MENU_PRACTICE;          // CMPI.W #2 ; BEQ LAB_00B8
	}
	return menuMove(s, uwJoy) ? MENU_REDRAW : MENU_IDLE;                // LAB_00B6: BSR LAB_00BA ; BEQ LAB_00B5
}

uint16_t menuItemY(uint16_t uwCursor) {
	return kItemY[uwCursor > ITEM_BACK ? ITEM_BACK : uwCursor];
}

// The main menu: the cursor wraps at both ends, fire takes the row.
MainAction mainMenuInput(MainMenuState &s, uint16_t uwJoy) {
	if (uwJoy & 0x10) return (MainAction)(MAINACT_LOCAL + s.uwCursor);  // MAINACT_LOCAL, _MULTIPLAYER, _SETTINGS follow MainItem
	if (uwJoy & 8) {                                                    // up
		s.uwCursor = (uint16_t)(s.uwCursor == 0 ? MAIN_ITEMS - 1 : s.uwCursor - 1);
		return MAINACT_REDRAW;
	}
	if (uwJoy & 4) {                                                    // down
		s.uwCursor = (uint16_t)(s.uwCursor + 1 >= MAIN_ITEMS ? 0 : s.uwCursor + 1);
		return MAINACT_REDRAW;
	}
	return MAINACT_IDLE;
}

uint16_t mainMenuItemY(uint16_t uwCursor) {
	return kMainRowY[uwCursor >= MAIN_ITEMS ? MAIN_ITEMS - 1 : uwCursor];
}

// ---------------------------------------------------------------------------------------------------------
// SceneMainMenu (no original): the first screen after the intro, on the title picture with the banner and the arrow of the title menu.

void SceneMainMenu::draw(const MenuOps &ops) {
	ops.pfnSetPlanes();
	ops.pfnBlitBackground();
	ops.pfnClearHits();
	ops.pfnSetTextFlag(1);
	ops.pfnDrawSprite(MENUBANK_BACKDROP, 0x49, 5, 0x0A);                // the title banner
	ops.pfnDrawSprite(MENUBANK_SPRITES, 0, kMenuCursorX, mainMenuItemY(state.uwCursor));   // the arrow
	ops.pfnSetTextFlag(0);
	ops.pfnDrawText(MENUTEXT_MAIN_MENU);
	ops.pfnFlip();
	ops.pfnWait(10);
}

void SceneMainMenu::enter(const MenuOps &ops, bool bFade) {
	eResult = MAINACT_IDLE;
	ops.pfnIrqOn();
	ops.pfnClearHits();
	ops.pfnFlip();
	draw(ops);
	if (bFade) ops.pfnPalette(MENUPAL_TITLE);                           // after a screen that faded out; from the title menu the palette stands
}

bool SceneMainMenu::update(const MenuOps &ops) {
	const MainAction eAct = mainMenuInput(state, ops.pfnReadJoy());
	if (eAct == MAINACT_REDRAW) { draw(ops); return false; }
	if (eAct == MAINACT_IDLE) return false;
	eResult = eAct;
	return true;
}

bool SceneMainMenu::exit(const MenuOps &ops) {
	if (eResult == MAINACT_LOCAL) return false;                         // the title menu is drawn on the same picture, no fade between them
	ops.pfnFadeOut();
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// SceneMenu

// LAB_00C4 (mog.asm 1768)
void SceneMenu::draw(const MenuOps &ops) {
	ops.pfnSetPlanes();                                                 // MOVE.L LAB_0D92,D0 ; JSR LAB_0426+2
	ops.pfnBlitBackground();                                            // LAB_05C0 -> LAB_0D92, JSR LAB_0419
	ops.pfnClearHits();                                                 // JSR LAB_03A7
	const uint16_t y = menuItemY(state.uwCursor);
	ops.pfnSetTextFlag(1);                                              // MOVE.W #1,LAB_0D05
	ops.pfnDrawSprite(MENUBANK_BACKDROP, 0x49, 5, 0x0A);                // title banner: ActiveKnights+10, D0=$49 D1=5 D2=$A
	ops.pfnDrawSprite(MENUBANK_SPRITES, 0, kMenuCursorX, y);            // cursor: LAB_05C4, D0=0 D1=LAB_05D8 D2=LAB_05D9
	ops.pfnSetTextFlag(0);                                              // MOVE.W #0,LAB_0D05
	ops.pfnDrawNumber(state.uwPlayers);                                 // MOVE.W LAB_05C5,D0 ; LEA LAB_06B9,A2 ; JSR LAB_0442
	ops.pfnDrawText(state.bBack ? MENUTEXT_MAIN_BACK : MENUTEXT_MAIN);  // LEA LAB_06AE,A0 ; JSR LAB_0432 (the Gore lines are not in the list)
	if (state.bCoopAllowed) {                                           // no original: the Mode row, the party size is the Players row's
		const uint8_t *pMode = reinterpret_cast<const uint8_t *>(state.bCoop ? "Mode  Coop" : "Mode  Classic");
		ops.pfnDrawString(pMode, 0x58, menuItemY(ITEM_MODE), 0);
	}
	ops.pfnFlip();                                                      // JSR LAB_0416
	ops.pfnWait(10);                                                    // MOVEQ #10,D0 ; JSR LAB_0D74
}

// mog.asm 1669-1690
void SceneMenu::enter(const MenuOps &ops, bool bFade) {
	eResult = MENU_IDLE;
	ops.pfnIrqOn();                                                     // JSR LAB_0D7C (the MOVE #$2000,SR behind it is NOPed, mog.json sr-super)
	ops.pfnClearHits();                                                 // JSR LAB_03A7
	menuSetPlayers(state, 0);                                           // MOVEQ #0,D0 ; JSR LAB_00C6
	if (state.bBack) ops.pfnClearKeyWord();                             // no original: a key pressed before this screen is not an Esc for it
	ops.pfnFlip();                                                      // JSR LAB_0416
	draw(ops);                                                          // BSR LAB_00C4
	if (bFade) ops.pfnPalette(MENUPAL_TITLE);                           // LEA LAB_0D2B,A0 ; JSR LAB_03F2
}

bool SceneMenu::update(const MenuOps &ops) {
	uint16_t uwJoy = ops.pfnReadJoy();                                  // LAB_00B5: JSR LAB_00EE
	if (state.bBack && ops.pfnKeyWord() == MENUKEY_ESC) {               // no original: Esc is the Back row
		ops.pfnClearKeyWord();
		eResult = MENU_BACK;
		return true;
	}
	switch (menuInput(state, uwJoy)) {
	case MENU_REDRAW: draw(ops); return false;                          // LAB_00B7
	case MENU_PRACTICE: eResult = MENU_PRACTICE; return true;
	case MENU_SELECT: eResult = MENU_SELECT; return true;
	case MENU_BACK: eResult = MENU_BACK; return true;
	default: return false;
	}
}

bool SceneMenu::exit(const MenuOps &ops) {
	if (eResult == MENU_BACK) return false;                             // the main menu is drawn on the same picture, no fade between them
	if (eResult == MENU_SELECT) ops.pfnPublishPlayerCount(state.uwPlayers);  // LAB_00B9: ActiveKnights +14 := LAB_05C5
	ops.pfnFadeOut();                                                   // JSR LAB_03F0
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// SceneKnights

// LAB_00E0 (mog.asm 1996)
void SceneKnights::draw(const MenuOps &ops) {
	ops.pfnClearBuffer(MENUBUF_SCREEN);                                 // MOVEA.L LAB_0D92,A0 ; JSR LAB_0D72
	ops.pfnSetPlanes();                                                 // MOVE.L LAB_0D92,D0 ; JSR LAB_0426+2
	ops.pfnDrawText(MENUTEXT_SELECT);                                   // LEA LAB_06E7,A0 ; JSR LAB_0432
	for (uint16_t d7 = 0; d7 < 4; ++d7) {                               // BTST D7,LAB_06F9 : the knights still free
		if ((ubFree >> d7) & 1) ops.pfnDrawSprite(MENUBANK_SPRITES, (uint16_t)(d7 + 2), kPortraitX[d7], 0x50);
	}
	const bool bPad = ePhase == KP_PAD_RELEASE || ePhase == KP_PAD_CHOICE;
	if (bPad) ops.pfnDrawSprite(MENUBANK_SPRITES, (uint16_t)((uwSel & 3) + 2), kPortraitX[uwSel & 3], 0x50);   // co-op: the taken portrait stays
	ops.pfnDrawSprite(MENUBANK_SPRITES, 1, kPortraitX[uwSel & 3], 0x50);  // the highlight frame, D0=1
	if (uwEditing) ops.pfnDrawString(pName, 0x32, 0x32, 0);             // TST.W LAB_05D7 ; LAB_0431 with D2 = 0
	if (bPad) {                                                         // co-op: the name and the controller on offer
		ops.pfnDrawString(pName, 0x32, 0x32, 0);
		ops.pfnDrawString(aubPadText, KNIGHTS_PAD_X, KNIGHTS_PAD_Y, 0);
	}
	ops.pfnFlip();                                                      // JSR LAB_0416
	if (!uwEditing) ops.pfnWait(6);                                     // MOVEQ #6,D0 ; JSR LAB_0D74
}

// LAB_00DD (mog.asm 1982): the first free portrait.  The asm loops forever when none is free (unreachable: at least
// one is free while a pick is left); capped at 8 steps here.
void SceneKnights::selectFirstFree() {
	uint16_t d0 = 0;
	while (d0 < 8 && !((ubFree >> d0) & 1)) ++d0;
	uwSel = d0;
}

// LAB_00D3 (mog.asm 1905)
void SceneKnights::enter(const MenuOps &ops) {
	ops.pfnClearBuffer(MENUBUF_BACKGROUND);                             // MOVEA.L LAB_05C0,A0 ; JSR LAB_0D72
	ops.pfnBlitBackgroundBoth();                                        // JSR LAB_0418
	uwRemaining = env.uwPlayers;                                        // MOVE.W LAB_05C5,LAB_06F8
	ubFree = 0x0F;                                                      // MOVE.B #$F,LAB_06F9
	uwSlot = 0;                                                         // MOVE.L #LAB_0613,LAB_06F7
	uwSel = 0;                                                          // MOVE.W #0,LAB_0703
	uwEditing = 0;                                                      // LAB_05D7 is 0 outside the name editor
	uwNamePos = 0;
	pName = env.apNames[0];
	ePhase = KP_SELECT;
	ubPad = PAD_NONE;
	aubPadText[0] = 0;
	ops.pfnSetPlanes();                                                 // MOVE.L LAB_0D92,D0 ; JSR LAB_0426+2
	draw(ops);                                                          // BSR LAB_00E0
	ops.pfnPalette(MENUPAL_KNIGHTS);                                    // LEA LAB_06FD,A0 ; JSR LAB_03F2
	ulJob = ops.pfnSpawnJob(0x0F, 0x88, 1, 0);                          // LAB_0E5A -> LAB_05A5
}

// LAB_00E5 head (mog.asm 2023): the 4-tick wait, then the name buffer of the portrait; the editor follows.
void SceneKnights::beginNameEntry(const MenuOps &ops) {
	ops.pfnWait(4);                                                     // MOVEQ #4,D0 ; JSR LAB_0D74 (registers saved around it)
	pName = env.apNames[uwSel & 3];                                     // MOVE.L #LAB_06Bx,LAB_06B4
	ePhase = KP_NAME_RELEASE;                                           // BSR LAB_00C9
}

// LAB_00CE (mog.asm 1877): put the caret in, redraw, forget the key.
void SceneKnights::refresh(const MenuOps &ops) {
	pName[uwNamePos] = kCaretChar;                                      // BSR LAB_00D1
	draw(ops);                                                          // JSR (LAB_05D5) = LAB_00E0
	ops.pfnClearKeyWord();                                              // MOVE.W #0,SECSTRT_21
}

// LAB_00C9 (mog.asm 1829): wait for fire to be released, then set the editor up.
void SceneKnights::nameRelease(const MenuOps &ops) {
	uint16_t uwJoy = ops.pfnReadJoy();                                  // JSR LAB_00EE
	if (uwJoy & 0x10) return;                                           // BTST #4,D1 ; BNE LAB_00C9
	uwEditing = 1;                                                      // MOVE.W #1,LAB_05D7
	ops.pfnKeyReset();                                                  // JSR LAB_0B82
	ops.pfnClearHits();                                                 // JSR LAB_03A7
	uint16_t d0 = 0;                                                    // caret at the first space / NUL
	while (pName[d0] != 0x20 && pName[d0] != 0) ++d0;
	uwNamePos = d0;
	refresh(ops);                                                       // BRA LAB_00CE
	ePhase = KP_NAME_EDIT;
}

// LAB_00D2 (mog.asm 1903) and what follows it: LAB_00E5's tail (the portrait is taken, the record filled in) and
// the LAB_00D4 fire branch (next pick or done).  Returns true when that was the last pick.
bool SceneKnights::finishName(const MenuOps &ops) {
	pName[uwNamePos] = 0;                                               // MOVE.B #0,0(A0,D0.W)
	uwEditing = 0;                                                      // MOVE.W #0,LAB_05D7
	uint16_t sel = uwSel;
	ubFree = (uint8_t)(ubFree & ~(1u << (sel & 7)));                    // BCLR #n,LAB_06F9
	Knight &k = env.aKnights[uwSlot];                                   // MOVEA.L LAB_06F7,A1
	k.ulKind = sel;                                                     // MOVE.L #n,54(A1)
	k.ubInputPort = raw(InputPort::Joy1);                               // MOVE.B #2,11(A1)
	if (coop()) {                                                       // co-op: this knight's controller first
		ubPad = partyFirstFreePad(*env.pParty, (uint8_t)uwSlot);
		if (env.pParty->isNet) env.pParty->ubFocus = (uint8_t)(uwSlot & (PARTY_MAX - 1));   // only the owner node's input moves its line
		padText();
		ePhase = KP_PAD_RELEASE;
		draw(ops);
		return false;
	}
	return nextPick(ops);
}

// The LAB_00D4 fire branch after LAB_00E5: count the pick, then the next knight or the end.
bool SceneKnights::nextPick(const MenuOps &ops) {
	uwRemaining = (uint16_t)(uwRemaining - 1);                          // SUBI.W #1,LAB_06F8 ; BEQ LAB_00DA
	if (uwRemaining == 0) return true;
	++uwSlot;                                                           // ADDI.L #$84,LAB_06F7
	selectFirstFree();                                                  // BSR LAB_00DD
	ePhase = KP_SELECT;                                                 // (before the draw: a co-op pad line must not stay)
	draw(ops);                                                          // BSR LAB_00E0
	return false;
}

// LAB_00CC (mog.asm 1850)
bool SceneKnights::nameEdit(const MenuOps &ops) {
	uint16_t uwJoy = ops.pfnReadJoy();                                  // JSR LAB_00EE
	if (uwJoy & 0x10) return finishName(ops);                           // BTST #4,D1 ; BNE LAB_00D2
	uint16_t key = ops.pfnKeyWord();                                    // TST.W SECSTRT_21 ; BEQ LAB_00CC
	if (key == 0) return false;
	if (key == 0x1C) return finishName(ops);                            // Return
	if (key == 0x0E) {                                                  // Backspace: LAB_00CF
		pName[uwNamePos] = 0x20;
		uwNamePos = (uint16_t)(uwNamePos - 1);                          // SUBI.W #1 ; BGE
		if ((int16_t)uwNamePos < 0) uwNamePos = 0;
		pName[uwNamePos] = 0x20;                                        // LAB_00D0
		refresh(ops);
		return false;
	}
	uint8_t ch = ops.pfnTranslateKey(key);                              // JSR LAB_0D8D ; TST.W D0 ; BEQ LAB_00CE
	if (ch != 0) {
		if ((int16_t)uwNamePos >= (int16_t)kNameMax) ops.pfnErrorFlash();  // CMPI.W #$D ; BLT
		else {
			pName[uwNamePos] = ch;                                      // LAB_00CD
			uwNamePos = (uint16_t)(uwNamePos + 1);
		}
	}
	refresh(ops);
	return false;
}

bool SceneKnights::coop() const {
	return env.pParty && env.pParty->active;
}

// "Control  Joystick 1": the line under the portraits (font: letters, digits, spaces only).
void SceneKnights::padText() {
	static const char kLabel[] = "Control  ";
	static const char kPlayer[] = "Player N  ";   // network game: whose line this is (the N is replaced)
	uint8_t i = 0;
	const bool bNet = env.pParty && env.pParty->isNet;
	for (const char *p = bNet ? kPlayer : kLabel; *p; ++p) aubPadText[i++] = (uint8_t)*p;
	if (bNet) aubPadText[7] = (uint8_t)('1' + (uwSlot & (PARTY_MAX - 1)));
	for (const char *p = ms::padName(ubPad); *p && i < sizeof(aubPadText) - 1; ++p) aubPadText[i++] = (uint8_t)*p;
	aubPadText[i] = 0;
}

// Co-op controller choice (no original).  The name may have been taken with fire: wait for its release first (as
// LAB_00C9 does before the name editor), then left / right cycle the free controllers and fire binds the one on offer.
bool SceneKnights::padChoice(const MenuOps &ops) {
	if (env.pParty->isNet && partyPadTakenFor(*env.pParty, (uint8_t)uwSlot, ubPad)) {
		// A drop moved this member to another node while its line was open (partyNetDrop): the pad on offer was free on the old node, but
		// another member of the new one holds it.  Offer the next free one (same on every machine: the party is shared state).
		ubPad = partyNextPad(*env.pParty, (uint8_t)uwSlot, ubPad, 1);
		padText();
		draw(ops);
	}
	uint16_t uwJoy = ops.pfnReadJoy();
	if (ePhase == KP_PAD_RELEASE) {
		if (!(uwJoy & 0x10)) ePhase = KP_PAD_CHOICE;
		return false;
	}
	if (uwJoy & 0x10) {
		env.pParty->aubPad[uwSlot & (PARTY_MAX - 1)] = ubPad;
		if (env.pParty->isNet) env.pParty->ubFocus = PARTY_FOCUS_ANY;   // the next portrait is picked by anybody again
		return nextPick(ops);
	}
	if (uwJoy & 3) {
		const uint8_t ubNext = partyNextPad(*env.pParty, (uint8_t)uwSlot, ubPad, (uwJoy & 2) ? -1 : 1);
		if (ubNext != ubPad) {
			ubPad = ubNext;
			padText();
			draw(ops);
		}
	}
	return false;
}

// LAB_00D4 loop body (mog.asm 1925-1962)
bool SceneKnights::update(const MenuOps &ops) {
	if (ePhase == KP_PAD_RELEASE || ePhase == KP_PAD_CHOICE) return padChoice(ops);
	if (ePhase == KP_NAME_RELEASE) { nameRelease(ops); return false; }
	if (ePhase == KP_NAME_EDIT) return nameEdit(ops);
	uint16_t uwJoy = ops.pfnReadJoy();                                  // LAB_00D4: JSR LAB_00EE
	if (uwJoy & 0x10) { beginNameEntry(ops); return false; }            // BTST #4,D1 ; BEQ LAB_00D5
	if (uwJoy & 2) {                                                    // LAB_00D5: left, the next free portrait below
		int16_t d0 = (int16_t)uwSel;
		for (;;) {
			d0 = (int16_t)(d0 - 1);                                     // SUBI.W #1,D0 ; BLT LAB_00D9
			if (d0 < 0) break;
			if ((ubFree >> (d0 & 7)) & 1) { uwSel = (uint16_t)d0; draw(ops); break; }
		}
		return false;
	}
	if (uwJoy & 1) {                                                    // LAB_00D7: right
		uint16_t d0 = uwSel;
		for (;;) {
			d0 = (uint16_t)(d0 + 1);                                    // ADDI.W #1,D0 ; CMP.W #4 ; BEQ LAB_00D9
			if (d0 == 4) break;
			if ((ubFree >> (d0 & 7)) & 1) { uwSel = d0; draw(ops); break; }
		}
	}
	return false;
}

// LAB_00DA (mog.asm 1964)
void SceneKnights::exit(const MenuOps &ops) {
	ops.pfnKillJob(ulJob);                                              // MOVEA.L LAB_05A5,A0 ; CLR.L (A0)
	ops.pfnFadeOut();                                                   // JSR LAB_03F0
}

}}  // namespace ms::game
