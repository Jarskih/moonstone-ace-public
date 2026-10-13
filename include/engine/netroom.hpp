// engine/netroom - the lobby of a network game = the state of the knight screen before START (ROADMAP M12, docs/NETPLAY.md 4.2b).
// Pure: no ACE, no sockets, no globals, no heap.  src/engine/netnode.cpp keeps one NetRoom per node (the host's is the AUTHORITY, a
// client keeps a mirror of the host's LOBBY_STATE), src/game/scene_lobby.cpp draws it and turns the player's input into NetLobbyPicks.
//
// A SEAT is a player's place in the room: seat 0 is the host, seat k the client on link k - 1.  A seat keeps its number (so the colour of
// its selector) until its player leaves.  The seat goes through
//
//     free --join--> browsing --claim--> claimed --done--> done          (any joined seat --browse--> browsing: lets go of the portrait)
//
// `sel` is the portrait under the selector while browsing and the player's knight once claimed / done.  Browsing selectors may overlap (they
// are invisible to the others), picked knights never do; when somebody picks a knight, the others browsing it move on.  A portrait is HELD by the
// first seat that claims it, so two players who press fire on the same portrait "at the same time" are settled by the order in which the
// HOST processes the claims (the loser is put back to browsing on the next free portrait).  A player who leaves frees the portrait.
//
// START (host only) is enabled when at least one other player has joined and everybody has picked.
#pragma once
#include <stdint.h>

#include "engine/netproto.hpp"

namespace ms {

constexpr uint8_t ROOM_SEATS = NET_NODE_MAX;
constexpr uint8_t ROOM_NAME_SHOWN = 13;          // the knight name editor of the original takes 13 characters at most

struct NetRoom {
	uint8_t ubMode;                  // NetMode: the host's CO-OP / CLASSIC choice
	NetSeatRec aSeat[ROOM_SEATS];
};

// A room with the host in seat 0 (browsing the first portrait), co-op mode (owner 2026-10-10: CO-OP default).
void roomInit(NetRoom &r, const char *szHostName);
// A player takes seat ubSeat (browsing roomFirstUnused: the next knight nobody stands on).  False: seat out of range or not free.  An empty name becomes PLAYER.
bool roomJoin(NetRoom &r, uint8_t ubSeat, const char *szName);
// The player of the seat left: the seat is free again and so is its portrait.  Seat 0 (the host) cannot leave.
void roomLeave(NetRoom &r, uint8_t ubSeat);
// One LOBBY_PICK of a seat (wire-valid).  True: the room changed.  The rules are in the file comment; a pick that cannot apply (claim of a
// held portrait, done without a claim, a pad that cannot be chosen) changes nothing or, for a lost race, puts the seat back to browsing.
bool roomApplyPick(NetRoom &r, uint8_t ubSeat, const NetLobbyPick &p);
// The host's mode line.  True: it changed.
bool roomSetMode(NetRoom &r, uint8_t ubMode);

uint8_t roomJoined(const NetRoom &r);                                             // seats in use
bool roomIsJoined(const NetRoom &r, uint8_t ubSeat);
// The portrait is claimed or picked by a seat other than ubExceptSeat.
bool roomIsHeld(const NetRoom &r, uint8_t ubPortrait, uint8_t ubExceptSeat);
// Which seat holds the portrait (claimed or done); ROOM_SEATS when nobody.
uint8_t roomHolder(const NetRoom &r, uint8_t ubPortrait);
// The first portrait from ubFrom (inclusive, wrapping) that no seat but ubSeat holds.
uint8_t roomFirstFree(const NetRoom &r, uint8_t ubFrom, uint8_t ubSeat);
// Where a joiner's selector starts: the lowest portrait no other seat stands on (browsing or picked); when all four are stood on, the lowest
// nobody has picked.  The host (first in the room) therefore starts on portrait 0, the 2nd, 3rd, 4th player on 1, 2, 3.
uint8_t roomFirstUnused(const NetRoom &r, uint8_t ubSeat);
// The next portrait from ubFrom in direction sbDir (+1 / -1) that no seat but ubSeat holds; ubFrom itself when there is none.  No wrap
// (the original knight screen's selector stops at the ends).
uint8_t roomStep(const NetRoom &r, uint8_t ubFrom, int8_t sbDir, uint8_t ubSeat);
bool roomAllDone(const NetRoom &r);                                               // every joined seat has picked
// START may be pressed: the host and at least one other player are in, everybody has picked.
bool roomStartEnabled(const NetRoom &r);

// The room as the LOBBY_STATE the player of seat ubSeat is told.
void roomToState(const NetRoom &r, uint8_t ubSeat, NetLobbyState &out);
// A client takes over the host's state.  True: it differs from what the room held.
bool roomFromState(NetRoom &r, const NetLobbyState &s);
bool roomEqual(const NetRoom &a, const NetRoom &b);

// The nodes of the game START makes: the joined seats in seat order, node i = the i-th.  Returns the count; aubSeatOfNode[node] = seat.
uint8_t roomNodes(const NetRoom &r, uint8_t (&aubSeatOfNode)[ROOM_SEATS]);

}  // namespace ms
