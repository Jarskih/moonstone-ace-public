// engine/netudp - the pure half of per-tick input over UDP with redundant history (ROADMAP 12.21, docs/NETPLAY.md 2.2).  No sockets, no globals, no heap.
//
// The lockstep sends one small message per tick in each direction (the client's INPUT, the host's merged TICK).  Over TCP a lost segment stalls
// the stream until the retransmit; over UDP every datagram carries the last NET_UDP_HISTORY_MAX ticks the peer has not acknowledged, so a lost
// datagram costs nothing as long as the next one gets through.  TCP stays for everything that must arrive exactly once and in order but is rare
// (HELLO, CONFIG, READY, SEED, HASH, DROP, BYE, PING ...) and is the fallback for the ticks:
//
//   NetUdpLink        one per link (host: per client, client: link 0).  The state of the probe handshake, the peer's acknowledgement of our ticks,
//                     the timers.  Embedded in the node's link record, so it is zeroed with it.
//   netUdpPlan        what to send NOW for that link: one datagram (the window of unacked ticks) and/or ticks to send over TCP once (the history
//                     cannot cover them, or the datagrams are not getting through).  Pure function of the link state, the clock and the production
//                     counter.
//   netUdpOnDatagram  a valid datagram arrived: the peer's ack, the handshake.
//   netUdpHeld*       the acks WE put into our datagrams: how many ticks we hold, from the session's rings.
//
// States: OFF (not started, or UDP not wanted) -> PROBING (datagrams without frames both ways, a few seconds at most) -> ACTIVE (the ticks travel
// in datagrams) -> TCP (fell back for good: nothing answered, or a loss streak).  A TCP link never comes back to UDP in the same session.
#pragma once
#include <stdint.h>

#include "engine/netplay.hpp"
#include "engine/netproto.hpp"

