// game/scene_lobby - see include/game/scene_lobby.hpp (ROADMAP M12, docs/NETPLAY.md 4.2b).  No original: new.
// The drawing follows SceneKnights::draw (game/scene_menu.cpp: LAB_00E0); the keys follow SceneNet (game/net_menu.cpp).
#include "game/scene_lobby.hpp"

#include "engine/pad.hpp"
#include "game/net_menu.hpp"

namespace ms { namespace game {

namespace {

const uint8_t kCaretChar = 0x5C;               // the caret glyph the name editors draw
const uint16_t kKeyEsc = NETKEY_ESC;           // the game key codes the name editor tests (Return $1C, Backspace $0E)
const uint16_t kKeyTableSize = 0x62;           // the key translation table LAB_0D99 has 98 entries
const uint16_t kPortraitX[NET_PORTRAITS] = {0x000C, 0x0058, 0x00A4, 0x00F0};   // LAB_0702
const uint8_t kPadFirst = PAD_JOY1;

uint8_t strLen(const char *s) {
	uint8_t n = 0;
	while(s[n]) {
		++n;
	}
	return n;
}

void trimEnd(char *s) {
	uint8_t n = strLen(s);
	while(n > 0 && s[n - 1] == ' ') {
		s[--n] = 0;
	}
}

}  // namespace

uint16_t lobbyPortraitX(uint8_t ubPortrait) {
	return kPortraitX[ubPortrait & (NET_PORTRAITS - 1)];
}

void lobbyShortName(const LobbyOps &lops, const char *szName, char (&acOut)[NET_NAME_LEN]) {
	const uint16_t uwMax = LOBBY_PORTRAIT_W - 2 * LOBBY_NAME_INSET - 2 * LOBBY_NAME_PAD;
	uint8_t n = 0;
	while(szName[n] && n + 1 < NET_NAME_LEN) {
		acOut[n] = szName[n];
		++n;
	}
	acOut[n] = 0;
	while(n > 0 && lops.pfnSmallWidth(reinterpret_cast<const uint8_t *>(acOut)) > uwMax) {
		acOut[--n] = 0;
	}
}

uint8_t lobbyPlayersText(uint8_t ubJoined, char (&acOut)[24]) {
	static const char kText[] = "PLAYERS ";
	uint8_t n = 0;
	for(const char *p = kText; *p; ++p) {
		acOut[n++] = *p;
	}
	acOut[n++] = static_cast<char>('0' + (ubJoined % 10));
	acOut[n++] = ' ';
	acOut[n++] = 'O';
	acOut[n++] = 'F';
	acOut[n++] = ' ';
	acOut[n++] = static_cast<char>('0' + ROOM_SEATS);
	acOut[n] = 0;
	return n;
}

const char *lobbyModeText(uint8_t ubMode) {
	return ubMode == NET_MODE_COOP ? "MODE  COOP" : "MODE  CLASSIC";
}

// ---------------------------------------------------------------------------------------------------------
// state machine

const NetSeatRec &SceneLobby::me() const {
	return view.room.aSeat[view.ubSeat & (ROOM_SEATS - 1)];
}

void SceneLobby::sendPick(const LobbySeam &seam, uint8_t ubStage, uint8_t ubPortrait, const char *szName, uint8_t ubPadValue) {
	NetLobbyPick p;
	p.ubStage = ubStage;
	p.ubSel = ubPortrait;
	p.ubPad = ubPadValue;
	netNameSet(p.acName, szName);
	seam.pfnPick(p);
}

bool SceneLobby::moveSelector(const LobbySeam &seam, int8_t sbDir) {
	const uint8_t ubNext = roomStep(view.room, ubSel, sbDir, view.ubSeat);
	if(ubNext == ubSel) {
		return false;
	}
	ubSel = ubNext;
	sendPick(seam, NET_PICK_BROWSE, ubSel, "", 0xFF);
	return true;
}

void SceneLobby::startNameEntry() {
	// pre-filled with the name of the Multiplayer screen (the host holds it as this seat's name), editable
	uint8_t n = 0;
	const char *szJoin = me().acName;
	while(szJoin[n] && n < ROOM_NAME_SHOWN) {
		acName[n] = szJoin[n];
		++n;
	}
	acName[n] = 0;
	ubNameLen = n;
	ePhase = LP_NAME;
	isDirty = true;
}

void SceneLobby::finishName(const LobbySeam &seam) {
	trimEnd(acName);
	if(acName[0] == 0) {                              // an empty name is the join name again
		uint8_t n = 0;
		while(me().acName[n] && n < ROOM_NAME_SHOWN) {
			acName[n] = me().acName[n];
			++n;
		}
		acName[n] = 0;
	}
	ubNameLen = strLen(acName);
	if(view.room.ubMode == NET_MODE_COOP) {           // co-op: the controller next
		ubPad = kPadFirst;
		ePhase = LP_PAD;
	} else {
		sendPick(seam, NET_PICK_DONE, ubSel, acName, kPadFirst);   // classic: every knight uses Joystick 1 of its own machine
		ePhase = LP_DONE;
	}
	isDirty = true;
}

void SceneLobby::release(const LobbySeam &seam) {
	ePhase = LP_BROWSE;
	eFocus = LF_PORTRAITS;
	sendPick(seam, NET_PICK_BROWSE, ubSel, "", 0xFF);
	isDirty = true;
}

// Reconciles the local phase with the host's answer: a claim is ours when the room says so, lost when somebody else holds the portrait; a
// selector on a portrait that was taken meanwhile steps to the next free one.
void SceneLobby::followRoom(const LobbySeam &seam) {
	if(ePhase == LP_BROWSE) {
		if(roomIsHeld(view.room, ubSel, view.ubSeat)) {              // somebody else picked it: the host has moved this seat on already
			ubSel = roomFirstFree(view.room, me().ubSel, view.ubSeat);
			if(ubSel != me().ubSel) {
				sendPick(seam, NET_PICK_BROWSE, ubSel, "", 0xFF);
			}
			isDirty = true;
		}
	} else if(ePhase == LP_CLAIMING) {
		if(me().ubState == NET_SEAT_CLAIM && me().ubSel == ubSel) {
			startNameEntry();
		} else if(roomIsHeld(view.room, ubSel, view.ubSeat)) {
			ePhase = LP_BROWSE;                                    // lost the race
			ubSel = roomFirstFree(view.room, ubSel, view.ubSeat);
			isDirty = true;
		}
	}
}

void SceneLobby::applyInput(const MenuOps &ops, const LobbySeam &seam, uint16_t uwJoy, uint16_t uwKey, uint8_t ubChar, uint8_t ubAction) {
	const bool isFireDown = (uwJoy & 0x10) != 0;
	const bool isFire = isFireDown && !isFirePrev;
	isFirePrev = isFireDown;
	const bool isHoriz = (uwJoy & 3) != 0;
	const bool isHorizEdge = isHoriz && !isHorizPrev;
	isHorizPrev = isHoriz;
	const bool isHost = view.isHost;
	const bool isIdle = ePhase == LP_BROWSE || ePhase == LP_DONE;

	if(ubAction == NETACT_START && isHost && isIdle && roomStartEnabled(view.room) && seam.pfnStart()) {   // `poke netaction 3`: the START button
		eResult = LOBBY_STARTED;
		return;
	}
	if(uwKey == kKeyEsc) {
		if(ePhase == LP_BROWSE) {
			eResult = LOBBY_LEFT;
		} else {
			release(seam);
		}
		return;
	}
	switch(ePhase) {
		case LP_CLAIMING:
			break;
		case LP_NAME:
			if(uwKey == NETKEY_RETURN || isFire) {
				finishName(seam);
			} else if(uwKey == NETKEY_BACKSPACE) {
				if(ubNameLen > 0) {
					acName[--ubNameLen] = 0;
					isDirty = true;
				}
			} else if(ubChar && netFieldAccepts(NETROW_NAME, ubChar)) {
				if(ubNameLen >= ROOM_NAME_SHOWN) {
					ops.pfnErrorFlash();                         // the original editor's "name too long" flash
				} else if(!(ubChar == ' ' && ubNameLen == 0)) {
					acName[ubNameLen++] = static_cast<char>(ubChar);
					acName[ubNameLen] = 0;
					isDirty = true;
				}
			}
			break;
		case LP_PAD:
			if(isFire) {
				sendPick(seam, NET_PICK_DONE, ubSel, acName, ubPad);
				ePhase = LP_DONE;
				isDirty = true;
			} else if(uwJoy & 3) {
				ubPad = static_cast<uint8_t>((ubPad + PAD_SELECTABLE_COUNT + ((uwJoy & 2) ? -1 : 1)) % PAD_SELECTABLE_COUNT);
				isDirty = true;
				isMoved = true;
			}
			break;
		default:   // browsing, done: the focus rows
			if(isHost && (uwJoy & 8) && eFocus != LF_MODE) {
				eFocus = eFocus == LF_START ? LF_PORTRAITS : LF_MODE;
				isDirty = true;
				isMoved = true;
			} else if(isHost && (uwJoy & 4) && eFocus != LF_START) {
				eFocus = eFocus == LF_MODE ? LF_PORTRAITS : LF_START;
				isDirty = true;
				isMoved = true;
			} else if(eFocus == LF_MODE) {
				if(isHost && (isHorizEdge || isFire)) {
					seam.pfnMode(view.room.ubMode == NET_MODE_COOP ? NET_MODE_CLASSIC : NET_MODE_COOP);
					isDirty = true;
				}
			} else if(eFocus == LF_START) {
				if(isHost && isFire && roomStartEnabled(view.room) && seam.pfnStart()) {
					eResult = LOBBY_STARTED;
				}
			} else if(ePhase == LP_BROWSE) {
				if(isFire) {
					ePhase = LP_CLAIMING;
					sendPick(seam, NET_PICK_CLAIM, ubSel, "", 0xFF);
					isDirty = true;
				} else if(uwJoy & 2) {
					if(moveSelector(seam, -1)) {
						isDirty = true;
						isMoved = true;
					}
				} else if(uwJoy & 1) {
					if(moveSelector(seam, 1)) {
						isDirty = true;
						isMoved = true;
					}
				}
			}
			break;
	}
}

// ---------------------------------------------------------------------------------------------------------
// drawing

void SceneLobby::draw(const MenuOps &ops, const LobbyOps &lops, bool bWait) {
	const NetRoom &room = view.room;
	ops.pfnClearBuffer(MENUBUF_SCREEN);
	ops.pfnSetPlanes();
	ops.pfnDrawText(MENUTEXT_SELECT);                                    // "Select a Knight"
	char acPlayers[24];
	lobbyPlayersText(roomJoined(room), acPlayers);
	ops.pfnDrawString(reinterpret_cast<const uint8_t *>(acPlayers), 0, LOBBY_PLAYERS_Y, 1);
	ops.pfnDrawString(reinterpret_cast<const uint8_t *>(lobbyModeText(room.ubMode)), 0, LOBBY_MODE_Y, 1);
	for(uint8_t d = 0; d < NET_PORTRAITS; ++d) {
		ops.pfnDrawSprite(MENUBANK_SPRITES, static_cast<uint16_t>(d + 2), kPortraitX[d], LOBBY_PORTRAIT_Y);
	}
	ops.pfnDrawSprite(MENUBANK_SPRITES, 1, kPortraitX[ubSel & 3], LOBBY_PORTRAIT_Y);   // the knight screen's highlight frame: the OWN knight only, from the browse to the START
	for(uint8_t s = 0; s < ROOM_SEATS; ++s) {                           // a picked knight: its player's name in the portrait's top-left corner
		const NetSeatRec &seat = room.aSeat[s];
		if(seat.ubState < NET_SEAT_CLAIM) {
			continue;
		}
		char acShort[NET_NAME_LEN];
		lobbyShortName(lops, seat.acName, acShort);
		const uint16_t uwX = static_cast<uint16_t>(kPortraitX[seat.ubSel & 3] + LOBBY_NAME_INSET);
		const uint16_t uwY = static_cast<uint16_t>(LOBBY_PORTRAIT_Y + LOBBY_NAME_INSET);
		const uint8_t *pText = reinterpret_cast<const uint8_t *>(acShort);
		lops.pfnBar(uwX, uwY, static_cast<uint16_t>(lops.pfnSmallWidth(pText) + 2 * LOBBY_NAME_PAD), LOBBY_NAME_H, LOBBY_BOX_COLOUR);
		lops.pfnSmallString(pText, static_cast<uint16_t>(uwX + LOBBY_NAME_PAD), static_cast<uint16_t>(uwY + LOBBY_NAME_PAD));
	}
	// the bottom line: what the player does next
	if(ePhase == LP_NAME) {
		char acLine[ROOM_NAME_SHOWN + 2];
		uint8_t n = 0;
		for(; n < ubNameLen; ++n) {
			acLine[n] = acName[n];
		}
		acLine[n++] = static_cast<char>(kCaretChar);
		acLine[n] = 0;
		ops.pfnDrawString(reinterpret_cast<const uint8_t *>(acLine), 0, LOBBY_BOTTOM_Y, 1);
	} else if(ePhase == LP_PAD) {
		uint8_t aText[40];
		uint8_t n = 0;
		for(const char *p = "Control  "; *p; ++p) {
			aText[n++] = static_cast<uint8_t>(*p);
		}
		for(const char *p = padName(ubPad); *p && n < sizeof aText - 1; ++p) {
			aText[n++] = static_cast<uint8_t>(*p);
		}
		aText[n] = 0;
		ops.pfnDrawString(aText, 0, LOBBY_BOTTOM_Y, 1);
	} else if(view.isHost) {
		ops.pfnDrawString(reinterpret_cast<const uint8_t *>("START"), 0, LOBBY_BOTTOM_Y, 1);
		if(!roomStartEnabled(room)) {
			lops.pfnRecolour(LOBBY_START_X, LOBBY_BOTTOM_Y, LOBBY_START_W, LOBBY_START_H, LOBBY_GREY);
		}
	} else {
		ops.pfnDrawString(reinterpret_cast<const uint8_t *>("WAITING FOR HOST"), 0, LOBBY_BOTTOM_Y, 1);
	}
	if(view.isHost && (ePhase == LP_BROWSE || ePhase == LP_DONE) && eFocus != LF_PORTRAITS) {   // the menu arrow on the host's MODE / START row
		ops.pfnSetTextFlag(1);
		ops.pfnDrawSprite(MENUBANK_SPRITES, 0, LOBBY_CURSOR_X, static_cast<uint16_t>((eFocus == LF_MODE ? LOBBY_MODE_Y : LOBBY_BOTTOM_Y) + 2));
		ops.pfnSetTextFlag(0);
	}
	if(view.isHost && view.ubListing != NLS_PRIVATE) {
		const char *szListing = netLobbyText(view.ubListing);
		if(szListing[0]) {
			lops.pfnSmallString(reinterpret_cast<const uint8_t *>(szListing), 4, LOBBY_LISTING_Y);
		}
	}
	ops.pfnFlip();
	if(bWait) {
		ops.pfnWait(6);                                                  // the knight screen's pace (also the repeat rate of the stick)
	}
	ulDrawnRev = view.ulRev;
	isDirty = false;
	isMoved = false;
}

void SceneLobby::enter(const MenuOps &ops, const LobbyOps &lops, const LobbySeam &seam) {
	eResult = LOBBY_RUNNING;
	ePhase = LP_BROWSE;
	eFocus = LF_PORTRAITS;
	isFirePrev = true;
	isHorizPrev = true;
	isDirty = false;
	isMoved = false;
	ubNameLen = 0;
	acName[0] = 0;
	ubPad = kPadFirst;
	seam.pfnPoll(view);
	ubSel = me().ubSel;
	ops.pfnIrqOn();
	ops.pfnClearBuffer(MENUBUF_BACKGROUND);
	ops.pfnBlitBackgroundBoth();
	ops.pfnClearHits();
	ops.pfnKeyReset();                                                   // no stale key from the Multiplayer screen
	ops.pfnSetPlanes();
	draw(ops, lops, false);
	ops.pfnPalette(MENUPAL_KNIGHTS);
	ulJob = ops.pfnSpawnJob(0x0F, 0x88, 1, 0);                            // the highlight frame's colour ramp, as the knight screen
}

bool SceneLobby::update(const MenuOps &ops, const LobbyOps &lops, const LobbySeam &seam) {
	seam.pfnPoll(view);
	if(view.isStarted) {
		eResult = LOBBY_STARTED;
		return true;
	}
	if(view.isLost) {
		eResult = LOBBY_LOST;
		return true;
	}
	followRoom(seam);
	uint16_t uwKey = ops.pfnKeyWord();
	uint8_t ubChar = 0;
	if(uwKey) {
		ops.pfnClearKeyWord();
		if(uwKey < kKeyTableSize) {
			const uint8_t ub = ops.pfnTranslateKey(uwKey);
			if(ub >= 0x20 && ub < 0x7F) {
				ubChar = ub;                                               // printable only
			}
		}
	}
	const uint8_t ubAction = seam.pfnTakeAction ? seam.pfnTakeAction() : 0;
	applyInput(ops, seam, ops.pfnReadJoy(), uwKey, ubChar, ubAction);
	if(eResult != LOBBY_RUNNING) {
		return true;
	}
	if(isDirty || view.ulRev != ulDrawnRev) {
		draw(ops, lops, isMoved);                                          // the stick repeats once per redraw wait; typing and remote changes do not wait
	}
	return false;
}

void SceneLobby::exit(const MenuOps &ops, const LobbySeam &seam) {
	if(eResult == LOBBY_LEFT || eResult == LOBBY_LOST) {
		seam.pfnLeave();
	}
	ops.pfnKillJob(ulJob);
	ops.pfnFadeOut();
	if(ops.pfnRestoreBackground) {
		ops.pfnRestoreBackground();                                          // the lobby cleared the background for its own screen: the title picture comes back for the menus
	}
}

}}  // namespace ms::game
