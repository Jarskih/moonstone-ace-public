// game/net_find - see include/game/net_find.hpp.  No original: the game browser is new (ROADMAP 12.18).
#include "game/net_find.hpp"

namespace ms { namespace game {

namespace {

constexpr uint8_t kGreyColour = 3;            // the palette entry a dimmed row is recoloured to (the lobby's greyed START uses the same)
constexpr uint16_t kDimWidth = 250;           // a dimmed row: from the name to the right edge of the status column

NetResult stronger(NetResult a, NetResult b) { return a > b ? a : b; }

uint8_t putText(char *pOut, uint8_t ubAt, const char *szText) {
	while(*szText && ubAt + 1 < FIND_TEXT_MAX) {
		pOut[ubAt++] = *szText++;
	}
	pOut[ubAt] = 0;
	return ubAt;
}

uint8_t putNumber(char *pOut, uint8_t ubAt, uint32_t ulValue) {
	char acDigits[11];
	uint8_t n = 0;
	do {
		acDigits[n++] = static_cast<char>('0' + ulValue % 10);
		ulValue /= 10;
	} while(ulValue > 0 && n < sizeof acDigits);
	while(n > 0 && ubAt + 1 < FIND_TEXT_MAX) {
		pOut[ubAt++] = acDigits[--n];
	}
	pOut[ubAt] = 0;
	return ubAt;
}

void clearLine(char (&acLine)[FIND_TEXT_MAX]) {
	acLine[0] = 0;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Texts

bool findIsOtherVersion(const FindView &v, const NetListEntry &e) {
	return e.listing.ulBuild != v.ulMyBuild || e.listing.uwVersion != v.uwMyVersion;
}

bool findIsFull(const NetListEntry &e) {
	return e.listing.ubPlayers >= e.listing.ubMaxPlayers;
}

const char *findModeText(uint8_t ubMode) {
	return ubMode == NET_MODE_COOP ? "COOP" : "CLASSIC";
}

uint8_t findPlayersText(const NetListing &l, char (&acOut)[FIND_TEXT_MAX]) {
	uint8_t n = putNumber(acOut, 0, l.ubPlayers);
	n = putText(acOut, n, " OF ");
	return putNumber(acOut, n, l.ubMaxPlayers);
}

void findNameText(const NetListing &l, char (&acOut)[NET_NAME_LEN + 1]) {
	uint8_t n = 0;
	for(; n < NET_NAME_LEN && l.acName[n]; ++n) {
		char c = l.acName[n];
		if(c >= 'a' && c <= 'z') {
			c = static_cast<char>(c - 'a' + 'A');
		} else if(c == '_') {
			c = ' ';
		}
		acOut[n] = c;
	}
	acOut[n] = 0;
}

void findVersionText(uint16_t uwVersion, char (&acOut)[FIND_TEXT_MAX]) {
	uint8_t n = putNumber(acOut, 0, static_cast<uint32_t>(uwVersion >> 8));
	n = putText(acOut, n, ".");
	putNumber(acOut, n, static_cast<uint32_t>(uwVersion & 0xFF));
}

void findStatusLines(uint8_t eMsg, uint16_t uwHostVersion, uint16_t uwMyVersion, char (&acLine1)[FIND_TEXT_MAX], char (&acLine2)[FIND_TEXT_MAX]) {
	clearLine(acLine1);
	clearLine(acLine2);
	switch(eMsg) {
		case FM_CONTACTING: putText(acLine1, 0, "CONTACTING LOBBY"); break;
		case FM_NO_STACK: putText(acLine1, 0, "NO TCP IP STACK"); break;      // not "TCP/IP": the font draws '/' as the caret glyph
		case FM_NO_SERVER: putText(acLine1, 0, "SERVER NOT FOUND"); break;
		case FM_NO_ANSWER: putText(acLine1, 0, "SERVER NOT ANSWERING"); break;
		case FM_NO_GAMES: putText(acLine1, 0, "NO OPEN GAMES FOUND"); break;
		case FM_GAME_FULL:
		case FM_LINK_FULL: putText(acLine1, 0, "GAME FULL"); break;
		case FM_GAME_GONE: putText(acLine1, 0, "GAME NOT FOUND"); break;
		case FM_OTHER_VERSION:
			putText(acLine1, 0, "OTHER VERSION");
			if(uwHostVersion != 0) {
				char acHost[FIND_TEXT_MAX], acMine[FIND_TEXT_MAX];
				findVersionText(uwHostVersion, acHost);
				findVersionText(uwMyVersion, acMine);
				uint8_t n = putText(acLine2, 0, "HOST RUNS ");
				n = putText(acLine2, n, acHost);
				n = putText(acLine2, n, " YOU HAVE ");
				putText(acLine2, n, acMine);
			}
			break;
		case FM_CONNECTING: putText(acLine1, 0, "CONNECTING"); break;
		case FM_WAITING_FOR_HOST: putText(acLine1, 0, "WAITING FOR HOST"); break;
		case FM_LINK_NO_ANSWER: putText(acLine1, 0, "NO ANSWER"); break;
		default: break;
	}
}

// ---------------------------------------------------------------------------------------------------------
// FindMenu

void FindMenu::setMsg(uint8_t eNew, NetResult &r) {
	if(eMsg != eNew) {
		eMsg = eNew;
		r = stronger(r, NET_REDRAW);
	}
}

void FindMenu::enter(const char *szPlayerName, const FindSeam &find) {
	uint8_t n = 0;
	for(; szPlayerName[n] && n < NET_NAME_MAX; ++n) {
		szName[n] = szPlayerName[n];
	}
	szName[n] = 0;
	view.ubStatus = NBS_IDLE;
	view.ubCount = 0;
	view.join.ubResult = NET_JOIN_NOT_FOUND;
	view.join.ulAddr = 0;
	view.join.uwPort = 0;
	view.join.ulSession = 0;
	view.join.uwHostVersion = 0;
	view.ulRev = 0;
	view.ulMyBuild = 0;
	view.uwMyVersion = 0;
	ubCursor = 0;
	ubCountSeen = 0;
	bMoved = false;
	eState = FS_LISTING;
	eMsg = FM_CONTACTING;
	uwHostVersion = 0;
	ulSeenRev = 0;
	bFirePrev = true;
	poll.eStatus = NETST_NONE;
	poll.bSeated = false;
	find.pfnList();
}

NetResult FindMenu::back(const FindSeam &find, const NetSeam &net) {
	if(eState == FS_LINKING) {
		net.pfnClose();
	}
	find.pfnStop();
	return NET_LEAVE;
}

NetResult FindMenu::move(int8_t sbDelta) {
	const int16_t swNext = static_cast<int16_t>(ubCursor) + sbDelta;
	if(swNext < 0 || swNext >= rows() || swNext == ubCursor) {
		return NET_IDLE;
	}
	ubCursor = static_cast<uint8_t>(swNext);
	bMoved = true;
	return NET_REDRAW;
}

NetResult FindMenu::activate(const FindSeam &find, const NetSeam &net) {
	NetResult r = NET_IDLE;
	if(isOnGame()) {
		if(eState != FS_LISTED) {
			return NET_IDLE;                                         // a request or a connect is under way
		}
		const NetListEntry &e = view.aEntries[ubCursor];
		if(findIsOtherVersion(view, e)) {
			uwHostVersion = e.listing.uwVersion;
			setMsg(FM_OTHER_VERSION, r);
			return stronger(r, NET_REDRAW);
		}
		if(findIsFull(e)) {
			setMsg(FM_GAME_FULL, r);
			return stronger(r, NET_REDRAW);
		}
		if(find.pfnJoinReq(ubCursor)) {
			eState = FS_ASKING;
			setMsg(FM_CONTACTING, r);
		}
		return stronger(r, NET_REDRAW);
	}
	if(isOnRefresh()) {
		if(eState != FS_LISTED) {
			return NET_IDLE;
		}
		find.pfnList();
		eState = FS_LISTING;
		setMsg(FM_CONTACTING, r);
		return stronger(r, NET_REDRAW);
	}
	return back(find, net);
}

NetResult FindMenu::onJoinReply(const NetSeam &net) {
	const NetJoinReply &j = view.join;
	switch(j.ubResult) {
		case NET_JOIN_DIRECT: {
			char acIp[16];
			netFormatIpv4(j.ulAddr, acIp);
			net.pfnJoinStart(acIp, j.uwPort, szName);
			eState = FS_LINKING;
			eMsg = FM_CONNECTING;
			poll.eStatus = NETST_CONNECTING;
			poll.bSeated = false;
			break;
		}
		case NET_JOIN_RELAY:
			if(net.pfnJoinRelay) {
				net.pfnJoinRelay(j.ulSession, szName);
				eState = FS_LINKING;
				eMsg = FM_CONNECTING;
				poll.eStatus = NETST_CONNECTING;
				poll.bSeated = false;
			} else {
				eState = FS_LISTED;
				eMsg = FM_NO_STACK;
			}
			break;
		case NET_JOIN_FULL:
			eState = FS_LISTED;
			eMsg = FM_GAME_FULL;
			break;
		case NET_JOIN_BUILD:
			eState = FS_LISTED;
			eMsg = FM_OTHER_VERSION;
			uwHostVersion = j.uwHostVersion;
			break;
		case NET_JOIN_NOT_FOUND:
			eState = FS_LISTED;
			eMsg = FM_GAME_GONE;
			break;
		default:
			eState = FS_LISTED;
			eMsg = FM_NO_ANSWER;
			break;
	}
	return NET_REDRAW;
}

// A new list is in: the cursor goes to the first game while the player has not moved it; otherwise it stays on its game (cut to the new length) or, on
// REFRESH / BACK, stays on that row (the rows move with the end of the list).
void FindMenu::placeCursor() {
	const uint8_t ubOld = ubCountSeen;
	ubCountSeen = view.ubCount;
	if(!bMoved) {
		ubCursor = 0;                                                // the first game (REFRESH when there is none)
	} else if(ubCursor >= ubOld) {
		ubCursor = static_cast<uint8_t>(view.ubCount + (ubCursor - ubOld));
	} else if(ubCursor >= view.ubCount) {
		ubCursor = view.ubCount > 0 ? static_cast<uint8_t>(view.ubCount - 1) : 0;
	}
}

// The browse machine's state changed.
NetResult FindMenu::onView(const FindSeam &, const NetSeam &net) {
	ulSeenRev = view.ulRev;
	NetResult r = NET_REDRAW;
	switch(view.ubStatus) {
		case NBS_CONTACTING:
			eMsg = FM_CONTACTING;
			break;
		case NBS_LISTED:
			eState = FS_LISTED;
			eMsg = view.ubCount == 0 ? FM_NO_GAMES : FM_NONE;
			placeCursor();
			break;
		case NBS_JOINED:
			if(eState == FS_ASKING) {
				r = onJoinReply(net);
			}
			break;
		case NBS_NO_STACK:
		case NBS_NO_SERVER:
		case NBS_NO_ANSWER:
			eState = FS_LISTED;
			eMsg = view.ubStatus == NBS_NO_STACK ? FM_NO_STACK : (view.ubStatus == NBS_NO_SERVER ? FM_NO_SERVER : FM_NO_ANSWER);
			placeCursor();                                           // the failed request cleared the list
			break;
		default:
			break;
	}
	return r;
}

// The connect after the lobby's answer: the same progress as the Multiplayer screen's join page.
NetResult FindMenu::pollLink(const NetSeam &net) {
	NetPoll p;
	p.eStatus = NETST_NONE;
	p.bSeated = false;
	net.pfnPoll(p);
	poll = p;
	NetResult r = NET_IDLE;
	switch(p.eStatus) {
		case NETST_CONNECTED:
			if(p.bSeated) {
				return NET_LOBBY_CLIENT;                             // the host accepted us: to the lobby
			}
			setMsg(FM_WAITING_FOR_HOST, r);
			break;
		case NETST_NO_STACK:
		case NETST_NO_ANSWER:
		case NETST_WRONG_VERSION:
		case NETST_GAME_FULL:
			net.pfnClose();                                          // a failed link releases its socket; the page stays with the message
			eState = FS_LISTED;
			uwHostVersion = 0;
			eMsg = p.eStatus == NETST_NO_STACK ? FM_NO_STACK : (p.eStatus == NETST_NO_ANSWER ? FM_LINK_NO_ANSWER : (p.eStatus == NETST_GAME_FULL ? FM_LINK_FULL : FM_OTHER_VERSION));
			r = NET_REDRAW;
			break;
		default:
			setMsg(FM_CONNECTING, r);
			break;
	}
	return r;
}

NetResult FindMenu::update(const NetInput &in, const FindSeam &find, const NetSeam &net) {
	const bool bFireDown = (in.uwJoy & 0x10) != 0;
	const bool bFire = bFireDown && !bFirePrev;
	bFirePrev = bFireDown;
	NetResult r = NET_IDLE;
	if(eState == FS_LINKING) {
		r = pollLink(net);
		if(r == NET_LOBBY_CLIENT) {
			return r;
		}
	} else {
		find.pfnPoll(view);
		if(view.ulRev != ulSeenRev) {
			r = onView(find, net);
		}
	}
	if(in.uwKey == NETKEY_ESC) {
		return back(find, net);
	}
	if(bFire) {
		return stronger(r, activate(find, net));
	}
	if(in.uwJoy & 8) {
		return stronger(r, move(-1));
	}
	if(in.uwJoy & 4) {
		return stronger(r, move(1));
	}
	return r;
}

// ---------------------------------------------------------------------------------------------------------
// SceneFind

namespace {

void drawText(const MenuOps &ops, const char *szText, uint16_t uwX, uint16_t uwY, uint16_t uwStyle) {
	if(szText[0]) {
		ops.pfnDrawString(reinterpret_cast<const uint8_t *>(szText), uwX, uwY, uwStyle);
	}
}

}  // namespace

// The banner and the arrow are sprites of the big font's table; every text is the small font (as the Options screen: the whole list is small).  The
// font is put back to the big one before the flip so the title menu finds the bank it expects.
void SceneFind::draw(const MenuOps &ops, const LobbyOps &lops, bool bWait) {
	ops.pfnSetPlanes();
	ops.pfnBlitBackground();
	ops.pfnClearHits();
	const FindMenu &m = menu;
	uint16_t uwCursorY = FIND_BACK_Y;
	if(m.isOnGame()) {
		uwCursorY = static_cast<uint16_t>(FIND_ROW_Y + m.ubCursor * FIND_ROW_STEP);
	} else if(m.isOnRefresh()) {
		uwCursorY = FIND_REFRESH_Y;
	}
	ops.pfnUseFont(MENUFONT_BIG);
	ops.pfnSetTextFlag(1);
	ops.pfnDrawSprite(MENUBANK_BACKDROP, 0x49, 5, 0x0A);                                   // the title banner, as the title menu
	ops.pfnDrawSprite(MENUBANK_SPRITES, 0, FIND_CURSOR_X, static_cast<uint16_t>(uwCursorY - FIND_ARROW_UP));   // the arrow, centred on the row
	ops.pfnSetTextFlag(0);
	ops.pfnUseFont(MENUFONT_SMALL);
	for(uint8_t i = 0; i < m.view.ubCount; ++i) {
		const NetListEntry &e = m.view.aEntries[i];
		const uint16_t y = static_cast<uint16_t>(FIND_ROW_Y + i * FIND_ROW_STEP);
		char acName[NET_NAME_LEN + 1];
		char acPlayers[FIND_TEXT_MAX];
		findNameText(e.listing, acName);
		findPlayersText(e.listing, acPlayers);
		drawText(ops, acName, FIND_NAME_X, y, 0);
		drawText(ops, findModeText(e.listing.ubMode), FIND_MODE_X, y, 0);
		drawText(ops, acPlayers, FIND_PLAYERS_X, y, 0);
		drawText(ops, findIsFull(e) ? "FULL" : "OPEN", FIND_STATE_X, y, 0);
		if(findIsOtherVersion(m.view, e)) {
			lops.pfnRecolour(FIND_NAME_X, y, kDimWidth, 7, kGreyColour);                     // another build / version: dimmed, not joinable
		}
	}
	drawText(ops, "REFRESH", FIND_NAME_X, FIND_REFRESH_Y, 0);
	drawText(ops, "BACK", FIND_NAME_X, FIND_BACK_Y, 0);
	char acLine1[FIND_TEXT_MAX], acLine2[FIND_TEXT_MAX];
	findStatusLines(m.eMsg, m.uwHostVersion, m.view.uwMyVersion, acLine1, acLine2);
	drawText(ops, acLine1, 0, FIND_STATUS_Y, 1);
	drawText(ops, acLine2, 0, static_cast<uint16_t>(FIND_STATUS_Y + FIND_STATUS_STEP), 1);
	ops.pfnUseFont(MENUFONT_BIG);
	ops.pfnFlip();
	if(bWait) {
		ops.pfnWait(10);                                                                     // the title menu's pace (also the repeat rate of the stick)
	}
}

void SceneFind::enter(const MenuOps &ops, const LobbyOps &lops, const char *szPlayerName, const FindSeam &find) {
	eResult = NET_IDLE;
	menu.enter(szPlayerName, find);
	ops.pfnIrqOn();
	ops.pfnClearHits();
	ops.pfnKeyReset();                                                                       // no stale key from the Multiplayer screen
	ops.pfnFlip();
	draw(ops, lops, true);
	ops.pfnPalette(MENUPAL_TITLE);
}

bool SceneFind::update(const MenuOps &ops, const LobbyOps &lops, const FindSeam &find, const NetSeam &net) {
	NetInput in;
	in.uwJoy = ops.pfnReadJoy();
	in.uwKey = ops.pfnKeyWord();
	in.ubChar = 0;
	in.ubAction = 0;
	if(in.uwKey) {
		ops.pfnClearKeyWord();
	}
	const NetResult r = menu.update(in, find, net);
	if(r == NET_REDRAW) {
		draw(ops, lops, true);
	}
	if(r == NET_LEAVE || r == NET_LOBBY_CLIENT) {
		eResult = r;
		return true;
	}
	return false;
}

void SceneFind::exit(const MenuOps &ops, const FindSeam &find, const NetSeam &net) {
	if(eResult != NET_LOBBY_CLIENT && menu.eState == FS_LINKING) {
		net.pfnClose();                                              // (back() has closed it already; a screen left any other way must not leak the link)
	}
	find.pfnStop();
	ops.pfnFadeOut();
}

}}  // namespace ms::game
