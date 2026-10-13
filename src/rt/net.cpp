// rt/net - see net.hpp.  ROADMAP 12.2 (guards, windows, ReadArgs) and 12.8b (the byte-stream transport the session drives).
#include "rt/net.hpp"

#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <ace/managers/system.h>
#include <ace/utils/custom.h>
#include <hardware/intbits.h>

// The Roadshow SDK headers of the toolchain assume a classic Amiga C environment: stdint, ssize_t and the __stdargs keyword.
#include <stdint.h>
#include <stddef.h>
typedef long ssize_t;
#ifndef __stdargs
#define __stdargs
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "rt/bsdsocket_calls.hpp"
#include <sys/errno.h>
#include <sys/filio.h>
#include <netinet/tcp.h>

#include "engine/netclock.hpp"
#include "rt/irq.hpp"
#include "rt/amiga/serlog.hpp"
#include "rt/system.hpp"

// The serial log exists in MS_AUTOPLAY builds only; elsewhere the spike's log lines compile away (errors that a player needs go to
// PROGDIR:boot.log through rt::bootLog).
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
#define NET_LOG(...) ::rt::serLogf(__VA_ARGS__)
#else
#define NET_LOG(...) ((void)0)
#endif

// proto/bsdsocket.h inlines use this global (the library base is per task: opened by the game's own task).
struct Library *SocketBase = nullptr;

// Supervisor-mode helper: D0 = VBR (68010+).
extern "C" void rt_net_getvbr();
asm(R"(
	.text
	.globl rt_net_getvbr
rt_net_getvbr:
	movec %vbr,%d0
	rte
)");

