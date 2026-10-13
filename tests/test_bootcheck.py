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
                # play_*.txt shoot play-*, the two-window network scripts (net_pair_*, net_relay_*; tools/netpair.py) netpair-h-* / netpair-j-*
                # per window, net_end_* (a two-window game played to the ending) netend-h-* / netend-j-*, the rest reg-*
                prefix = ('play-' if f.startswith('play_') else 'netend-' if f.startswith('net_end_') else 'netpair-' if f.startswith('net_') else 'reg-')
                self.assertTrue(all(n.startswith(prefix) for n in r['shots']), f)
                self.assertTrue(all(len(n) <= 27 for n in r['shots']), f + ': shot names are at most 27 characters')


class NetPairTickPokes(unittest.TestCase):
    """Batch5: the relay run desynced at tick 1600 because the pair plan's `frame N poke warp_lair` landed on a different logical tick per window (the host
    entered the lair at tick 1457, the join never did), and the classic run drifted a bar apart after a timed-out wait.  The scripts must not repeat that."""

    STATE = ('who ', 'warp_', 'lair_left', 'cursorx', 'cursory', 'strength', 'constitution', 'endurance', 'hpmax', 'hp ', 'moonstones')

    def script(self, name):
        return open(os.path.join(BOOT, name), newline='').read().replace('\r\n', '\n').rstrip('\n').split('\n')

    def test_state_pokes_of_the_pair_and_relay_scripts_are_tick_pokes(self):
        for name in ('net_pair_host.txt', 'net_pair_join.txt', 'net_relay_host.txt', 'net_relay_join.txt', 'net_classic_host.txt', 'net_classic_join.txt'):
            lines = self.script(name)
            plain = [l for l in lines if l.startswith('frame') and ' poke ' in l and l.split(' poke ', 1)[1].startswith(self.STATE)]
            self.assertEqual(plain, [], name + ': a plain poke lands on a different tick per window')
            self.assertTrue([l for l in lines if l == 'tick hash+10 poke warp_lair 0'], name)
        self.assertEqual([l for l in self.script('net_pair_host.txt') if l.startswith('tick ')], [l for l in self.script('net_pair_join.txt') if l.startswith('tick ')])
        self.assertEqual([l for l in self.script('net_relay_host.txt') if l.startswith('tick ')], [l for l in self.script('net_pair_host.txt') if l.startswith('tick ')])

    def test_classic_next_day_press_waits_for_the_screen(self):
        for name in ('net_classic_host.txt', 'net_classic_join.txt'):
            lines = self.script(name)
            i = lines.index('wait var turn 0 max 30000')
            self.assertEqual(lines[i + 1:i + 3], ['wait log next_day_waits max 6000', 'frame 5 joy1 fire pulse 12'], name)   # no blind bars before the press

    def test_loot_exit_press_is_long_enough_for_a_polling_loop(self):
        for name, press in (('net_pair_host.txt', 'frame 5 joy1 fire pulse 20'), ('net_pair_join.txt', 'frame 25 key LALT up'), ('net_classic_host.txt', 'frame 5 joy1 fire pulse 20')):
            self.assertIn(press, self.script(name), name)


