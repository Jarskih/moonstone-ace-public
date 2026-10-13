// rt/net_lobby - see net_lobby.hpp.  ROADMAP 12.18 (host half).
#include "rt/net_lobby.hpp"

#include "engine/lobby.hpp"
#include "engine/version.hpp"
#include "rt/logwrite.hpp"

namespace rt {

namespace {

struct Options {
	bool isPublic;
	char szHost[48];
	uint16_t uwPort;
};

Options s_opt;
ms::NetLobby s_lobby;
bool s_isInit;
uint8_t s_ubLoggedStatus = 0xFF;

void ensureInit() {
	if(!s_isInit) {
		s_isInit = true;
		ms::netLobbyInit(s_lobby);
	}
}

const char *lobbyHost() {
	return s_opt.szHost[0] ? s_opt.szHost : ms::LOBBY_DEFAULT_HOST;
}

uint16_t lobbyPort() {
	return s_opt.uwPort ? s_opt.uwPort : ms::LOBBY_DEFAULT_PORT;
}

// ---- the lobby socket as the state machine's connection ---------------------------------------------------------------------

bool ioBegin(void *, bool isMayResolve) {
	netSub(NETSUB_LOBBY_BEGIN);
	NetTransport *pT = netTransport();
	return pT && pT->lobbyBegin(lobbyHost(), lobbyPort(), isMayResolve);
}

int32_t ioState(void *) {
	netSub(NETSUB_LOBBY_STATE);
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->lobbyState()) : -1;
}

int32_t ioSend(void *, const uint8_t *pData, uint32_t ulLen) {
	netSub(NETSUB_LOBBY_IO);
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->lobbySend(pData, ulLen)) : -1;
}

int32_t ioRecv(void *, uint8_t *pBuf, uint32_t ulCap) {
	netSub(NETSUB_LOBBY_IO);
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->lobbyRecv(pBuf, ulCap)) : -1;
}

void ioClose(void *) {
	netSub(NETSUB_LOBBY_IO);
	NetTransport *pT = netTransport();
	if(pT) {
		pT->lobbyClose();
	}
}

const ms::NetLobbyIo s_io = {nullptr, ioBegin, ioState, ioSend, ioRecv, ioClose};

void logStatus() {
	const uint8_t ubStatus = static_cast<uint8_t>(ms::netLobbyStatus(s_lobby));
	if(ubStatus != s_ubLoggedStatus) {
		s_ubLoggedStatus = ubStatus;
		logWrite("net: lobby status %u (phase %u)\n", static_cast<unsigned>(ubStatus), static_cast<unsigned>(s_lobby.ubPhase));
	}
}

}  // namespace

void lobbySetOptions(bool isPublic, const char *szHost, uint16_t uwPort) {
	s_opt.isPublic = isPublic;
	uint8_t n = 0;
	if(szHost) {
		for(; szHost[n] && n + 1 < sizeof(s_opt.szHost); ++n) {
			s_opt.szHost[n] = szHost[n];
		}
	}
	s_opt.szHost[n] = 0;
	s_opt.uwPort = uwPort;
}

void lobbyHostBegin(const char *szGameName, uint16_t uwGamePort, uint32_t ulBuild, uint8_t ubMode) {
	ensureInit();
	if(s_lobby.ubPhase != ms::NL_OFF) {
		return;                                    // a second HOST press keeps the listing it has
	}
	if(!s_opt.isPublic) {
		s_lobby.ubStatus = ms::NLS_PRIVATE;
		return;
	}
	ms::NetListing sListing;
	ms::netNameSet(sListing.acName, szGameName);
	sListing.ubMode = ubMode;                      // the room's mode (the lobby's MODE line); lobbyMode() keeps it current
	sListing.ubPlayers = 1;
	sListing.ubMaxPlayers = ms::NET_NODE_MAX;
	sListing.ulBuild = ulBuild;
	sListing.uwPort = uwGamePort;
	sListing.uwVersion = ms::GAME_VERSION;
	ms::netLobbyOpen(s_lobby, sListing, netClockMs());
	logWrite("net: lobby: listing '%s' on %s port %u\n", szGameName, lobbyHost(), static_cast<unsigned>(lobbyPort()));
}

void lobbyPump(NetTransport &t) {
	if(s_lobby.ubPhase == ms::NL_OFF || s_lobby.ubPhase == ms::NL_DEAD) {
		return;
	}
	const uint32_t ulNow = netClockMs();
	if(!ms::netLobbyIsDue(s_lobby, ulNow)) {
		return;
	}
	(void)t;                                       // the lobby socket is the transport's (netTransport()); the caller is inside its window
	netSub(static_cast<UBYTE>(NETSUB_LOBBY_PHASE + s_lobby.ubPhase));
	ms::netLobbyStep(s_lobby, s_io, ulNow);
	logStatus();
}

void lobbyMode(uint8_t ubMode) {
	if(s_lobby.ubPhase != ms::NL_OFF) {
		ms::netLobbySetMode(s_lobby, ubMode);
	}
}

void lobbyPlayers(uint8_t ubPlayers) {
	if(s_lobby.ubPhase != ms::NL_OFF) {
		ms::netLobbySetPlayers(s_lobby, ubPlayers);
	}
}

void lobbyPlaying() {
	if(s_lobby.ubPhase != ms::NL_OFF) {
		ms::netLobbySetPlaying(s_lobby);
	}
}

void lobbyHostEnd(NetTransport &t) {
	(void)t;
	if(s_isInit && s_lobby.ubPhase != ms::NL_OFF) {
		ms::netLobbyEnd(s_lobby, s_io);
		logWrite("net: lobby: unlisted\n");
	}
	s_ubLoggedStatus = 0xFF;
}

const char *lobbyHostName() {
	return lobbyHost();
}

uint16_t lobbyPortNumber() {
	return lobbyPort();
}

uint32_t lobbyRelaySession() {
	if(!s_isInit || s_lobby.ubPhase == ms::NL_OFF || s_lobby.ubPhase == ms::NL_DEAD) {
		return 0;
	}
	return ms::netLobbyStatus(s_lobby) == ms::NLS_LISTED_RELAY ? s_lobby.ulSession : 0;
}

ms::NetLobbyStatus lobbyStatus() {
	if(!s_isInit) {
		return ms::NLS_PRIVATE;
	}
	return ms::netLobbyStatus(s_lobby);
}

}  // namespace rt
