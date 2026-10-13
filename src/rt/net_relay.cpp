// rt/net_relay - see net_relay.hpp.  ROADMAP 12.13a.
#include "rt/net_relay.hpp"

#include "rt/logwrite.hpp"
#include "rt/net_lobby.hpp"

namespace rt {

namespace {

ms::NetRelayHost s_relay;
bool s_isInit;
uint8_t s_ubLoggedPhase = 0xFF;

void ensureInit() {
	if(!s_isInit) {
		s_isInit = true;
		ms::netRelayHostInit(s_relay);
	}
}

// ---- the idle connection as the state machine's connection -------------------------------------------------------------------

bool ioBegin(void *) {
	NetTransport *pT = netTransport();
	return pT && pT->relayBegin(lobbyPortNumber());       // the address lobbyBegin resolved: the listing was made there
}

int32_t ioState(void *) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->relayState()) : -1;
}

int32_t ioSend(void *, const uint8_t *pData, uint32_t ulLen) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->relaySend(pData, ulLen)) : -1;
}

int32_t ioRecv(void *, uint8_t *pBuf, uint32_t ulCap) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->relayRecv(pBuf, ulCap)) : -1;
}

void ioClose(void *) {
	NetTransport *pT = netTransport();
	if(pT) {
		pT->relayClose();
	}
}

int32_t ioHandOver(void *, const uint8_t *pFirst, uint32_t ulLen) {
	NetTransport *pT = netTransport();
	return pT ? static_cast<int32_t>(pT->relayHandOver(pFirst, ulLen)) : -1;
}

const ms::NetRelayIo s_io = {nullptr, ioBegin, ioState, ioSend, ioRecv, ioClose, ioHandOver};

void logPhase() {
	if(s_relay.ubPhase != s_ubLoggedPhase) {
		s_ubLoggedPhase = s_relay.ubPhase;
		logWrite("net: relay state %u session %08lX pairs %lu fails %lu\n", static_cast<unsigned>(s_relay.ubPhase),
			static_cast<unsigned long>(s_relay.ulSession), static_cast<unsigned long>(s_relay.ulPairs), static_cast<unsigned long>(s_relay.ulFails));
	}
}

}  // namespace

int32_t relayHostPump(NetTransport &t, uint32_t ulSession) {
	(void)t;                                       // the relay socket is the transport's (netTransport()); the caller is inside its window
	ensureInit();
	if(ulSession == 0 && s_relay.ubPhase == ms::NR_OFF) {
		return -1;                                 // a direct listing (or none yet): nothing to do, nothing to read
	}
	const uint32_t ulNow = netClockMs();
	netSub(static_cast<UBYTE>(NETSUB_RELAY_PHASE + s_relay.ubPhase));
	ms::netRelayHostSession(s_relay, s_io, ulSession, ulNow);
	const int32_t lLink = ms::netRelayHostStep(s_relay, s_io, ulNow);
	netSub(NETSUB_NONE);
	logPhase();
	if(lLink >= 0) {
		logWrite("net: relay client arrived on link %ld\n", static_cast<long>(lLink));
	}
	return lLink;
}

void relayHostEnd(NetTransport &t) {
	(void)t;
	if(s_isInit && s_relay.ubPhase != ms::NR_OFF) {
		ms::netRelayHostEnd(s_relay, s_io);
		logWrite("net: relay idle connection closed\n");
	}
	s_ubLoggedPhase = 0xFF;
}

bool relayHostIsWaiting() {
	return s_isInit && ms::netRelayHostIsWaiting(s_relay);
}

}  // namespace rt
