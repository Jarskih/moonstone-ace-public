"""Host test of the ping and loss numbers of the on-screen link line (ROADMAP M12, docs/NETPLAY.md 4.7): engine/netstats.cpp.

tests/netstats_driver.cpp is compiled with clang++ (-fno-exceptions -fno-rtti, the one engine source) and checks the PING / PONG round trip (tokens, the
EWMA), the loss window (percent of covered ticks over five one-second buckets, too few ticks, the window sliding, a long silence, a stale clock,
saturation), the worst link of several (highest ping, highest loss, TCP as soon as one link is not on UDP, nothing in the session) and the text formatter (the
characters of the small font only).  The whole-node behaviour (real NetNodes: PONG round trips, datagram loss) is tests/test_netsession.py, section `stats`.

The mutation test proves the driver has teeth: each deliberate bug in engine/netstats.cpp must make it fail.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = 'tests/netstats_driver.cpp'
SOURCE = 'src/engine/netstats.cpp'


def build(out_dir, mutated_text=None):
    exe = os.path.join(out_dir, 'netstats.exe')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
           '-I', os.path.join(ROOT, 'include'), '-o', exe, os.path.join(ROOT, DRIVER)]
    if mutated_text is None:
        cmd.append(os.path.join(ROOT, SOURCE))
    else:
        p = os.path.join(out_dir, 'netstats_mut.cpp')
        with open(p, 'w', encoding='utf-8', newline='') as fh:
            fh.write(mutated_text)
        cmd.append(p)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


@unittest.skipUnless(CXX, 'needs clang++')
class NetStats(unittest.TestCase):
    def test_ping_loss_worst_format(self):
        tmp = tempfile.mkdtemp(prefix='netstats_')
        try:
            r = subprocess.run([build(tmp)], capture_output=True, text=True, timeout=300)
            self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
            self.assertIn('OK ', r.stdout)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_mutations_are_caught(self):
        with open(os.path.join(ROOT, SOURCE), encoding='utf-8', newline='') as fh:
            text = fh.read()
        mutations = [
            # any PONG is a sample, whatever its token
            ('if(!s.isPingOut || ulToken != s.ulToken) {', 'if(!s.isPingOut) {'),
            # the smoothing is gone: the last sample wins
            ('const int32_t lDelta = (lSample16 - static_cast<int32_t>(s.ulRttX16)) / (1 << NS_RTT_SHIFT);', 'const int32_t lDelta = (lSample16 - static_cast<int32_t>(s.ulRttX16));'),
            # a PONG may answer the same PING twice
            ('	s.isPingOut = false;\n	const int32_t lSample16', '	const int32_t lSample16'),
            # the ms are cut instead of rounded
            ('ulMs = s.hasRtt ? (s.ulRttX16 + 8) / 16 : 0;', 'ulMs = s.hasRtt ? s.ulRttX16 / 16 : 0;'),
            # covered and fresh ticks are not told apart
            ('uint16_t &uwCount = isCovered ? s.auwCovered[s.ubHead] : s.auwFresh[s.ubHead];', 'uint16_t &uwCount = s.auwFresh[s.ubHead];'),
            # old seconds never leave the window
            ('		clearBucket(s, s.ubHead);\n		s.ulBucketMs += NS_LOSS_BUCKET_MS;', '		s.ulBucketMs += NS_LOSS_BUCKET_MS;'),
            # the percentage is not rounded
            ('(ulCovered * 100 + ulTotal / 2) / ulTotal', '(ulCovered * 100) / ulTotal'),
            # a handful of ticks is judged
            ('if(ulTotal < NS_LOSS_MIN_TICKS) {', 'if(ulTotal == 0) {'),
            # the counters wrap
            ('if(uwCount != 0xFFFF) {', 'if(true) {'),
            # the best link is shown instead of the worst
            ('if(uwMs > out.uwPingMs) {', 'if(out.uwPingMs == 0 || uwMs < out.uwPingMs) {'),
            ('if(ubLoss > out.ubLossPct) {', 'if(ubLoss < out.ubLossPct) {'),
            # a link on TCP does not turn the line to TCP
            ('			out.isUdp = false;\n		}\n	}', '		}\n	}'),
            # the unit is a percent sign (the small font has no glyph for it)
            ('appendText(acOut, ubPos, " PCT");', 'appendText(acOut, ubPos, "%");'),
            # the ping is not clamped
            ('const uint16_t uwMs = ulMs > NS_PING_MAX_MS ? NS_PING_MAX_MS : static_cast<uint16_t>(ulMs);', 'const uint16_t uwMs = static_cast<uint16_t>(ulMs);'),
        ]
        tmp = tempfile.mkdtemp(prefix='netstats_mut_')
        try:
            for old, new in mutations:
                with self.subTest(mutation=old[:70]):
                    self.assertIn(old, text, 'mutation target vanished: ' + old)
                    try:
                        exe = build(tmp, text.replace(old, new, 1))
                    except RuntimeError:
                        continue   # does not compile: caught as well
                    r = subprocess.run([exe], capture_output=True, text=True, timeout=300)
                    self.assertNotEqual(r.returncode, 0, 'mutation not caught: ' + old + ' -> ' + new)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
