// engine/netplay - the lockstep core of network play (ROADMAP 12.6, docs/NETPLAY.md sections 1, 3 and 4.2).  Pure: no ACE, no
// hardware, no sockets, no globals; src/rt/input.cpp (ROADMAP 12.4/12.9) fills the structs and calls these functions at the
// game's input poll, src/rt/net.cpp (12.7) moves the messages of engine/netproto between the machines.
//
// What it does, in the order a poll uses it:
//   1. the machine packs what its player did this poll into a NetFrame (pads, raw key events, mouse delta),
//   2. netLocalSubmit() files it as the input of tick k + D (D = input delay) and tells the caller which tick that is,
//   3. frames of the other nodes arrive (netPutFrame) until netTickReady() says tick k has the frames of every node,
//   4. netMerge() turns those frames plus the current game state (NetGameState) into the words the original code reads:
//      the "player stick" (port 1), port 0, the co-op pad frame, the keyboard events and the mouse delta,
//   5. netTickAdvance() moves to tick k + 1.
// The merge is a pure function of (game state, frames of one tick), and the game state is identical on every machine by
// determinism, so every machine computes the same merged input and the game runs in lockstep.
//
// A node is one machine; node 0 is the host.  At most NET_NODE_MAX = 4 (one knight per machine at most in classic play, up to
// four co-op members spread over the machines).
#pragma once
#include <stdint.h>

#include "engine/input.hpp"
#include "engine/pad.hpp"

