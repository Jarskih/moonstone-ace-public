// engine/netplay - the lockstep core of network play (ROADMAP 12.6).  See include/engine/netplay.hpp.  New code, no original
// label: the original reads two joystick ports, the keyboard interrupt and the mouse counters directly; this file only decides
// which machine's input those reads see.
#include "engine/netplay.hpp"

namespace ms {

namespace {

constexpr uint8_t kPadBits = 5;
constexpr uint32_t kPadMask = 0x1F;
constexpr uint32_t kPadsUsedMask = (1UL << (kPadBits * PAD_SOURCE_COUNT)) - 1;   // 30 bits
constexpr uint8_t kKeyDownFlag = 0x80;

int8_t saturate(int16_t swValue) {
	if(swValue > 127) {
		return 127;
	}
	if(swValue < -127) {
		return -127;
	}
	return static_cast<int8_t>(swValue);
}

}  // namespace

// ---- frame -----------------------------------------------------------------------------------------------------------------

void netFrameClear(NetFrame &f) {
	f.ulPads = 0;
	f.ubFlags = 0;
	f.sbMouseDx = 0;
	f.sbMouseDy = 0;
	for(uint8_t i = 0; i < NET_KEYS_MAX; ++i) {
		f.aubKeys[i] = 0;
	}
}

bool netFrameEqual(const NetFrame &a, const NetFrame &b) {
	if(a.ulPads != b.ulPads || a.ubFlags != b.ubFlags || a.sbMouseDx != b.sbMouseDx || a.sbMouseDy != b.sbMouseDy) {
		return false;
	}
	for(uint8_t i = 0; i < NET_KEYS_MAX; ++i) {
		if(a.aubKeys[i] != b.aubKeys[i]) {
			return false;
		}
	}
	return true;
}

void netFrameSetPads(NetFrame &f, const PadFrame &pads) {
	uint32_t ulPads = 0;
	for(uint8_t i = 0; i < PAD_SOURCE_COUNT; ++i) {
		ulPads |= (static_cast<uint32_t>(pads.auwBits[i]) & kPadMask) << (kPadBits * i);
	}
	f.ulPads = ulPads;
}

void netFrameGetPads(const NetFrame &f, PadFrame &pads) {
	for(uint8_t i = 0; i < PAD_SOURCE_COUNT; ++i) {
		pads.auwBits[i] = static_cast<uint16_t>((f.ulPads >> (kPadBits * i)) & kPadMask);
	}
}

uint16_t netFramePad(const NetFrame &f, uint8_t ubSource) {
	if(ubSource >= PAD_SOURCE_COUNT) {
		return 0;
	}
	return static_cast<uint16_t>((f.ulPads >> (kPadBits * ubSource)) & kPadMask);
}

bool netFrameAddKey(NetFrame &f, const KeyEvent &ev) {
	const uint8_t ubCount = netFrameKeyCount(f);
	if(ubCount >= NET_KEYS_MAX || ev.ubKey > 0x7F) {
		return false;
	}
	f.aubKeys[ubCount] = static_cast<uint8_t>(ev.ubKey | (ev.isDown ? kKeyDownFlag : 0));
	f.ubFlags = static_cast<uint8_t>((f.ubFlags & ~NET_FLAG_KEYS_MASK) | (ubCount + 1));
	return true;
}

uint8_t netFrameKeyCount(const NetFrame &f) {
	return static_cast<uint8_t>(f.ubFlags & NET_FLAG_KEYS_MASK);
}

KeyEvent netFrameKey(const NetFrame &f, uint8_t ubIndex) {
	KeyEvent ev;
	ev.ubKey = 0;
	ev.isDown = false;
	if(ubIndex < netFrameKeyCount(f) && ubIndex < NET_KEYS_MAX) {
		ev.ubKey = static_cast<uint8_t>(f.aubKeys[ubIndex] & 0x7F);
		ev.isDown = (f.aubKeys[ubIndex] & kKeyDownFlag) != 0;
	}
	return ev;
}

void netFrameSetMouse(NetFrame &f, int16_t swDx, int16_t swDy, bool isButton) {
	f.sbMouseDx = saturate(swDx);
	f.sbMouseDy = saturate(swDy);
	f.ubFlags = static_cast<uint8_t>(isButton ? (f.ubFlags | NET_FLAG_MOUSE_BUTTON) : (f.ubFlags & ~NET_FLAG_MOUSE_BUTTON));
}

bool netFrameMouseButton(const NetFrame &f) {
	return (f.ubFlags & NET_FLAG_MOUSE_BUTTON) != 0;
}

void netFrameWrite(const NetFrame &f, uint8_t *pOut) {
	pOut[0] = static_cast<uint8_t>(f.ulPads >> 24);
	pOut[1] = static_cast<uint8_t>(f.ulPads >> 16);
	pOut[2] = static_cast<uint8_t>(f.ulPads >> 8);
	pOut[3] = static_cast<uint8_t>(f.ulPads);
	pOut[4] = f.ubFlags;
	pOut[5] = static_cast<uint8_t>(f.sbMouseDx);
	pOut[6] = static_cast<uint8_t>(f.sbMouseDy);
	for(uint8_t i = 0; i < NET_KEYS_MAX; ++i) {
		pOut[7 + i] = f.aubKeys[i];
	}
}

bool netFrameRead(const uint8_t *pIn, NetFrame &f) {
	NetFrame r;
	r.ulPads = (static_cast<uint32_t>(pIn[0]) << 24) | (static_cast<uint32_t>(pIn[1]) << 16) |
		(static_cast<uint32_t>(pIn[2]) << 8) | pIn[3];
	r.ubFlags = pIn[4];
	r.sbMouseDx = static_cast<int8_t>(pIn[5]);
	r.sbMouseDy = static_cast<int8_t>(pIn[6]);
	for(uint8_t i = 0; i < NET_KEYS_MAX; ++i) {
		r.aubKeys[i] = pIn[7 + i];
	}
	if((r.ulPads & ~kPadsUsedMask) != 0 || (r.ubFlags & 0xF0) != 0 || netFrameKeyCount(r) > NET_KEYS_MAX) {
		return false;
	}
	if(r.sbMouseDx == -128 || r.sbMouseDy == -128) {
		return false;
	}
	for(uint8_t i = netFrameKeyCount(r); i < NET_KEYS_MAX; ++i) {
		if(r.aubKeys[i] != 0) {
			return false;
		}
	}
	f = r;
	return true;
}

// ---- tick ring -----------------------------------------------------------------------------------------------------------

void netRingInit(NetTickRing &r, uint32_t ulBase) {
	r.ulBase = ulBase;
	r.uwHave = 0;
	for(uint8_t i = 0; i < NET_RING_TICKS; ++i) {
		netFrameClear(r.aFrames[i]);
	}
}

NetPut netRingPut(NetTickRing &r, uint32_t ulTick, const NetFrame &f) {
	if(ulTick < r.ulBase) {
		return NET_PUT_OLD;
	}
	if(ulTick - r.ulBase >= NET_RING_TICKS) {
		return NET_PUT_FAR;
	}
	const uint8_t ubSlot = static_cast<uint8_t>(ulTick & (NET_RING_TICKS - 1));
	const uint16_t uwBit = static_cast<uint16_t>(1U << ubSlot);
	if(r.uwHave & uwBit) {
		return netFrameEqual(r.aFrames[ubSlot], f) ? NET_PUT_DUP : NET_PUT_CONFLICT;
	}
	r.aFrames[ubSlot] = f;
	r.uwHave = static_cast<uint16_t>(r.uwHave | uwBit);
	return NET_PUT_OK;
}

bool netRingHas(const NetTickRing &r, uint32_t ulTick) {
	if(ulTick < r.ulBase || ulTick - r.ulBase >= NET_RING_TICKS) {
		return false;
	}
	return (r.uwHave & (1U << (ulTick & (NET_RING_TICKS - 1)))) != 0;
}

bool netRingGet(const NetTickRing &r, uint32_t ulTick, NetFrame &f) {
	if(!netRingHas(r, ulTick)) {
		return false;
	}
	f = r.aFrames[ulTick & (NET_RING_TICKS - 1)];
	return true;
}

void netRingConsume(NetTickRing &r, uint32_t ulNewBase) {
	if(ulNewBase <= r.ulBase) {
		return;
	}
	if(ulNewBase - r.ulBase > NET_RING_TICKS) {
		ulNewBase = r.ulBase + NET_RING_TICKS;
	}
	for(uint32_t ulTick = r.ulBase; ulTick < ulNewBase; ++ulTick) {
		r.uwHave = static_cast<uint16_t>(r.uwHave & ~(1U << (ulTick & (NET_RING_TICKS - 1))));
	}
	r.ulBase = ulNewBase;
}

// ---- session -------------------------------------------------------------------------------------------------------------

namespace {

// True when the node supplies no input at the tick any more (dropped at or before it).
bool isGone(const NetSession &s, uint8_t ubNode, uint32_t ulTick) {
	return s.abDropped[ubNode] && ulTick >= s.aulDropTick[ubNode];
}

}  // namespace

void netSessionInit(NetSession &s, uint8_t ubNodes, uint8_t ubLocal, uint8_t ubDelay) {
	s.ubNodes = ubNodes < 1 ? 1 : (ubNodes > NET_NODE_MAX ? NET_NODE_MAX : ubNodes);
	s.ubLocal = ubLocal < s.ubNodes ? ubLocal : 0;
	s.ubDelay = ubDelay > NET_DELAY_MAX ? NET_DELAY_MAX : ubDelay;
	s.ulTick = 0;
	s.ulNextSend = s.ubDelay;
	NetFrame empty;
	netFrameClear(empty);
	for(uint8_t n = 0; n < NET_NODE_MAX; ++n) {
		netRingInit(s.aRing[n], 0);
		s.abDropped[n] = false;
		s.aulDropTick[n] = 0;
		for(uint8_t t = 0; t < s.ubDelay && n < s.ubNodes; ++t) {
			netRingPut(s.aRing[n], t, empty);
		}
	}
}

uint32_t netLocalSubmit(NetSession &s, const NetFrame &f) {
	const uint32_t ulTick = s.ulNextSend++;
	netRingPut(s.aRing[s.ubLocal], ulTick, f);
	return ulTick;
}

NetPut netPutFrame(NetSession &s, uint8_t ubNode, uint32_t ulTick, const NetFrame &f) {
	if(ubNode >= s.ubNodes) {
		return NET_PUT_FAR;
	}
	if(isGone(s, ubNode, ulTick)) {
		return NET_PUT_OLD;
	}
	return netRingPut(s.aRing[ubNode], ulTick, f);
}

uint8_t netMissingMask(const NetSession &s, uint32_t ulTick) {
	uint8_t ubMask = 0;
	for(uint8_t n = 0; n < s.ubNodes; ++n) {
		if(!isGone(s, n, ulTick) && !netRingHas(s.aRing[n], ulTick)) {
			ubMask = static_cast<uint8_t>(ubMask | (1U << n));
		}
	}
	return ubMask;
}

bool netTickReady(const NetSession &s) {
	return netMissingMask(s, s.ulTick) == 0;
}

void netTickFrames(const NetSession &s, NetFrame (&aOut)[NET_NODE_MAX]) {
	for(uint8_t n = 0; n < NET_NODE_MAX; ++n) {
		netFrameClear(aOut[n]);
		if(n < s.ubNodes && !isGone(s, n, s.ulTick)) {
			netRingGet(s.aRing[n], s.ulTick, aOut[n]);
		}
	}
}

void netTickAdvance(NetSession &s) {
	++s.ulTick;
	for(uint8_t n = 0; n < NET_NODE_MAX; ++n) {
		netRingConsume(s.aRing[n], s.ulTick);
	}
}

bool netDropNode(NetSession &s, uint8_t ubNode, uint32_t ulTick) {
	if(ubNode >= s.ubNodes || s.abDropped[ubNode] || ulTick < s.ulTick) {
		return false;
	}
	s.abDropped[ubNode] = true;
	s.aulDropTick[ubNode] = ulTick;
	return true;
}

// ---- rejoin ----

bool netUndropNode(NetSession &s, uint8_t ubNode, uint32_t ulUntil) {
	if(ubNode == 0 || ubNode >= s.ubNodes || ubNode == s.ubLocal || !s.abDropped[ubNode] || ulUntil < s.ulTick || ulUntil - s.ulTick > s.ubDelay) {
		return false;
	}
	NetFrame empty;
	netFrameClear(empty);
	netRingInit(s.aRing[ubNode], s.ulTick);
	for(uint32_t t = s.ulTick; t < ulUntil; ++t) {
		netRingPut(s.aRing[ubNode], t, empty);
	}
	s.abDropped[ubNode] = false;
	s.aulDropTick[ubNode] = 0;
	return true;
}

namespace {

void sessionBase(NetSession &s, uint8_t ubNodes, uint8_t ubLocal, uint8_t ubDelay, uint32_t ulTick, uint8_t ubLiveMask) {
	s.ubNodes = ubNodes < 1 ? 1 : (ubNodes > NET_NODE_MAX ? NET_NODE_MAX : ubNodes);
	s.ubLocal = ubLocal < s.ubNodes ? ubLocal : 0;
	s.ubDelay = ubDelay > NET_DELAY_MAX ? NET_DELAY_MAX : ubDelay;
	s.ulTick = ulTick;
	s.ulNextSend = ulTick;
	for(uint8_t n = 0; n < NET_NODE_MAX; ++n) {
		netRingInit(s.aRing[n], ulTick);
		const bool isLive = n < s.ubNodes && (ubLiveMask & (1U << n)) != 0;
		s.abDropped[n] = n < s.ubNodes && !isLive;
		s.aulDropTick[n] = s.abDropped[n] ? ulTick : 0;
	}
}

}  // namespace

bool netSessionInitAt(NetSession &s, uint8_t ubNodes, uint8_t ubLocal, uint8_t ubDelay, uint32_t ulTick, uint8_t ubLiveMask) {
	sessionBase(s, ubNodes, ubLocal, ubDelay, ulTick, ubLiveMask);
	if((ubLiveMask & (1U << s.ubLocal)) == 0) {
		return false;
	}
	s.ulNextSend = ulTick + s.ubDelay;
	NetFrame empty;
	netFrameClear(empty);
	for(uint8_t n = 0; n < s.ubNodes; ++n) {
		for(uint8_t t = 0; t < s.ubDelay && !s.abDropped[n]; ++t) {
			netRingPut(s.aRing[n], ulTick + t, empty);
		}
	}
	return true;
}

bool netSessionInitRejoin(NetSession &s, uint8_t ubNodes, uint8_t ubLocal, uint8_t ubDelay, uint32_t ulTick, uint8_t ubLiveMask) {
	sessionBase(s, ubNodes, ubLocal, ubDelay, ulTick, ubLiveMask);
	return (ubLiveMask & (1U << s.ubLocal)) != 0 && s.ubLocal != 0;
}

bool netSessionRejoinGo(NetSession &s, uint32_t ulUntil) {
	if(ulUntil < s.ulTick || ulUntil - s.ulTick > s.ubDelay) {
		return false;
	}
	NetFrame empty;
	netFrameClear(empty);
	for(uint32_t t = s.ulTick; t < ulUntil; ++t) {
		netRingPut(s.aRing[s.ubLocal], t, empty);
	}
	s.ulNextSend = ulUntil;
	return true;
}

uint8_t netLiveNodes(const NetSession &s) {

	uint8_t ubLive = 0;
	for(uint8_t n = 0; n < s.ubNodes; ++n) {
		if(!isGone(s, n, s.ulTick)) {
			++ubLive;
		}
	}
	return ubLive;
}

// ---- merge ---------------------------------------------------------------------------------------------------------------------
//
// The rules, row by row (NETPLAY.md 3.1 classic / 3.2 co-op).  "word(n, pad)" = pad word `pad` of node n's frame, 0 for a node
// that does not exist, was dropped or never binds.  A knight's controller is its binding's (node, pad); a classic player uses
// his own port-1 joystick (PAD_JOY1) unless the lobby bound another local controller.
//
//   classic   screen     port 1 (player stick)        port 0                   keyboard + mouse
//             MENU       word(0, JOY1)                word(0, JOY0)            host            [decision A]
//             MAP        word(turn knight)            0                        turn knight's node; nobody on an AI turn
//             PLACE      word(turn knight)            0                        turn knight's node; nobody on an AI turn
//             DUEL       word(first knight)           word(second knight)      first knight's node; the second's if the first is AI
//             PRACTICE   word(knight 0)               word(knight 1)           host
//             NEXTDAY    OR of every human knight's word 0                        host            [decision C]
//   A knight with no binding (node NONE) is an AI / black knight: its word is 0.  The AI moves from the RNG, identical everywhere.
//   A dropped knight (node DROPPED, 12.11) is one too, except that an open PLACE screen of its turn takes the host's joystick 1 and keyboard.
//
//   co-op     focus      port 1 (player stick)        merged pad frame         keyboard + mouse
//             ANY        every pad of every node OR-ed  slot s = OR over nodes   host            [decision B]
//                         (the menus' "any controller" wait accepts any node)  of word(n, s)
//             TURN       word(member ubTurn)          member m's word at       node of member ubTurn; nobody if no member
//             member m   word(member m)               slot aBind[m].ubSlot       node of member m
//             PADS       every pad of the node of     that word at slot of m   node of member m   (knight screen controller line,
//                         member m OR-ed                                                              NETPLAY.md 3.2)
//             port 0 is 0.  The MENU screen always merges as focus ANY.  Members that share a slot are OR-ed.
//
// Decisions where NETPLAY.md is silent: A) the title menu / knight screen of a classic session is the host's: the host's raw
// ports and keyboard (12.9 refines the knight select: each node picks its own knights).  B) a co-op session's keyboard and mouse
// follow the player stick's owner (the host while the stick is "any").  C) the "Next Day" screen comes after the black knights' turns, when the current
// knight is a black knight (no node): no player's fire would ever count, so every human knight's controller counts, like the co-op "any controller" waits.

namespace {

uint16_t nodeWord(const NetFrame (&aFrames)[NET_NODE_MAX], uint8_t ubNodes, uint8_t ubNode, uint8_t ubPad) {
	if(ubNode >= ubNodes) {
		return 0;
	}
	return netFramePad(aFrames[ubNode], ubPad);
}

// The node a record is bound to, NET_NODE_NONE when it is an AI knight, unbound or the node does not exist.
uint8_t recordNode(const NetGameState &g, uint8_t ubNodes, uint8_t ubRecord) {
	if(ubRecord >= NET_KNIGHTS_MAX || g.aBind[ubRecord].ubNode >= ubNodes) {
		return NET_NODE_NONE;
	}
	return g.aBind[ubRecord].ubNode;
}

uint16_t recordWord(const NetGameState &g, const NetFrame (&aFrames)[NET_NODE_MAX], uint8_t ubNodes, uint8_t ubRecord) {
	const uint8_t ubNode = recordNode(g, ubNodes, ubRecord);
	return ubNode == NET_NODE_NONE ? 0 : nodeWord(aFrames, ubNodes, ubNode, g.aBind[ubRecord].ubPad);
}

void mergeClassic(const NetGameState &g, const NetFrame (&aFrames)[NET_NODE_MAX], uint8_t ubNodes, NetMerged &out) {
	switch(g.ubScreen) {
		case NET_SCREEN_PLACE:
			if(g.ubTurn < NET_KNIGHTS_MAX && g.aBind[g.ubTurn].ubNode == NET_NODE_DROPPED) {
				// A dropped knight's town / loot screen is still open (an AI knight never gets here): the host closes it.
				out.joy.uwPort1 = nodeWord(aFrames, ubNodes, 0, PAD_JOY1);
				out.ubInputNode = 0;
				break;
			}
			out.joy.uwPort1 = recordWord(g, aFrames, ubNodes, g.ubTurn);
			out.ubInputNode = recordNode(g, ubNodes, g.ubTurn);
			break;
		case NET_SCREEN_MAP:
			out.joy.uwPort1 = recordWord(g, aFrames, ubNodes, g.ubTurn);
			out.ubInputNode = recordNode(g, ubNodes, g.ubTurn);
			break;
		case NET_SCREEN_DUEL:
			out.joy.uwPort1 = recordWord(g, aFrames, ubNodes, g.ubDuelFirst);
			out.joy.uwPort0 = recordWord(g, aFrames, ubNodes, g.ubDuelSecond);
			out.ubInputNode = recordNode(g, ubNodes, g.ubDuelFirst);
			if(out.ubInputNode == NET_NODE_NONE) {
				out.ubInputNode = recordNode(g, ubNodes, g.ubDuelSecond);
			}
			break;
		case NET_SCREEN_NEXTDAY:
			for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) {
				out.joy.uwPort1 |= recordWord(g, aFrames, ubNodes, k);
			}
			out.ubInputNode = 0;
			break;
		case NET_SCREEN_PRACTICE:
			out.joy.uwPort1 = recordWord(g, aFrames, ubNodes, 0);
			out.joy.uwPort0 = recordWord(g, aFrames, ubNodes, 1);
			out.ubInputNode = 0;
			break;
		default:   // NET_SCREEN_MENU
			out.joy.uwPort1 = nodeWord(aFrames, ubNodes, 0, PAD_JOY1);
			out.joy.uwPort0 = nodeWord(aFrames, ubNodes, 0, PAD_JOY0);
			out.ubInputNode = 0;
			break;
	}
}

// The knight screen asks one player for his Control line (NETPLAY.md 3.2): the member ubFocus's node steers with whatever it has (every pad
// of the node OR-ed: the player may hold any controller, the one on offer is only what he is about to choose); every other node is ignored,
// so the screen's state is a function of that one node's frames (no other member's pad appears in the merged frame either).
void mergePadChoice(const NetGameState &g, const NetFrame (&aFrames)[NET_NODE_MAX], uint8_t ubNodes, NetMerged &out) {
	const uint8_t ubNode = recordNode(g, ubNodes, g.ubFocus);
	if(ubNode == NET_NODE_NONE) {
		return;   // nobody owns this member: the screen waits (the drop code rebinds it)
	}
	uint16_t uwAny = 0;
	for(uint8_t s = 0; s < PAD_SOURCE_COUNT; ++s) {
		uwAny |= nodeWord(aFrames, ubNodes, ubNode, s);
	}
	const uint8_t ubSlot = g.aBind[g.ubFocus].ubSlot;
	if(ubSlot < PAD_SOURCE_COUNT) {
		out.pads.auwBits[ubSlot] |= uwAny;
	}
	out.joy.uwPort1 = uwAny;
	out.ubInputNode = ubNode;
}

void mergeCoop(const NetGameState &g, const NetFrame (&aFrames)[NET_NODE_MAX], uint8_t ubNodes, NetMerged &out) {
	if(g.ubScreen == NET_SCREEN_PADS && g.ubFocus < NET_KNIGHTS_MAX) {
		mergePadChoice(g, aFrames, ubNodes, out);
		return;
	}
	const bool isAny = g.ubScreen == NET_SCREEN_MENU || g.ubScreen == NET_SCREEN_PADS || g.ubScreen == NET_SCREEN_NEXTDAY;
	const uint8_t ubFocus = isAny ? NET_FOCUS_ANY : g.ubFocus;
	if(ubFocus == NET_FOCUS_ANY) {
		for(uint8_t s = 0; s < PAD_SOURCE_COUNT; ++s) {
			for(uint8_t n = 0; n < ubNodes; ++n) {
				out.pads.auwBits[s] |= nodeWord(aFrames, ubNodes, n, s);
			}
		}
		for(uint8_t s = 0; s < PAD_SOURCE_COUNT; ++s) {
			out.joy.uwPort1 |= out.pads.auwBits[s];   // = ms::padsAny(out.pads), without linking pad.cpp
		}
		out.ubInputNode = 0;
		return;
	}
	for(uint8_t m = 0; m < NET_KNIGHTS_MAX; ++m) {
		const uint8_t ubSlot = g.aBind[m].ubSlot;
		if(recordNode(g, ubNodes, m) != NET_NODE_NONE && ubSlot < PAD_SOURCE_COUNT) {
			out.pads.auwBits[ubSlot] |= recordWord(g, aFrames, ubNodes, m);
		}
	}
	const uint8_t ubStick = ubFocus == NET_FOCUS_TURN ? g.ubTurn : ubFocus;
	out.joy.uwPort1 = recordWord(g, aFrames, ubNodes, ubStick);
	out.ubInputNode = recordNode(g, ubNodes, ubStick);
}

}  // namespace

