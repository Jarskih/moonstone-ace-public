// game/net_menu - see include/game/net_menu.hpp.  No original: the Multiplayer screen is new (ROADMAP 12.8a; the lobby is game/scene_lobby.cpp).
#include "game/net_menu.hpp"

#include "engine/inifile.hpp"

namespace ms { namespace game {

namespace {

const uint8_t kCaretChar = 0x5C;            // the caret glyph the knight name editor draws (MOVE.B #$5C,LAB_05D4)
const uint8_t kKeyTableSize = 0x62;         // the key translation table LAB_0D99 has 98 entries

uint16_t strLen(const char *s) {
	uint16_t n = 0;
	while(s[n]) ++n;
	return n;
}

void strCopy(char *pDst, uint16_t uwCap, const char *pSrc, uint16_t uwLen) {   // at most uwCap characters, NUL-terminated
	uint16_t n = 0;
	while(n < uwLen && n < uwCap) { pDst[n] = pSrc[n]; ++n; }
	pDst[n] = 0;
}

bool isDigit(uint8_t c) { return c >= '0' && c <= '9'; }
bool isLetter(uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

void trimTrailingSpaces(char *s) {
	uint16_t n = strLen(s);
	while(n > 0 && s[n - 1] == ' ') s[--n] = 0;
}

// Appends the characters of an ini value to a field: only those the field accepts, up to its capacity.
void loadField(char *pDst, uint8_t ubMax, NetRow eRow, IniSlice s) {
	uint8_t n = 0;
	for(uint16_t i = 0; i < s.n && n < ubMax; ++i) {
		if(netFieldAccepts(eRow, (uint8_t)s.p[i])) pDst[n++] = s.p[i];
	}
	pDst[n] = 0;
}

// ---- net.ini writer -------------------------------------------------------------------------------------

struct Writer {
	char *p;
	uint32_t ulCap;
	uint32_t ulLen;
	bool bOk;
	void put(char c) {
		if(ulLen < ulCap) p[ulLen] = c;
		else bOk = false;
		++ulLen;
	}
	void put(const char *s, uint32_t n) { for(uint32_t i = 0; i < n; ++i) put(s[i]); }
	void put(const char *s) { while(*s) put(*s++); }
};

constexpr int8_t kKeys = 4;
const char *const kKeyName[kKeys] = {"name", "address", "port", "public"};

struct LineIter {
	const char *p;
	uint32_t ulLeft;
	bool next(const char *&pLine, uint32_t &ulLen) {   // one line without LF / CRLF
		if(ulLeft == 0) return false;
		uint32_t n = 0;
		while(n < ulLeft && p[n] != '\n') ++n;
		pLine = p;
		ulLen = n;
		p += n < ulLeft ? n + 1 : n;
		ulLeft -= n < ulLeft ? n + 1 : n;
		if(ulLen > 0 && pLine[ulLen - 1] == '\r') --ulLen;
		return true;
	}
};

enum LineKind : uint8_t { LK_OTHER = 0, LK_NET_HEADER, LK_SECTION, LK_KEY };

// What a line is; bNet tracks whether the lines belong to [net] (updated on a section header).  LK_KEY: *pKey = 0..2 for name / address / port.
LineKind classify(const char *pLine, uint32_t ulLen, bool &bNet, int8_t &sbKey) {
	sbKey = -1;
	IniReader r;
	iniOpen(r, pLine, ulLen);
	IniLine t;
	const uint8_t ubTok = iniNext(r, t);
	if(ubTok == INI_SECTION) {
		bNet = iniEqual(t.kind, "net");
		return bNet ? LK_NET_HEADER : LK_SECTION;
	}
	if(ubTok == INI_PAIR && bNet) {
		for(int8_t i = 0; i < kKeys; ++i) {
			if(iniEqual(t.key, kKeyName[i])) { sbKey = i; return LK_KEY; }
		}
	}
	return LK_OTHER;
}

const char *keyValue(const NetConfig &c, int8_t sbKey) {
	return sbKey == 0 ? c.szName : (sbKey == 1 ? c.szAddr : c.szPort);
}

// `yes` / `true` / `on` / `1` and the negatives, any case, quoted or not; false when the value is none of them.
bool parseYesNo(IniSlice v, bool &bOut) {
	static const char *const kYes[] = {"yes", "true", "on", "1", "y"};
	static const char *const kNo[] = {"no", "false", "off", "0", "n"};
	for(uint8_t i = 0; i < 5; ++i) {
		if(iniEqual(v, kYes[i])) { bOut = true; return true; }
		if(iniEqual(v, kNo[i])) { bOut = false; return true; }
	}
	return false;
}

void emitPair(Writer &w, const NetConfig &c, int8_t sbKey) {
	w.put(kKeyName[sbKey]);
	w.put(" = ");
	if(sbKey == 3) {                                   // public = yes | no
		w.put(c.bPublic ? "yes" : "no");
	} else if(sbKey == 2) {                            // the port is a plain number
		const char *s = c.szPort;
		w.put(*s ? s : "6502");
	} else {
		w.put('"');
		w.put(keyValue(c, sbKey));
		w.put('"');
	}
	w.put('\n');
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Config

void netConfigDefaults(NetConfig &c) {
	strCopy(c.szName, NET_NAME_MAX, "PLAYER", 6);
	c.szAddr[0] = 0;
	strCopy(c.szPort, NET_PORT_MAX, "6502", 4);
	c.bPublic = true;                                  // owner 2026-10-10: public by default
	c.szLobby[0] = 0;
	c.uwLobbyPort = 0;
	c.bUdp = true;
	c.bStats = true;
}

void netConfigLoad(NetConfig &c, const char *pText, uint32_t ulLen) {
	IniReader r;
	iniOpen(r, pText, ulLen);
	IniLine t;
	bool bNet = false;
	for(;;) {
		const uint8_t ubTok = iniNext(r, t);
		if(ubTok == INI_END) break;
		if(ubTok == INI_SECTION) { bNet = iniEqual(t.kind, "net"); continue; }
		if(ubTok != INI_PAIR || !bNet) continue;
		IniSlice v = t.value;
		IniSlice inside;
		if(iniParseString(v, inside)) v = inside;      // "text" or a bare token
		if(iniEqual(t.key, "name")) {
			loadField(c.szName, NET_NAME_MAX, NETROW_NAME, v);
			trimTrailingSpaces(c.szName);
		} else if(iniEqual(t.key, "address")) {
			loadField(c.szAddr, NET_ADDR_MAX, NETROW_ADDRESS, v);
		} else if(iniEqual(t.key, "port")) {
			loadField(c.szPort, NET_PORT_MAX, NETROW_PORT, v);
		} else if(iniEqual(t.key, "public")) {
			bool bPublic = c.bPublic;
			if(parseYesNo(v, bPublic)) c.bPublic = bPublic;     // an unreadable value keeps the default
		} else if(iniEqual(t.key, "lobby")) {
			loadField(c.szLobby, NET_ADDR_MAX, NETROW_ADDRESS, v);
		} else if(iniEqual(t.key, "udp")) {
			bool bUdp = c.bUdp;
			if(parseYesNo(v, bUdp)) c.bUdp = bUdp;     // an unreadable value keeps the default
		} else if(iniEqual(t.key, "stats")) {
			bool bStats = c.bStats;
			if(parseYesNo(v, bStats)) c.bStats = bStats;     // an unreadable value keeps the default
		} else if(iniEqual(t.key, "lobby_port")) {
			int32_t slPort = 0;
			if(iniParseInt(v, slPort) == INI_NUM_OK && slPort >= 1 && slPort <= 65535) c.uwLobbyPort = (uint16_t)slPort;
		}
	}
}

uint32_t netConfigSave(const NetConfig &c, const char *pOld, uint32_t ulOldLen, char *pOut, uint32_t ulCap) {
	Writer w = {pOut, ulCap, 0, true};
	bool bFound[kKeys] = {false, false, false, false};
	bool bHasNet = false;
	{   // pass 1: which of the three keys the old [net] section has
		LineIter it = {pOld, ulOldLen};
		const char *pLine;
		uint32_t ulLen;
		bool bNet = false;
		int8_t sbKey;
		while(it.next(pLine, ulLen)) {
			const LineKind k = classify(pLine, ulLen, bNet, sbKey);
			if(k == LK_NET_HEADER) bHasNet = true;
			if(k == LK_KEY) bFound[sbKey] = true;
		}
	}
	{   // pass 2: copy, replace the key lines, add the missing ones behind the header
		LineIter it = {pOld, ulOldLen};
		const char *pLine;
		uint32_t ulLen;
		bool bNet = false;
		int8_t sbKey;
		bool bAdded = false;
		while(it.next(pLine, ulLen)) {
			const LineKind k = classify(pLine, ulLen, bNet, sbKey);
			if(k == LK_KEY) {
				emitPair(w, c, sbKey);
				continue;
			}
			w.put(pLine, ulLen);
			w.put('\n');
			if(k == LK_NET_HEADER && !bAdded) {
				bAdded = true;
				for(int8_t i = 0; i < kKeys; ++i) if(!bFound[i]) emitPair(w, c, i);
			}
		}
		if(!bHasNet) {
			w.put("[net]\n");
			for(int8_t i = 0; i < kKeys; ++i) emitPair(w, c, i);
		}
	}
	return w.bOk ? w.ulLen : 0;
}

bool netParsePort(const char *szPort, uint16_t &uwPort) {
	if(*szPort == 0) { uwPort = NET_DEFAULT_PORT; return true; }
	uint32_t ulValue = 0;
	for(const char *p = szPort; *p; ++p) {
		if(!isDigit((uint8_t)*p)) return false;
		ulValue = ulValue * 10 + (uint32_t)(*p - '0');
		if(ulValue > 65535) return false;
	}
	if(ulValue == 0) return false;
	uwPort = (uint16_t)ulValue;
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// Texts, rows and filters

// The two lines of a failed rejoin by reason (engine/netrejoin NetRejoinFail: 1 no seat, 2 bad token, 3 other build or rules, 4 host gone, 5 refused, 6 protocol).
static const char *rejoinFailLine1(uint8_t ubReason) {
	switch(ubReason) {
	case 1: return "NO SEAT TO REJOIN";
	case 2: return "REJOIN REFUSED";
	case 3: return "OTHER VERSION";
	case 4: return "HOST NOT ANSWERING";
	case 5:
	case 6: return "REJOIN FAILED";
	default: return "REJOIN FAILED";
	}
}

static const char *rejoinFailLine2(uint8_t ubReason) {
	switch(ubReason) {
	case 1: return "THE GAME HAS NONE FREE";
	case 2: return "THIS IS NOT YOUR GAME";
	case 3: return "OR OTHER RULES";
	case 5: return "THE HOST PLAYS ON";
	default: return "";
	}
}

const char *netStatusText(NetStatus e, uint8_t ubReason) {
	switch(e) {
	case NETST_REJOIN_ASKING: return "ASKING THE HOST";
	case NETST_REJOIN_WAIT: return "WAITING FOR THE";
	case NETST_REJOIN_FAILED: return rejoinFailLine1(ubReason);
	case NETST_CONNECTING: return "CONNECTING";
	case NETST_CONNECTED: return "CONNECTED";
	case NETST_NO_STACK: return "NO TCP IP STACK";        // not "TCP/IP": the font draws '/' as the caret glyph
	case NETST_NO_ANSWER: return "NO ANSWER";
	case NETST_WRONG_VERSION: return "WRONG VERSION";
	case NETST_NEED_ADDRESS: return "ENTER AN ADDRESS";
	case NETST_BAD_PORT: return "BAD PORT";
	case NETST_GAME_FULL: return "GAME FULL";
	default: return "";
	}
}

const char *netStatusText2(NetStatus e, uint8_t ubReason) {
	if(e == NETST_REJOIN_WAIT) return "NEXT TURN";
	if(e == NETST_REJOIN_FAILED) return rejoinFailLine2(ubReason);
	return e == NETST_CONNECTED ? "WAITING FOR HOST" : "";
}

const char *netLobbyText(uint8_t ubLobbyStatus) {
	switch(ubLobbyStatus) {
	case NLS_PRIVATE: return "PRIVATE GAME";
	case NLS_CONTACTING: return "CONTACTING LOBBY";
	case NLS_LISTED: return "LISTED ON THE LOBBY";
	case NLS_LISTED_RELAY: return "LISTED VIA RELAY";
	case NLS_UNREACHABLE: return "LOBBY UNREACHABLE";
	case NLS_REFUSED_NAME: return "NAME REFUSED BY LOBBY";
	case NLS_ALREADY_LISTED: return "ALREADY LISTED";
	case NLS_SERVER_FULL: return "LOBBY FULL";
	default: return "";
	}
}

bool netRowIsField(NetRow eRow) {
	return eRow == NETROW_NAME || eRow == NETROW_ADDRESS || eRow == NETROW_PORT;
}

bool netFieldAccepts(NetRow eRow, uint8_t ubChar) {
	switch(eRow) {
	case NETROW_NAME: return isLetter(ubChar) || isDigit(ubChar) || ubChar == ' ' || ubChar == '.';
	case NETROW_ADDRESS: return isLetter(ubChar) || isDigit(ubChar) || ubChar == '.';
	case NETROW_PORT: return isDigit(ubChar);
	default: return false;
	}
}

uint8_t netPageRows(NetPage ePage, NetRow *pOut, bool bRejoin) {
	uint8_t n = 0;
	if(ePage == NETPAGE_MAIN) {
		pOut[n++] = NETROW_HOST; pOut[n++] = NETROW_JOIN; pOut[n++] = NETROW_FIND;
		if(bRejoin) pOut[n++] = NETROW_REJOIN;
		pOut[n++] = NETROW_NAME; pOut[n++] = NETROW_PUBLIC; pOut[n++] = NETROW_MAIN_BACK;
	} else {
		pOut[n++] = NETROW_ADDRESS; pOut[n++] = NETROW_PORT; pOut[n++] = NETROW_CONNECT; pOut[n++] = NETROW_JOIN_BACK;
	}
	return n;
}

uint16_t netRowY(NetRow eRow, bool bRejoin) {
	static const uint16_t kY[NETROW_COUNT] = {64, 81, 115, 149,   72, 92, 118, 140,   132, 98, 98};   // main page: six rows 17 apart (HOST JOIN FIND NAME PUBLIC BACK), BACK ends above the status lines
	// with the REJOIN row the main page has seven rows, 14 apart (HOST JOIN FIND REJOIN NAME PUBLIC BACK): BACK still ends above the status lines
	static const uint16_t kYRejoin[NETROW_COUNT] = {58, 72, 114, 142,   72, 92, 118, 140,   128, 86, 100};
	if(bRejoin && (eRow == NETROW_HOST || eRow == NETROW_JOIN || eRow == NETROW_NAME || eRow == NETROW_MAIN_BACK || eRow == NETROW_PUBLIC || eRow == NETROW_FIND ||
		eRow == NETROW_REJOIN)) {
		return kYRejoin[eRow];
	}
	return kY[eRow < NETROW_COUNT ? eRow : 0];
}

uint16_t netValueX(NetRow eRow) {
	return eRow == NETROW_ADDRESS ? 192 : 160;
}

uint16_t netValueMaxPx(NetRow eRow) {
	return (uint16_t)(320 - netValueX(eRow) - 8 - 6);      // the screen's edge, the caret, a margin
}

uint16_t netGlyphWidth(char c) {
	if(isDigit((uint8_t)c)) return 7;
	if(c == '.' || c == ',') return 4;
	if(c == ' ') return 8;
	return 12;
}

uint16_t netVisibleStart(const char *szText, uint16_t uwMaxPx) {
	const uint16_t n = strLen(szText);
	uint16_t uwStart = n;
	uint16_t uwWidth = 0;
	while(uwStart > 0) {
		const uint16_t w = netGlyphWidth(szText[uwStart - 1]);
		if(uwWidth + w > uwMaxPx) break;
		uwWidth = (uint16_t)(uwWidth + w);
		--uwStart;
	}
	return uwStart;
}

// ---------------------------------------------------------------------------------------------------------
// NetMenu

namespace {
NetResult stronger(NetResult a, NetResult b) { return a > b ? a : b; }
}

void NetMenu::init(const NetConfig &c, NetRow eStart) {
	cfg = c;
	ePage = NETPAGE_MAIN;
	eRow = eStart;
	eStatus = NETST_NONE;
	poll.eStatus = NETST_NONE;
	poll.bSeated = false;
	poll.bRejoined = false;
	poll.ubReason = 0;
	bRejoin = false;
	szRejoinHost[0] = 0;
	bLinkActive = false;
	bHorizPrev = false;
	bFirePrev = true;
	bDirty = false;
}

char *NetMenu::field(NetRow eWhich) {
	switch(eWhich) {
	case NETROW_NAME: return cfg.szName;
	case NETROW_ADDRESS: return cfg.szAddr;
	case NETROW_PORT: return cfg.szPort;
	default: return nullptr;
	}
}

uint8_t NetMenu::fieldMax(NetRow eWhich) const {
	return eWhich == NETROW_NAME ? NET_NAME_MAX : (eWhich == NETROW_ADDRESS ? NET_ADDR_MAX : NET_PORT_MAX);
}

bool NetMenu::typeChar(uint8_t ubChar) {
	char *pText = field(eRow);
	if(!pText || !netFieldAccepts(eRow, ubChar)) return false;
	const uint16_t n = strLen(pText);
	if(n >= fieldMax(eRow)) return false;
	if(ubChar == ' ' && n == 0) return false;               // no leading space in a name
	pText[n] = (char)ubChar;
	pText[n + 1] = 0;
	bDirty = true;
	return true;
}

void NetMenu::togglePublic() {
	cfg.bPublic = !cfg.bPublic;
	bDirty = true;
}

bool NetMenu::erase() {
	char *pText = field(eRow);
	if(!pText) return false;
	const uint16_t n = strLen(pText);
	if(n == 0) return false;
	pText[n - 1] = 0;
	bDirty = true;
	return true;
}

// A name never ends in spaces and is never empty (the default "PLAYER").
void NetMenu::commitName() {
	trimTrailingSpaces(cfg.szName);
	if(cfg.szName[0] == 0) strCopy(cfg.szName, NET_NAME_MAX, "PLAYER", 6);
}

NetResult NetMenu::move(int8_t sbDelta) {
	NetRow aRows[NET_PAGE_ROWS_MAX];
	const uint8_t n = netPageRows(ePage, aRows, bRejoin);
	uint8_t at = 0;
	while(at < n && aRows[at] != eRow) ++at;
	if(at == n) at = n - 1;
	int16_t swNext = (int16_t)at + sbDelta;
	if(swNext < 0) swNext = 0;
	if(swNext > (int16_t)n - 1) swNext = (int16_t)n - 1;
	if(netRowIsField(eRow) && aRows[swNext] != eRow) commitName();
	const bool bMoved = aRows[swNext] != eRow;
	eRow = aRows[swNext];
	return bMoved ? NET_REDRAW : NET_IDLE;
}

void NetMenu::enterPage(NetPage ePageNew) {
	ePage = ePageNew;
	NetRow aRows[NET_PAGE_ROWS_MAX];
	netPageRows(ePage, aRows, bRejoin);
	eRow = aRows[0];
}

NetResult NetMenu::back(const NetSeam &seam) {
	if(ePage == NETPAGE_MAIN) return NET_LEAVE;
	close(seam);
	eStatus = NETST_NONE;
	enterPage(NETPAGE_MAIN);
	eRow = NETROW_JOIN;                                             // the cursor returns to the button it came from
	return NET_REDRAW;
}

void NetMenu::close(const NetSeam &seam) {
	if(bLinkActive) {
		seam.pfnClose();
		bLinkActive = false;
	}
}

NetResult NetMenu::activate(const NetSeam &seam) {
	commitName();
	uint16_t uwPort = NET_DEFAULT_PORT;
	switch(eRow) {
	case NETROW_HOST: {
		if(bLinkActive) return NET_IDLE;                       // a listen is open already: a second press opens nothing
		if(!netParsePort(cfg.szPort, uwPort)) uwPort = NET_DEFAULT_PORT;   // the port field lives on the join page
		if(seam.pfnHostOptions) seam.pfnHostOptions(cfg.bPublic, cfg.szLobby, cfg.uwLobbyPort);
		const bool bOk = seam.pfnHostStart(uwPort, cfg.szName);
		bLinkActive = bOk;
		eStatus = bOk ? NETST_LISTENING : NETST_NO_STACK;
		poll.eStatus = eStatus;
		poll.bSeated = false;
		return bOk ? NET_LOBBY_HOST : NET_REDRAW;              // straight into the lobby (the knight screen); no stack: the message stays here
	}
	case NETROW_PUBLIC:
		togglePublic();
		return NET_REDRAW;
	case NETROW_JOIN:
		eStatus = NETST_NONE;
		enterPage(NETPAGE_JOIN);
		return NET_REDRAW;
	case NETROW_FIND:
		return NET_FIND;                                       // the game browser is a screen of its own (game/net_find); this one waits underneath
	case NETROW_REJOIN:
		if(!bRejoin || !seam.pfnRejoinStart) return NET_IDLE;
		if(bLinkActive && (eStatus == NETST_CONNECTING || eStatus == NETST_REJOIN_ASKING || eStatus == NETST_REJOIN_WAIT)) return NET_IDLE;   // under way
		close(seam);                                           // a retry after a failure drops the old attempt first
		seam.pfnRejoinStart(cfg.szName);
		bLinkActive = true;
		eStatus = NETST_CONNECTING;
		poll.eStatus = NETST_CONNECTING;
		poll.bSeated = false;
		poll.bRejoined = false;
		poll.ubReason = 0;
		return NET_REDRAW;
	case NETROW_CONNECT:
		if(cfg.szAddr[0] == 0) { eStatus = NETST_NEED_ADDRESS; return NET_REDRAW; }
		if(!netParsePort(cfg.szPort, uwPort)) { eStatus = NETST_BAD_PORT; return NET_REDRAW; }
		if(bLinkActive && (eStatus == NETST_CONNECTING || eStatus == NETST_CONNECTED)) return NET_IDLE;   // under way: a second press must not open a second connection
		close(seam);                                           // a retry after a failure drops the old attempt first
		seam.pfnJoinStart(cfg.szAddr, uwPort, cfg.szName);
		bLinkActive = true;
		eStatus = NETST_CONNECTING;
		poll.eStatus = NETST_CONNECTING;
		poll.bSeated = false;
		return NET_REDRAW;
	case NETROW_MAIN_BACK:
	case NETROW_JOIN_BACK:
		return back(seam);
	default:
		return NET_IDLE;
	}
}

// An outside button press (autoplay `poke netaction`): moves to the button's page and row, then activates it like fire.  Not on its page: ignored.
NetResult NetMenu::action(uint8_t ubAction, const NetSeam &seam) {
	NetResult r = NET_IDLE;
	switch(ubAction) {
	case NETACT_HOST:
		if(ePage != NETPAGE_MAIN) return NET_IDLE;
		eRow = NETROW_HOST;
		return activate(seam);
	case NETACT_FIND:
		if(ePage != NETPAGE_MAIN) return NET_IDLE;
		eRow = NETROW_FIND;
		return activate(seam);
	case NETACT_REJOIN:
		if(ePage != NETPAGE_MAIN || !bRejoin) return NET_IDLE;
		eRow = NETROW_REJOIN;
		return activate(seam);
	case NETACT_CONNECT:
		if(ePage == NETPAGE_MAIN) {
			eRow = NETROW_JOIN;
			r = activate(seam);                                // to the join page
		}
		if(ePage != NETPAGE_JOIN) return r;
		eRow = NETROW_CONNECT;
		return stronger(r, activate(seam));
	default:
		return NET_IDLE;
	}
}

// The transport's progress while a link is open.
NetResult NetMenu::pollLink(const NetSeam &seam) {
	if(!bLinkActive) return NET_IDLE;
	NetPoll p;
	p.eStatus = NETST_NONE;
	p.bSeated = false;
	p.bRejoined = false;
	p.ubReason = 0;
	seam.pfnPoll(p);
	const bool bChanged = p.eStatus != eStatus || p.ubReason != poll.ubReason;
	poll = p;
	eStatus = p.eStatus;
	if(p.bRejoined) return NET_REJOIN;                         // the world is here: the screen leaves for the game (the link stays with it)
	if(eStatus == NETST_REJOIN_FAILED) {
		close(seam);                                           // the refusal's link is closed; the message stays
	}
	if(ePage == NETPAGE_JOIN && eStatus == NETST_CONNECTED && p.bSeated) return NET_LOBBY_CLIENT;   // the host accepted us: to the lobby
	if(eStatus == NETST_NO_STACK || eStatus == NETST_NO_ANSWER || eStatus == NETST_WRONG_VERSION || eStatus == NETST_GAME_FULL) {
		close(seam);                                           // a failed link releases its socket; the page stays with the message
	}
	return bChanged ? NET_REDRAW : NET_IDLE;
}

NetResult NetMenu::update(const NetInput &in, const NetSeam &seam) {
	NetResult rRejoin = NET_IDLE;
	{   // the REJOIN row appears while a dropped game is remembered
		char acHost[NET_NAME_LEN];
		const bool bNow = seam.pfnRejoinInfo != nullptr && seam.pfnRejoinInfo(acHost);
		if(bNow != bRejoin) {
			bRejoin = bNow;
			if(bNow) {
				uint8_t i = 0;
				for(; i < NET_NAME_LEN - 1 && acHost[i]; ++i) szRejoinHost[i] = acHost[i];
				szRejoinHost[i] = 0;
			} else if(eRow == NETROW_REJOIN) {
				eRow = NETROW_FIND;
			}
			rRejoin = NET_REDRAW;
		}
	}
	const bool bFireDown = (in.uwJoy & 0x10) != 0;
	const bool bFire = bFireDown && !bFirePrev;
	bFirePrev = bFireDown;

	NetResult r = stronger(rRejoin, pollLink(seam));
	if(r == NET_LOBBY_CLIENT || r == NET_REJOIN) return r;

	if(in.ubAction) return stronger(r, action(in.ubAction, seam));
	if(in.uwKey == NETKEY_ESC) return stronger(r, back(seam));
	if(in.uwKey == NETKEY_RETURN) return stronger(r, netRowIsField(eRow) ? move(1) : activate(seam));
	if(in.uwKey == NETKEY_BACKSPACE && netRowIsField(eRow)) {
		if(erase()) r = stronger(r, NET_REDRAW);
	} else if(in.ubChar && netRowIsField(eRow)) {
		if(typeChar(in.ubChar)) r = stronger(r, NET_REDRAW);
	}
	const bool bHoriz = (in.uwJoy & 3) != 0;                      // right | left
	const bool bHorizEdge = bHoriz && !bHorizPrev;
	bHorizPrev = bHoriz;
	if(bHorizEdge && eRow == NETROW_PUBLIC) {
		togglePublic();
		r = stronger(r, NET_REDRAW);
	}
	if(bFire) return stronger(r, netRowIsField(eRow) ? move(1) : activate(seam));
	if(in.uwJoy & 8) return stronger(r, move(-1));
	if(in.uwJoy & 4) return stronger(r, move(1));
	return r;
}

// ---------------------------------------------------------------------------------------------------------
// SceneNet

void SceneNet::drawRowText(const MenuOps &ops, const char *szText, uint16_t uwX, uint16_t uwY, uint16_t uwStyle) {
	if(szText[0]) ops.pfnDrawString((const uint8_t *)szText, uwX, uwY, uwStyle);
}

void SceneNet::drawStatus(const MenuOps &ops, uint16_t uwY) {
	drawRowText(ops, netStatusText(menu.eStatus, menu.poll.ubReason), 0, uwY, 1);
	drawRowText(ops, netStatusText2(menu.eStatus, menu.poll.ubReason), 0, (uint16_t)(uwY + NET_STATUS_STEP), 1);
}

// "LABEL value": the value is the tail of the text that fits (netVisibleStart), a caret behind it on the row under the cursor.
void SceneNet::drawField(const MenuOps &ops, NetRow eRow, const char *szLabel) {
	const uint16_t y = netRowY(eRow, menu.bRejoin);
	drawRowText(ops, szLabel, NET_LABEL_X, y, 0);
	const char *pText = menu.field(eRow);
	const char *pShown = pText + netVisibleStart(pText, netValueMaxPx(eRow));
	char szLine[NET_ADDR_MAX + 2];
	uint16_t i = 0;
	for(const char *q = pShown; *q && i < NET_ADDR_MAX; ++q) szLine[i++] = *q;
	if(menu.eRow == eRow) szLine[i++] = (char)kCaretChar;
	szLine[i] = 0;
	if(i) drawRowText(ops, szLine, netValueX(eRow), y, 0);
}

void SceneNet::draw(const MenuOps &ops, bool bWait) {
	ops.pfnSetPlanes();
	ops.pfnBlitBackground();
	ops.pfnClearHits();
	ops.pfnSetTextFlag(1);
	ops.pfnDrawSprite(MENUBANK_BACKDROP, 0x49, 5, 0x0A);                       // the title banner, as the title menu
	ops.pfnDrawSprite(MENUBANK_SPRITES, 0, NET_CURSOR_X, (uint16_t)(netRowY(menu.eRow, menu.bRejoin) + 2));   // the arrow
	ops.pfnSetTextFlag(0);
	switch(menu.ePage) {
	case NETPAGE_MAIN:
		drawRowText(ops, "HOST GAME", 0, netRowY(NETROW_HOST, menu.bRejoin), 1);
		drawRowText(ops, "JOIN GAME", 0, netRowY(NETROW_JOIN, menu.bRejoin), 1);
		drawRowText(ops, "FIND GAME", 0, netRowY(NETROW_FIND, menu.bRejoin), 1);
		if(menu.bRejoin) drawRowText(ops, "REJOIN GAME", 0, netRowY(NETROW_REJOIN, true), 1);
		drawField(ops, NETROW_NAME, "NAME");
		drawRowText(ops, "PUBLIC", NET_LABEL_X, netRowY(NETROW_PUBLIC, menu.bRejoin), 0);
		drawRowText(ops, menu.cfg.bPublic ? "YES" : "NO", NET_PUBLIC_VALUE_X, netRowY(NETROW_PUBLIC, menu.bRejoin), 0);
		drawRowText(ops, "BACK", 0, netRowY(NETROW_MAIN_BACK, menu.bRejoin), 1);
		drawStatus(ops, NET_STATUS_Y_JOIN);
		break;
	default:
		drawField(ops, NETROW_ADDRESS, "ADDRESS");
		drawField(ops, NETROW_PORT, "PORT");
		drawRowText(ops, "CONNECT", 0, netRowY(NETROW_CONNECT), 1);
		drawRowText(ops, "BACK", 0, netRowY(NETROW_JOIN_BACK), 1);
		drawStatus(ops, NET_STATUS_Y_JOIN);
		break;
	}
	ops.pfnFlip();
	if(bWait) ops.pfnWait(10);                                                // the title menu's pace (also the repeat rate of the stick); not while typing, as the name editor
}

void SceneNet::enter(const MenuOps &ops, const NetConfig &c, NetRow eStart) {
	eResult = NET_IDLE;
	menu.init(c, eStart);
	ops.pfnIrqOn();
	ops.pfnClearHits();
	ops.pfnKeyReset();                                                        // no stale key from the title menu
	ops.pfnFlip();
	draw(ops, true);
	ops.pfnPalette(MENUPAL_TITLE);
}

bool SceneNet::update(const MenuOps &ops, const NetSeam &seam) {
	NetInput in;
	in.uwJoy = ops.pfnReadJoy();
	in.uwKey = ops.pfnKeyWord();
	in.ubChar = 0;
	in.ubAction = seam.pfnTakeAction ? seam.pfnTakeAction() : 0;
	if(in.uwKey) {
		ops.pfnClearKeyWord();
		if(in.uwKey < kKeyTableSize) {
			const uint8_t ubChar = ops.pfnTranslateKey(in.uwKey);
			if(ubChar >= 0x20 && ubChar < 0x7F) in.ubChar = ubChar;           // printable only (the table holds 1 for the dead keys)
		}
	}
	const NetResult r = menu.update(in, seam);
	const bool bTyping = in.ubChar != 0 || in.uwKey == NETKEY_BACKSPACE;       // keys arrive faster than a redraw with its wait: no wait for them
	if(r == NET_REDRAW) draw(ops, !bTyping);
	if(r == NET_LEAVE || r == NET_LOBBY_HOST || r == NET_LOBBY_CLIENT || r == NET_FIND || r == NET_REJOIN) {
		eResult = r;
		return true;
	}
	return false;
}

void SceneNet::exit(const MenuOps &ops, const NetSeam &seam) {
	if(eResult != NET_LOBBY_HOST && eResult != NET_LOBBY_CLIENT && eResult != NET_REJOIN) menu.close(seam);   // the lobby / the game keeps the link

	ops.pfnFadeOut();
}

}}  // namespace ms::game