namespace ms {

constexpr uint8_t NET_NODE_MAX = 4;
constexpr uint8_t NET_NODE_NONE = 0xFF;
constexpr uint8_t NET_NODE_DROPPED = 0xFE;   // a binding whose node was dropped (12.11): nobody steers it, like NONE, but its place screens are closed by the host
constexpr uint8_t NET_KNIGHT_NONE = 0xFF;
constexpr uint8_t NET_KNIGHTS_MAX = 4;      // knight records 0..3 are the human knights / co-op members (game/party.hpp PARTY_MAX)
constexpr uint8_t NET_KEYS_MAX = 4;         // raw key events carried by one frame
constexpr uint8_t NET_DELAY_MAX = 8;        // input delay limit (the ring below holds NET_RING_TICKS)
constexpr uint8_t NET_RING_TICKS = 16;      // power of two

// ---- the input frame ---------------------------------------------------------------------------------------------------------
//
// One node's input for one tick, 11 bytes on the wire (netFrameWrite; big-endian where a field is wider than a byte):
//
//   byte 0..3   ulPads     six pad words (ms::PadSource order JOY1, JOY0, ARROWS, WASD, ADAPTER3, ADAPTER4), 5 bits each: source i
//                          sits at bits 5i..5i+4 (kJoyRight 1, Left 2, Down 4, Up 8, Fire $10), bits 30/31 zero.  The two joystick
//                          words of the original are the sources JOY1 (port 1) and JOY0 (port 0), exactly as ms::padsFill
//                          stores them; the keyboard sets and the adapter are folded in by the sender, so a remote machine never
//                          has to know its key-down table.
//   byte 4      ubFlags    bits 0..2 number of key events (0..4), bit 3 left mouse button down, bits 4..7 zero
//   byte 5      sbMouseDx  mouse pointer counter delta since the previous poll, saturated to -127..127
//   byte 6      sbMouseDy
//   byte 7..10  aubKeys[4] raw key events in arrival order: bit 7 = pressed, bits 0..6 = translated game key (the index of
//                          the key-down table); unused entries 0.  Events beyond four in one poll are dropped (netFrameAddKey says
//                          so): a poll is 1/25 s or shorter.
//
// The frame is canonical: every bit pattern netFrameRead accepts is rebuilt exactly by netFrameWrite.
constexpr uint8_t NET_FRAME_BYTES = 11;
constexpr uint8_t NET_FLAG_KEYS_MASK = 0x07;
constexpr uint8_t NET_FLAG_MOUSE_BUTTON = 0x08;

struct NetFrame {
	uint32_t ulPads;
	uint8_t ubFlags;
	int8_t sbMouseDx, sbMouseDy;
	uint8_t aubKeys[NET_KEYS_MAX];
};

void netFrameClear(NetFrame &f);
bool netFrameEqual(const NetFrame &a, const NetFrame &b);
// Packs / unpacks the six pad words (each masked to its 5 bits).
void netFrameSetPads(NetFrame &f, const PadFrame &pads);
void netFrameGetPads(const NetFrame &f, PadFrame &pads);
// One pad word (0 for PAD_NONE or an unknown source).
uint16_t netFramePad(const NetFrame &f, uint8_t ubSource);
// Appends a key event; false (and nothing stored) when the frame already holds NET_KEYS_MAX events or the key is above 127.
bool netFrameAddKey(NetFrame &f, const KeyEvent &ev);
uint8_t netFrameKeyCount(const NetFrame &f);
KeyEvent netFrameKey(const NetFrame &f, uint8_t ubIndex);
// Mouse delta (saturated to -127..127) and the left button.
void netFrameSetMouse(NetFrame &f, int16_t swDx, int16_t swDy, bool isButton);
bool netFrameMouseButton(const NetFrame &f);
// Wire form: NET_FRAME_BYTES bytes.  Read returns false for a frame that is not canonical (key count above 4, reserved bits set,
// mouse delta -128).
void netFrameWrite(const NetFrame &f, uint8_t *pOut);
bool netFrameRead(const uint8_t *pIn, NetFrame &f);

// ---- the tick ring -----------------------------------------------------------------------------------------------------------
//
// The frames of one node for the ticks ulBase .. ulBase + NET_RING_TICKS - 1.  A tick below ulBase is history (already
// consumed), one at or above the end is too far ahead (the sender is more than a ring ahead: a protocol violation).
struct NetTickRing {
	uint32_t ulBase;                          // oldest tick not yet consumed
	uint16_t uwHave;                          // bit (tick & 15) set = the slot holds that tick's frame
	NetFrame aFrames[NET_RING_TICKS];
};

enum NetPut : uint8_t {
	NET_PUT_OK = 0,        // stored
	NET_PUT_DUP,           // already there with the identical frame (the host relays a frame the sender also sent): harmless
	NET_PUT_CONFLICT,      // already there with a DIFFERENT frame: desync evidence, the stored frame is kept
	NET_PUT_OLD,           // tick already consumed (or dropped node): ignored
	NET_PUT_FAR            // tick beyond the ring or unknown node: rejected
};

void netRingInit(NetTickRing &r, uint32_t ulBase);
NetPut netRingPut(NetTickRing &r, uint32_t ulTick, const NetFrame &f);
bool netRingHas(const NetTickRing &r, uint32_t ulTick);
bool netRingGet(const NetTickRing &r, uint32_t ulTick, NetFrame &f);
// Drops everything below ulNewBase (never moves the base backwards, never more than one ring forward).
void netRingConsume(NetTickRing &r, uint32_t ulNewBase);

// ---- the session: one ring per node, input delay, drops ------------------------------------------------------------------------
struct NetSession {
	uint8_t ubNodes;                       // 1..NET_NODE_MAX
	uint8_t ubLocal;                       // this machine's node
	uint8_t ubDelay;                       // input delay D in ticks, 0..NET_DELAY_MAX
	uint32_t ulTick;                       // the tick the game runs next
	uint32_t ulNextSend;                   // the tick the next netLocalSubmit files (starts at D)
	NetTickRing aRing[NET_NODE_MAX];
	bool abDropped[NET_NODE_MAX];
	uint32_t aulDropTick[NET_NODE_MAX];    // first tick with no input from the node (valid when abDropped)
};

// Starts a session at tick 0.  Ticks 0..D-1 are pre-filled with empty frames for every node (nobody could have sent input for
// them), so the game starts at once and the first real frames are those of tick D.  Nodes are clamped to 1..NET_NODE_MAX, the
// delay to NET_DELAY_MAX.
void netSessionInit(NetSession &s, uint8_t ubNodes, uint8_t ubLocal, uint8_t ubDelay);
// Files the local node's frame for tick k + D and returns that tick (the caller sends INPUT(tick, local, frame)).
uint32_t netLocalSubmit(NetSession &s, const NetFrame &f);
// Files a frame that arrived for a node (INPUT or one entry of TICK).  Frames of a dropped node at or after its drop tick are
// ignored (NET_PUT_OLD).
NetPut netPutFrame(NetSession &s, uint8_t ubNode, uint32_t ulTick, const NetFrame &f);
// Node bits (bit n = node n) that the tick still waits for: a dropped node does not count from its drop tick on.  0 = ready.
uint8_t netMissingMask(const NetSession &s, uint32_t ulTick);
bool netTickReady(const NetSession &s);                // the current tick has the frames of every live node
// The frames of the current tick, one per node, in node order; a node that is dropped at this tick, or does not exist, is an
// empty frame.  Only meaningful when netTickReady().
void netTickFrames(const NetSession &s, NetFrame (&aOut)[NET_NODE_MAX]);
// Moves to the next tick and releases the ring slots of the finished one.
void netTickAdvance(NetSession &s);
// A node's input stops at ulTick: from then on its frames are empty on every machine.  Must be called with the same tick on
// every node before that tick runs; returns false when the tick has already run here (ulTick < s.ulTick) or the node is
// unknown or already dropped.  The host picks the tick (NETPLAY.md 4.2: its current tick; every client is at or behind it).
bool netDropNode(NetSession &s, uint8_t ubNode, uint32_t ulTick);

// ---- rejoin (NETPLAY.md 4.5) ---------------------------------------------------------------------------------------------------
// A dropped node comes back at a map turn: every machine that holds the session calls this with the same node while its tick is the turn's (s.ulTick,
// not yet run).  The node's ring starts over at that tick with empty frames up to ulUntil (= turn tick + input delay: the rejoiner has no input for them,
// the others already hold or will receive TICKs for them with an empty slot), real frames from ulUntil on.  False: not dropped, not a remote node, or
// ulUntil is not ahead of / within a ring of the tick.
bool netUndropNode(NetSession &s, uint8_t ubNode, uint32_t ulUntil);
// A session that starts in the middle of a game at ulTick with only the nodes of ubLiveMask alive (bit n = node n; the host plus the rejoiners): the
// host that played on alone and takes players back.  Ticks ulTick .. ulTick + delay - 1 are empty for everybody (as at the start of a game), the others
// are dropped from ulTick on.  Returns false for a mask without the local node.
bool netSessionInitAt(NetSession &s, uint8_t ubNodes, uint8_t ubLocal, uint8_t ubDelay, uint32_t ulTick, uint8_t ubLiveMask);
// The rejoining machine's session: at ulTick, the nodes outside the mask dropped, every ring empty.  netSessionRejoinGo then files the empty frames of
// the local node for ulTick .. ulUntil - 1 (it has no input for them) and makes ulUntil the tick of its first real frame.  The frames of the other
// nodes for those ticks come from the host as TICKs.
bool netSessionInitRejoin(NetSession &s, uint8_t ubNodes, uint8_t ubLocal, uint8_t ubDelay, uint32_t ulTick, uint8_t ubLiveMask);
bool netSessionRejoinGo(NetSession &s, uint32_t ulUntil);

// A drop as the game applies it (12.11): the node's input stopped at ulTick, so the game changes at that tick on every machine.
struct NetDropEvent {
	uint32_t ulTick;
	uint8_t ubNode;
};
// Number of live nodes at the current tick.
uint8_t netLiveNodes(const NetSession &s);

// ---- the merge -------------------------------------------------------------------------------------------------------------------
enum NetMode : uint8_t {
	NET_MODE_CLASSIC = 0,
	NET_MODE_COOP = 1
};

// The kind of screen the game is on (NETPLAY.md 3.1 rows).
enum NetScreen : uint8_t {
	NET_SCREEN_MENU = 0,      // title menu, knight select, anything before the game proper
	NET_SCREEN_MAP,           // map turn of the knight ubTurn (a black knight's turn too)
	NET_SCREEN_PLACE,         // town / place / loot screens of the knight ubTurn
	NET_SCREEN_DUEL,          // fight: classic human duel of ubDuelFirst (port 1) and ubDuelSecond (port 0); co-op fight
	NET_SCREEN_PRACTICE,      // Practice: knight 0 on port 1 against knight 1 on port 0
	NET_SCREEN_PADS,          // co-op knight screen, controller line of member ubFocus: only that member's node steers, with ANY of its pads
	NET_SCREEN_NEXTDAY        // the "Next Day" screen between two rounds: it waits for a fire press of ANY player (the turn record is a black knight's there)
};

// Mirrors of game/party.hpp PARTY_FOCUS_* (this file is engine layer and must not include game/; the test checks they match).
constexpr uint8_t NET_FOCUS_ANY = 0xFF;
constexpr uint8_t NET_FOCUS_TURN = 0xFE;

// Who steers one knight record (classic) or party member (co-op): the node it sits at and which of that node's controllers.
// ubSlot is co-op only: the pad slot of the MERGED pad frame this member is read from (partySlot(): the member index in a net game, so
// two nodes' "joystick 1" do not collide).  ubNode / ubPad are the owner's choice on the knight screen (game/party.hpp aubNode / aubPad,
// shared game state); ubPad PAD_NONE = not chosen yet (the member reads nothing).
struct NetBinding {
	uint8_t ubNode;      // NET_NODE_NONE = nobody (classic: an AI / black knight)
	uint8_t ubPad;       // ms::PadSource on that node (normally PAD_JOY1)
	uint8_t ubSlot;      // co-op: slot in the merged PadFrame
};

// The part of the game state the merge depends on.  The rt layer fills it from the game (turn record, mode, party) once per
// poll; every field is identical on every machine.
struct NetGameState {
	uint8_t ubMode;                        // NetMode
	uint8_t ubScreen;                      // NetScreen
	uint8_t ubTurn;                        // knight record / party member whose turn it is (MAP, PLACE); NET_KNIGHT_NONE
	uint8_t ubDuelFirst, ubDuelSecond;     // DUEL: the two fighters' records (NET_KNIGHT_NONE = none)
	uint8_t ubFocus;                       // co-op: NET_FOCUS_ANY, NET_FOCUS_TURN or a member index (game/party.hpp ubFocus)
	                                       // NET_SCREEN_PADS: the member whose Control line is on offer (a member index)
	NetBinding aBind[NET_KNIGHTS_MAX];     // by knight record / member index; unused records have ubNode NET_NODE_NONE
};

// What the game reads for one tick.
struct NetMerged {
	JoyBits joy;                           // uwPort1 = the "player stick" (LAB_0630), uwPort0 (LAB_062F)
	PadFrame pads;                         // co-op: the merged pad frame (all zero in classic)
	uint8_t ubInputNode;                   // node that owns keyboard and mouse this tick; NET_NODE_NONE = nobody
	uint8_t ubKeyCount;                    // raw key events of the owner, in order
	KeyEvent aKeys[NET_KEYS_MAX];
	int8_t sbMouseDx, sbMouseDy;           // the owner's mouse delta (0 when nobody owns the mouse)
	bool isMouseButton;                    // the owner's left button
};

// The merge itself.  aFrames: one frame per node (netTickFrames), ubNodes: how many nodes exist.  The rule of every row is
// written out in netplay.cpp (NETPLAY.md 3.1 / 3.2 and the decisions made where the note is silent).
void netMerge(const NetGameState &g, const NetFrame (&aFrames)[NET_NODE_MAX], uint8_t ubNodes, NetMerged &out);
// netTickFrames + netMerge for the session's current tick.
void netMergeTick(const NetSession &s, const NetGameState &g, NetMerged &out);

// A node dropped (NETPLAY.md 4.2, owner 2026-10-10) in a binding table: its classic knights become AI knights (binding node
// NET_NODE_DROPPED; the original's black-knight state is applied by the game layer: ulKind = 4); its co-op members pass to the
// taker node (normally the host, node 0; the node that plays on alone after the host left) on the first controller none of the
// taker's other members uses (the slot stays).  Nothing happens when the node is the taker or not a node.  Every machine calls it at
// the drop tick, in the order of (tick, node), with the same table.
void netBindingsDrop(NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubMode, uint8_t ubDroppedNode, uint8_t ubTakerNode);
// The same on the merge's view of the game.
void netStateApplyDrop(NetGameState &g, uint8_t ubDroppedNode, uint8_t ubTakerNode = 0);

}  // namespace ms
