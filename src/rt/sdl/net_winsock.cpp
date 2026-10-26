// rt/sdl/net_winsock - the network transport of the PC build (ROADMAP 12.14): rt/net.hpp over Winsock.
//
// The Amiga counterpart is src/rt/net.cpp (bsdsocket.library inside OS windows).  The classes and their contract are the same ones, so the
// session (rt/net_session.cpp), the lobby, the relay, the rejoin and the game browser run unchanged; what differs is below:
//   * there is no OS to give back: NetWindow / LeanNetWindow do nothing, window() just runs the callback (and starts Winsock the first time);
//   * the clock is SDL's millisecond counter and netPauseMs sleeps (the PC game is one thread: the tick does not advance while it waits);
//   * addresses and ports are host-order numbers (rt/sdl/winsock.hpp); the swaps are inside that layer.
// The Winsock headers stay out of this file (their LONG / ULONG clash with ace/types.h), see rt/sdl/winsock.hpp.
// the window guards keep the Amiga fields (vectors, interrupt masks) that the PC leaves unused
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#include "rt/net.hpp"
#pragma clang diagnostic pop

#include <SDL3/SDL_timer.h>

#include "rt/logwrite.hpp"
#include "rt/sdl/winsock.hpp"

namespace rt {

namespace {

NetTransport *s_pTransport = nullptr;

}  // namespace

// ---- guards ----------------------------------------------------------------------------------------------------------------
SocketLib::~SocketLib() { close(); }
bool SocketLib::open() { return wsock::startup(); }
void SocketLib::close() { wsock::cleanup(); }

// The PC has no OS to hand the hardware back to: the windows are empty brackets, and they cost no beam lines.
LeanNetWindow::LeanNetWindow(const ULONG *pulOsVec, ULONG, UWORD uwLevelMask, ULONG &ulCostLines)
	: m_pulOsVec(pulOsVec), m_pVec(nullptr), m_aulAceVec{0, 0, 0, 0, 0, 0, 0, 0}, m_uwMask(uwLevelMask), m_uwIntena(0), m_ulCost(ulCostLines), m_lStart(0) {}
LeanNetWindow::~LeanNetWindow() {}
NetWindow::NetWindow(ULONG &ulCostLines) : m_ulCost(ulCostLines), m_ulStart(0) {}
NetWindow::~NetWindow() {}

Socket::~Socket() { close(); }
void Socket::adopt(LONG lFd) { close(); m_lFd = lFd; }
void Socket::close() {
	if(m_lFd >= 0) {
		wsock::closeFd(m_lFd);
		m_lFd = -1;
	}
}

// ---- clock -----------------------------------------------------------------------------------------------------------------
ULONG netClockMs() { return static_cast<ULONG>(SDL_GetTicks()) + 1000; }   // starts at 1000 like the Amiga clock (0 means "never" to some callers)

void netPauseMs(ULONG ulMs) { SDL_Delay(ulMs); }

// ---- transport -------------------------------------------------------------------------------------------------------------
NetTransport::NetTransport()
	: m_ulLobbyIp(0), m_ulJoinIp(0), m_isTried(false), m_isStack(false), m_isConnecting(false), m_isConnectFailed(false), m_ulVbr(0), m_ulCost(0) {
	m_szLobbyHost[0] = 0;
	for(UBYTE i = 0; i < NET_LINKS; ++i) ms::netPushbackClear(m_aPush[i]);
	for(UWORD i = 0; i < 8; ++i) m_aulOsVec[i] = 0;
	s_pTransport = this;
}

NetTransport::~NetTransport() {
	if(s_pTransport == this) {
		s_pTransport = nullptr;
	}
	if(m_isStack) {
		closeAll();
		m_sLib.close();
	}
}

NetTransport *netTransport() { return s_pTransport; }
UBYTE netTransportCall() { return 0; }

bool NetTransport::hostListen(UWORD uwPort) {
	closeStreams();
	return openListen(uwPort);
}

bool NetTransport::listenAgain(UWORD uwPort) {
	m_sListen.close();
	return openListen(uwPort);
}

bool NetTransport::openListen(UWORD uwPort) {
	const LONG lFd = wsock::openStream();
	if(lFd < 0) {
		logWrite("NET error: socket() failed, error %d\n", static_cast<int>(wsock::lastError()));
		return false;
	}
	m_sListen.adopt(lFd);
	if(!wsock::bindAny(lFd, uwPort, true) || !wsock::listenOn(lFd, NET_LINKS)) {
		logWrite("NET error: bind/listen port %u failed, error %d\n", static_cast<unsigned>(uwPort), static_cast<int>(wsock::lastError()));
		m_sListen.close();
		return false;
	}
	wsock::setNonBlocking(lFd, true);
	logWrite("NET host: listening on port %u\n", static_cast<unsigned>(uwPort));
	return true;
}

LONG NetTransport::hostAccept() {
	if(!m_sListen.isOpen()) {
		return -1;
	}
	LONG lFree = -1;
	for(UBYTE i = 0; i < NET_LINKS; ++i) {
		if(!m_aConn[i].isOpen()) {
			lFree = i;
			break;
		}
	}
	if(lFree < 0) {
		return -1;
	}
	const LONG lFd = wsock::acceptFrom(m_sListen.fd());
	if(lFd < 0) {
		return -1;
	}
	m_aConn[lFree].adopt(lFd);
	wsock::setNonBlocking(lFd, true);
	logWrite("NET host: client connected on link %ld (descriptor %ld)\n", static_cast<long>(lFree), static_cast<long>(lFd));
	return lFree;
}

bool NetTransport::joinBegin(const char *szAddr, UWORD uwPort) {
	closeStreams();
	m_isConnectFailed = true;
	m_ulJoinIp = 0;
	const ULONG ulIp = wsock::resolve(szAddr);
	if(ulIp == 0xFFFFFFFF) {
		logWrite("NET error: cannot resolve '%s'\n", szAddr);
		return false;
	}
	m_ulJoinIp = ulIp;
	const LONG lFd = wsock::openStream();
	if(lFd < 0) {
		logWrite("NET error: socket() failed, error %d\n", static_cast<int>(wsock::lastError()));
		return false;
	}
	m_aConn[0].adopt(lFd);
	wsock::setNonBlocking(lFd, true);
	if(wsock::connectTo(lFd, ulIp, uwPort) < 0) {
		logWrite("NET error: connect failed, error %d\n", static_cast<int>(wsock::lastError()));
		m_aConn[0].close();
		return false;
	}
	m_isConnecting = true;
	m_isConnectFailed = false;
	logWrite("NET join: connecting to %s:%u\n", szAddr, static_cast<unsigned>(uwPort));
	return true;
}

LONG NetTransport::joinState() {
	if(m_isConnectFailed) {
		return -1;
	}
	if(!m_isConnecting) {
		return m_aConn[0].isOpen() ? 1 : -1;
	}
	if(!wsock::isReady(m_aConn[0].fd(), true, 0)) {
		return 0;
	}
	const LONG lErr = wsock::socketError(m_aConn[0].fd());
	m_isConnecting = false;
	if(lErr) {
		logWrite("NET error: connect failed, SO_ERROR %ld\n", static_cast<long>(lErr));
		m_aConn[0].close();
		m_isConnectFailed = true;
		return -1;
	}
	logWrite("NET join: connected\n");
	return 1;
}

// ---- outgoing connections to the lobby server ------------------------------------------------------------------------------

bool OutConn::begin(ULONG ulIp, UWORD uwPort) {
	close();
	m_isFailed = true;
	const LONG lFd = wsock::openStream();
	if(lFd < 0) {
		return false;
	}
	m_sSock.adopt(lFd);
	wsock::setNonBlocking(lFd, true);
	if(wsock::connectTo(lFd, ulIp, uwPort) < 0) {
		logWrite("NET lobby: connect failed, error %d\n", static_cast<int>(wsock::lastError()));
		m_sSock.close();
		return false;
	}
	m_isConnecting = true;
	m_isFailed = false;
	return true;
}

LONG OutConn::state() {
	if(m_isFailed || !m_sSock.isOpen()) {
		return -1;
	}
	if(!m_isConnecting) {
		return 1;
	}
	if(!wsock::isReady(m_sSock.fd(), true, 0)) {
		return 0;
	}
	const LONG lErr = wsock::socketError(m_sSock.fd());
	m_isConnecting = false;
	if(lErr) {
		m_sSock.close();
		m_isFailed = true;
		return -1;
	}
	return 1;
}

LONG OutConn::send(const void *pData, ULONG ulLen) {
	return m_sSock.isOpen() ? wsock::sendBytes(m_sSock.fd(), pData, ulLen) : -1;
}

LONG OutConn::recv(void *pBuf, ULONG ulCap) {
	return m_sSock.isOpen() ? wsock::recvBytes(m_sSock.fd(), pBuf, ulCap) : -1;
}

void OutConn::close() {
	m_sSock.close();
	m_isConnecting = false;
	m_isFailed = true;
}

LONG OutConn::release() {
	m_isConnecting = false;
	m_isFailed = true;
	return m_sSock.release();
}

// The lobby's address: resolved once per name and kept for the next attempts, by the lobby listing, the relay's idle connection and the
// game browser alike.
bool NetTransport::resolveLobby(const char *szHost, bool bMayResolve) {
	bool isSame = m_ulLobbyIp != 0;
	for(UWORD i = 0; isSame && i < sizeof(m_szLobbyHost); ++i) {
		if(m_szLobbyHost[i] != szHost[i]) {
			isSame = false;
		}
		if(!szHost[i]) {
			break;
		}
	}
	if(isSame) {
		return true;
	}
	if(!bMayResolve) {
		return false;
	}
	const ULONG ulIp = wsock::resolve(szHost);
	if(ulIp == 0xFFFFFFFF) {
		logWrite("NET lobby: cannot resolve '%s'\n", szHost);
		return false;
	}
	m_ulLobbyIp = ulIp;
	UWORD n = 0;
	for(; szHost[n] && n + 1 < sizeof(m_szLobbyHost); ++n) {
		m_szLobbyHost[n] = szHost[n];
	}
	m_szLobbyHost[n] = 0;
	return true;
}

bool NetTransport::lobbyBegin(const char *szHost, UWORD uwPort, bool bMayResolve) {
	m_lobby.close();
	return resolveLobby(szHost, bMayResolve) && m_lobby.begin(m_ulLobbyIp, uwPort);
}

bool NetTransport::relayBegin(UWORD uwPort) {
	m_relay.close();
	return m_ulLobbyIp != 0 && m_relay.begin(m_ulLobbyIp, uwPort);
}

LONG NetTransport::relayHandOver(const UBYTE *pFirst, ULONG ulLen) {
	LONG lFree = -1;
	for(UBYTE i = 0; i < NET_LINKS; ++i) {
		if(!m_aConn[i].isOpen()) {
			lFree = i;
			break;
		}
	}
	if(lFree < 0) {
		m_relay.close();
		return -1;
	}
	m_aConn[lFree].adopt(m_relay.release());
	ms::netPushbackSet(m_aPush[lFree], pFirst, ulLen);
	logWrite("NET host: relayed client on link %ld\n", static_cast<long>(lFree));
	return lFree;
}

LONG NetTransport::findBegin(const char *szHost, UWORD uwPort) {
	m_find.close();
	return resolveLobby(szHost, true) && m_find.begin(m_ulLobbyIp, uwPort) ? 1 : 0;
}

LONG NetTransport::send(UBYTE ubLink, const void *pData, ULONG ulLen) {
	if(ubLink >= NET_LINKS || !m_aConn[ubLink].isOpen()) {
		return -1;
	}
	return wsock::sendBytes(m_aConn[ubLink].fd(), pData, ulLen);
}

LONG NetTransport::recv(UBYTE ubLink, void *pBuf, ULONG ulCap) {
	if(ubLink >= NET_LINKS || !m_aConn[ubLink].isOpen()) {
		return -1;
	}
	if(m_aPush[ubLink].ubFill != 0) {   // a relay connection handed over: the bytes already read come first
		return static_cast<LONG>(ms::netPushbackTake(m_aPush[ubLink], static_cast<UBYTE *>(pBuf), ulCap));
	}
	return wsock::recvBytes(m_aConn[ubLink].fd(), pBuf, ulCap);
}

void NetTransport::waitReadable(ULONG ulMs) {
	int32_t alFds[1 + NET_LINKS];
	uint32_t ulCount = 0;
	if(m_sListen.isOpen()) {
		alFds[ulCount++] = m_sListen.fd();
	}
	for(UBYTE i = 0; i < NET_LINKS; ++i) {
		if(m_aConn[i].isOpen()) {
			alFds[ulCount++] = m_aConn[i].fd();
		}
	}
	wsock::waitReadable(alFds, ulCount, ulMs);
}

void NetTransport::closeLink(UBYTE ubLink) {
	if(ubLink < NET_LINKS) {
		if(m_aConn[ubLink].isOpen()) {
			logWrite("NET host: closing link %u (descriptor %ld)\n", static_cast<unsigned>(ubLink), static_cast<long>(m_aConn[ubLink].fd()));
		}
		m_aConn[ubLink].close();
		ms::netPushbackClear(m_aPush[ubLink]);
	}
}

void NetTransport::closeListen() {
	m_sListen.close();
}

// ---- UDP (ROADMAP 12.21) ---------------------------------------------------------------------------------------------------

bool NetTransport::udpOpen(UWORD uwBindPort) {
	m_sUdp.close();
	Socket sNew;   // the guard: any early return closes it; the transport takes it over only when it is bound
	sNew.adopt(wsock::openDgram());
	if(!sNew.isOpen()) {
		logWrite("NET error: udp socket() failed, error %d\n", static_cast<int>(wsock::lastError()));
		return false;
	}
	if(!wsock::bindAny(sNew.fd(), uwBindPort, uwBindPort != 0)) {
		logWrite("NET error: udp bind port %u failed, error %d\n", static_cast<unsigned>(uwBindPort), static_cast<int>(wsock::lastError()));
		return false;
	}
	wsock::setNonBlocking(sNew.fd(), false);
	m_sUdp.adopt(sNew.release());
	logWrite("NET udp: socket open on port %u\n", static_cast<unsigned>(uwBindPort));
	return true;
}

LONG NetTransport::udpSend(ULONG ulAddr, UWORD uwPort, const void *pData, ULONG ulLen) {
	if(!m_sUdp.isOpen()) {
		return -1;
	}
	return wsock::sendDgram(m_sUdp.fd(), ulAddr, uwPort, pData, ulLen);
}

LONG NetTransport::udpRecv(void *pBuf, ULONG ulCap, ULONG &ulAddr, UWORD &uwPort) {
	if(!m_sUdp.isOpen()) {
		return 0;
	}
	uint32_t ulFrom = 0;
	uint16_t uwFrom = 0;
	const LONG lGot = wsock::recvDgram(m_sUdp.fd(), pBuf, ulCap, ulFrom, uwFrom);
	if(lGot > 0) {
		ulAddr = ulFrom;
		uwPort = uwFrom;
	}
	return lGot;
}

bool NetTransport::udpLocal(ULONG &ulAddr, UWORD &uwPort) {
	if(!m_sUdp.isOpen()) {
		return false;
	}
	LONG lTcp = -1;
	for(UBYTE i = 0; i < NET_LINKS && lTcp < 0; ++i) {
		lTcp = m_aConn[i].isOpen() ? m_aConn[i].fd() : -1;
	}
	if(lTcp < 0 && m_lobby.isOpen()) {
		lTcp = m_lobby.fd();
	}
	if(lTcp < 0 && m_relay.isOpen()) {
		lTcp = m_relay.fd();
	}
	if(lTcp < 0) {
		return false;
	}
	uint32_t ulIp = 0;
	uint16_t uwTcpPort = 0, uwUdpPort = 0;
	uint32_t ulUnused = 0;
	if(!wsock::localName(lTcp, ulIp, uwTcpPort) || ulIp == 0 || !wsock::localName(m_sUdp.fd(), ulUnused, uwUdpPort) || uwUdpPort == 0) {
		return false;
	}
	ulAddr = ulIp;
	uwPort = uwUdpPort;
	return true;
}

void NetTransport::closeAll() {
	closeStreams();
	m_sUdp.close();
}

void NetTransport::closeStreams() {
	m_lobby.close();
	m_relay.close();
	m_find.close();
	m_sListen.close();
	for(UBYTE i = 0; i < NET_LINKS; ++i) {
		m_aConn[i].close();
		ms::netPushbackClear(m_aPush[i]);
	}
	m_isConnecting = false;
	m_isConnectFailed = false;
}

}  // namespace rt
