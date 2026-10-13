// rt/net_stub - see rt/net_stub.hpp.  The seams of the Multiplayer screen and of the lobby: the real session (rt/net_session) unless an autoplay hook
// (`poke netfake 1`) switched the fake network on (tests/boot/play_netmenu.txt).
#include "rt/net_stub.hpp"

#include "engine/netroom.hpp"
#include "rt/net_find.hpp"
#include "rt/net_session.hpp"

#include "rt/logwrite.hpp"

namespace rt {

namespace {

using namespace ms::game;

enum Mode : uint8_t { MODE_NONE = 0, MODE_HOST, MODE_CLIENT };

// Fake network state: written by the autoplay poke hooks (VBL interrupt), read by the screens' loops.
volatile uint8_t s_ubFake;       // a fake stack is present
volatile uint8_t s_ubPeers;      // fake host: joined clients (each has picked: BOB portrait 3, CAROL 2, DAVE 1)
volatile uint8_t s_ubJoin;       // fake join outcome: 0 connected, 1 no answer, 2 wrong version, else connecting
volatile uint8_t s_ubSeated;     // fake join: the host seated us (the lobby opens)
volatile uint8_t s_ubStarted;    // fake lobby (client): the host's start message
uint8_t s_ubMode;
ms::NetRoom s_room;              // the fake room
uint8_t s_ubFakePeers;           // peers already in s_room
bool s_isFakeStarted;
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
volatile uint8_t s_ubAction;     // `poke netaction N`: a NetAction for the Multiplayer screen / the lobby, taken by the loop
#endif

const char *const kFakePeers[NET_PEERS_MAX] = {"BOB", "CAROL", "DAVE"};

// PUBLIC yes / no and the lobby address of net.ini, right before the host starts (ROADMAP 12.18).
void stubHostOptions(bool bPublic, const char *szLobby, uint16_t uwLobbyPort) {
	if(s_ubFake) return;
	netSessionHostOptions(bPublic, szLobby, uwLobbyPort);
}

bool stubHostStart(uint16_t uwPort, const char *szName) {
	if(!s_ubFake) return netSessionHostStart(uwPort, szName);
	logWrite("net: host port %u name %s%s\n", (unsigned)uwPort, szName, s_ubFake ? " (fake stack)" : " (no stack)");
	s_ubMode = s_ubFake ? MODE_HOST : MODE_NONE;
	ms::roomInit(s_room, szName);
	s_ubFakePeers = 0;
	s_isFakeStarted = false;
	return s_ubFake != 0;
}

void stubJoinStart(const char *szAddr, uint16_t uwPort, const char *szName) {
	if(!s_ubFake) { netSessionJoinStart(szAddr, uwPort, szName); return; }
	logWrite("net: join %s port %u name %s%s\n", szAddr, (unsigned)uwPort, szName, s_ubFake ? " (fake stack)" : " (no stack)");
	s_ubMode = MODE_CLIENT;
	ms::roomInit(s_room, "HOSTY");                 // the fake host sits in seat 0 ...
	ms::NetLobbyPick sPick;
	sPick.ubStage = ms::NET_PICK_CLAIM; sPick.ubSel = 0; sPick.ubPad = 0xFF; sPick.acName[0] = 0;
	ms::roomApplyPick(s_room, 0, sPick);           // ... and has picked portrait 0
	ms::netNameSet(sPick.acName, "HOSTY"); sPick.ubStage = ms::NET_PICK_DONE; sPick.ubPad = 0;
	ms::roomApplyPick(s_room, 0, sPick);
	ms::roomJoin(s_room, 1, szName);               // we are seat 1
}

void stubPoll(NetPoll &out) {
	if(!s_ubFake) { netSessionPoll(out); return; }
	out.eStatus = NETST_NO_STACK;
	out.bSeated = false;
	if(s_ubMode == MODE_HOST) {
		out.eStatus = NETST_LISTENING;
	} else if(s_ubMode == MODE_CLIENT) {
		switch(s_ubJoin) {
		case 0: out.eStatus = NETST_CONNECTED; out.bSeated = s_ubSeated != 0; break;
		case 1: out.eStatus = NETST_NO_ANSWER; break;
		case 2: out.eStatus = NETST_WRONG_VERSION; break;
		default: out.eStatus = NETST_CONNECTING; break;
		}
	}
}

void stubClose() {
	netSessionClose();   // always: a real attempt that began before `poke netfake 1` must not outlive its screen (a stale node made the fake start wait for a session)
	if(!s_ubFake) return;
	logWrite("net: close\n");
	s_ubMode = MODE_NONE;
}

// ---- the lobby ------------------------------------------------------------------------------------------------------------------------

// The fake peers that have joined (and picked) by now: BOB takes portrait 3, CAROL 2, DAVE 1.
void fakeJoinPeers() {
	uint8_t ubPeers = s_ubPeers;
	if(ubPeers > NET_PEERS_MAX) ubPeers = NET_PEERS_MAX;
	while(s_ubFakePeers < ubPeers) {
		const uint8_t ubSeat = static_cast<uint8_t>(1 + s_ubFakePeers);
		ms::roomJoin(s_room, ubSeat, kFakePeers[s_ubFakePeers]);
		ms::NetLobbyPick p;
		p.ubSel = static_cast<uint8_t>(3 - s_ubFakePeers);
		p.ubStage = ms::NET_PICK_CLAIM; p.ubPad = 0xFF; p.acName[0] = 0;
		ms::roomApplyPick(s_room, ubSeat, p);
		p.ubStage = ms::NET_PICK_DONE; p.ubPad = 0;
		ms::netNameSet(p.acName, kFakePeers[s_ubFakePeers]);
		ms::roomApplyPick(s_room, ubSeat, p);
		++s_ubFakePeers;
	}
	while(s_ubFakePeers > ubPeers) {               // `poke netpeers N` with a smaller N: the last ones leave
		--s_ubFakePeers;
		ms::roomLeave(s_room, static_cast<uint8_t>(1 + s_ubFakePeers));
	}
}

void lobbyPoll(LobbyView &out) {
	if(!s_ubFake) { netSessionLobbyPoll(out); return; }
	out.isHost = s_ubMode == MODE_HOST;
	out.isStarted = !out.isHost && s_ubStarted != 0;
	out.isLost = s_ubMode == MODE_NONE;
	out.ubListing = ms::NLS_PRIVATE;
	if(out.isHost) {
		fakeJoinPeers();
	}
	out.room = s_room;
	out.ubSeat = out.isHost ? 0 : 1;
	out.ulRev = static_cast<uint32_t>(s_ubFakePeers) + 16UL * s_room.ubMode;
	for(uint8_t i = 0; i < ms::ROOM_SEATS; ++i) {   // any change of a seat: a new revision
		out.ulRev = out.ulRev * 31 + s_room.aSeat[i].ubState * 7 + s_room.aSeat[i].ubSel * 3 + s_room.aSeat[i].ubPad;
	}
}

void lobbyPick(const ms::NetLobbyPick &p) {
	if(!s_ubFake) { netSessionLobbyPick(p); return; }
	ms::roomApplyPick(s_room, s_ubMode == MODE_HOST ? 0 : 1, p);
}

void lobbyMode(uint8_t ubMode) {
	if(!s_ubFake) { netSessionLobbyMode(ubMode); return; }
	ms::roomSetMode(s_room, ubMode);
}

bool lobbyStart() {
	if(!s_ubFake) return netSessionLobbyStart();
	logWrite("net: start (%u clients)\n", (unsigned)s_ubFakePeers);
	s_isFakeStarted = s_ubMode == MODE_HOST && ms::roomStartEnabled(s_room);
	return s_isFakeStarted;
}

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
uint8_t stubTakeAction() {
	const uint8_t ubAction = s_ubAction;
	s_ubAction = 0;
	return ubAction;
}
#define MS_TAKE_ACTION stubTakeAction
#else
#define MS_TAKE_ACTION nullptr
#endif

// The game browser's join through the lobby's relay (ROADMAP 12.13a): the real session, or - under the fake stack - the same fake connect as a typed address.
void stubJoinRelay(uint32_t ulSession, const char *szName) {
	if(!s_ubFake) { netSessionJoinRelay(ulSession, szName); return; }
	stubJoinStart("relay", 0, szName);
}

// REJOIN row (ROADMAP M12): the real session remembers a dropped game; the fake stack never has one.
bool stubRejoinInfo(char (&acHost)[ms::NET_NAME_LEN]) {
	return !s_ubFake && netSessionRejoinAvailable(acHost);
}

void stubRejoinStart(const char *szName) {
	if(!s_ubFake) netSessionRejoinStart(szName);
}

const NetSeam kStubSeam = {stubHostStart, stubJoinStart, stubPoll, stubClose, MS_TAKE_ACTION, stubHostOptions, stubJoinRelay, stubRejoinInfo, stubRejoinStart};
const LobbySeam kLobbySeam = {lobbyPoll, lobbyPick, lobbyMode, lobbyStart, stubClose, MS_TAKE_ACTION};
const FindSeam kFindSeam = {netFindList, netFindJoinReq, netFindPoll, netFindStop};

}  // namespace

const NetSeam &netSeam() {
	return kStubSeam;
}

const FindSeam &findSeam() {
	return kFindSeam;
}

const LobbySeam &lobbySeam() {
	return kLobbySeam;
}

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
namespace {
bool eqName(const char *a, const char *b) {
	for(; *a && *b; ++a, ++b) {
		const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
		if(ca != *b) return false;
	}
	return *a == *b;
}
}  // namespace

bool netFakePoke(const char *szName, ULONG ulValue) {
	if(eqName(szName, "netfake")) { s_ubFake = ulValue ? 1 : 0; return true; }
	if(eqName(szName, "netpeers")) {
		if(ulValue > NET_PEERS_MAX) return false;
		s_ubPeers = static_cast<uint8_t>(ulValue);
		return true;
	}
	if(eqName(szName, "netjoin")) {
		if(ulValue > 3) return false;
		s_ubJoin = static_cast<uint8_t>(ulValue);
		return true;
	}
	if(eqName(szName, "netaction")) {   // 1 host, 2 connect, 3 start, 4 find game, 5 rejoin: the Multiplayer screen's button / the lobby's START, through the same code path as fire on it
		if(ulValue > NETACT_REJOIN) return false;
		s_ubAction = static_cast<uint8_t>(ulValue);
		return true;
	}
	if(eqName(szName, "netseated")) { s_ubSeated = ulValue ? 1 : 0; return true; }      // the fake host accepts us: the join opens the lobby
	if(eqName(szName, "netstarted")) { s_ubStarted = ulValue ? 1 : 0; return true; }    // the fake host starts the game (client lobby)
	return false;
}
#endif

}  // namespace rt
