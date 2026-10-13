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
        printf("E %u %u %u %u %u %u %u %u %s\n", (unsigned)ev[i].ulFrame, ev[i].ubKind, ev[i].ubCode, ev[i].isDown, ev[i].ubBits,
               (unsigned)ev[i].uwMax, (unsigned)ev[i].ulValue, (unsigned)ev[i].ulTick, ev[i].szText);
    }
    printf("K %d %d %d %d\n", ms::apKeyCode("SPACE"), ms::apKeyCode("a"), ms::apKeyCode("f10"), ms::apKeyCode("bogus"));
    return 0;
}
'''

KEY, JOY, SHOT, LOG, QUIT, WAIT_FILE, WAIT_LOG, WAIT_INPUT, SYNC, POKE, MASH, WAIT_VAR, TICK_POKE = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
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
        ticks = []
        for line in out[1:-1]:
            p = line.split(' ', 9)
            extra.append((int(p[6]), int(p[7])))   # (uwMax, ulValue) per event
            ticks.append(int(p[8]))                # ulTick (AP_TICK_POKE)
            events.append((int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]), p[9] if len(p) > 9 else ''))
        keys = tuple(map(int, out[-1].split()[1:]))
        return dict(count=count, errors=errors, first=first, overflow=overflow, events=events, keys=keys, extra=extra, ticks=ticks)

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

    def test_tick_poke_forms(self):
        # `tick N|+K|hash+K poke NAME VALUE`: no "frame" prefix needed (N = 0 after the previous barrier), an explicit one is accepted
        r = self.run_script('wait log hash_tick\ntick hash+30 poke who 1\ntick 7000 poke moonstones 0xF\nTICK +5 poke gold 9\nframe 4 tick 8 poke who 0\n')
        self.assertEqual(r['errors'], 0)
        self.assertEqual([(e[0], e[1], e[2], e[5]) for e in r['events']],
                         [(0, WAIT_LOG, 0, 'hash_tick'), (0, TICK_POKE, 2, 'who'), (0, TICK_POKE, 0, 'moonstones'), (0, TICK_POKE, 1, 'gold'), (4, TICK_POKE, 0, 'who')])
        self.assertEqual(r['ticks'][1:], [30, 7000, 5, 8])
        self.assertEqual([x[1] for x in r['extra']][1:], [1, 15, 9, 0])

    def test_tick_poke_bad_forms(self):
        r = self.run_script('tick poke who 1\ntick 5 who 1\ntick x poke who 1\ntick hash+ poke who 1\ntick +x poke who 1\ntick 5 poke who\n'
                            'tick 5 poke who 0xZZ\ntick 5 poke bad/name 1\ntick -3 poke who 1\ntick 5 poke who 1 extra\n')
        self.assertEqual((r['count'], r['errors']), (0, 10))

    def test_tick_poke_stays_in_its_segment(self):
        # a tick-poke is queued where the script is: it does not sort in front of the barrier before it
        r = self.run_script('frame 50 key A down\nwait log x\ntick 9 poke who 1\nframe 3 key B down\n')
        self.assertEqual([e[1] for e in r['events']], [KEY, WAIT_LOG, TICK_POKE, KEY])

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


QUEUE_DRIVER = r'''
// The tick-poke queue (engine/tickpoke): commands on stdin, one result line each.
//   resolve MODE ARG NOW HASH      push NAME TICK NOW VALUE      take NOW      hash "<log line>"
#include <stdio.h>
#include <string.h>
#include "engine/tickpoke.hpp"
int main() {
    static ms::TickPokeQueue q;
    ms::tickPokeClear(q);
    char cmd[16], a[96];
    unsigned long x, y, z, w;
    while(scanf("%15s", cmd) == 1) {
        if(!strcmp(cmd, "resolve")) {
            scanf("%lu %lu %lu %lx", &x, &y, &z, &w);
            uint32_t t = 0;
            bool ok = ms::tickPokeResolve((uint8_t)x, (uint32_t)y, (uint32_t)z, (uint32_t)w, t);
            printf("%d %u\n", ok, (unsigned)t);
        } else if(!strcmp(cmd, "push")) {
            scanf("%95s %lu %lu %lu", a, &x, &y, &z);
            printf("%d\n", (int)ms::tickPokePush(q, (uint32_t)x, (uint32_t)y, a, (uint32_t)z));
        } else if(!strcmp(cmd, "take")) {
            scanf("%lu", &x);
            ms::TickPoke p;
            int r = (int)ms::tickPokeTake(q, (uint32_t)x, p);
            if(r) printf("%d %s %u %u\n", r, p.szName, (unsigned)p.ulValue, (unsigned)p.ulTick); else printf("0\n");
        } else if(!strcmp(cmd, "floor")) {
            scanf("%lu %lu", &x, &y);
            printf("%u\n", (unsigned)ms::tickPokeHashFloor((uint32_t)x, (uint32_t)y));
        } else if(!strcmp(cmd, "hash")) {
            fgets(a, sizeof a, stdin);
            uint32_t t = 0;
            bool ok = ms::tickPokeHashLine(a, t);
            printf("%d %u\n", ok, (unsigned)t);
        }
    }
    return 0;
}
'''


@unittest.skipUnless(CXX, 'needs clang++')
class TickPokeQueueTest(unittest.TestCase):
    """The queue behind `tick N poke ...` (engine/tickpoke): order, late detection, resolution of the three tick forms, the hash line."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        src = os.path.join(cls.tmp, 'q.cpp')
        with open(src, 'w') as f:
            f.write(QUEUE_DRIVER)
        cls.exe = os.path.join(cls.tmp, 'q.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-result', '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), src,
                            os.path.join(ROOT, 'src', 'engine', 'tickpoke.cpp'), '-o', cls.exe], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_cmds(self, *cmds):
        out = subprocess.run([self.exe], input=(chr(10).join(cmds) + chr(10)).encode(), capture_output=True, check=True).stdout.decode().splitlines()
        self.assertEqual(len(out), len(cmds))
        return out

    def test_due_in_tick_then_script_order(self):
        out = self.run_cmds('push who 100 90 1', 'push moonstones 100 90 15', 'push gold 99 90 7', 'push mapx 101 90 2',
                            'take 98', 'take 99', 'take 100', 'take 100', 'take 100', 'take 100', 'take 101')
        self.assertEqual(out[:4], ['0'] * 4)   # all four queued
        # nothing before tick 99; gold (99); who then moonstones (tick 100, in push order); nothing more at 100; mapx (101)
        self.assertEqual(out[4:], ['0', '1 gold 7 99', '1 who 1 100', '1 moonstones 15 100', '0', '0', '1 mapx 2 101'])

    def test_late_push_is_refused_and_late_take_is_reported(self):
        self.assertEqual(self.run_cmds('push who 99 100 1', 'take 200'), ['1', '0'])   # tick 99 when the game is on 100: LATE, nothing queued
        # a poke queued for tick 100 while the game is on tick 100, taken at 101 (the tick began before the push landed): reported late, not applied
        self.assertEqual(self.run_cmds('push who 100 100 1', 'take 101', 'take 101'), ['0', '2 who 1 100', '0'])

    def test_queue_full(self):
        out = self.run_cmds(*['push p 5 0 %d' % i for i in range(17)])
        self.assertEqual(out, ['0'] * 16 + ['2'])

    def test_resolve(self):
        none = 'ffffffff'
        self.assertEqual(self.run_cmds('resolve 0 7000 123 ' + none), ['1 7000'])            # absolute
        self.assertEqual(self.run_cmds('resolve 1 5 123 ' + none), ['1 128'])               # +K from the tick the game is on
        self.assertEqual(self.run_cmds('resolve 1 0 123 ' + none), ['0 0'])                 # +0 is refused
        self.assertEqual(self.run_cmds('resolve 2 30 123 %x' % 3050), ['1 3080'])           # hash+K from the barrier's hash tick, whatever the game's tick
        self.assertEqual(self.run_cmds('resolve 2 30 123 ' + none), ['0 0'])                # no hash barrier consumed yet
        self.assertEqual(self.run_cmds('resolve 9 1 1 1'), ['0 0'])

    def test_hash_floor_is_the_first_hash_tick_after_the_variable_tick(self):
        # a `sync` behind a `wait var`: windows that noticed the variable at 6199 and 6201 still pick the same line (the tick the variable held at is the same)
        self.assertEqual(self.run_cmds('floor 6199 50', 'floor 6200 50', 'floor 6201 50', 'floor 0 50'), ['6200', '6250', '6250', '50'])

    def test_hash_line(self):
        self.assertEqual(self.run_cmds('hash net: hash tick 3050 crc 1A2B3C4D'), ['1 3050'])
        self.assertEqual(self.run_cmds('hash net: hash tick 50 crc 00000000'), ['1 50'])
        self.assertEqual(self.run_cmds('hash net: waiting tick 3050, missing nodes mask 2'), ['0 0'])
        self.assertEqual(self.run_cmds('hash net: hash tick crc'), ['0 0'])


HITS_DRIVER = r"""
// The hits the barriers consume (engine/tickpoke ApHitLog): commands on stdin, one result line each.
//   clear   hash TICK PAT   line PAT   take PAT   sync   var TICK|-   other   timeout   base   poke ARG NOW
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/tickpoke.hpp"
static const uint32_t kInterval = 50;
static void printTick(uint32_t t) {
    if(t == ms::kTickPokeNone) printf("none\n"); else printf("%u\n", (unsigned)t);
}
int main() {
    static ms::ApHitLog l;
    static ms::TickPokeQueue q;
    ms::apHitsClear(l);
    ms::tickPokeClear(q);
    char cmd[16], a[16];
    unsigned long x, y;
    while(scanf("%15s", cmd) == 1) {
        if(!strcmp(cmd, "clear")) {
            ms::apHitsClear(l);
            ms::tickPokeClear(q);
            printf("ok\n");
        } else if(!strcmp(cmd, "hash")) {
            scanf("%lu %lu", &x, &y);
            ms::apHitsNote(l, (uint8_t)y, (uint32_t)x);
            printf("ok\n");
        } else if(!strcmp(cmd, "line")) {
            scanf("%lu", &x);
            ms::apHitsNote(l, (uint8_t)x, ms::kTickPokeNone);
            printf("ok\n");
        } else if(!strcmp(cmd, "take")) {
            scanf("%lu", &x);
            if(ms::apHitsTake(l, (uint8_t)x)) { printf("1 "); printTick(ms::apHitsBase(l)); } else printf("0\n");
        } else if(!strcmp(cmd, "sync")) {
            ms::apHitsSync(l, kInterval);
            printf("ok\n");
        } else if(!strcmp(cmd, "var")) {
            scanf("%15s", a);
            ms::apHitsVarPassed(l, a[0] == '-' ? ms::kTickPokeNone : (uint32_t)strtoul(a, 0, 10));
            printf("ok\n");
        } else if(!strcmp(cmd, "other")) {
            ms::apHitsOtherBarrier(l);
            printf("ok\n");
        } else if(!strcmp(cmd, "timeout")) {
            ms::apHitsTimedOut(l);
            printf("ok\n");
        } else if(!strcmp(cmd, "base")) {
            printTick(ms::apHitsBase(l));
        } else if(!strcmp(cmd, "poke")) {   // `tick hash+ARG poke` reached while the game is on tick NOW: what rt/autoplay queueTickPoke does
            scanf("%lu %lu", &x, &y);
            uint32_t t = 0;
            if(!ms::tickPokeResolve(ms::TPM_HASH, (uint32_t)x, (uint32_t)y, ms::apHitsBase(l), t)) { printf("no-tick\n"); continue; }
            const ms::TickPokePush e = ms::tickPokePush(q, t, (uint32_t)y, "who", 1);
            printf("%s %u\n", e == ms::TPP_OK ? "ok" : (e == ms::TPP_LATE ? "late" : "full"), (unsigned)t);
        }
    }
    return 0;
}
"""


@unittest.skipUnless(CXX, 'needs clang++')
class BarrierHitsTest(unittest.TestCase):
    """The hit log behind `wait log` / `sync` (engine/tickpoke ApHitLog): which hash line a barrier takes on each window, and what `tick hash+K` is based on.
    The two windows of tests/boot/net_classic_*.txt (build/rejoin2/batch4_classic.log) are replayed with the timings that went wrong."""

    HASH, BOX = 0, 1

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        src = os.path.join(cls.tmp, 'h.cpp')
        with open(src, 'w') as f:
            f.write(HITS_DRIVER)
        cls.exe = os.path.join(cls.tmp, 'h.exe')
        r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-result', '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), src,
                            os.path.join(ROOT, 'src', 'engine', 'tickpoke.cpp'), '-o', cls.exe], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_cmds(self, cmds):
        out = subprocess.run([self.exe], input=(chr(10).join(cmds) + chr(10)).encode(), capture_output=True, check=True).stdout.decode().splitlines()
        self.assertEqual(len(out), len(cmds))
        return out

    def hashes(self, first, last):
        return ['hash %d %d' % (t, self.HASH) for t in range(first, last + 1, 50)]

    def test_plain_waits_take_the_oldest_hash_line_and_sync_forgets(self):
        out = self.run_cmds(self.hashes(50, 150) + ['take 0', 'take 0', 'take 0', 'take 0', 'sync', 'take 0', 'hash 200 0', 'take 0', 'base'])
        self.assertEqual(out[3:], ['1 50', '1 100', '1 150', '0', 'ok', '0', 'ok', '1 200', '200'])

    def test_floor_line_is_exact_when_the_wait_var_tick_is_known(self):
        # `wait var turn 0` held at tick 1006, `sync` behind it, then the hash wait: line 1050 on every window, printed before or after the sync
        out = self.run_cmds(self.hashes(50, 1000) + ['var 1006', 'sync', 'take 0', 'hash 1050 0', 'take 0'])
        self.assertEqual(out[-3:], ['0', 'ok', '1 1050'])
        # a window whose script (VBL) was a whole bar late: lines 1050 and 1100 are both there already, it still takes 1050 and not the newest
        late = self.run_cmds(self.hashes(50, 1100) + ['var 1006', 'sync', 'take 0'])
        self.assertEqual(late[-1], '1 1050')

    def test_floor_line_survives_an_overwritten_ring(self):
        # the window waited long in the `wait var` (hundreds of ticks of other log lines): the ring of 64 hits no longer holds line 1050 - it must still count as 1050
        flood = ['line %d' % self.BOX for _ in range(70)]
        out = self.run_cmds(self.hashes(50, 1000) + flood + ['var 1006', 'sync', 'take 0', 'hash 1050 0', 'take 0', 'base'])
        self.assertEqual(out[-4:], ['0', 'ok', '1 1050', '1050'])
        # ... and when line 1050 itself fell out of the ring because many lines followed it, the barrier is released at once and still takes 1050
        out = self.run_cmds(self.hashes(50, 1050) + self.hashes(1100, 1200) * 2 + flood + ['var 1006', 'sync', 'take 0'])
        self.assertEqual(out[-1], '1 1050')

    def test_floor_is_one_shot(self):
        out = self.run_cmds(self.hashes(50, 1100) + ['var 1006', 'sync', 'take 0', 'hash 1150 0', 'take 0'])
        self.assertEqual((out[-3], out[-1]), ('1 1050', '1 1100'))   # a second wait without a sync takes the next line, not the floor again
        out = self.run_cmds(self.hashes(50, 1100) + ['var 1006', 'sync', 'sync', 'hash 1150 0', 'take 0'])
        self.assertEqual(out[-1], '1 1150')   # the second sync is not behind a wait var any more: it forgets everything

    def test_a_sync_after_another_barrier_is_not_behind_the_variable(self):
        # wait var; wait log box_waiting; sync : the sync is plain (the stale floor of the classic run, lair pokes anchored to tick 1350)
        out = self.run_cmds(self.hashes(50, 1100) + ['var 1006', 'other', 'sync', 'take 0', 'hash 1150 0', 'take 0'])
        self.assertEqual((out[-3], out[-1]), ('0', '1 1150'))

    def test_classic_host_window(self):
        # the host of batch4_classic.log: ... wait var turn 0 held at 1006 -> sync -> hash waits 1050..1350 (each behind a sync) -> `wait var turn 1 max 6000` TIMED OUT
        # (the game never reached turn 1) -> sync -> `wait log hash_tick max 3000` TIMED OUT (the session had ended in a desync) -> tick hash+10 pokes.
        cmds = self.hashes(50, 1000) + ['var 1006', 'sync']
        want = []
        for t in range(1050, 1351, 50):
            cmds += ['hash %d 0' % t, 'take 0', 'sync']
            want.append('1 %d' % t)
        cmds += ['base', 'timeout', 'sync', 'timeout', 'base', 'poke 10 9202']
        out = self.run_cmds(cmds)
        self.assertEqual(out[-6], '1350')
        self.assertEqual(out[-2:], ['none', 'no-tick'])   # the old code kept 1350 as the base, queued the poke for tick 1360 while the game was on 9202 and called it "late"
        self.assertEqual([o for o in out if o.startswith('1 ')], want)

    def test_classic_join_window_takes_the_fresh_line_after_a_plain_sync(self):
        cmds = self.hashes(50, 6300) + ['timeout', 'sync', 'hash 6350 0', 'take 0', 'poke 10 6351', 'poke 10 6361']
        out = self.run_cmds(cmds)
        self.assertEqual(out[-3:], ['1 6350', 'ok 6360', 'late 6360'])   # queued for 6360; a second line reached after tick 6360 began is late, not applied silently

    def test_a_timeout_or_a_var_wait_drops_the_base_a_sync_keeps_it(self):
        out = self.run_cmds(self.hashes(50, 100) + ['take 0', 'sync', 'base', 'var -', 'base'])
        self.assertEqual(out[2:], ['1 50', 'ok', '50', 'ok', 'none'])
        out = self.run_cmds(self.hashes(50, 100) + ['take 0', 'timeout', 'base', 'take 0', 'base'])
        self.assertEqual(out[2:], ['1 50', 'ok', 'none', '1 100', '100'])

    def test_a_timed_out_wait_var_leaves_no_tick_for_the_sync_behind_it(self):
        # `wait var A` held at 1006, `wait var B` (no sync between) timed out: the sync behind B is a plain one, not anchored to the tick of A
        out = self.run_cmds(self.hashes(50, 1100) + ['var 1006', 'timeout', 'sync', 'take 0', 'hash 1150 0', 'take 0'])
        self.assertEqual((out[-3], out[-1]), ('0', '1 1150'))

    def test_a_wait_on_another_line_leaves_no_hash_base(self):
        out = self.run_cmds(self.hashes(50, 100) + ['take 0', 'line 1', 'take 1', 'base'])
        self.assertEqual(out[-2:], ['1 none', 'none'])

    def test_a_timed_out_wait_var_leaves_the_windows_a_hash_bar_apart(self):
        # batch5_classic.log: `wait var turn 1 max 6000` timed out on both windows (the Next Day screen was never dismissed), at the same *frame* count but not
        # at the same tick: the host's script took hash 6400 behind the sync, the join's 6450, and every `tick hash+10` landed 50 ticks apart (6410 / 6460).
        # A timeout is frame-based, so no passing run may contain one: netpair.py counts `wait-timeout` as BAD and the script waits for the Next Day screen instead.
        host = self.run_cmds(self.hashes(50, 6350) + ['timeout', 'sync', 'hash 6400 0', 'take 0', 'base'])
        join = self.run_cmds(self.hashes(50, 6400) + ['timeout', 'sync', 'hash 6450 0', 'take 0', 'base'])
        self.assertEqual(host[-2:], ['1 6400', '6400'])
        self.assertEqual(join[-2:], ['1 6450', '6450'])

    def test_both_windows_agree_whatever_their_timing(self):
        # window A noticed the variable early, B a bar later, with more hits in its ring: the same base at the same barrier
        a = self.run_cmds(self.hashes(50, 1000) + ['var 1006', 'sync', 'hash 1050 0', 'take 0', 'base'])
        b = self.run_cmds(self.hashes(50, 1200) + ['line 1'] * 40 + ['var 1006', 'sync', 'take 0', 'base'])
        self.assertEqual(a[-2:], ['1 1050', '1050'])
        self.assertEqual(b[-2:], ['1 1050', '1050'])


if __name__ == '__main__':
    unittest.main()
