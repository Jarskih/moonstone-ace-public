"""Host test of the network session (ROADMAP 12.8b): engine/netnode.cpp (handshake, lockstep ring over byte pipes, seeds, hashes, ends),
engine/netglue.cpp (local frame <-> network frame <-> raw key codes), game/net_state.cpp (the merge's view of the game) and
game/net_hash.cpp (the state walk: pointers normalised, same state at two base addresses -> same hash).

tests/netsession_driver.cpp is compiled with clang++ (-fno-exceptions -fno-rtti) and runs two, three and four in-process nodes joined by fake
byte pipes (random chunking, partial sends): every node must merge exactly the frames a central reference computes.  The mutation test
proves the driver has teeth: each deliberate mutation of the sources must make it fail.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = 'tests/netsession_driver.cpp'
SOURCES = ['src/engine/netnode.cpp', 'src/engine/netudp.cpp', 'src/engine/netstats.cpp', 'src/engine/netroom.cpp', 'src/game/net_knights.cpp', 'src/engine/netplay.cpp', 'src/engine/netproto.cpp', 'src/engine/netglue.cpp',
           'src/engine/nettick.cpp', 'src/engine/input.cpp', 'src/engine/pad.cpp', 'src/engine/netwait.cpp', 'src/game/net_state.cpp', 'src/game/net_hash.cpp',
           'src/game/net_drop.cpp', 'src/game/party.cpp', 'src/game/scene_menu.cpp', 'src/engine/netrejoin.cpp', 'src/game/net_rejoin.cpp']


def build(out_dir, overrides=None):
    """Compile the driver; overrides = {source path: replacement text} for the mutation test."""
    exe = os.path.join(out_dir, 'netsession.exe')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-D_CRT_SECURE_NO_WARNINGS', '-Wno-missing-field-initializers',
           '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), '-o', exe, os.path.join(ROOT, DRIVER)]
    for s in SOURCES:
        if overrides and s in overrides:
            p = os.path.join(out_dir, s.replace('/', '_'))
            with open(p, 'w', encoding='utf-8') as fh:
                fh.write(overrides[s])
            cmd.append(p)
        else:
            cmd.append(os.path.join(ROOT, s))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


@unittest.skipUnless(CXX, 'needs clang++')
class NetSession(unittest.TestCase):
    def test_session_glue_state_hash(self):
        tmp = tempfile.mkdtemp(prefix='netsession_')
        try:
            r = subprocess.run([build(tmp)], capture_output=True, text=True, timeout=600)
            self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
            self.assertIn('OK ', r.stdout)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_mutations_are_caught(self):
        def src(path):
            with open(os.path.join(ROOT, path), encoding='utf-8') as fh:
                return fh.read()
        mutations = [
            # 12.8c lobby = knight screen: two claims of one portrait both succeed
            ('src/engine/netroom.cpp', 'if(roomIsHeld(r, p.ubSel, ubSeat)) {   // the race is lost: back to browsing, on a free portrait', 'if(false) {'),
            # a leaving player keeps its seat and its portrait
            ('src/engine/netroom.cpp', '	freeSeat(r.aSeat[ubSeat]);\n}', '}'),
            # a closed link in the lobby ends the host's session instead of freeing the seat
            ('src/engine/netnode.cpp', 'lobbyLeave(n, ubLink, NCW_LINK_LOST);    // the lobby goes on without it', 'endNode(n, NE_LINK_LOST, 0);'),
            # a refusal for other game data is logged as a refusal for another build (the exact reason is lost)
            ('src/engine/netnode.cpp', 'refuseLink(n, io, ubLink, NET_BYE_BUILD, NCW_DATA);', 'refuseLink(n, io, ubLink, NET_BYE_BUILD, NCW_BUILD);'),
            # the HELLO values of a refused guest are not kept for the log
            ('src/engine/netnode.cpp', '	g.ulData = h.ulData;\n', ''),
            # a host with a full-looking lobby (picked seats count as taken) refuses a late joiner
            ('src/engine/netnode.cpp', 'if(ubJoined + 1 >= n.p.ubMaxNodes) {', 'if(ubJoined + 1 >= n.p.ubMaxNodes || roomAllDone(n.room)) {'),
            # the probe's close is not explained
            ('src/engine/netnode.cpp', '	l.ulOutFill = 0;\n	markClosed(n, ubLink, ubWhy);\n}', '	l.ulOutFill = 0;\n	markClosed(n, ubLink, NCW_NONE);\n}'),
            # a BYE in the lobby ends the host's session
            ('src/engine/netnode.cpp', 'lobbyLeave(n, ubLink, NCW_BYE);   // a player backed out of the lobby (or was refused by its own build check): the seat is free again', 'endNode(n, NE_PEER_BYE, 0);'),
            # a connection that never says HELLO keeps its link slot for ever (the lobby server's reachability probe, a stray connect)
            ('src/engine/netnode.cpp', 'if(l.isUsed && !l.isHello && ulNow - l.ulUp > NN_HELLO_TIMEOUT_MS) {', 'if(false) {'),
            # garbage on one connection ends the host's whole session
            ('src/engine/netnode.cpp', 'if(isHostLobby(n)) {\n				lobbyLeave(n, ubLink, NCW_GARBAGE);   // stray bytes', 'if(false) {\n				lobbyLeave(n, ubLink, NCW_GARBAGE);   // stray bytes'),
            # START with nobody else in the room
            ('src/engine/netroom.cpp', 'return roomJoined(r) >= 2 && roomAllDone(r);', 'return roomAllDone(r);'),
            # START while somebody has not picked
            ('src/engine/netroom.cpp', 'if(r.aSeat[i].ubState != NET_SEAT_FREE && r.aSeat[i].ubState != NET_SEAT_DONE) {', 'if(false) {'),
            # the nodes of the game keep the seat numbers (a hole when a player left)
            ('src/engine/netnode.cpp', 'n.aLink[aubSeat[node] - 1].ubNode = node;', 'n.aLink[aubSeat[node] - 1].ubNode = aubSeat[node];'),
            # a client may change the mode line
            ('src/engine/netnode.cpp', 'if(!isHostLobby(n) || !roomSetMode(n.room, ubMode)) {', 'if(!roomSetMode(n.room, ubMode)) {'),
            # a pick is not answered when it changed nothing (a lost race would not be told)
            ('src/engine/netnode.cpp', '	if(roomApplyPick(n.room, static_cast<uint8_t>(ubLink + 1), p)) {\n		++n.ulRoomRev;\n	}\n	n.isRoomDirty = true;', '	if(roomApplyPick(n.room, static_cast<uint8_t>(ubLink + 1), p)) {\n		++n.ulRoomRev;\n		n.isRoomDirty = true;\n	}'),
            # CONFIG carries the node number as the portrait
            ('src/engine/netnode.cpp', 'c.aubPortrait[node] = s.ubSel;', 'c.aubPortrait[node] = node;'),
            # classic play takes the chosen controller instead of joystick 1
            ('src/engine/netnode.cpp', 'c.aBind[node].ubPad = n.room.ubMode == NET_MODE_COOP ? s.ubPad : static_cast<uint8_t>(PAD_JOY1);', 'c.aBind[node].ubPad = s.ubPad;'),
            # the knights ignore the portraits of the CONFIG
            ('src/game/net_knights.cpp', 'k.ulKind = ubPortrait;', 'k.ulKind = i;'),
            # the co-op party loses the chosen controllers
            ('src/game/net_knights.cpp', 'env.pParty->aubPad[i] = cfg.aBind[i].ubPad;', ';'),
            # the picked name never reaches the knight
            ('src/game/net_knights.cpp', 'writeName(env.apNames[ubPortrait], cfg.acNames[i]);', ';'),
            # the name buffer keeps the leftovers of an earlier game behind the name (machines that held different names would disagree)
            ('src/game/net_knights.cpp', '	for(; n < NET_KNIGHT_NAME_BYTES - 1; ++n) {\n		pBuf[n] = 0x20;\n	}\n', ''),
            ('src/game/net_knights.cpp', '	pBuf[NET_KNIGHT_NAME_BYTES - 1] = 0;\n', ''),
            ('src/game/net_knights.cpp', '	pBuf[n++] = 0;\n', '	n++;\n'),
            # the party keeps what an earlier game left in it
            ('src/game/net_knights.cpp', '		partyReset(*env.pParty);\n', '		;\n'),
            # the party is not bound to the nodes
            ('src/game/net_knights.cpp', 'partyNetBind(*env.pParty, aubNode);', ';'),
            # a hash mismatch is ignored
            ('src/engine/netnode.cpp', 'if(mine.ulCrc != theirs.ulCrc) {', 'if(mine.ulCrc == 0xFFFFFFFF) {'),
            # the handshake lets a different rule set in
            ('src/engine/netnode.cpp', 'if(h.ubRules != n.p.ubRules) {', 'if(false) {'),
            # the host relays a tick before every node's frame is in
            ('src/engine/netnode.cpp', 'netMissingMask(n.ses, n.ulNextBroadcast) == 0) {', 'true) {'),
            # the silence timeout never fires
            ('src/engine/netnode.cpp', 'ulNow - n.aLink[k].ulLastRx <= n.p.ulTimeoutMs', 'true'),
            # the safety timeout of a tick wait never drops anybody
            ('src/engine/netnode.cpp', 'if(ulWaited > n.p.ulTimeoutMs + (n.eRole == NN_CLIENT ? NN_CLIENT_GRACE_MS : 0)) {', 'if(false) {'),
            # 12.11: the drop tick is the host's current tick instead of the first tick it has not relayed (a client may be ahead of it)
            ('src/engine/netnode.cpp', 'const uint32_t ulTick = n.ulNextBroadcast;', 'const uint32_t ulTick = n.ses.ulTick;'),
            # the DROP names the wrong node
            ('src/engine/netnode.cpp', 'm.u.drop.ubNode = ubNode;', 'm.u.drop.ubNode = 0;'),
            # a client's closed connection ends the session instead of dropping the client
            ('src/engine/netnode.cpp', 'return n.eRole == NN_HOST && n.isConfigured && n.ePhase == NN_RUNNING;', 'return false;'),
            # the keepalive is gone: a waiting host looks dead to the clients
            ('src/engine/netnode.cpp', 'ulNow - n.ulLastPing >= NN_KEEPALIVE_MS) {', 'false) {'),
            # the first press confirms
            ('src/engine/netwait.cpp', 'const bool isPress = isPressed && !b.isDown;', 'const bool isPress = isPressed;'),
            # the question never lapses
            ('src/engine/netwait.cpp', 'if(ulWaitedMs - b.ulAskedMs > NW_CONFIRM_MS) {', 'if(false) {'),
            # a dropped classic knight stays human
            ('src/game/net_drop.cpp', 'k.ulKind = KIND_AI;', 'k.ulKind = k.ulKind;'),
            # the game-over count includes the AI knights
            ('src/game/net_drop.cpp', 'if(aRecords[i].ulKind < KIND_AI) {', 'if(true) {'),
            # drops are applied in arrival order
            ('src/game/net_drop.cpp', 'q.aEv[i - 1].ubNode > ev.ubNode', 'false'),
            # key events come back with the wrong raw code
            ('src/engine/netglue.cpp', 'static_cast<uint8_t>(~idx & 0x7F)', 'static_cast<uint8_t>(idx & 0x7F)'),
            # pointers are hashed as addresses
            ('src/game/net_hash.cpp', 'return tagged(TAG_KNIGHT, ulPtr - im.ulKnightsBase);', 'return ulPtr;'),
            # the state fill forgets the binding mask
            ('src/game/net_state.cpp', 'if(k < snap.ubPlayers) {', 'if(true) {'),
            # the "Next Day" screen is not told apart
            ('src/game/net_state.cpp', 'g.ubScreen = NET_SCREEN_NEXTDAY;', 'g.ubScreen = NET_SCREEN_MAP;'),
            # a pad is unique per party, not per node (two players on two machines could not both keep Joystick 1)
            ('src/game/party.cpp', '(!p.isNet || p.aubNode[i] == p.aubNode[ubMember & (PARTY_MAX - 1)])', 'true'),
            # the knight screen's Control line is not handed to its owner node
            ('src/game/net_state.cpp', 'NET_SCREEN_PADS : NET_SCREEN_MENU', 'NET_SCREEN_MENU : NET_SCREEN_MENU'),
            ('src/game/scene_menu.cpp', 'if (env.pParty->isNet) env.pParty->ubFocus = (uint8_t)(uwSlot & (PARTY_MAX - 1));', ''),
            # 12.11 x net pads: a co-op drop leaves the live binding g_party on the dropped node
            ('src/game/net_drop.cpp', 'partyNetDrop(*pParty, ubDroppedNode, ubTakerNode);', ';'),
            # the handed-over member keeps its pad instead of the taker's first free one
            ('src/game/party.cpp', 'if(!partyPadTakenFor(p, m, c)) {', 'if(true) {'),
            # an open Control line keeps offering a pad the new node already uses
            ('src/game/scene_menu.cpp', 'if (env.pParty->isNet && partyPadTakenFor(*env.pParty, (uint8_t)uwSlot, ubPad)) {', 'if (false) {'),
            # a member without a pad stays without one when the session ends
            ('src/game/party.cpp', 'if(p.aubPad[i] != PAD_NONE) {', 'if(true) {'),
            # the knight screen's member focus survives the session
            ('src/game/party.cpp', 'if(p.ubFocus < PARTY_MAX) {', 'if(false) {'),
            # a play-alone party still copies the CONFIG table's pads over the live binding
            ('src/game/net_drop.cpp', 'if(g.isCoop && pParty && !pParty->isNet) {', 'if(g.isCoop && pParty) {'),
            # ---- rejoin (NETPLAY.md 4.5) ----
            # the token is not checked
            ('src/engine/netrejoin.cpp', 'if(h.aulToken[r.ubNode] == 0 || h.aulToken[r.ubNode] != r.ulToken) {', 'if(false) {'),
            # the rejoiner's hash is not compared
            ('src/engine/netrejoin.cpp', 'if(!h.isTransfer || r.ulTick != h.begin.ulTick || r.ulHash != h.begin.ulHash) {', 'if(!h.isTransfer) {'),
            # the host never says go
            ('src/engine/netrejoin.cpp', '			m.u.resume.ulTick = ulFirstTick;\n', '			m.u.resume.ulTick = ulFirstTick + 1;\n'),
            # the state is sent without the map-turn CONFIG
            ('src/engine/netrejoin.cpp', '			m.u.config = h.cfgNow;', '			m.u.config = h.cfg;'),
            # the rejoiner takes the first message after the map-turn wait as the state
            ('src/engine/netrejoin.cpp', 'if(c.ubState == RC_RECEIVING && snapRxDone(c.rx, m.u.stateEnd.uwChunks, m.u.stateEnd.ulCrc)) {', 'if(c.ubState == RC_RECEIVING) {'),
            # the transfer starts for links that are not waiting
            ('src/engine/netrejoin.cpp', 'if(l.ubState == RJL_WAIT && (ubNodeMask & (1U << l.ubNode)) != 0) {', 'if((ubNodeMask & (1U << l.ubNode)) != 0) {'),
            # a loaded state that never answers is waited for for ever
            ('src/engine/netrejoin.cpp', 'ulNow - h.aLink[k].ulLoadedAt > NR_LOADED_TIMEOUT_MS', 'false'),
            # the CONFIG with a dropped binding is refused by the encoder
            ('src/engine/netproto.cpp', 'b.ubNode == NET_NODE_NONE || b.ubNode == NET_NODE_DROPPED)', 'b.ubNode == NET_NODE_NONE)'),
            # the dropped node is expected again at once (a tick that was relayed without it)
            ('src/engine/netplay.cpp', 'ulUntil < s.ulTick || ulUntil - s.ulTick > s.ubDelay) {\n		return false;\n	}\n	NetFrame empty;\n	netFrameClear(empty);\n	netRingInit(s.aRing[ubNode], s.ulTick);\n	for(uint32_t t = s.ulTick; t < ulUntil; ++t) {',
             'ulUntil < s.ulTick || ulUntil - s.ulTick > s.ubDelay) {\n		return false;\n	}\n	NetFrame empty;\n	netFrameClear(empty);\n	netRingInit(s.aRing[ubNode], s.ulTick);\n	for(uint32_t t = s.ulTick; t < s.ulTick; ++t) {'),
            # the rejoiner's first real frame is for the wrong tick
            ('src/engine/netplay.cpp', '	s.ulNextSend = ulUntil;\n	return true;', '	s.ulNextSend = ulUntil + 1;\n	return true;'),
            # the other clients are not told
            ('src/engine/netnode.cpp', '	m.u.boundary.ulHash = ulHash;\n	sendToPeers(n, io, m);', '	m.u.boundary.ulHash = ulHash;'),
            # the other clients do not take the node back
            ('src/engine/netnode.cpp', '		if((ubMask & (1U << i)) != 0 && !netUndropNode(n.ses, i, n.ses.ulTick + n.ses.ubDelay)) {', '		if(false) {'),
            # the ticks already relayed are not replayed to the rejoiner
            ('src/engine/netnode.cpp', '		buildTick(n, t, m);\n		queueMsg(n, io, ubLink, m);', '		buildTick(n, t, m);'),
            # the host that played on alone starts its session at tick 0
            ('src/engine/netnode.cpp', 'netSessionInitAt(n.ses, n.ubNodes, 0, n.cfg.ubDelay, ulTick, 1);', 'netSessionInitAt(n.ses, n.ubNodes, 0, n.cfg.ubDelay, 0, 1);'),
            # the host that played on alone keeps its ended state
            ('src/engine/netnode.cpp', '	n.ubEnd = NE_NONE;\n	n.ubByeReason = 0;\n	n.uwPeerVersion = 0;\n	n.ubCloseMask = 0;', '	n.ubByeReason = 0;\n	n.uwPeerVersion = 0;\n	n.ubCloseMask = 0;'),
            # the tokens are all the same
            ('src/engine/netnode.cpp', 'n.aulToken[node] = netRejoinToken(ulEntropy, ulSeed, node);', 'n.aulToken[node] = 7;'),
            # the CONFIG of a client carries another node's token
            ('src/engine/netnode.cpp', 'm.u.config.ulToken = n.aulToken[l.ubNode];', 'm.u.config.ulToken = n.aulToken[1];'),
            # the rejoin effects: the knight stays an AI knight / the binding stays dropped
            ('src/game/net_rejoin.cpp', 'aRecords[k].ulKind = aubPortrait[k & (NET_NODE_MAX - 1)];', 'aRecords[k].ulKind = aRecords[k].ulKind;'),
            # ---- UDP input with history (ROADMAP 12.21, NETPLAY.md 2.2) ----
            # the host sends a TICK over TCP as well as in the datagram
            ('src/engine/netnode.cpp', 'if(isLinkInSession(n, k) && !isUdpCarrying(n.aLink[k])) {', 'if(isLinkInSession(n, k)) {'),
            # the client sends its INPUT over TCP as well as in the datagram
            ('src/engine/netnode.cpp', '		if(!isUdpCarrying(n.aLink[0])) {   // the datagrams carry it otherwise (udpFlush below)', '		if(true) {'),
            # no history of the relayed TICKs: the datagrams carry empty frames
            ('src/engine/netnode.cpp', '		storeHist(n, m);\n', ''),
            # a datagram sent behind a DROP the client has not received is applied
            ('src/engine/netnode.cpp', '	if(!netUdpDropSeqOk(u.ubDropSeq, n.ubDropSeq)) {', '	if(false) {'),
            # the client does not count the DROPs it receives (it would ignore every datagram after the first drop)
            ('src/engine/netnode.cpp', '	queueDrop(n, d.ulTick, d.ubNode);\n	++n.ubDropSeq;\n', '	queueDrop(n, d.ulTick, d.ubNode);\n'),
            # the host accepts a client datagram with any token / any session / the client one with any token
            ('src/engine/netnode.cpp', 'n.aulToken[u.ubNode] == u.ulToken && l.udp.ulSession == u.ulSession) {', 'l.udp.ulSession == u.ulSession) {'),
            ('src/engine/netnode.cpp', 'n.aulToken[u.ubNode] == u.ulToken && l.udp.ulSession == u.ulSession) {', 'n.aulToken[u.ubNode] == u.ulToken) {'),
            ('src/engine/netnode.cpp', 'if(!l.isUsed || u.ulToken != n.cfg.ulToken || u.ubNode != n.ubLocal', 'if(!l.isUsed || u.ubNode != n.ubLocal'),
            # a conflicting frame in a datagram is not evidence of a desync
            ('src/engine/netnode.cpp', 'if(ePut == NET_PUT_CONFLICT) {\n		endNode(n, NE_DESYNC, NET_BYE_DESYNC);\n		return false;', 'if(false) {\n		endNode(n, NE_DESYNC, NET_BYE_DESYNC);\n		return false;'),
            # the ticks the plan sends over TCP are never queued
            ('src/engine/netnode.cpp', '		queueMsg(n, io, ubLink, m);\n		++n.udp.st.ulEscalated;', '		++n.udp.st.ulEscalated;'),
            # the pump never flushes the datagrams (resends, acks, probes)
            ('src/engine/netnode.cpp', '	broadcast(n, io);\n	udpFlush(n, io, ulNow);\n	n.ubTrace = NN_TRACE_FLUSH;', '	broadcast(n, io);\n	n.ubTrace = NN_TRACE_FLUSH;'),
            # a client without the host's address probes nowhere instead of staying on TCP
            ('src/engine/netnode.cpp', '(n.eRole == NN_CLIENT && !l.udp.peer.isSet)', 'false'),
            # the datagrams of a reordered / duplicated network are counted as evidence of loss coverage wrongly (statistics)
            ('src/engine/netnode.cpp', 'if(ePut == NET_PUT_OK && !isNewest) {', 'if(ePut == NET_PUT_OK) {'),
            # the on-screen link line (NETPLAY.md 4.7): a PONG gives no round trip
            ('src/engine/netnode.cpp', 'netStatsPong(n.aLink[ubLink].stats, m.u.ping.ulToken, n.ulNow);', ';'),
            # ... the PING does not remember when it went out
            ('src/engine/netnode.cpp', 'netStatsPingSent(n.aLink[k].stats, n.ulPingToken, ulNow);', ';'),
            # ... a covered tick is counted as a fresh one (the loss stays 0)
            ('src/engine/netnode.cpp', 'netStatsTickIn(l.stats, n.ulNow, !isNewest);', 'netStatsTickIn(l.stats, n.ulNow, false);'),
            # pauses that TCP would survive (a scene load) count as a loss streak: a silent peer's clock keeps running (the cap on our own gaps is a mutation of test_netudp)
            ('src/engine/netnode.cpp', 'l.udp.isPeerSilent = ulNow - l.ulLastRx > NU_ALIVE_MS;', 'l.udp.isPeerSilent = false;'),
            # a dropped player's datagram state survives on the host
            ('src/engine/netnode.cpp', '			netUdpLinkInit(l.udp);   // the datagram state of the gone player', '			;   // the datagram state of the gone player'),
            # a stale datagram of the old incarnation is filed for a rejoiner's link
            ('src/engine/netnode.cpp', 'isLinkInSession(n, k) && !isRejoiner && l.ubNode == u.ubNode', 'isLinkInSession(n, k) && l.ubNode == u.ubNode'),
            # a rejoiner's link is not marked TCP-only on the host / on the rejoiner
            ('src/engine/netnode.cpp', '	netUdpFallback(l.udp, NUW_REJOIN);   // a rejoiner stays on TCP', '	;   // a rejoiner stays on TCP'),
            # ... a link that is not on UDP counts as one that is
            ('src/engine/netnode.cpp', 'aLinks[k].isUdpActive = n.aLink[k].udp.eState == NU_ACTIVE;', 'aLinks[k].isUdpActive = true;'),
            # the rejoiner's node does not get the bytes its rejoin pump read behind the host's RESUME (the first ticks of the burst: tests/netsession_driver "rejoin burst hand-over")
            ('src/engine/netnode.cpp', '	l.ulLastRx = ulNow;\n	l.framer = framer;\n	netUdpFallback(l.udp, NUW_REJOIN);\n	n.ulLastPing = ulNow;',
             '	l.ulLastRx = ulNow;\n	netUdpFallback(l.udp, NUW_REJOIN);\n	n.ulLastPing = ulNow;'),
            # ---- the relay-assisted direct path (NETPLAY.md 2.2b)
            # the host never completes a direct handshake / the client never does
            ('src/engine/netnode.cpp', 'if(isDirectDg && netDirectOnDatagram(l.udp.dir, ulNow, from, u.ubFlags)) {\n		pushUdpEvent(n, NUE_DIRECT_OK, static_cast<uint8_t>(iLink)', 'if(false) {\n		pushUdpEvent(n, NUE_DIRECT_OK, static_cast<uint8_t>(iLink)'),
            ('src/engine/netnode.cpp', 'if(isDirectDg && netDirectOnDatagram(l.udp.dir, ulNow, from, u.ubFlags)) {\n		pushUdpEvent(n, NUE_DIRECT_OK, 0,', 'if(false) {\n		pushUdpEvent(n, NUE_DIRECT_OK, 0,'),
            # nobody registers with the relay
            ('src/engine/netnode.cpp', 'if(dp.isReg && l.udp.peer.isSet) {', 'if(false) {'),
            # the datagrams of a direct link go to the relay anyway
            ('src/engine/netnode.cpp', 'io.pfnSendDgram(io.pCtx, plan.isDirect ? l.udp.dir.peer : l.udp.peer, aub, ulLen);', 'io.pfnSendDgram(io.pCtx, l.udp.peer, aub, ulLen);'),
            # the test option does not kill an established direct path
            ('src/engine/netnode.cpp', 'return n.udp.ubDirectTest == NDT_KILL && d.ulDirectSince != 0 && ulNow - d.ulDirectSince >= n.udp.ulDirectKillMs;', 'return false;'),
            # the relay's address on the host is replaced by the direct source (the host would answer a relayed client direct)
            ('src/engine/netnode.cpp', '	if(!isDirectDg) {\n		l.udp.peer = from;', '	if(true) {\n		l.udp.peer = from;'),
            # a datagram flagged DIRECT in a direct (non-relayed) game is accepted
            ('src/engine/netnode.cpp', 'if(isDirectDg && (u.ulSession == 0 || isDirectBlocked(n, l.udp.dir, ulNow))) {   // a direct game', 'if(isDirectDg && (isDirectBlocked(n, l.udp.dir, ulNow))) {   // a direct game'),
        ]

        for path, old, new in mutations:
            with self.subTest(mutation=old[:70]):   # every mutation is judged on its own: one that survives does not hide the next
                text = src(path)
                self.assertIn(old, text, 'mutation target vanished: ' + old)
                tmp = tempfile.mkdtemp(prefix='netsession_mut_')
                try:
                    exe = build(tmp, {path: text.replace(old, new, 1)})
                    r = subprocess.run([exe], capture_output=True, text=True, timeout=600)
                    self.assertNotEqual(r.returncode, 0, 'mutation not caught: ' + old)
                finally:
                    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
