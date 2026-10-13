"""Host test of the lockstep core (ROADMAP 12.6): engine/netplay.cpp (input frame, tick ring, session with input delay and drops,
the merge of docs/NETPLAY.md 3.1 / 3.2) with the CRC of engine/statehash.hpp.

tests/netplay_driver.cpp is compiled with clang++ (engine sources only, -fno-exceptions -fno-rtti) and checks:
  * the input frame codec and the tick ring,
  * the session (pre-filled delay ticks, missing-node mask, drop semantics),
  * the merge: every row of the classic table (map turn of a human / AI knight, place screens, duels incl. hot-seat and AI
    opponents, Practice, title menu) and of the co-op table (focus TURN / member / ANY, menu, fights, shared and invalid slots),
    and the state change on a drop,
  * simulated runs of 2, 3 and 4 nodes in the star topology with random link latency, input delay 0..8 and a node that stops
    answering and is dropped: every node must compute exactly the merged input a central reference computes.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = 'tests/netplay_driver.cpp'
SOURCES = ['src/engine/netplay.cpp']


def build(out_dir, sources=None, driver=DRIVER):
    exe = os.path.join(out_dir, 'netplay.exe')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-D_CRT_SECURE_NO_WARNINGS', '-Wno-missing-field-initializers',
           '-fno-exceptions', '-I', os.path.join(ROOT, 'include'), '-o', exe, os.path.join(ROOT, driver)]
    cmd += [os.path.join(ROOT, s) if not os.path.isabs(s) else s for s in (sources or SOURCES)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


@unittest.skipUnless(CXX, 'needs clang++')
class NetPlay(unittest.TestCase):
    def test_lockstep_core(self):
        tmp = tempfile.mkdtemp(prefix='netplay_')
        try:
            exe = build(tmp)
            r = subprocess.run([exe], capture_output=True, text=True, timeout=600)
            self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
            self.assertIn('OK ', r.stdout)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_mutated_core_is_caught(self):
        """The test has teeth: five deliberate mutations of netplay.cpp must each make the driver fail."""
        with open(os.path.join(ROOT, 'src/engine/netplay.cpp'), encoding='utf-8') as fh:
            src = fh.read()
        mutations = [
            # the "Next Day" screen takes the turn knight's pad only (a black knight's: nobody's fire counts)
            ('out.joy.uwPort1 |= recordWord(g, aFrames, ubNodes, k);', 'out.joy.uwPort1 = recordWord(g, aFrames, ubNodes, g.ubTurn);'),
            # the duel gives the second knight the port-1 word
            ('out.joy.uwPort0 = recordWord(g, aFrames, ubNodes, g.ubDuelSecond);',
             'out.joy.uwPort0 = recordWord(g, aFrames, ubNodes, g.ubDuelFirst);'),
            # a dropped node is still awaited
            ('if(!isGone(s, n, ulTick) && !netRingHas(s.aRing[n], ulTick)) {',
             'if(!netRingHas(s.aRing[n], ulTick)) {'),
            # co-op: two handed-over members get the same controller
            ('if(j != k && aBind[j].ubNode == ubTakerNode && aBind[j].ubPad == c) {',
             'if(false) {'),
            # a dropped knight's open place screen is not closed by the host
            ('if(g.ubTurn < NET_KNIGHTS_MAX && g.aBind[g.ubTurn].ubNode == NET_NODE_DROPPED) {',
             'if(false) {'),
            # the merge reads the wrong node's frame for a binding
            ('return ubNode == NET_NODE_NONE ? 0 : nodeWord(aFrames, ubNodes, ubNode, g.aBind[ubRecord].ubPad);',
             'return ubNode == NET_NODE_NONE ? 0 : nodeWord(aFrames, ubNodes, 0, g.aBind[ubRecord].ubPad);'),
            # the knight screen's Control line listens to the host instead of the owner node
            ('uwAny |= nodeWord(aFrames, ubNodes, ubNode, s);', 'uwAny |= nodeWord(aFrames, ubNodes, 0, s);'),
            # a member's merged slot is its controller instead of its member index (two Joystick 1 collide)
            ('out.pads.auwBits[ubSlot] |= recordWord(g, aFrames, ubNodes, m);', 'out.pads.auwBits[g.aBind[m].ubPad] |= recordWord(g, aFrames, ubNodes, m);'),
        ]
        for old, new in mutations:
            self.assertIn(old, src)
            tmp = tempfile.mkdtemp(prefix='netplay_mut_')
            try:
                path = os.path.join(tmp, 'netplay.cpp')
                with open(path, 'w', encoding='utf-8') as f:
                    f.write(src.replace(old, new))
                exe = build(tmp, sources=[path])
                r = subprocess.run([exe], capture_output=True, text=True, timeout=600)
                self.assertNotEqual(r.returncode, 0, 'mutation survived: ' + new)
            finally:
                shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
