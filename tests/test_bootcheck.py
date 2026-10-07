import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import bootcheck as B  # noqa: E402

BOOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'boot')


class Parse(unittest.TestCase):
    def test_directives(self):
        r = B.parse('# shotcmp-tol a 2.5\n# shotdiff a b 0.3\nframe 10 shot a\nFRAME 20 SHOT b\nframe 99 quit\n')
        self.assertEqual(r['shots'], ['a', 'b'])
        self.assertEqual(r['tol'], {'a': 2.5})
        self.assertEqual(r['differ'], [('a', 'b', 0.3)])
        self.assertEqual(r['last_frame'], 99)

    def test_unknown_names_rejected(self):
        with self.assertRaises(ValueError):
            B.parse('# shotdiff a b 1\nframe 1 shot a\n')
        with self.assertRaises(ValueError):
            B.parse('# shotcmp-tol zz 1\nframe 1 shot a\n')
        with self.assertRaises(ValueError):
            B.parse('frame 1 shot a\nframe 2 shot a\n')

    def test_judge(self):
        self.assertTrue(B.judge_ref('x', 0.4, 0.5)[0])
        self.assertFalse(B.judge_ref('x', 0.6, 0.5)[0])
        self.assertTrue(B.judge_differ('a', 'b', 0.5, 0.2)[0])
        self.assertFalse(B.judge_differ('a', 'b', 0.0, 0.2)[0])

    def test_timeout(self):
        self.assertEqual(B.timeout_for(5000, 100), 200)
        self.assertEqual(B.timeout_for(5000, 100, shots=3), 230)

    def test_barriers_sum_the_segments(self):
        # frames after a barrier count from its release: the budget adds the segments up (a wait without `max` counts WAIT_GUESS)
        text = chr(10).join(('frame 300 key SPACE tap', 'wait input 10 max 3000', 'frame 60 shot a', 'frame 500 joy1 fire pulse', 'frame 20 wait file Re.a', 'frame 100 shot b', 'sync', 'frame 50 quit'))
        r = B.parse(text)
        self.assertEqual(r['last_frame'], (300 + 3000) + (500 + B.WAIT_GUESS) + 100 + 50)   # sync never blocks: no guess
        # no barrier: the last frame, as before
        self.assertEqual(B.parse(chr(10).join(('frame 7 shot a', 'frame 9 quit')))['last_frame'], 9)
        # a comment mentioning wait is not a barrier
        self.assertEqual(B.parse(chr(10).join(('# wait for it', 'frame 9 quit')))['last_frame'], 9)

    def test_play_scripts_match_their_sources(self):
        # tests/boot/play_*.txt are generated from tests/boot/src/*.mk (py tests/boot/src/mkplay.py): they must not drift apart
        sys.path.insert(0, os.path.join(BOOT, 'src'))
        import mkplay
        n = 0
        for f in sorted(os.listdir(os.path.join(BOOT, 'src'))):
            if f.endswith('.mk'):
                want = mkplay.build(open(os.path.join(BOOT, 'src', f)).read())
                have = open(os.path.join(BOOT, f[:-3] + '.txt'), newline='').read()
                self.assertEqual(have, want, f)
                n += 1
        self.assertGreaterEqual(n, 12)

    def test_checked_in_scripts_parse_and_fit_the_game_parser(self):
        for f in sorted(os.listdir(BOOT)):
            if f.endswith('.txt'):
                text = open(os.path.join(BOOT, f)).read()
                r = B.parse(text)
                # perf_*.txt (ROADMAP 8.4a) take no shots on purpose: a shot freezes the guest and spoils the timing; mods_*.txt (9.4c) check log lines.
                self.assertTrue(r['shots'] or f.startswith(('perf_', 'mods_')), f)
                self.assertLessEqual(len(text), 16384, f + ' exceeds the 16 KB script limit')
                prefix = 'play-' if f.startswith('play_') else 'reg-'
                self.assertTrue(all(n.startswith(prefix) for n in r['shots']), f)
                self.assertTrue(all(len(n) <= 27 for n in r['shots']), f + ': shot names are at most 27 characters')


if __name__ == '__main__':
    unittest.main()
