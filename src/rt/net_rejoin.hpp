// rt/net_rejoin - a dropped player comes back (ROADMAP M12, docs/NETPLAY.md 4.5): the runtime glue of engine/netrejoin (the host's listener and the
// rejoiner's client machine), game/net_world (the snapshot) and game/net_rejoin (what the return does to the game).
//
//   host     rejoinHostBegin at START keeps the tokens and the CONFIG of the game; while a seat is away the listen socket (and the relay's idle connection)
//            stay open and the lobby listing stays "Playing"; a REJOIN is answered at once, a short note says "<NAME> REJOINS AT THE NEXT TURN" and the game
//            plays on.  At the next map turn (netTurnBoundary, called by the game's turn scene on every node) the host snapshots the world, sends it,
//            compares the hashes and takes the player back - or, when nothing checks out, plays on without it.
//   others   a client that stays waits at the map turn for the host's BOUNDARY (only while somebody is away) and takes the returning nodes back at the same tick.
//   rejoiner the Multiplayer screen's REJOIN (the remembered session) connects, says REJOIN, and leaves for the game like a lobby start; the game builds the
//            knights from the CONFIG, and at the end of the campaign set-up (netRejoinApplyStep) waits for the host's map turn, loads the snapshot, answers
//            with its hash, and the lockstep starts at the map scene (netRejoinGo) on the host's tick and clock.
#pragma once
#include <ace/types.h>

#include "engine/netnode.hpp"
#include "engine/netrejoin.hpp"
#include "rt/net.hpp"

namespace rt {

// ---- the memory of a client (what a REJOIN needs) ----------------------------------------------------------------------------
struct RejoinMemory {
	bool isSet;
	UBYTE ubVia;               // 0 direct (szAddr:uwPort), 1 relay (the lobby server, ulSession)
	char szAddr[48];
	UWORD uwPort;
	ULONG ulSession;           // relay session id
	ULONG ulGameId;            // the lobby's id of the game (FIND GAME: JOIN_REQ with the rejoin flag), 0 = unknown
	UBYTE ubNode;
	ULONG ulToken;
	char szHost[ms::NET_NAME_LEN];   // the game's name (the host player's name), for the screens
};
const RejoinMemory &rejoinMemory();
void rejoinRemember(const RejoinMemory &sMem);
void rejoinForget();
// FIND GAME told us the lobby's id of the game we joined (JOIN_REQ was answered): keep it with the memory.
void rejoinNoteGameId(ULONG ulGameId);
// The game as it started (the CONFIG): the bindings and portraits a returning player gets back.  Called when a lockstep begins.
void rejoinNoteStart(const ms::NetConfig &cfg);

// ---- the host, called by rt/net_session.cpp ---------------------------------------------------------------------------------
// START went out: keeps the tokens, the bindings of the start and the port.
void rejoinHostBegin(const ms::NetNode &sNode, uint16_t uwPort);
// Inside a window, every step of the session: opens / serves the rejoin listener while a seat is away.  Cheap (a compare) when nobody is away.
void rejoinHostService(NetTransport &sT);
// The node dropped these nodes (bit n): they may rejoin.
void rejoinHostDropped(UBYTE ubNode);
// The host's node ended with every client gone but the seats are rejoinable: the session goes on as "solo" (true), nothing is closed except the links.
bool rejoinHostEnterSolo();
bool rejoinHostActive();
// True while the lobby listing and the relay should stay up (a seat is away).
bool rejoinHostKeepsListing();
// The game quits or the session is over for good.
void rejoinHostEnd();
// The solo host's idle pump (the game plays on alone, the listener must be served now and then): a window at most every 100 ms.
void rejoinHostIdle();

// ---- the rejoiner, called by rt/net_session.cpp ------------------------------------------------------------------------------
// Connects to the remembered session; progress through rejoinClientStatus.
void rejoinClientStart(const char *szName);
struct RejoinStatus {
	UBYTE ubState;             // 0 none, 1 connecting, 2 asking (REJOIN sent), 3 accepted (waiting for the turn), 4 failed
	UBYTE ubReason;            // NetRejoinFail when failed
};
void rejoinClientStatus(RejoinStatus &sOut);
// The Multiplayer screen leaves for the game: the CONFIG goes to the knights.  False: not accepted.
bool rejoinClientBegin();
bool rejoinClientConfig(ms::NetConfig &sOut);
void rejoinClientCancel();

}  // namespace rt