void netMerge(const NetGameState &g, const NetFrame (&aFrames)[NET_NODE_MAX], uint8_t ubNodes, NetMerged &out) {
	out.joy.uwPort0 = 0;
	out.joy.uwPort1 = 0;
	for(uint8_t i = 0; i < PAD_SOURCE_COUNT; ++i) {
		out.pads.auwBits[i] = 0;
	}
	out.ubInputNode = NET_NODE_NONE;
	out.ubKeyCount = 0;
	for(uint8_t i = 0; i < NET_KEYS_MAX; ++i) {
		out.aKeys[i].ubKey = 0;
		out.aKeys[i].isDown = false;
	}
	out.sbMouseDx = 0;
	out.sbMouseDy = 0;
	out.isMouseButton = false;
	if(ubNodes > NET_NODE_MAX) {
		ubNodes = NET_NODE_MAX;
	}

	if(g.ubMode == NET_MODE_COOP) {
		mergeCoop(g, aFrames, ubNodes, out);
	} else {
		mergeClassic(g, aFrames, ubNodes, out);
	}

	// The keyboard events and the mouse of the one owner node.
	if(out.ubInputNode < ubNodes) {
		const NetFrame &f = aFrames[out.ubInputNode];
		out.ubKeyCount = netFrameKeyCount(f);
		for(uint8_t i = 0; i < out.ubKeyCount; ++i) {
			out.aKeys[i] = netFrameKey(f, i);
		}
		out.sbMouseDx = f.sbMouseDx;
		out.sbMouseDy = f.sbMouseDy;
		out.isMouseButton = netFrameMouseButton(f);
	} else {
		out.ubInputNode = NET_NODE_NONE;
	}
}