class NetEnd(unittest.TestCase):
    """The game-to-the-ending pair (tests/boot/net_end_*.txt, tools/netpair.py --ending, NETPLAY.md 4.6)."""

    def script(self, side):
        return open(os.path.join(BOOT, 'net_end_%s.txt' % side), newline='').read().replace('\r\n', '\n').rstrip('\n').split('\n')

    def test_generated_files_are_current(self):
        sys.path.insert(0, os.path.join(BOOT, 'src'))
        import mknetpair
        for side in ('host', 'join'):
            self.assertEqual('\n'.join(self.script(side)), mknetpair.render(side, False, True).rstrip('\n'), side)

    def test_both_windows_poke_the_same_ticks_and_leave_hashing_behind(self):
        host, join = self.script('host'), self.script('join')
        pokes = [l for l in host if l.startswith('tick ')]
        self.assertTrue(pokes)
        self.assertEqual(pokes, [l for l in join if l.startswith('tick ')])   # one tick-poke list, applied by both windows
        for side in (host, join):
            self.assertFalse([l for l in side if l.startswith('frame') and ' poke ' in l and any(v in l for v in ('moonstones', 'warp_node', 'who '))],
                             'state pokes must be tick-pokes')
            tail = side[side.index('wait log enter_program max 3000'):]
            self.assertFalse([l for l in tail if 'hash_tick' in l], 'no hash barrier once the ending runs')
            self.assertEqual(tail[-1], 'frame 400 quit')

    def test_a_second_fire_ends_the_quest_text_before_the_enter_program_wait(self):
        # the leader's Alt enters Stonehenge, the place text waits for a fresh fire of any pad: that press must precede `wait log enter_program`
        # (the wait is the effect of the press; runs 2 / 3 waited for it with no press in front and timed out)
        for side, prefix in (('host', 'netend-h'), ('join', 'netend-j')):
            lines = self.script(side)
            head = lines[:lines.index('wait log enter_program max 3000')]
            shot = 'frame 5 shot %s-quest' % prefix
            self.assertIn(shot, head)
            self.assertTrue([l for l in head[head.index(shot):] if ' fire pulse ' in l or ' key LALT down' in l], side + ': no second fire after the quest shot')

    def test_netpair_verdicts(self):
        import netpair as N
        h = ['net: hash tick 100 crc AAAAAAAA', 'AUTOPLAY tick-poke moonstones 15 at tick 110', 'AUTOPLAY tick-poke warp_node 7 at tick 112', 'net: hash tick 150 crc BBBBBBBB',
             'net: the quest is complete: the listing is removed, the session closes after the ending', 'net: lobby: unlisted',
             'net: session closed after the ending', 'rtGameRun: enter mog', 'AUTOPLAY quit frame 9']
        j = [l for l in h if 'lobby: unlisted' not in l]
        ok, rep = N.compare(h, j)
        self.assertTrue(ok, rep)
        self.assertTrue(N.ending_checks(h, j)[0])
        self.assertTrue(N.ending_checks(h, j)[0] is True and not N.ending_checks(h, [l for l in j if 'closed after' not in l])[0])   # the guest never closed
        # a tick-poke at another tick on one window is a desync of the script, not a pass
        self.assertFalse(N.compare(h, [l.replace('at tick 112', 'at tick 113') for l in j])[0])
        # a late tick-poke is counted BAD, a hash after the quest too
        self.assertFalse(N.compare(h + ['AUTOPLAY ERROR tick-poke who 1 late: wanted tick 5, now tick 9'], j)[0])
        self.assertFalse(N.ending_checks(h + ['net: hash tick 200 crc CCCCCCCC'], j + ['net: hash tick 200 crc CCCCCCCC'])[0])
        # the hash bar must lie behind the pokes (otherwise the poked state was never compared)
        self.assertFalse(N.ending_checks([l for l in h if 'tick 150' not in l], [l for l in j if 'tick 150' not in l])[0])


