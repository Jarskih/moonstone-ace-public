"""Host test for src/engine/autoplay.cpp: the headless boot-test script parser (docs/AUTOPLAY.md). Compiled with clang++, fed
script text on stdin; one line per event on stdout."""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/autoplay.hpp"
int main(int argc, char **argv) {
    unsigned max = argc > 1 ? (unsigned)atoi(argv[1]) : ms::kApMaxEvents;
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof buf, stdin);
    static ms::ApEvent ev[1024];
    ms::ApParse r = ms::apParse(buf, (uint32_t)n, ev, (uint16_t)max);
    printf("R %u %u %u %u\n", r.uwCount, r.uwErrors, r.uwFirstErrLine, r.isOverflow);
    for(unsigned i = 0; i < r.uwCount; ++i) {
        printf("E %u %u %u %u %u %u %u %s\n", (unsigned)ev[i].ulFrame, ev[i].ubKind, ev[i].ubCode, ev[i].isDown, ev[i].ubBits,
               (unsigned)ev[i].uwMax, (unsigned)ev[i].ulValue, ev[i].szText);
    }
    printf("K %d %d %d %d\n", ms::apKeyCode("SPACE"), ms::apKeyCode("a"), ms::apKeyCode("f10"), ms::apKeyCode("bogus"));
    return 0;
}
'''

KEY, JOY, SHOT, LOG, QUIT, WAIT_FILE, WAIT_LOG, WAIT_INPUT, SYNC, POKE, MASH, WAIT_VAR = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
UP, DOWN, LEFT, RIGHT, FIRE = 0x08, 0x04, 0x02, 0x01, 0x10


@unittest.skipUnless(CXX, 'needs clang++')
class AutoplayParserTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        src = os.path.join(cls.tmp, 'drv.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'drv.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', os.path.join(ROOT, 'include'), src,
                            os.path.join(ROOT, 'src', 'engine', 'autoplay.cpp'), '-o', cls.exe], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_script(self, text, maxev=None):
        args = [self.exe] + ([str(maxev)] if maxev else [])
        out = subprocess.run(args, input=text.encode(), capture_output=True, check=True).stdout.decode().splitlines()
        count, errors, first, overflow = map(int, out[0].split()[1:])
        events = []
        extra = []
        for line in out[1:-1]:
            p = line.split(' ', 8)
            extra.append((int(p[6]), int(p[7])))   # (uwMax, ulValue) per event
            events.append((int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]), p[8] if len(p) > 8 else ''))
        keys = tuple(map(int, out[-1].split()[1:]))
        return dict(count=count, errors=errors, first=first, overflow=overflow, events=events, keys=keys, extra=extra)

    def test_basic_keys(self):
        r = self.run_script('frame 600 key SPACE down\nframe 606 key SPACE up\n')
        self.assertEqual(r['events'], [(600, KEY, 0x40, 1, 0, ''), (606, KEY, 0x40, 0, 0, '')])
        self.assertEqual(r['errors'], 0)

    def test_joy_and_tap_and_quit(self):
        r = self.run_script('frame 900 joy1 down\nframe 930 joy1 fire\nframe 960 joy1 none\nframe 1000 joy0 up+fire\n'
                            'frame 1010 key RETURN tap\nframe 3000 quit\n')
        self.assertEqual(r['events'], [
            (900, JOY, 1, 0, DOWN, ''), (930, JOY, 1, 0, FIRE, ''), (960, JOY, 1, 0, 0, ''),
            (1000, JOY, 0, 0, UP | FIRE, ''), (1010, KEY, 0x44, 1, 0, ''), (1025, KEY, 0x44, 0, 0, ''),
            (3000, QUIT, 0, 0, 0, '')])

    def test_type_expands_to_pairs(self):
        r = self.run_script('frame 1200 type ACE\n')
        # A = 0x20, C = 0x33, E = 0x12; a key-down every 30 frames, held 15
        self.assertEqual([(e[0], e[2], e[3]) for e in r['events']],
                         [(1200, 0x20, 1), (1215, 0x20, 0), (1230, 0x33, 1), (1245, 0x33, 0), (1260, 0x12, 1), (1275, 0x12, 0)])

    def test_type_digits_and_space(self):
        r = self.run_script('frame 0 type A_1\n')
        self.assertEqual([e[2] for e in r['events'] if e[3]], [0x20, 0x40, 0x01])

    def test_sorted_stable(self):
        r = self.run_script('frame 50 key A down\nframe 10 key B down\nframe 50 key A up\nframe 10 key B up\n')
        self.assertEqual([(e[0], e[2], e[3]) for e in r['events']], [(10, 0x35, 1), (10, 0x35, 0), (50, 0x20, 1), (50, 0x20, 0)])

    def test_shot_log_and_names(self):
        r = self.run_script('frame 700 shot menu-1.a\nframe 710 log hello_world\n')
        self.assertEqual(r['events'], [(700, SHOT, 0, 0, 0, 'menu-1.a'), (710, LOG, 0, 0, 0, 'hello_world')])

    def test_comments_blank_crlf_case(self):
        r = self.run_script('# header\r\n\r\nFRAME 5 KEY esc DOWN ; trailing\r\n   frame 6 Key Esc Up   # more\r\n')
        self.assertEqual(r['errors'], 0)
        self.assertEqual([(e[0], e[2], e[3]) for e in r['events']], [(5, 0x45, 1), (6, 0x45, 0)])

    def test_bad_lines_are_skipped_and_counted(self):
        r = self.run_script('frame 1 key SPACE down\nbogus\nframe x quit\nframe 2 key NOPE down\nframe 3 joy1 sideways\n'
                            'frame 4 shot bad/name\nframe 5 key A hold\nframe 6 type A!\nframe 7 quit extra\nframe 8 quit\n')
        self.assertEqual(r['errors'], 8)
        self.assertEqual(r['first'], 2)
        self.assertEqual([e[0] for e in r['events']], [1, 8])

    def test_overflow(self):
        r = self.run_script('frame 1 type ABC\n', maxev=4)
        self.assertEqual(r['count'], 4)
        self.assertTrue(r['overflow'])

    def test_key_names(self):
        self.assertEqual(self.run_script('')['keys'], (0x40, 0x20, 0x59, -1))

    def test_all_letters_digits_match_ace(self):
        # ACE key.h: rows Q.. (0x10), A.. (0x20), Z.. (0x31); digits 1-9 = 0x01.., 0 = 0x0A
        text = ''.join('frame 0 key %s down\n' % c for c in 'QWERTYUIOPASDFGHJKLZXCVBNM1234567890')
        codes = [e[2] for e in self.run_script(text)['events']]
        want = list(range(0x10, 0x1A)) + list(range(0x20, 0x29)) + list(range(0x31, 0x38)) + list(range(1, 10)) + [0x0A]
        self.assertEqual(codes, want)

    def test_joy_pulse(self):
        r = self.run_script('frame 10 joy1 fire pulse\nframe 20 joy0 down+left PULSE\nframe 30 joy1 fire\nframe 40 joy1 fire press\n')
        self.assertEqual(r['events'], [(10, JOY, 1, 1, FIRE, ''), (20, JOY, 0, 1, DOWN | LEFT, ''), (30, JOY, 1, 0, FIRE, '')])
        self.assertEqual(r['errors'], 1)
        r = self.run_script('frame 1 joy1 fire pulse 6\nframe 2 joy1 fire pulse 0\nframe 3 joy1 fire pulse 256\nframe 4 joy1 fire pulse x\n'
                            'frame 5 joy1 fire pulse 2 3\n')
        self.assertEqual((r['count'], r['errors']), (1, 4))
        self.assertEqual(r['extra'][0], (6, 0))

    def test_wait_file_log_input_sync(self):
        r = self.run_script('wait file Re.a\nframe 5 wait log name_entry max 300\nframe 7 wait input\nwait input 9 max 40\n'
                            'wait input max 12\nsync\nframe 3 sync\n')
        self.assertEqual(r['errors'], 0)
        self.assertEqual([(e[0], e[1], e[5]) for e in r['events']], [
            (0, WAIT_FILE, 'Re.a'), (5, WAIT_LOG, 'name_entry'), (7, WAIT_INPUT, ''), (0, WAIT_INPUT, ''), (0, WAIT_INPUT, ''),
            (0, SYNC, ''), (3, SYNC, '')])
        self.assertEqual(r['extra'][:5], [(0, 0), (300, 0), (0, 5), (40, 9), (12, 5)])

    def test_wait_input_gap(self):
        r = self.run_script('wait input 4 gap 3 max 100\nwait input max 9 gap 2\nwait input gap 300\nwait input 5 gap\nwait input foo\n')
        self.assertEqual((r['count'], r['errors']), (2, 3))
        self.assertEqual([(e[1], e[4]) for e in r['events']], [(WAIT_INPUT, 3), (WAIT_INPUT, 2)])
        self.assertEqual(r['extra'][:2], [(100, 4), (9, 5)])

    def test_mash(self):
        r = self.run_script('frame 5 mash joy1 12\nframe 6 mash joy0 off\nframe 7 mash joy1 0\nframe 8 mash joy2 3\nframe 9 mash joy1 256\n')
        self.assertEqual((r['count'], r['errors']), (2, 3))
        self.assertEqual([(e[0], e[1], e[2], e[4]) for e in r['events']], [(5, MASH, 1, 12), (6, MASH, 0, 0)])

    def test_wait_var(self):
        script = ('wait var scene 2', 'frame 3 wait var Gold 0x10 max 99', 'wait var scene', 'wait var scene 2 max', 'wait var s_c 1 2',
                  'wait var scene zz', 'frame 9 key A down', 'wait var turn 1', 'frame 1 quit')
        r = self.run_script(chr(10).join(script) + chr(10))
        self.assertEqual(r['errors'], 4)
        self.assertEqual([(e[0], e[1], e[5]) for e in r['events']], [
            (0, WAIT_VAR, 'scene'), (3, WAIT_VAR, 'Gold'), (9, KEY, ''), (9, WAIT_VAR, 'turn'), (1, QUIT, '')])
        self.assertEqual([r['extra'][i] for i in (0, 1, 3)], [(0, 2), (99, 16), (0, 1)])

    def test_wait_bad_forms(self):
        r = self.run_script('wait\nwait file\nwait file a b\nwait disk x\nwait file x max\nwait file x max 70000\nwait input x\nsync now\n')
        self.assertEqual((r['count'], r['errors']), (0, 8))

    def test_barrier_is_a_sort_wall(self):
        # frames after a barrier are relative to it: events never sort in front of it, and the barrier itself goes behind all
        # lines before it, whatever their frames
        r = self.run_script('frame 50 key A down\nframe 10 key B down\nwait log x\nframe 5 key C down\nframe 2 key D down\nwait file y\n'
                            'frame 1 quit\n')
        self.assertEqual([(e[0], e[1], e[2]) for e in r['events']], [
            (10, KEY, 0x35), (50, KEY, 0x20), (50, WAIT_LOG, 0), (2, KEY, 0x22), (5, KEY, 0x33), (5, WAIT_FILE, 0), (1, QUIT, 0)])

    def test_poke(self):
        r = self.run_script('frame 5 poke knight_x 0x1F4\nframe 6 poke a_b 12\nframe 7 poke x 0xZZ\n')
        self.assertEqual(r['errors'], 1)
        self.assertEqual([(e[0], e[1], e[5]) for e in r['events']], [(5, POKE, 'knight_x'), (6, POKE, 'a_b')])
        self.assertEqual([x[1] for x in r['extra']], [500, 12])

    def test_checked_in_boot_scripts_parse_clean(self):
        # tests/boot/*.txt (tools/integrate.py --boot): no bad lines, fits the event table, ends with quit
        bootdir = os.path.join(ROOT, 'tests', 'boot')
        for f in sorted(os.listdir(bootdir)):
            if f.endswith('.txt'):
                r = self.run_script(open(os.path.join(bootdir, f)).read())
                self.assertEqual((r['errors'], r['overflow']), (0, 0), f)
                self.assertEqual(r['events'][-1][1], QUIT, f)
                if not f.startswith('mods_'):   # mods_*.txt (ROADMAP 9.4c) only wait for loader log lines
                    self.assertTrue(any(e[1] == JOY for e in r['events']), f)


if __name__ == '__main__':
    unittest.main()