void netMergeTick(const NetSession &s, const NetGameState &g, NetMerged &out) {
	NetFrame aFrames[NET_NODE_MAX];
	netTickFrames(s, aFrames);
	netMerge(g, aFrames, s.ubNodes, out);
}

void netBindingsDrop(NetBinding (&aBind)[NET_KNIGHTS_MAX], uint8_t ubMode, uint8_t ubDroppedNode, uint8_t ubTakerNode) {
	if(ubDroppedNode >= NET_NODE_MAX || ubTakerNode >= NET_NODE_MAX || ubDroppedNode == ubTakerNode) {
		return;   // not a node, or the taker itself (the host leaving ends the session for the others: they play on alone as their own taker)
	}
	for(uint8_t k = 0; k < NET_KNIGHTS_MAX; ++k) {
		NetBinding &b = aBind[k];
		if(b.ubNode != ubDroppedNode) {
			continue;
		}
		if(ubMode != NET_MODE_COOP) {
			b.ubNode = NET_NODE_DROPPED;   // the original's black-knight state is applied by the game layer (ulKind = 4)
			continue;
		}
		// Co-op: the taker steers the member on the first selectable controller none of its other members uses.
		uint8_t ubPad = b.ubPad;
		for(uint8_t c = 0; c < PAD_SELECTABLE_COUNT; ++c) {
			bool isUsed = false;
			for(uint8_t j = 0; j < NET_KNIGHTS_MAX; ++j) {
				if(j != k && aBind[j].ubNode == ubTakerNode && aBind[j].ubPad == c) {
					isUsed = true;
				}
			}
			if(!isUsed) {
				ubPad = c;
				break;
			}
		}
		b.ubNode = ubTakerNode;
		b.ubPad = ubPad;
	}
}

void netStateApplyDrop(NetGameState &g, uint8_t ubDroppedNode, uint8_t ubTakerNode) {
	netBindingsDrop(g.aBind, g.ubMode, ubDroppedNode, ubTakerNode);
}

}  // namespace ms