class NetQuadClassic(unittest.TestCase):
    """The 4-player co-op scripts (net_quad_*.txt, `netpair.py --quad`) and the classic pair (net_classic_*.txt, `netpair.py --classic`), and the N-window verdict."""

    QUAD = ('net_quad_host.txt', 'net_quad_join1.txt', 'net_quad_join2.txt', 'net_quad_join3.txt')

    def script(self, name):
        return open(os.path.join(BOOT, name), newline='').read().replace('\r\n', '\n').rstrip('\n').split('\n')

    def test_generated_files_are_current(self):
        sys.path.insert(0, os.path.join(BOOT, 'src'))
        import mknetpair
        outs = mknetpair.extra_outputs()
        self.assertEqual(len(outs), 6)
        for name, text in outs:
            self.assertEqual('\n'.join(self.script(name)), text.rstrip('\n'), name)

    @staticmethod
    def lockstep_part(lines):
        """The barrier skeleton from the START on: every `sync` / `wait` line in order (frame lines are the windows' own business)."""
        i = lines.index('wait log lockstep_begins max 12000')
        return [l for l in lines[i:] if not l.startswith(('frame', '#'))]

    def test_quad_windows_share_one_barrier_structure(self):
        skel = [self.lockstep_part(self.script(n)) for n in self.QUAD]
        for s in skel[1:]:
            self.assertEqual(s, skel[0])
        self.assertEqual([l for l in skel[0] if l.startswith('wait var')], ['wait var leader %d max 6000' % n for n in (1, 2, 3, 0)])

    def test_quad_lobby_chain_and_leader_rotation(self):
        host = self.script(self.QUAD[0])
        seats = [l for l in host if l.startswith('wait log lobby_seat_')]
        self.assertEqual([l.split()[2] for l in seats], ['lobby_seat_%d_done' % k for k in range(4)])
        self.assertLess(host.index(seats[-1]), host.index('frame 190 joy1 fire pulse 30'))   # START only behind all four picks
        for k in (1, 2, 3):
            w = self.script(self.QUAD[k])
            self.assertIn('wait log lobby_seat_%d_done max 30000' % (k - 1), w)
            self.assertFalse([l for l in w if 'left pulse' in l or 'right pulse' in l], 'the selector starts on its own free portrait: no moves')
        # window k presses E (ends the turn) in round k only; every window pushes its own stick right in every round (the dumps show whose knight moved)
        for name in self.QUAD:
            w = self.script(name)
            self.assertEqual(sum(1 for l in w if l.endswith('key E down')), 1, name)
            self.assertEqual(sum(1 for l in w if l == 'frame 5 joy1 right'), 4, name)

    def test_quad_shots_are_per_window(self):
        for k, name in enumerate(self.QUAD):
            r = B.parse(open(os.path.join(BOOT, name)).read())
            self.assertTrue(r['shots'] and all(n.startswith('netpair-q%d-' % k) for n in r['shots']), name)

    def test_classic_pair(self):
        host, join = self.script('net_classic_host.txt'), self.script('net_classic_join.txt')
        self.assertEqual(self.lockstep_part(host), self.lockstep_part(join))
        i = host.index('frame 70 joy1 up pulse 12')                                     # up from the portraits: the MODE row, fire switches it
        self.assertEqual(host[i + 2], 'frame 160 joy1 fire pulse 30')
        self.assertIn('wait log lobby_mode_classic max 3000', host)
        self.assertIn('wait log lobby_mode_classic max 15000', join)                    # the guest picks only when the room is CLASSIC
        self.assertFalse([l for l in host + join if 'control' in l and 'shot' in l], 'classic has no controller line')
        self.assertEqual(sum(1 for l in host if l.endswith('key E down')), 2)           # the host ends its turn in both rounds, the guest only in the first
        self.assertEqual(sum(1 for l in join if l.endswith('key E down')), 1)
        self.assertEqual([l for l in host if l.startswith('wait var turn')], ['wait var turn 1 max 6000', 'wait var turn 0 max 30000', 'wait var turn 1 max 6000'])

    def test_netpair_verdict_for_n_windows(self):
        import netpair as N

        def win(crcs, quit_=True, extra=()):
            lines = ['net: hash tick %d crc %s' % (t, c) for t, c in crcs] + list(extra)
            return lines + (['AUTOPLAY quit frame 9'] if quit_ else [])
        good = [(50, 'AAAAAAAA'), (100, 'BBBBBBBB')]
        names = N.window_names(4)
        self.assertEqual(names, ['host', 'join1', 'join2', 'join3'])
        self.assertEqual(N.window_names(2), ['host', 'join'])
        ok, rep = N.compare_n([(n, win(good)) for n in names])
        self.assertTrue(ok, rep)
        self.assertIn('hashes: host 2, join1 2, join2 2, join3 2, common 2', rep)
        self.assertTrue(any(l.startswith('  tick 100') and l.count('BBBBBBBB') == 4 and l.endswith(' ok') for l in rep), rep)
        # one window with another crc at one tick: MISMATCH, whichever window it is
        for bad in range(4):
            ws = [(n, win(good if i != bad else [(50, 'AAAAAAAA'), (100, 'CCCCCCCC')])) for i, n in enumerate(names)]
            ok, rep = N.compare_n(ws)
            self.assertFalse(ok)
            self.assertTrue([l for l in rep if l.endswith('MISMATCH')], rep)
        # a window that never quit, a desync line, no tick hashed by all
        self.assertFalse(N.compare_n([(n, win(good, quit_=(n != 'join2'))) for n in names])[0])
        self.assertFalse(N.compare_n([(n, win(good, extra=['net: desync at tick 100'] if n == 'join3' else [])) for n in names])[0])
        self.assertFalse(N.compare_n([(n, win(good if n != 'join1' else [(150, 'AAAAAAAA')])) for n in names])[0])
        # a tick only one window hashed is not compared, the common ticks still are
        ok, rep = N.compare_n([(n, win(good + ([(150, 'DDDDDDDD')] if n == 'host' else []))) for n in names])
        self.assertTrue(ok, rep)
        # the two-window answer keeps its format
        h, j = win(good), win(good)
        self.assertEqual(N.compare(h, j), N.compare_n([('host', h), ('join', j)]))
        self.assertIn('  tick 50     host AAAAAAAA join AAAAAAAA ok', N.compare(h, j)[1])


if __name__ == '__main__':
    unittest.main()
