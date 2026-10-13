// rt/net - network transport over bsdsocket.library (ROADMAP 12.2 spike, 12.7 transport, 12.8b session).  Design: docs/NETPLAY.md.
//
// bsdsocket.library prototypes come from rt/bsdsocket_calls.hpp (Roadshow SDK LVOs as inline asm).  This file is the byte-stream layer:
// non-blocking TCP sockets (one listening socket, up to three connections), RAII guards for the library, a socket and the OS window.
// The session logic (handshake, lockstep) is engine/netnode.hpp, driven by rt/net_session.cpp.
//
// The game runs with the OS off (ACE systemUnuse): every socket call happens inside a window.  The first window is ACE's own
// (systemUse ... systemUnuse; it also opens the library); every later one is the LeanNetWindow (docs/NETPLAY.md 2.1, ROADMAP 12.2 option
// A2: the OS's level 1-6 vectors swapped in for a few beam lines).  Stack objects only (global constructors do not run): the
// NetTransport lives in gameMain's frame.
#pragma once
#include <ace/types.h>

#include "engine/netrelay.hpp"

namespace rt {

constexpr UWORD NET_DEFAULT_PORT = 6502;
constexpr UBYTE NET_LINKS = 3;            // connections a host holds (engine/netnode NN_LINKS_MAX)

// The command line of a net session (template HOST/S,JOIN/K,PORT/K/N,NAME/K,DELAY/K/N,TIMEOUT/K/N,TCP/S), plain data.  HOST / JOIN only
// pre-fill the Multiplayer screen (JOIN's address, the name, the port); the session itself starts from that screen.
struct NetArgs {
	bool isHost;           // HOST
	bool isJoin;           // JOIN <address>
	char szJoin[48];       // the address or name given to JOIN
	UWORD uwPort;          // PORT (default NET_DEFAULT_PORT)
	char szName[16];       // NAME
	UWORD uwDelay;         // DELAY: the lockstep's input delay in ticks (0 = the default of engine/netnode)
	UWORD uwTimeoutSec;    // TIMEOUT: seconds a silent peer is waited for (0 = the default of 300: the safety timeout, five minutes)
	bool isTcpOnly;        // TCP: the per-tick traffic stays on TCP (no UDP with history, ROADMAP 12.21); for debugging.  net.ini `udp = no` does the same
	bool isActive() const { return isHost || isJoin; }
};

// Parses the CLI with dos.library ReadArgs. Call in main() BEFORE rt::systemCreate() (it opens dos.library itself and frees the
// RDArgs again). Returns false after printing the usage on a bad command line. No arguments / Workbench start: inactive args.
bool netParseArgs(NetArgs &sArgs);

// ---- RAII guards (bodies in net.cpp, which has the bsdsocket headers) ----------------------------------------------------
// bsdsocket.library, opened lazily inside a NetWindow (OpenLibrary needs the OS); the base is per task. close() / destructor.
class SocketLib {
public:
	SocketLib() {}
	~SocketLib();
	SocketLib(const SocketLib &) = delete;
	SocketLib &operator=(const SocketLib &) = delete;
	SocketLib(SocketLib &&) = delete;
	SocketLib &operator=(SocketLib &&) = delete;
	bool open();
	void close();
};

// Option A2 (lean window): no ACE systemUse/systemUnuse (they spend two VBL waits, ~40 ms).  With the master interrupt off the OS's
// own level 1/2/6 vectors (exec software interrupts, cia.resource dispatch to the ACE handlers, EXTER) are swapped in over the ones ACE
// has installed, the interrupt enables the OS needs (SOFTINT, PORTS, EXTER) are added to the game's, and the master comes back on; the
// destructor undoes it exactly (vectors and INTENA).  The game's level 3 (VBL, BLIT, COPER) and 4 (audio) stay installed unless
// the level mask says otherwise, so the music tick and the frame counter never miss a frame (docs/NETPLAY.md 2.1).
// The vectors are the ones the OS had before ACE took over (NetTransport reads them while the OS is still whole).
class LeanNetWindow {
public:
	LeanNetWindow(const ULONG *pulOsVec, ULONG ulVbr, UWORD uwLevelMask, ULONG &ulCostLines);
	~LeanNetWindow();
	LeanNetWindow(const LeanNetWindow &) = delete;
	LeanNetWindow &operator=(const LeanNetWindow &) = delete;
	LeanNetWindow(LeanNetWindow &&) = delete;
	LeanNetWindow &operator=(LeanNetWindow &&) = delete;
private:
	const ULONG *m_pulOsVec;
	volatile ULONG *m_pVec;      // the vector table (VBR)
	ULONG m_aulAceVec[8];        // the game's/ACE's vectors for levels 1..6, put back by the destructor
	UWORD m_uwMask;              // bit n-1 set: level n is swapped
	UWORD m_uwIntena;            // INTENAR at entry
	ULONG &m_ulCost;
	LONG m_lStart;
};

// One socket descriptor (CloseSocket). Needs the library and the OS.
class Socket {
public:
	Socket() : m_lFd(-1) {}
	~Socket();
	Socket(const Socket &) = delete;
	Socket &operator=(const Socket &) = delete;
	Socket(Socket &&) = delete;
	Socket &operator=(Socket &&) = delete;
	void adopt(LONG lFd);
	void close();
	LONG release() { const LONG lFd = m_lFd; m_lFd = -1; return lFd; }   // hands the descriptor on without closing it
	LONG fd() const { return m_lFd; }
	bool isOpen() const { return m_lFd >= 0; }
private:
	LONG m_lFd;
};

// A non-blocking outgoing TCP connection to the lobby server: the host's lobby listing (rt/net_lobby), the relay's idle connection (rt/net_relay) and the
// game browser's request (rt/net_find) each own one.  Members need the library and the OS (inside a window).  The destructor closes it.
class OutConn {
public:
	OutConn() : m_isConnecting(false), m_isFailed(true) {}
	bool begin(ULONG ulIp, UWORD uwPort);        // closes the old one; socket + non-blocking connect to the IPv4 address (network order); false = cannot
	LONG state();                                // 0 connecting, 1 connected, -1 failed / none
	LONG send(const void *pData, ULONG ulLen);   // bytes taken (0 = would block), < 0 = broken
	LONG recv(void *pBuf, ULONG ulCap);          // bytes (> 0), 0 = nothing now, < 0 = closed / broken
	void close();
	bool isOpen() const { return m_sSock.isOpen(); }
	LONG release();                              // the descriptor leaves (a game link takes it over); the connection is empty afterwards
private:
	Socket m_sSock;
	bool m_isConnecting;
	bool m_isFailed;
};

// The OS window for socket calls (the full ACE one): systemUse() ... systemUnuse(); adds its cost (beam lines) to ulCost.
class NetWindow {
public:
	explicit NetWindow(ULONG &ulCostLines);
	~NetWindow();
	NetWindow(const NetWindow &) = delete;
	NetWindow &operator=(const NetWindow &) = delete;
	NetWindow(NetWindow &&) = delete;
	NetWindow &operator=(NetWindow &&) = delete;
private:
	ULONG &m_ulCost;
	ULONG m_ulStart;
};

// Waits about ulMs milliseconds on the beam line, OUTSIDE any window: no OS call, no timer.device, so it cannot hang (the tick wait
// loops sleep here between two short windows; WaitSelect's timeout depends on the OS timer, which ACE's CIA setup does not keep running).
void netPauseMs(ULONG ulMs);

// Milliseconds on the network clock (ms::netClockRead: the VBL count and the accumulated beam line, the larger, monotonic).
// Starts at 1000 on the first call.  Never call it to wait INSIDE a lean window (the VBL belongs to the OS there).
ULONG netClockMs();

// ---- the transport -------------------------------------------------------------------------------------------------------
// The sockets of one network game.  Lives on the stack of gameMain for the whole run; the destructor closes everything (inside a window).
// Every member except window() and the cheap getters must be called from inside window().
class NetTransport {
public:
	NetTransport();
	~NetTransport();
	NetTransport(const NetTransport &) = delete;
	NetTransport &operator=(const NetTransport &) = delete;
	NetTransport(NetTransport &&) = delete;
	NetTransport &operator=(NetTransport &&) = delete;

