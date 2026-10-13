// rt/net_find - see net_find.hpp.  ROADMAP 12.18 (the game browser).
#include "rt/net_find.hpp"

#include "engine/netbrowse.hpp"
#include "engine/version.hpp"
#include "rt/logwrite.hpp"
#include "rt/net.hpp"
#include "rt/net_lobby.hpp"
#include "rt/net_session.hpp"

namespace rt {

namespace {

ms::NetBrowse s_browse;
bool s_isInit;
uint8_t s_ubLoggedStatus = 0xFF;

void ensureInit() {
	if(!s_isInit) {
		s_isInit = true;
		ms::netBrowseInit(s_browse, netSessionBuildHash(), ms::GAME_VERSION);
	}
}

// ---- the request's socket as the state machine's connection ------------------------------------------------------------------

int32_t ioBegin(void *) {
	NetTransport *pT = netTransport();
	if(!pT || !pT->hasStack()) {
		return ms::NB_BEGIN_NO_STACK;
	}
	return pT->findBegin(lobbyHostName(), lobbyPortNumber()) ? ms::NB_BEGIN_OK : ms::NB_BEGIN_NO_SERVER;
}

int32_t ioState(void *) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->findState()) : -1;
}

int32_t ioSend(void *, const uint8_t *pData, uint32_t ulLen) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->findSend(pData, ulLen)) : -1;
}

int32_t ioRecv(void *, uint8_t *pBuf, uint32_t ulCap) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->findRecv(pBuf, ulCap)) : -1;
}

void ioClose(void *) {
	NetTransport *pT = netTransport();
	if(pT) {
		pT->findClose();
	}
}

const ms::NetBrowseIo s_io = {nullptr, ioBegin, ioState, ioSend, ioRecv, ioClose};

void logStatus() {
	if(s_browse.ubStatus != s_ubLoggedStatus) {
		s_ubLoggedStatus = s_browse.ubStatus;
		logWrite("net: find status %u games %u\n", static_cast<unsigned>(s_browse.ubStatus), static_cast<unsigned>(s_browse.ubCount));   // the netpair scripts wait on `status 2 games 1`
	}
}

// One step of the machine inside an OS window.  Without a stack there is no window: the step runs outside (ioBegin answers "no stack" before it touches
// a socket) so that the screen says so.
void stepBrowse() {
	NetTransport *pT = netTransport();
	const uint32_t ulNow = netClockMs();
	if(!pT || !pT->window([ulNow]() { ms::netBrowseStep(s_browse, s_io, ulNow); })) {
		ms::netBrowseStep(s_browse, s_io, ulNow);
	}
	logStatus();
}

}  // namespace

void netFindList() {
	ensureInit();
	ms::netBrowseList(s_browse, netClockMs());
	logWrite("net: find asking %s port %u for the list\n", lobbyHostName(), static_cast<unsigned>(lobbyPortNumber()));
}

bool netFindJoinReq(uint8_t ubIndex) {
	ensureInit();
	if(!ms::netBrowseJoin(s_browse, ubIndex, netClockMs())) {
		return false;
	}
	logWrite("net: find joining game %u of the list\n", static_cast<unsigned>(ubIndex));
	return true;
}

void netFindPoll(ms::game::FindView &sOut) {
	ensureInit();
	if(ms::netBrowseIsDue(s_browse, netClockMs())) {
		stepBrowse();
	}
	sOut.ubStatus = s_browse.ubStatus;
	sOut.ubCount = s_browse.ubCount;
	for(uint8_t i = 0; i < s_browse.ubCount; ++i) {
		sOut.aEntries[i] = s_browse.aEntries[i];
	}
	sOut.join = s_browse.join;
	sOut.ulRev = s_browse.ulRev;
	sOut.ulMyBuild = s_browse.ulBuild;
	sOut.uwMyVersion = s_browse.uwVersion;
}

void netFindStop() {
	if(!s_isInit) {
		return;
	}
	NetTransport *pT = netTransport();
	if(ms::netBrowseIsBusy(s_browse) && pT && pT->hasStack()) {
		pT->window([]() { ms::netBrowseStop(s_browse, s_io); });
	} else {
		ms::netBrowseStop(s_browse, s_io);
	}
	s_ubLoggedStatus = 0xFF;
}

}  // namespace rt
