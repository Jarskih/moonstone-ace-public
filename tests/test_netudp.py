"""Host test of the rules of per-tick input over UDP with redundant history (ROADMAP 12.21, docs/NETPLAY.md 2.2): engine/netudp.cpp.

tests/netudp_driver.cpp is compiled with clang++ (-fno-exceptions -fno-rtti, the engine sources only).  It checks the planner (the window of
unacked ticks, the ticks the history cannot carry, the escalation to TCP after a quarter second of stalled acks, the revert after the loss
streak, the delayed ack, the keepalive), the three-datagram probe handshake and its give-up budget, the ack bookkeeping (only forward, never
beyond what was produced), the acks a node derives from its rings, the DROP sequence guard, and two endpoints over a lossy channel (every 2nd
datagram lost, bursts, random loss with duplicates and reordering, a one second outage, black holes).  The whole-node behaviour (real NetNodes over a
fake datagram network) is tests/test_netsession.py, section `nudp`.

The mutation test proves the driver has teeth: each deliberate bug in engine/netudp.cpp must make it fail.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = 'tests/netudp_driver.cpp'
SOURCE = 'src/engine/netudp.cpp'
OTHERS = ['src/engine/netplay.cpp', 'src/engine/pad.cpp', 'src/engine/input.cpp']


def build(out_dir, mutated_text=None):
    exe = os.path.join(out_dir, 'netudp.exe')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wno-unused-parameter', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
           '-I', os.path.join(ROOT, 'include'), '-o', exe, os.path.join(ROOT, DRIVER)]
    if mutated_text is None:
        cmd.append(os.path.join(ROOT, SOURCE))
    else:
        p = os.path.join(out_dir, 'netudp_mut.cpp')
        with open(p, 'w', encoding='utf-8', newline='') as fh:
            fh.write(mutated_text)
        cmd.append(p)
    cmd += [os.path.join(ROOT, s) for s in OTHERS]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


@unittest.skipUnless(CXX, 'needs clang++')
class NetUdp(unittest.TestCase):
    def test_rules_and_lossy_channel(self):
        tmp = tempfile.mkdtemp(prefix='netudp_')
        try:
            r = subprocess.run([build(tmp)], capture_output=True, text=True, timeout=600)
            self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
            self.assertIn('OK ', r.stdout)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_mutations_are_caught(self):
        with open(os.path.join(ROOT, SOURCE), encoding='utf-8', newline='') as fh:
            text = fh.read()
        mutations = [
            # the handshake never gives up
            ('if(u.ulProbeMs >= NU_PROBE_GIVEUP_MS) {', 'if(false) {'),
            # a long gap between two pumps (a scene load) counts in full
            ('u.ulProbeMs += ulGap < NU_PUMP_GAP_MAX_MS ? ulGap : NU_PUMP_GAP_MAX_MS;', 'u.ulProbeMs += ulGap;'),
            # a datagram without HEARS completes the handshake (a one-way path goes active)
            ('if((ubFlags & NET_UDP_HEARS) != 0) {', 'if(true) {'),
            # the window after the handshake starts at tick 0 instead of at the tick of the handshake
            ('u.ulPeerAck = ulProduced;\n			u.ulSentNext = ulProduced;', 'u.ulPeerAck = 0;\n			u.ulSentNext = ulProduced;'),
            # an ack beyond what was produced is believed
            ('			ulClamped = ulProduced;   // a peer cannot hold ticks we never made', '			;'),
            # an older ack (a reordered datagram) moves the window back
            ('if(isAhead(ulClamped, u.ulPeerAck)) {', 'if(true) {'),
            # the acks can stall for ever without anything over TCP
            ('if(ulUnacked > 0 && ulStuck >= NU_ESCALATE_MS && u.dir.eState != ND_DIRECT) {', 'if(false) {'),
            # the loss streak never reverts the link
            ('if(ulUnacked > 0 && ulStuck >= NU_LOSS_MS) {', 'if(false) {'),
            # a pause of ours (a scene load) counts in full towards the loss streak
            ('u.ulStuckMs += ulGap < NU_PUMP_GAP_MAX_MS ? ulGap : NU_PUMP_GAP_MAX_MS;', 'u.ulStuckMs += ulGap;'),
            # the stall clock runs while the peer is silent (it loads a scene)
            ('} else if(!u.isPeerSilent) {', '} else {'),
            # the window is not bounded by the history
            ('const uint32_t ulCount = ulUnacked < NET_UDP_HISTORY_MAX ? ulUnacked : NET_UDP_HISTORY_MAX;', 'const uint32_t ulCount = ulUnacked;'),
            # the ticks older than the window are not sent over TCP
            ('uint32_t ulTcpEnd = out.ulFirst;', 'uint32_t ulTcpEnd = 0;'),
            # TCP gets the same ticks again and again
            ('const uint32_t ulTcpStart = maxTick(ulAck, u.ulTcpNext);', 'const uint32_t ulTcpStart = ulAck;'),
            # a lost datagram is never sent again unless a new tick comes
            ('(ulUnacked > 0 && ulNow - u.ulLastTx >= NU_RESEND_MS)', 'false'),
            # no keepalive
            ('|| ulNow - u.ulLastTx >= NU_KEEPALIVE_MS) {', ') {'),
            # the delayed ack never goes out
            ('const bool isAckDue = u.isAckDirty && ulNow - u.ulAckDirtyAt >= NU_ACK_DELAY_MS;', 'const bool isAckDue = false;'),
            # the ack is sent alone at once instead of after the delay
            ('const bool isAckDue = u.isAckDirty && ulNow - u.ulAckDirtyAt >= NU_ACK_DELAY_MS;', 'const bool isAckDue = u.isAckDirty;'),
            # the ack that a datagram carries stays "dirty" (an ack-only datagram after every datagram)
            ('		u.ulAckSent = ulOurAck;   // this datagram carries the ack\n		u.isAckDirty = false;', '		u.ulAckSent = 0;'),
            # the floor (the client's consumed ticks) is ignored
            ('uint32_t ulAck = maxTick(u.ulPeerAck, ulFloor);', 'uint32_t ulAck = u.ulPeerAck;'),
            # the answer to a probe waits for the next probe interval
            ('		u.isReplyDue = true;   // answer a probe at once: the handshake takes three datagrams, not three probe intervals', '		;'),
            # a dropped node does not pin the client's ack
            ('isAnyDropped = isAnyDropped || s.abDropped[i];', 'isAnyDropped = false;'),
            # the ack counts ticks that are not complete
            ('while(ulTick - s.ulTick < NET_RING_TICKS && netMissingMask(s, ulTick) == 0) {', 'while(ulTick - s.ulTick < NET_RING_TICKS) {'),
            # the host's ack counts holes
            ('while(ulTick - s.ulTick < NET_RING_TICKS && netRingHas(s.aRing[ubNode], ulTick)) {', 'while(ulTick - s.ulTick < NET_RING_TICKS && (netRingHas(s.aRing[ubNode], ulTick) || netRingHas(s.aRing[ubNode], ulTick + 1))) {'),
            # a datagram behind a DROP the receiver has not seen is applied
            ('return static_cast<int8_t>(static_cast<uint8_t>(ubHostSeq - ubMySeq)) <= 0;', 'return true;'),
            ('return static_cast<int8_t>(static_cast<uint8_t>(ubHostSeq - ubMySeq)) <= 0;', 'return static_cast<int8_t>(static_cast<uint8_t>(ubHostSeq - ubMySeq)) < 0;'),
            # the probe is sent without a peer address
            ('if(u.peer.isSet && (!u.isProbed', 'if((!u.isProbed'),
            # ---- the direct path (NETPLAY.md 2.2b)
            # a direct datagram without DHEARS completes the punch (a one-way path goes direct)
            ('if(d.eState == ND_PUNCH && (ubFlags & NET_UDP_DHEARS) != 0) {', 'if(d.eState == ND_PUNCH) {'),
            # the direct path never gives up on its own stall
            ('if(u.dir.eState == ND_DIRECT && ulUnacked > 0 && u.ulStuckMs >= NU_DIRECT_STALL_MS) {', 'if(false) {'),
            # a public source replaces the LAN peer / the peer never follows a changed source
            ('if(!d.isHeard || (!d.isPeerLocal && !isSamePeer(d.peer, sFrom))) {', 'if(true) {'),
            ('if(!d.isHeard || (!d.isPeerLocal && !isSamePeer(d.peer, sFrom))) {', 'if(!d.isHeard) {'),
            # no keep-alive on the relay path while direct
            ('if(u.dir.eState == ND_DIRECT && ulNow - u.dir.ulLastRelayTx >= NU_KEEPALIVE_MS) {', 'if(false) {'),
            # the punch never gives up / waits for PUNCH_INFO for ever
            ('if(directBudget(d, ulNow) >= NU_PUNCH_GIVEUP_MS) {', 'if(false) {'),
            ('if(directBudget(d, ulNow) >= NU_PUNCH_INFO_GIVEUP_MS) {', 'if(false) {'),
            # a scene load counts in full towards the punch budget
            ('d.ulBudgetMs += ulGap < NU_PUMP_GAP_MAX_MS ? ulGap : NU_PUMP_GAP_MAX_MS;', 'd.ulBudgetMs += ulGap;'),
            # the direct path is used while the link is only probing / datagrams go direct before the handshake completed
            ('out.isDirect = u.dir.eState == ND_DIRECT;', 'out.isDirect = u.dir.eState >= ND_PUNCH;'),
            # a failed link keeps punching after the TCP fallback
            ('u.dir.eState = ND_OFF;   // no UDP at all any more', 'u.dir.eState = u.dir.eState;   // no UDP at all any more'),
            # the peer is not told once the punch completed (it never completes)
            ('d.isReplyDue = true;   // the peer completes when it hears a datagram of ours that carries DHEARS: send one now', 'd.isReplyDue = false;'),
            # a local candidate equal to the public one is probed twice / the local one without the flag
            ('d.sLocal.isSet = false;   // one candidate (a loopback test, a host without NAT)', ';'),
        ]
        for old, new in mutations:
            with self.subTest(mutation=old[:70]):
                self.assertIn(old, text, 'mutation target vanished: ' + old)
                tmp = tempfile.mkdtemp(prefix='netudp_mut_')
                try:
                    exe = build(tmp, text.replace(old, new, 1))
                    r = subprocess.run([exe], capture_output=True, text=True, timeout=600)
                    self.assertNotEqual(r.returncode, 0, 'mutation not caught: ' + old + ' -> ' + new)
                finally:
                    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