	// Runs fn inside the right OS window; false (fn not run) when there is no TCP/IP stack.  Not re-entrant.
	template<typename Fn> bool window(Fn &&fn) {
		if(!m_isTried) {            // the very first window: ACE's, it opens bsdsocket.library
			m_isTried = true;
			NetWindow sWin(m_ulCost);
			m_isStack = m_sLib.open();
			if(m_isStack) {
				fn();
			}
			return m_isStack;
		}
		if(!m_isStack) {
			return false;
		}
		LeanNetWindow sWin(m_aulOsVec, m_ulVbr, 0x3F, m_ulCost);
		fn();
		return true;
	}
	bool hasStack() const { return m_isStack; }
	bool wasTried() const { return m_isTried; }

	// -- inside a window --
	bool hostListen(UWORD uwPort);               // socket, bind, listen, non-blocking
	bool listenAgain(UWORD uwPort);              // the same, but only the listen socket is replaced: the lobby, the relay and the links stay
	bool isListening() const { return m_sListen.isOpen(); }
	LONG hostAccept();                           // a new connection's link number (0..NET_LINKS-1), -1 = none (or all links in use)
	bool joinBegin(const char *szAddr, UWORD uwPort);   // non-blocking connect on link 0; false = cannot resolve / socket
	LONG joinState();                            // 0 connecting, 1 connected, -1 failed (also after joinBegin failed)
	LONG send(UBYTE ubLink, const void *pData, ULONG ulLen);   // bytes taken (0 = would block), < 0 = broken
	LONG recv(UBYTE ubLink, void *pBuf, ULONG ulCap);          // bytes (> 0), 0 = nothing now, < 0 = closed / broken
	void waitReadable(ULONG ulMs);               // WaitSelect on the listen socket and every connection, at most ulMs
	void closeLink(UBYTE ubLink);
	void closeListen();
	void closeAll();                             // every socket, the datagram socket included
	// -- UDP (ROADMAP 12.21, NETPLAY.md 2.2): one non-blocking datagram socket for the per-tick traffic of a session.  Addresses are the network-order words of
	// sockaddr_in (opaque to the engine, which only hands them back), ports in host order.  A datagram that cannot go is simply lost.
	bool udpOpen(UWORD uwBindPort);              // socket + bind (0 = an ephemeral port: a client); false = cannot (the session stays on TCP)
	bool isUdpOpen() const { return m_sUdp.isOpen(); }
	void udpClose() { m_sUdp.close(); }
	LONG udpSend(ULONG ulAddr, UWORD uwPort, const void *pData, ULONG ulLen);   // bytes sent, < 0 = not sent
	LONG udpRecv(void *pBuf, ULONG ulCap, ULONG &ulAddr, UWORD &uwPort);       // the datagram's length (> 0), 0 = none waiting; where it came from
	ULONG joinIp() const { return m_ulJoinIp; }  // the address joinBegin resolved (network order), 0 = none: where a client's datagrams go
	ULONG lobbyIp() const { return m_ulLobbyIp; }  // the lobby server's address (network order), 0 = not resolved: where a relayed link's datagrams go
	bool isLinkOpen(UBYTE ubLink) const { return ubLink < NET_LINKS && m_aConn[ubLink].isOpen(); }