namespace rt {

namespace {

// Measurement clock in beam lines (312 per PAL frame, 64 us): a cost counter, monotonic by construction (forward steps of the beam
// line summed, ms::NetLineAccum).  Used for the cost of the windows; a window shorter than a frame loses nothing.
UWORD beamLine() {
	const ULONG ulV = *reinterpret_cast<volatile ULONG *>(&g_pCustom->vposr);   // VPOSR:VHPOSR in one read
	return static_cast<UWORD>((((ulV >> 16) & 1) << 8) | ((ulV >> 8) & 0xFF));
}
ms::NetLineAccum s_cost;
ms::NetClockState s_clock;

LONG nowLines() {
	return static_cast<LONG>(ms::netLineAccumStep(s_cost, beamLine()));
}
LONG linesSince(LONG lThen) {
	return static_cast<LONG>(static_cast<ULONG>(nowLines()) - static_cast<ULONG>(lThen));
}

NetTransport *s_pTransport = nullptr;

// Which socket call the main loop is inside right now (0 = none): the PERF line prints it, so a hang inside an OS call shows which one (a call that
// never returns leaves its code standing).  A stack guard per call; the codes are in net.hpp (NetCall).
volatile UBYTE s_ubCall = 0;
class CallMark {
public:
	explicit CallMark(UBYTE ubCall) : m_ubOuter(s_ubCall) { s_ubCall = ubCall; }
	~CallMark() { s_ubCall = m_ubOuter; }   // a mark inside a mark (a close inside a send's error path) gives the outer call back
	CallMark(const CallMark &) = delete;
	CallMark &operator=(const CallMark &) = delete;
	CallMark(CallMark &&) = delete;
	CallMark &operator=(CallMark &&) = delete;

private:
	UBYTE m_ubOuter;
};

// Every library call that can wait inside bsdsocket carries a mark: a freeze with "call 0" then means our own code, not an unmarked OS call.
LONG openSocket() {
	const CallMark sMark(NETCALL_SOCKET);
	return bsd::socket(AF_INET, SOCK_STREAM, 0);
}

// The socket's pending error (SO_ERROR), 0 = none.
LONG socketError(LONG lFd) {
	const CallMark sMark(NETCALL_SOCKOPT);
	LONG lErr = 0;
	socklen_t lLen = sizeof(lErr);
	bsd::getsockopt(lFd, SOL_SOCKET, SO_ERROR, &lErr, &lLen);
	return lErr;
}

LONG openDgramSocket() {
	const CallMark sMark(NETCALL_SOCKET);
	return bsd::socket(AF_INET, SOCK_DGRAM, 0);
}

// Dotted quad or host name to an address (0xFFFFFFFF = none).  gethostbyname may take seconds on a dead resolver.
ULONG resolveAddress(const char *szHost) {
	const CallMark sMark(NETCALL_RESOLVE);
	STRPTR szName = reinterpret_cast<STRPTR>(const_cast<char *>(szHost));
	ULONG ulIp = bsd::inet_addr(szName);
	if(ulIp == 0xFFFFFFFF) {
		struct hostent *pHost = static_cast<struct hostent *>(bsd::gethostbyname(szName));
		if(!pHost || pHost->h_length != 4) {
			return 0xFFFFFFFF;
		}
		ulIp = *reinterpret_cast<ULONG *>(pHost->h_addr_list[0]);
	}
	return ulIp;
}

void setNonBlocking(LONG lFd) {
	const CallMark sMark(NETCALL_NONBLOCK);
	LONG lOn = 1;
	bsd::IoctlSocket(lFd, FIONBIO, &lOn);
	LONG lNoDelay = 1;
	bsd::setsockopt(lFd, IPPROTO_TCP, TCP_NODELAY, &lNoDelay, sizeof(lNoDelay));
}

void setNonBlockingOnly(LONG lFd) {   // a datagram socket: no TCP options
	const CallMark sMark(NETCALL_NONBLOCK);
	LONG lOn = 1;
	bsd::IoctlSocket(lFd, FIONBIO, &lOn);
}

void zeroAddr(struct sockaddr_in &sAddr, UWORD uwPort) {
	char *pRaw = reinterpret_cast<char *>(&sAddr);
	for(UWORD i = 0; i < sizeof(sAddr); ++i) pRaw[i] = 0;
	sAddr.sin_len = sizeof(sAddr);
	sAddr.sin_family = AF_INET;
	sAddr.sin_port = uwPort;
}

}  // namespace

// ---- guards ----------------------------------------------------------------------------------------------------------------
SocketLib::~SocketLib() { close(); }
bool SocketLib::open() {
	if(!SocketBase) {
		SocketBase = OpenLibrary(reinterpret_cast<CONST_STRPTR>("bsdsocket.library"), 4);
	}
	return SocketBase != nullptr;
}
void SocketLib::close() {
	if(SocketBase) {
		CloseLibrary(SocketBase);
		SocketBase = nullptr;
	}
}

LeanNetWindow::LeanNetWindow(const ULONG *pulOsVec, ULONG ulVbr, UWORD uwLevelMask, ULONG &ulCostLines)
	: m_pulOsVec(pulOsVec), m_pVec(reinterpret_cast<volatile ULONG *>(ulVbr)), m_uwMask(uwLevelMask), m_uwIntena(0), m_ulCost(ulCostLines),
	  m_lStart(nowLines()) {
	for(UWORD i = 0; i < 8; ++i) m_aulAceVec[i] = 0;
	m_uwIntena = g_pCustom->intenar;
	g_pCustom->intena = INTF_INTEN;   // master off: nothing runs while the vectors are changed
	for(UWORD lvl = 1; lvl <= 6; ++lvl) {
		if(m_uwMask & (1 << (lvl - 1))) {
			m_aulAceVec[lvl] = m_pVec[24 + lvl];
			m_pVec[24 + lvl] = m_pulOsVec[lvl];
		}
	}
	g_pCustom->intena = INTF_SETCLR | INTF_INTEN | INTF_SOFTINT | INTF_PORTS | INTF_EXTER;
}
LeanNetWindow::~LeanNetWindow() {
	g_pCustom->intena = INTF_INTEN;
	for(UWORD lvl = 1; lvl <= 6; ++lvl) {
		if(m_uwMask & (1 << (lvl - 1))) {
			m_pVec[24 + lvl] = m_aulAceVec[lvl];
		}
	}
	g_pCustom->intena = 0x7FFF;   // every enable off, then back exactly as it was (bit 14, the master, included)
	g_pCustom->intena = INTF_SETCLR | (m_uwIntena & 0x7FFF);
	m_ulCost += static_cast<ULONG>(linesSince(m_lStart));
}

Socket::~Socket() { close(); }
void Socket::adopt(LONG lFd) { close(); m_lFd = lFd; }
void Socket::close() {
	if(m_lFd >= 0) {
		const CallMark sMark(NETCALL_CLOSE);
		bsd::CloseSocket(m_lFd);
		m_lFd = -1;
	}
}

NetWindow::NetWindow(ULONG &ulCostLines) : m_ulCost(ulCostLines), m_ulStart(static_cast<ULONG>(nowLines())) { systemUse(); }
NetWindow::~NetWindow() {
	systemUnuse();
	m_ulCost += static_cast<ULONG>(linesSince(static_cast<LONG>(m_ulStart)));
}

// ---- CLI -------------------------------------------------------------------------------------------------------------------
bool netParseArgs(NetArgs &sArgs) {
	sArgs = NetArgs();
	sArgs.uwPort = NET_DEFAULT_PORT;
	SysBase = *reinterpret_cast<struct ExecBase **>(4UL);   // ACE sets these in systemCreate, which has not run yet
	struct Process *pProc = reinterpret_cast<struct Process *>(FindTask(nullptr));
	if(!pProc->pr_CLI) {
		return true;   // Workbench start: no command line (icon tool types are 12.8)
	}
	DOSBase = reinterpret_cast<struct DosLibrary *>(OpenLibrary(reinterpret_cast<CONST_STRPTR>("dos.library"), 36));
	if(!DOSBase) {
		return true;
	}
	bool isOk = true;
	LONG alArgs[7] = {0, 0, 0, 0, 0, 0, 0};   // HOST, JOIN, PORT, NAME, DELAY, TIMEOUT, TCP
	struct RDArgs *pRd = ReadArgs(reinterpret_cast<CONST_STRPTR>("HOST/S,JOIN/K,PORT/K/N,NAME/K,DELAY/K/N,TIMEOUT/K/N,TCP/S"), alArgs, nullptr);
	if(!pRd) {
		PutStr(reinterpret_cast<CONST_STRPTR>("usage: moonstone [HOST] [JOIN <address>] [PORT <n>] [NAME <text>] [DELAY <n>] [TCP]\n"));
		isOk = false;
	}
	else {
		auto copy = [](char *szDst, ULONG ulMax, const char *szSrc) {
			ULONG i = 0;
			while(szSrc[i] && i + 1 < ulMax) { szDst[i] = szSrc[i]; ++i; }
			szDst[i] = 0;
		};
		sArgs.isHost = alArgs[0] != 0;
		if(alArgs[1]) { sArgs.isJoin = true; copy(sArgs.szJoin, sizeof(sArgs.szJoin), reinterpret_cast<const char *>(alArgs[1])); }
		if(alArgs[2]) sArgs.uwPort = static_cast<UWORD>(*reinterpret_cast<LONG *>(alArgs[2]));
		if(alArgs[3]) copy(sArgs.szName, sizeof(sArgs.szName), reinterpret_cast<const char *>(alArgs[3]));
		if(alArgs[4]) sArgs.uwDelay = static_cast<UWORD>(*reinterpret_cast<LONG *>(alArgs[4]));
		if(alArgs[5]) sArgs.uwTimeoutSec = static_cast<UWORD>(*reinterpret_cast<LONG *>(alArgs[5]));
		sArgs.isTcpOnly = alArgs[6] != 0;
		FreeArgs(pRd);   // everything needed is copied above; nothing can return in between (a guard in 12.8 proper)
		if(sArgs.isHost && sArgs.isJoin) {
			PutStr(reinterpret_cast<CONST_STRPTR>("moonstone: HOST and JOIN exclude each other\n"));
			isOk = false;
		}
	}
	CloseLibrary(reinterpret_cast<struct Library *>(DOSBase));
	DOSBase = nullptr;
	return isOk;
}

// ---- clock -----------------------------------------------------------------------------------------------------------------
ULONG netClockMs() {
	return ms::netClockRead(s_clock, irqVblCount(), beamLine());
}

void netPauseMs(ULONG ulMs) {
	ms::NetLineAccum sAcc;
	ms::netLineAccumInit(sAcc, beamLine());
	const ULONG ulLines = ms::netMsToLines(ulMs);
	while(ms::netLineAccumStep(sAcc, beamLine()) < ulLines) {
	}
	netClockMs();   // keep the clock's line accumulator fed
}

// ---- transport ------------------------------------------------------------------------------------------------------------
NetTransport::NetTransport()
	: m_ulLobbyIp(0), m_ulJoinIp(0), m_isTried(false), m_isStack(false), m_isConnecting(false), m_isConnectFailed(false), m_ulVbr(0), m_ulCost(0) {
	m_szLobbyHost[0] = 0;
	for(UBYTE i = 0; i < NET_LINKS; ++i) ms::netPushbackClear(m_aPush[i]);
	for(UWORD i = 0; i < 8; ++i) m_aulOsVec[i] = 0;
	// the OS's own interrupt vectors, read while the OS still owns them (before ACE's systemUnuse): what the lean window swaps in
	struct ExecBase *pExec = *reinterpret_cast<struct ExecBase **>(4UL);
	if(pExec->AttnFlags & AFF_68010) {
		m_ulVbr = Supervisor(reinterpret_cast<ULONG (*)()>(rt_net_getvbr));
	}
	for(UWORD lvl = 1; lvl <= 6; ++lvl) {
		m_aulOsVec[lvl] = reinterpret_cast<volatile ULONG *>(m_ulVbr)[24 + lvl];
	}
	s_pTransport = this;
}

NetTransport::~NetTransport() {
	if(s_pTransport == this) {
		s_pTransport = nullptr;
	}
	if(m_isStack) {   // sockets and library need the OS: close them inside a window
		ULONG ulCost = 0;
		NetWindow sWin(ulCost);
		closeAll();
		m_sLib.close();
	}
}

NetTransport *netTransport() {
	return s_pTransport;
}

UBYTE netTransportCall() {
	return s_ubCall;
}

bool NetTransport::hostListen(UWORD uwPort) {
	closeStreams();
	return openListen(uwPort);
}

// A game in which a seat is away listens again: the lobby connection, the relay's idle connection and the links of the players who stay are not touched
// (closing them dropped the listing and gave the relayed game a new session, which the dropped player's REJOIN no longer found).
bool NetTransport::listenAgain(UWORD uwPort) {
	m_sListen.close();
	return openListen(uwPort);
}

bool NetTransport::openListen(UWORD uwPort) {
	struct sockaddr_in sAddr;
	zeroAddr(sAddr, uwPort);
	sAddr.sin_addr.s_addr = INADDR_ANY;
	const LONG lFd = openSocket();
	if(lFd < 0) {
		NET_LOG("NET error: socket() failed, errno %ld\n", static_cast<long>(bsd::Errno()));
		return false;
	}
	m_sListen.adopt(lFd);
	const CallMark sBindMark(NETCALL_BIND);
	LONG lOne = 1;
	bsd::setsockopt(lFd, SOL_SOCKET, SO_REUSEADDR, &lOne, sizeof(lOne));
	if(bsd::bind(lFd, reinterpret_cast<struct sockaddr *>(&sAddr), sizeof(sAddr)) < 0 || bsd::listen(lFd, NET_LINKS) < 0) {
		NET_LOG("NET error: bind/listen port %u failed, errno %ld\n", uwPort, static_cast<long>(bsd::Errno()));
		m_sListen.close();
		return false;
	}
	setNonBlocking(lFd);
	NET_LOG("NET host: listening on port %u\n", uwPort);
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
	struct sockaddr_in sPeer;
	socklen_t lLen = sizeof(sPeer);
	LONG lFd;
	{
		const CallMark sMark(NETCALL_ACCEPT);
		lFd = bsd::accept(m_sListen.fd(), reinterpret_cast<struct sockaddr *>(&sPeer), &lLen);
	}
	if(lFd < 0) {
		return -1;
	}
	m_aConn[lFree].adopt(lFd);
	{
		const CallMark sMark(NETCALL_NONBLOCK);
		setNonBlocking(lFd);
	}
	NET_LOG("NET host: client connected on link %ld (descriptor %ld)\n", static_cast<long>(lFree), static_cast<long>(lFd));
	return lFree;
}

bool NetTransport::joinBegin(const char *szAddr, UWORD uwPort) {
	closeStreams();
	m_isConnectFailed = true;
	m_ulJoinIp = 0;
	struct sockaddr_in sAddr;
	zeroAddr(sAddr, uwPort);
	const ULONG ulIp = resolveAddress(szAddr);
	if(ulIp == 0xFFFFFFFF) {
		NET_LOG("NET error: cannot resolve '%s'\n", szAddr);
		return false;
	}
	sAddr.sin_addr.s_addr = ulIp;
	m_ulJoinIp = ulIp;
	const LONG lFd = openSocket();
	if(lFd < 0) {
		NET_LOG("NET error: socket() failed, errno %ld\n", static_cast<long>(bsd::Errno()));
		return false;
	}
	m_aConn[0].adopt(lFd);
	setNonBlocking(lFd);
	if(bsd::connect(lFd, reinterpret_cast<struct sockaddr *>(&sAddr), sizeof(sAddr)) < 0 && bsd::Errno() != EINPROGRESS) {
		NET_LOG("NET error: connect failed, errno %ld\n", static_cast<long>(bsd::Errno()));
		m_aConn[0].close();
		return false;
	}
	m_isConnecting = true;
	m_isConnectFailed = false;
	NET_LOG("NET join: connecting to %s:%u\n", szAddr, uwPort);
	return true;
}

namespace {

// WaitSelect on one descriptor: ready within ulMs milliseconds (0 = poll).
bool ready(LONG lFd, bool isWrite, ULONG ulMs) {
	fd_set sSet;
	for(UWORD i = 0; i < sizeof(sSet.fds_bits) / sizeof(sSet.fds_bits[0]); ++i) sSet.fds_bits[i] = 0;
	FD_SET(lFd, &sSet);
	struct { ULONG tv_secs; ULONG tv_micro; } sTv;   // struct __timeval (devices/timer.h: the Amiga timeval)
	sTv.tv_secs = ulMs / 1000;
	sTv.tv_micro = (ulMs % 1000) * 1000;
	const CallMark sMark(NETCALL_SELECT);
	const LONG lRc = isWrite ? bsd::WaitSelect(lFd + 1, nullptr, &sSet, nullptr, &sTv, nullptr)
	                         : bsd::WaitSelect(lFd + 1, &sSet, nullptr, nullptr, &sTv, nullptr);
	return lRc > 0 && FD_ISSET(lFd, &sSet);
}

}  // namespace

LONG NetTransport::joinState() {
	if(m_isConnectFailed) {
		return -1;
	}
	if(!m_isConnecting) {
		return m_aConn[0].isOpen() ? 1 : -1;
	}
	if(!ready(m_aConn[0].fd(), true, 0)) {
		return 0;
	}
	const LONG lErr = socketError(m_aConn[0].fd());
	m_isConnecting = false;
	if(lErr) {
		NET_LOG("NET error: connect failed, SO_ERROR %ld\n", static_cast<long>(lErr));
		m_aConn[0].close();
		m_isConnectFailed = true;
		return -1;
	}
	NET_LOG("NET join: connected\n");
	return 1;
}

// ---- outgoing connections to the lobby server ------------------------------------------------------------------------------

bool OutConn::begin(ULONG ulIp, UWORD uwPort) {
	close();
	m_isFailed = true;
	struct sockaddr_in sAddr;
	zeroAddr(sAddr, uwPort);
	sAddr.sin_addr.s_addr = ulIp;
	const LONG lFd = openSocket();
	if(lFd < 0) {
		return false;
	}
	m_sSock.adopt(lFd);
	setNonBlocking(lFd);
	const CallMark sMark(NETCALL_CONNECT);
	if(bsd::connect(lFd, reinterpret_cast<struct sockaddr *>(&sAddr), sizeof(sAddr)) < 0 && bsd::Errno() != EINPROGRESS) {
		NET_LOG("NET lobby: connect failed, errno %ld\n", static_cast<long>(bsd::Errno()));
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
	if(!ready(m_sSock.fd(), true, 0)) {
		return 0;
	}
	const LONG lErr = socketError(m_sSock.fd());
	m_isConnecting = false;
	if(lErr) {
		m_sSock.close();
		m_isFailed = true;
		return -1;
	}
	return 1;
}

LONG OutConn::send(const void *pData, ULONG ulLen) {
	if(!m_sSock.isOpen()) {
		return -1;
	}
	const CallMark sMark(NETCALL_OUT_SEND);
	const LONG lRc = bsd::send(m_sSock.fd(), const_cast<void *>(pData), static_cast<LONG>(ulLen), 0);
	if(lRc >= 0) {
		return lRc;
	}
	return bsd::Errno() == EWOULDBLOCK ? 0 : -1;
}

LONG OutConn::recv(void *pBuf, ULONG ulCap) {
	if(!m_sSock.isOpen()) {
		return -1;
	}
	const CallMark sMark(NETCALL_OUT_RECV);
	const LONG lRc = bsd::recv(m_sSock.fd(), pBuf, static_cast<LONG>(ulCap), 0);
	if(lRc > 0) {
		return lRc;
	}
	if(lRc == 0) {
		return -1;   // the server closed the connection
	}
	return bsd::Errno() == EWOULDBLOCK ? 0 : -1;
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

// The lobby's address: resolved once per name (gethostbyname blocks: only when bMayResolve) and kept for the next attempts, by the lobby listing, the
// relay's idle connection and the game browser alike.
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
	const ULONG ulIp = resolveAddress(szHost);   // may take seconds on a dead resolver
	if(ulIp == 0xFFFFFFFF) {
		NET_LOG("NET lobby: cannot resolve '%s'\n", szHost);
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
	NET_LOG("NET host: relayed client on link %ld\n", static_cast<long>(lFree));
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
	const CallMark sMark(NETCALL_SEND);
	const LONG lRc = bsd::send(m_aConn[ubLink].fd(), const_cast<void *>(pData), static_cast<LONG>(ulLen), 0);
	if(lRc >= 0) {
		return lRc;
	}
	return bsd::Errno() == EWOULDBLOCK ? 0 : -1;
}

LONG NetTransport::recv(UBYTE ubLink, void *pBuf, ULONG ulCap) {
	if(ubLink >= NET_LINKS || !m_aConn[ubLink].isOpen()) {
		return -1;
	}
	if(m_aPush[ubLink].ubFill != 0) {   // a relay connection handed over: the bytes already read come first
		return static_cast<LONG>(ms::netPushbackTake(m_aPush[ubLink], static_cast<UBYTE *>(pBuf), ulCap));
	}
	const CallMark sMark(NETCALL_RECV);
	const LONG lRc = bsd::recv(m_aConn[ubLink].fd(), pBuf, static_cast<LONG>(ulCap), 0);
	if(lRc > 0) {
		return lRc;
	}
	if(lRc == 0) {
		return -1;   // orderly close by the peer
	}
	return bsd::Errno() == EWOULDBLOCK ? 0 : -1;
}

void NetTransport::waitReadable(ULONG ulMs) {
	fd_set sSet;
	for(UWORD i = 0; i < sizeof(sSet.fds_bits) / sizeof(sSet.fds_bits[0]); ++i) sSet.fds_bits[i] = 0;
	LONG lMax = -1;
	if(m_sListen.isOpen()) {
		FD_SET(m_sListen.fd(), &sSet);
		lMax = m_sListen.fd();
	}
	for(UBYTE i = 0; i < NET_LINKS; ++i) {
		if(m_aConn[i].isOpen()) {
			FD_SET(m_aConn[i].fd(), &sSet);
			if(m_aConn[i].fd() > lMax) {
				lMax = m_aConn[i].fd();
			}
		}
	}
	if(lMax < 0) {
		return;
	}
	struct { ULONG tv_secs; ULONG tv_micro; } sTv;
	sTv.tv_secs = ulMs / 1000;
	sTv.tv_micro = (ulMs % 1000) * 1000;
	bsd::WaitSelect(lMax + 1, &sSet, nullptr, nullptr, &sTv, nullptr);
}

void NetTransport::closeLink(UBYTE ubLink) {
	if(ubLink < NET_LINKS) {
		if(m_aConn[ubLink].isOpen()) {
			NET_LOG("NET host: closing link %u (descriptor %ld)\n", static_cast<unsigned>(ubLink), static_cast<long>(m_aConn[ubLink].fd()));
		}
		const CallMark sMark(NETCALL_CLOSE);
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
	sNew.adopt(openDgramSocket());
	if(!sNew.isOpen()) {
		NET_LOG("NET error: udp socket() failed, errno %ld\n", static_cast<long>(bsd::Errno()));
		return false;
	}
	struct sockaddr_in sAddr;
	zeroAddr(sAddr, uwBindPort);
	sAddr.sin_addr.s_addr = INADDR_ANY;
	{
		const CallMark sBindMark(NETCALL_BIND);
		LONG lOne = 1;
		bsd::setsockopt(sNew.fd(), SOL_SOCKET, SO_REUSEADDR, &lOne, sizeof(lOne));
		if(bsd::bind(sNew.fd(), reinterpret_cast<struct sockaddr *>(&sAddr), sizeof(sAddr)) < 0) {
			NET_LOG("NET error: udp bind port %u failed, errno %ld\n", uwBindPort, static_cast<long>(bsd::Errno()));
			return false;
		}
	}
	setNonBlockingOnly(sNew.fd());
	m_sUdp.adopt(sNew.release());
	NET_LOG("NET udp: socket open on port %u\n", uwBindPort);
	return true;
}

LONG NetTransport::udpSend(ULONG ulAddr, UWORD uwPort, const void *pData, ULONG ulLen) {
	if(!m_sUdp.isOpen()) {
		return -1;
	}
	struct sockaddr_in sTo;
	zeroAddr(sTo, uwPort);
	sTo.sin_addr.s_addr = ulAddr;
	const CallMark sMark(NETCALL_UDP_SEND);
	const LONG lRc = bsd::sendto(m_sUdp.fd(), pData, static_cast<LONG>(ulLen), 0, &sTo, sizeof(sTo));
	if(lRc >= 0) {
		return lRc;
	}
	return bsd::Errno() == EWOULDBLOCK ? 0 : -1;
}

LONG NetTransport::udpRecv(void *pBuf, ULONG ulCap, ULONG &ulAddr, UWORD &uwPort) {
	if(!m_sUdp.isOpen()) {
		return 0;
	}
	struct sockaddr_in sFrom;
	socklen_t lLen = sizeof(sFrom);
	const CallMark sMark(NETCALL_UDP_RECV);
	const LONG lRc = bsd::recvfrom(m_sUdp.fd(), pBuf, static_cast<LONG>(ulCap), 0, &sFrom, &lLen);
	if(lRc <= 0) {
		return 0;   // nothing waiting (EWOULDBLOCK) or an error report of an earlier datagram (ICMP): either way no datagram now
	}
	ulAddr = sFrom.sin_addr.s_addr;
	uwPort = sFrom.sin_port;
	return lRc;
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