namespace ms {

constexpr uint32_t NU_PROBE_MS = 250;            // probe datagram interval while the handshake is open
constexpr uint32_t NU_PROBE_GIVEUP_MS = 3000;    // pump time without a two-way handshake after which the link stays on TCP
constexpr uint32_t NU_PUMP_GAP_MAX_MS = 250;     // a longer gap between two plans (a scene load) counts as this much towards the give-up
constexpr uint32_t NU_ACK_DELAY_MS = 80;         // an ack that no datagram carried yet is sent alone after this long (normally the next tick's datagram carries it)
constexpr uint32_t NU_RESEND_MS = 60;            // unacked ticks are sent again after this long without a datagram (only matters in a stall: while ticks flow, the next tick's datagram repeats them)
constexpr uint32_t NU_ESCALATE_MS = 250;         // the peer's ack has not moved this long (pump time, see ulStuckMs): the unacked ticks also go over TCP, once
constexpr uint32_t NU_LOSS_MS = 5000;            // ... this long: the link reverts to TCP for the rest of the session
constexpr uint32_t NU_ALIVE_MS = 2500;           // a peer that sent nothing at all (no datagram, no TCP: its PINGs come every second) this long is not pumping: its silence is no loss
constexpr uint32_t NU_KEEPALIVE_MS = 4000;       // an ack-only datagram when nothing else went out (a NAT forgets quiet UDP mappings, the relay expires members)
constexpr uint8_t NU_RX_ROUNDS = 16;
// ---- relay-assisted direct path (NETPLAY.md 2.2b) ----
constexpr uint32_t NU_PUNCH_REG_MS = 500;        // PUNCH_REG is repeated this often while the relay has not answered with PUNCH_INFO
constexpr uint32_t NU_PUNCH_INFO_GIVEUP_MS = 8000;   // pump time without PUNCH_INFO (an old relay, a peer that cannot do it): the link stays on the relay
constexpr uint32_t NU_PUNCH_PROBE_MS = 100;      // direct probes go to every candidate address this often (both ends send at once: that opens the NAT mappings)
constexpr uint32_t NU_PUNCH_GIVEUP_MS = 5000;    // pump time of probing without a two-way direct handshake: the link stays on the relay
constexpr uint32_t NU_DIRECT_STALL_MS = 400;     // on the direct path the peer's ack has not moved this long (pump time): back to the relay path (no TCP escalation meanwhile: it would move the acks and hide the stall)             // datagrams one pump reads at most

enum NetUdpState : uint8_t {
	NU_OFF = 0,        // not started (or UDP not wanted on this link)
	NU_PROBING,        // probe datagrams are exchanged; the ticks still travel over TCP
	NU_ACTIVE,         // the ticks travel in datagrams, TCP carries only what the history cannot cover
	NU_TCP             // fell back to TCP for the session (ubWhy says why)
};

enum NetUdpWhy : uint8_t {
	NUW_NONE = 0,
	NUW_DISABLED,      // the player forced TCP (net.ini udp = no, TCP on the command line)
	NUW_NO_SOCKET,     // the transport has no UDP socket (no stack, bind failed), or the link has no peer address
	NUW_NO_ANSWER,     // the handshake did not complete in NU_PROBE_GIVEUP_MS
	NUW_LOSS,          // the peer's acks stopped for NU_LOSS_MS
	NUW_REJOIN,        // a rejoiner's link: always TCP
	NUW_REASONS
};

// The address of the other end of a datagram path: IPv4 address and port in host order (the transport converts).
struct NetDgramPeer {
	uint32_t ulAddr;
	uint16_t uwPort;
	bool isSet;
};

// ---- the direct path of a relayed link ----
//   OFF     not wanted (a direct game, the player disabled it, or the link is not started)
//   REG     PUNCH_REG goes to the relay every NU_PUNCH_REG_MS until PUNCH_INFO tells the peer's endpoints
//   PUNCH   probes (datagrams with NET_UDP_DIRECT) go to the peer's candidate addresses; a two-way handshake (NET_UDP_DHEARS) completes it
//   DIRECT  this side's datagrams go to NetDirect::peer (the address the peer's direct datagrams came from); the relay path stays alive with keep-alives
//   RELAY   gave up or lost the direct path for good (ubWhy says why): everything goes via the relay
// Each direction chooses its own path: the receiver accepts a valid datagram from either, so a one-way failure only moves the sender that notices it.
enum NetDirectState : uint8_t { ND_OFF = 0, ND_REG, ND_PUNCH, ND_DIRECT, ND_RELAY };
enum NetDirectWhy : uint8_t {
	NDW_NONE = 0,
	NDW_NO_INFO,       // the relay never sent PUNCH_INFO (an old relay, the peer cannot do it, or it never registered)
	NDW_NO_ANSWER,     // the punch handshake did not complete in NU_PUNCH_GIVEUP_MS (symmetric NAT, a firewall)
	NDW_LOST,          // on the direct path the acks stopped for NU_DIRECT_STALL_MS
	NDW_REASONS
};

struct NetDirect {
	uint8_t eState;            // NetDirectState
	uint8_t ubWhy;             // NetDirectWhy once RELAY
	NetDgramPeer sPublic;      // the peer's address as the relay sees it
	NetDgramPeer sLocal;       // the peer's own (LAN) address: only when it is behind the same public address
	NetDgramPeer peer;         // the source of the peer's latest accepted direct datagram: where direct datagrams go
	bool isPeerLocal;          // peer is the local candidate (preferred: a LAN path beats a hairpinned or punched one)
	bool isHeard;              // a direct datagram of the peer arrived
	bool isReplyDue;           // answer a direct datagram at once (the handshake takes three datagrams, not three probe intervals)
	bool isRegSent;
	uint32_t ulLastPlan;
	uint32_t ulBudgetMs;       // pump time spent in REG or PUNCH (the give-up budgets)
	uint32_t ulLastReg, ulLastProbe;
	uint32_t ulLastRelayTx;    // clock of the last datagram that went via the relay (the relay path needs its keep-alive while DIRECT)
	uint32_t ulDirectSince;    // clock when the state became DIRECT
};

// What the direct state machine wants now: a PUNCH_REG to the relay, probes to up to three addresses, a failure to log.
struct NetDirectPlan {
	bool isReg;
	uint8_t ubProbes;          // destinations in aTo
	NetDgramPeer aTo[3];
	bool isFailed;             // the state moved to RELAY now (ubWhy)
};

struct NetUdpLink {
	uint8_t eState;            // NetUdpState
	uint8_t ubWhy;             // NetUdpWhy once TCP
	NetDgramPeer peer;         // where datagrams for this link go (client: preset; host: preset for a relayed link, else learned from the first datagram)
	uint32_t ulSession;        // the session id in our datagrams to this peer (relay session, 0 = direct)
	bool isHeard;              // a valid datagram of this peer arrived
	bool isReplyDue;           // PROBING: a datagram arrived, answer at once (the handshake takes three datagrams, not three probe intervals)
	bool isAckDirty;           // the ack we would send differs from the one in our last datagram
	uint32_t ulAckSent;        // the ack in our last datagram
	uint32_t ulAckDirtyAt;     // since when
	bool isPeerSilent;         // the node's word: nothing at all came from the peer for NU_ALIVE_MS (it is loading a scene, frozen): the stall clock stands still
	bool isProbed;             // a probe went out at least once
	uint32_t ulProbeMs;        // pump time spent probing (the give-up budget)
	uint32_t ulLastPlan;       // clock of the last plan (budget accounting)
	uint32_t ulLastProbe;
	uint32_t ulLastTx;         // clock of the last datagram
	uint32_t ulPeerAck;        // the peer holds every tick of ours below this one
	uint32_t ulStuckMs;        // pump time with ticks unacked and the peer's ack not moving while the peer was alive: the escalation and the loss rule run on it
	uint32_t ulSentNext;       // the first tick no datagram has carried yet
	uint32_t ulTcpNext;        // ticks below this one were also sent over TCP (or the peer got them there)
	NetDirect dir;             // the direct path of this (relayed) link
};

struct NetUdpPlan {
	bool isSend;               // send one datagram now
	bool isProbe;              // ... it carries no frames and is a handshake probe (state PROBING)
	uint32_t ulFirst;          // the datagram's window: ticks ulFirst .. ulFirst + ubCount - 1 (ascending)
	uint8_t ubCount;
	uint32_t ulTcpFrom, ulTcpTo;   // ticks [from, to) go over TCP now, once (empty when equal)
	bool isFellBack;           // the plan moved the link to TCP (loss or no answer): the node logs it
	bool isDirect;             // the datagram goes to NetDirect::peer with NET_UDP_DIRECT (else to NetUdpLink::peer, the relay or the direct game's host)
	bool isRelayKeep;          // an extra ack-only datagram via the relay: the direct path is up and the relay path must not go stale
	bool isDirectLost;         // the plan moved the direct path to RELAY (stall): the node logs it
};

void netUdpLinkInit(NetUdpLink &u);
// OFF -> PROBING (the handshake starts), or straight to TCP when the transport has no UDP / the peer address is unknown to a client.
void netUdpStart(NetUdpLink &u, uint32_t ulNow);
// To TCP for good, with the reason.
void netUdpFallback(NetUdpLink &u, uint8_t ubWhy);

// Time is counted in PUMP time like the handshake: a gap between two plans (this side loaded a scene, 10 s without a pump) counts at most NU_PUMP_GAP_MAX_MS,
// and no time counts while the node says the peer is silent (u.isPeerSilent: it is not pumping, TCP would stall just the same).  So only a path on which the
// peer is alive (its PINGs arrive over TCP) and the datagrams still do not get through reverts to TCP.
//
// What to do now.  ulProduced = the first tick this side has not produced yet (the host's ulNextBroadcast, the client's ses.ulNextSend); ulFloor = ticks below it
// are known to be held by the peer without an ack (the client's consumed ticks: the host cannot have run them without the client's input; a host passes 0);
// ulOurAck = the ack this side would put into a datagram now (netUdpHeldTicks / netUdpHeldInputs): when it moved and no datagram has carried it for
// NU_ACK_DELAY_MS, an ack-only datagram goes out.
void netUdpPlan(NetUdpLink &u, uint32_t ulNow, uint32_t ulProduced, uint32_t ulFloor, uint32_t ulOurAck, NetUdpPlan &out);

// A valid datagram of the peer arrived.  ubFlags = its flags, ulAck = its ack of our ticks.  Returns true when this datagram completed the handshake
// (state ACTIVE now).
bool netUdpOnDatagram(NetUdpLink &u, uint32_t ulNow, uint8_t ubFlags, uint32_t ulAck, uint32_t ulProduced);

inline bool netUdpIsActive(const NetUdpLink &u) { return u.eState == NU_ACTIVE; }
// True when the host's drop counter is not ahead of the receiver's own: a datagram sent after a DROP must not be applied before the DROP (TCP) has been.
bool netUdpDropSeqOk(uint8_t ubHostSeq, uint8_t ubMySeq);

// ---- the direct path ----------------------------------------------------------------------------------------------------------
// OFF -> REG.  Call when the link starts probing and the link is relayed and direct is wanted.
void netDirectStart(NetDirect &d, uint32_t ulNow);
// What to send for the direct path now (REG: PUNCH_REG repeats, PUNCH: probes).  Budgets run on pump time like the probe handshake.
void netDirectPlan(NetDirect &d, uint32_t ulNow, NetDirectPlan &out);
// The relay's PUNCH_INFO: REG -> PUNCH.  ulLocalAddr / uwLocalPort count only with isLocalValid (and a non-zero port).
void netDirectOnInfo(NetDirect &d, uint32_t ulNow, const NetDgramPeer &sPublic, bool isLocalValid, uint32_t ulLocalAddr, uint16_t uwLocalPort);
// A valid datagram that carries NET_UDP_DIRECT arrived from sFrom.  Returns true when it completed the handshake (state DIRECT now).
bool netDirectOnDatagram(NetDirect &d, uint32_t ulNow, const NetDgramPeer &sFrom, uint8_t ubFlags);
// To RELAY for good, with the reason (no effect when already RELAY).
void netDirectFail(NetDirect &d, uint8_t ubWhy);
inline bool netDirectIsUp(const NetDirect &d) { return d.eState == ND_DIRECT; }
// The flags a datagram to the peer carries: NET_UDP_HEARS when anything was heard, plus DIRECT / DHEARS for the direct path.
uint8_t netUdpFlags(const NetUdpLink &u, bool isDirect);

// ---- the acks we send ---------------------------------------------------------------------------------------------------------
// The client's ack of the host's TICKs: every tick below the result is complete in its ring.  Counts the complete ticks ahead of the current one as well, unless a
// node is dropped (a rejoin may still change the frames of the ticks ahead: they are not acknowledged until the game consumes them).
uint32_t netUdpHeldTicks(const NetSession &s);
// The host's ack of node ubNode's input: every tick below the result has its frame in the node's ring.
uint32_t netUdpHeldInputs(const NetSession &s, uint8_t ubNode);

}  // namespace ms