	// -- the lobby connection (ROADMAP 12.18): one more non-blocking TCP socket, apart from the game's links.  Inside a window.
	// lobbyBegin resolves szHost once (gethostbyname blocks: only when bMayResolve) and keeps the address for the next attempts.
	bool lobbyBegin(const char *szHost, UWORD uwPort, bool bMayResolve);
	LONG lobbyState() { return m_lobby.state(); }                                 // 0 connecting, 1 connected, -1 failed / none
	LONG lobbySend(const void *pData, ULONG ulLen) { return m_lobby.send(pData, ulLen); }   // bytes taken (0 = would block), < 0 = broken
	LONG lobbyRecv(void *pBuf, ULONG ulCap) { return m_lobby.recv(pBuf, ulCap); }           // bytes (> 0), 0 = nothing now, < 0 = closed / broken
	void lobbyClose() { m_lobby.close(); }

	// -- the relay (ROADMAP 12.13a): a host the lobby could not connect back to keeps one IDLE connection to the lobby server; a client that arrives through the
	// relay makes the server forward its bytes on it.  relayBegin dials the address lobbyBegin resolved (false when none was).  relayHandOver turns the idle
	// connection into a game link: the first free link slot takes the socket and serves the bytes already read (pFirst) before it reads the stream; returns
	// the link number, -1 when every slot is in use (the connection is closed).  isLinkOpen / send / recv then work on it like on an accepted link.
	bool relayBegin(UWORD uwPort);
	LONG relayState() { return m_relay.state(); }
	LONG relaySend(const void *pData, ULONG ulLen) { return m_relay.send(pData, ulLen); }
	LONG relayRecv(void *pBuf, ULONG ulCap) { return m_relay.recv(pBuf, ulCap); }
	void relayClose() { m_relay.close(); }
	LONG relayHandOver(const UBYTE *pFirst, ULONG ulLen);

	// -- the game browser's request (ROADMAP 12.18): one short connection to the lobby per LIST_REQ / JOIN_REQ.  findBegin: 1 ok, 0 the name does not resolve /
	// no socket, (the caller knows about a missing stack from window()).  Resolves szHost like lobbyBegin (a menu may wait for the lookup).
	LONG findBegin(const char *szHost, UWORD uwPort);
	LONG findState() { return m_find.state(); }
	LONG findSend(const void *pData, ULONG ulLen) { return m_find.send(pData, ulLen); }
	LONG findRecv(void *pBuf, ULONG ulCap) { return m_find.recv(pBuf, ulCap); }
	void findClose() { m_find.close(); }

