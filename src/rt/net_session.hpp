// rt/net_session - the network game of this run (ROADMAP 12.8b): glues engine/netnode (handshake, lockstep ring, hashes) to the TCP transport
// (rt/net) and to the deterministic tick (rt/net_tick).  Design: docs/NETPLAY.md; flow in the comment at the top of net_session.cpp.
//
//   Multiplayer screen   netSessionHostStart / JoinStart / Poll / Close (the NetSeam of game/net_menu.hpp, rt/net_stub.cpp)
//   the lobby            netSessionLobbyPoll / Pick / Mode / Start (the LobbySeam of game/scene_lobby.hpp = the knight screen; NOT in lockstep)
//   after the lobby      netSessionBegin(): the lockstep starts on all nodes at the same tick (frame hook, seed hook, deterministic mode);
//                        netSessionGameConfig(): the CONFIG the knight records are built from (game/net_knights.hpp), once
//   every tick           frameHook (installed with netSetFrameHook): ONE OS window per tick moves the bytes, waits for the tick, merges
//   the end              a BYE, a lost link, a silent peer (TIMEOUT) or a hash mismatch ends the session; the game goes on locally
#pragma once
#include <ace/types.h>

#include "engine/netnode.hpp"
#include "engine/netproto.hpp"
#include "game/net_menu.hpp"
#include "game/scene_lobby.hpp"
#include "rt/net.hpp"

namespace rt {

// The command line (kept for the Multiplayer screen's defaults and the session parameters: NAME, PORT, DELAY, TIMEOUT).
void netSessionSetArgs(const NetArgs &sArgs);
const NetArgs &netSessionArgs();
// net.ini `udp = yes|no` (default yes): no keeps the per-tick traffic on TCP, like the command line's TCP (ROADMAP 12.21, NETPLAY.md 2.2).  Read before a session starts.
void netSessionAllowUdp(bool isAllowed);
// The UDP counters of the running session (the PERF line, the session summary); false = this session does not use UDP (disabled, no socket, no node).
bool netSessionUdpStats(ms::NetUdpStats &sOut);

// ---- the Multiplayer screen ----------------------------------------------------------------------------------------------------
// PUBLIC yes / no and the lobby address (net.ini `lobby`, `lobby_port`; empty / 0 = the built-in lobby) for the next netSessionHostStart (ROADMAP 12.18).
void netSessionHostOptions(bool isPublic, const char *szLobby, uint16_t uwLobbyPort);
bool netSessionHostStart(uint16_t uwPort, const char *szName);   // false = no TCP/IP stack / cannot listen
void netSessionJoinStart(const char *szAddr, uint16_t uwPort, const char *szName);
// The join of the game browser (ROADMAP 12.13a / 12.18) when the lobby named a relay session: connect to the LOBBY server (the options of
// netSessionHostOptions), say RELAY_HELLO(session, client) first, then the usual HELLO; progress and the lobby as after netSessionJoinStart.
void netSessionJoinRelay(uint32_t ulSession, const char *szName);
// This build's fingerprint (HELLO's, the lobby listing's and the game browser's LIST_REQ).
uint32_t netSessionBuildHash();
void netSessionPoll(ms::game::NetPoll &sOut);
void netSessionClose();                                          // drop the listen socket / the connection

// ---- the lobby = the knight screen (docs/NETPLAY.md 4.2b) -----------------------------------------------------------------------------
void netSessionLobbyPoll(ms::game::LobbyView &sOut);             // pumps the transport (one short window at most every two frames) and fills the view
void netSessionLobbyPick(const ms::NetLobbyPick &sPick);         // this player's pick: the host applies it, a client sends LOBBY_PICK
void netSessionLobbyMode(uint8_t ubMode);                        // host: the MODE line
bool netSessionLobbyStart();                                     // host: START (needs another player, everybody picked): CONFIG to every client; false = refused

// ---- the lockstep ------------------------------------------------------------------------------------------------------------------
// Called on both nodes right after the Multiplayer screen was left with START: host waits for every READY, then the deterministic mode
// goes on at tick 0.  False = no session (the game continues locally).
bool netSessionBegin();
// The CONFIG of the session that just began (the picks of the lobby), handed out ONCE: the game builds its knights from it.  False = none pending.
bool netSessionGameConfig(ms::NetConfig &sOut);
// MS_AUTOPLAY diagnostics for a hang: the last tick the frame hook entered and the stage it reached (1 enter, 2 sent, 3 waiting, 4 wait over, 5 left).
ULONG netSessionTraceTick();
UBYTE netSessionTraceStage();
UBYTE netSessionTraceNode();   // the node pump's own mark (engine NN_TRACE_*): which part of netNodePump a freeze stands in
bool netSessionRunning();   // lockstep is running
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
// MS_AUTOPLAY test hook (`poke netsilent 1|0`, tests/boot/net_pair_join.txt): while on, this node's frame hook sends nothing and only serves the sockets
// (the peer's tick stalls and shows its WAITING FOR box) until the peer drops us, the session ends or the poke is cleared.
void netSessionAutoplaySilence(bool isSilent);
#endif
// The game quits (gameMain, before the sockets go): BYE(normal) to every peer, so the others drop this player at once / play on alone (ROADMAP 12.11).
void netSessionShutdown();

// ---- the ending (docs/NETPLAY.md 4.6) -------------------------------------------------------------------------------------------------
// The quest is complete (the stone at Stonehenge, called by the place code just before the program overlay plays the ending): the host's lobby listing
// goes and nobody can rejoin any more.  No-ops unless a network game runs.
void netSessionQuestComplete();
// The knight record whose colours the ending shows on THIS machine (game/ending_knight.hpp): the winner when it sits here, else the first record
// bound to this node; -1 when no lockstep runs (classic / local co-op / a node playing on alone: the winner stays).
int netSessionEndingKnight(uint8_t ubWinner);
// The class-colour kind a non-fight screen draws on THIS machine for the knight record ubShown (game/local_knight.hpp): this node's own knight in a
// network game, ulShownKind itself otherwise (and for a record that is no player's).  Display only.
uint32_t netSessionLocalKind(uint8_t ubShown, uint32_t ulShownKind);
// The ending is over, the game is about to re-enter the main menu: BYE(normal), the sockets close, the lockstep and the deterministic mode end, the
// rejoin memory is dropped.  The ending is outside the lockstep, so nothing was exchanged during it.  No-op unless a lockstep is still marked running.
void netSessionCloseAfterEnding();

// ---- rejoin (docs/NETPLAY.md 4.5, rt/net_rejoin.hpp) ---------------------------------------------------------------------------------
// The Multiplayer screen's REJOIN row: true (and the host's name) when a game this machine was dropped from is remembered and no lockstep runs here.
bool netSessionRejoinAvailable(char (&acHost)[ms::NET_NAME_LEN]);
void netSessionRejoinStart(const char *szName);     // connect to the remembered game and say REJOIN; progress through netSessionPoll
bool netSessionBeginRejoin();                       // the screen leaves for the game with the world in hand (NetPoll::bRejoined): false = not ready
// The game's three hooks of a rejoin (called whether or not a network game runs; each is a branch unless one does):
void netTurnBoundary();                             // the map turn (the turn scene): the host takes players back, the others take them too
void netRejoinApplyStep();                          // the campaign set-up of a rejoiner has built the knights: the world goes over them
void netRejoinGo();                                 // the map scene is about to start: the rejoiner's lockstep begins on the host's tick


}  // namespace rt