	ULONG windowCost() const { return m_ulCost; }    // beam lines spent in windows so far (statistics)

private:
	bool resolveLobby(const char *szHost, bool bMayResolve);   // m_ulLobbyIp / m_szLobbyHost for szHost (false = cannot)
	bool openListen(UWORD uwPort);               // socket, bind, listen on a fresh listen socket (the old one is closed)
	void closeStreams();                         // everything but the datagram socket (a new listen or connect keeps the session's UDP socket)

	SocketLib m_sLib;
	Socket m_sListen;
	Socket m_sUdp;                               // the session's datagram socket (udpOpen)
	Socket m_aConn[NET_LINKS];
	ms::NetPushback m_aPush[NET_LINKS];          // bytes in front of a link's stream (a relay connection handed over: the client's first bytes)
	OutConn m_lobby;                             // the lobby server (rt/net_lobby)
	OutConn m_relay;                             // the host's idle relay connection (rt/net_relay)
	OutConn m_find;                              // the game browser's request (rt/net_find)
	ULONG m_ulLobbyIp;                           // resolved address (network order), 0 = not resolved yet
	ULONG m_ulJoinIp;                            // the address of the last joinBegin (network order)
	char m_szLobbyHost[48];                      // the name m_ulLobbyIp belongs to
	bool m_isTried, m_isStack;
	bool m_isConnecting;                         // link 0 is a client connect in progress
	bool m_isConnectFailed;
	ULONG m_ulVbr;
	ULONG m_aulOsVec[8];                         // the OS's VBR[24 + level], levels 1..6 (index 1..6)
	ULONG m_ulCost;
};

// The transport of this run (gameMain's), nullptr before it exists / after it is gone.
NetTransport *netTransport();

// The socket call the main loop stands in right now (NetCall, 0 = none), for the PERF line: a hang inside an OS call leaves its code standing.
enum NetCall : UBYTE {
	NETCALL_NONE = 0,
	NETCALL_ACCEPT = 1,
	NETCALL_NONBLOCK = 2,
	NETCALL_SEND = 3,
	NETCALL_RECV = 4,
	NETCALL_CLOSE = 5,
	NETCALL_SELECT = 6,
	NETCALL_OUT_SEND = 7,
	NETCALL_OUT_RECV = 8,
	NETCALL_CONNECT = 9,
	NETCALL_SOCKET = 10,
	NETCALL_SOCKOPT = 11,   // getsockopt(SO_ERROR)
	NETCALL_RESOLVE = 12,   // inet_addr / gethostbyname
	NETCALL_BIND = 13,      // setsockopt / bind / listen of the host socket
	NETCALL_UDP_SEND = 14,  // sendto (ROADMAP 12.21)
	NETCALL_UDP_RECV = 15,  // recvfrom
};
UBYTE netTransportCall();

// Where inside a stage of the session's step() the main loop stands (the PERF line's last number; 0 = nowhere in particular): the lobby part sets
// 40 + its phase and 45..49 for its socket callbacks, the relay part 50.., the rejoin part 60.., so a freeze names the part, not only the stage.
inline volatile UBYTE g_ubNetSub = 0;
inline void netSub(UBYTE ubSub) { g_ubNetSub = ubSub; }
enum NetSub : UBYTE {
	NETSUB_NONE = 0,
	NETSUB_LOBBY_PHASE = 40,   // + NetLobbyPhase (40..46), set when a due step starts
	NETSUB_LOBBY_BEGIN = 47,   // the lobby socket callbacks (begin: resolves + connects)
	NETSUB_LOBBY_STATE = 48,
	NETSUB_LOBBY_IO = 49,      // send / recv / close
	NETSUB_RELAY_PHASE = 50,   // + NetRelayPhase (50..55)
	NETSUB_RELAY_IO = 56,
	NETSUB_REJOIN_ENTER = 60,
	NETSUB_REJOIN_LISTEN = 61,
	NETSUB_REJOIN_ACCEPT = 62,
	NETSUB_REJOIN_RELAY = 63,
	NETSUB_REJOIN_PUMP = 64,
	NETSUB_REJOIN_CLOSE = 65,
	NETSUB_WIN_ENTER = 70,     // pumpForScreen: before the window opens (a freeze here = the clock read or the window's entry)
	NETSUB_WIN_EXIT = 71,      // pumpForScreen: the window is closed again (sub 99 with the stage of the last part = still in the window's exit: the interrupt restore)
	NETSUB_DONE = 99,          // step() is over (the stage number then stands for the part that ran last)
};

}  // namespace rt
