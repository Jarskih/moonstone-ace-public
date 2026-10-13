"""Host test of the pure half of the deterministic mode for network play (ROADMAP 12.4, include/engine/nettick.hpp).

  * KeyQueue: order, wrap-around, the full queue drops and counts, localFrameTakeKeys hands over at most kNetMaxKeys per tick and
    keeps the rest for the next one;
  * TickLatch: latchCommit stores the frame, advances the clock by the VBLs of the tick, counts ticks, derives the pad frame from
    the frame's joystick words and the key-down table;
  * cursorStep: against a Python transcription of the ORIGINAL per-VBL cursor hook (mog LAB_057D as src/rt/combat_ui.cpp had it
    before 12.4): random stick words and start positions, one VBL and several; the clamp between VBLs; the lock cell;
  * tickIdle: calls the installed hook, does nothing without one.

The checks are C++ (a driver compiled with clang++ against the real engine files) and print `OK <name>` / `FAIL <name>: <detail>`
or, for the cursor, one `CUR <bits> <vbls> <x> <y> <x2> <y2> <lock>` line per case that Python compares with its model."""
import os
import random
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/nettick.hpp"
using namespace ms;

static int g_fails;
static void check(bool b, const char *name, const char *detail) {
	if (b) printf("OK %s\n", name);
	else { printf("FAIL %s: %s\n", name, detail); ++g_fails; }
}

static int g_idle;
static void idleHook() { ++g_idle; }

int main(int argc, char **argv) {
	// ---- key queue
	{
		static KeyQueue q;
		keyQueueClear(q);
		uint8_t c;
		check(!keyQueuePop(q, c), "queue-empty", "pop on empty");
		for (int i = 0; i < 10; ++i) keyQueuePush(q, (uint8_t)(0x40 + i));
		bool ok = true;
		for (int i = 0; i < 10; ++i) ok = ok && keyQueuePop(q, c) && c == 0x40 + i;
		check(ok, "queue-order", "FIFO order");
		// wrap: push/pop 3 x size
		ok = true;
		for (int i = 0; i < 3 * kNetKeyQueueSize; ++i) {
			ok = ok && keyQueuePush(q, (uint8_t)i) && keyQueuePop(q, c) && c == (uint8_t)i;
		}
		check(ok, "queue-wrap", "wrap-around");
		// full: size-1 usable, the rest dropped and counted
		keyQueueClear(q);
		int pushed = 0;
		for (int i = 0; i < kNetKeyQueueSize + 5; ++i) pushed += keyQueuePush(q, 1) ? 1 : 0;
		check(pushed == kNetKeyQueueSize - 1 && q.uwDropped == 6, "queue-full", "capacity / dropped count");
		// take at most kNetMaxKeys per frame, the rest stays queued, in order
		keyQueueClear(q);
		for (int i = 0; i < 40; ++i) keyQueuePush(q, (uint8_t)i);
		LocalFrame f;
		localFrameClear(f);
		localFrameTakeKeys(f, q);
		ok = f.ubKeys == kNetMaxKeys;
		for (int i = 0; i < kNetMaxKeys; ++i) ok = ok && f.aubKey[i] == i;
		check(ok, "take-first", "first 16");
		localFrameTakeKeys(f, q);
		ok = f.ubKeys == kNetMaxKeys && f.aubKey[0] == 16;
		localFrameTakeKeys(f, q);
		ok = ok && f.ubKeys == 8 && f.aubKey[0] == 32 && f.aubKey[7] == 39;
		localFrameTakeKeys(f, q);
		ok = ok && f.ubKeys == 0;
		check(ok, "take-rest", "the rest waits for the next ticks");
	}
	// ---- latch
	{
		TickLatch t;
		latchReset(t, 1000);
		check(t.ulTick == 0 && t.ulClock == 1000 && t.sFrame.joy.uwPort1 == 0, "latch-reset", "reset values");
		uint8_t keys[128];
		memset(keys, 0, sizeof keys);
		keys[kPadKeysWasd.ubUp] = 1;      // keyboard set B: up
		keys[kPadKeysArrows.ubFire] = 1;  // keyboard set A: fire
		LocalFrame f;
		localFrameClear(f);
		f.joy.uwPort1 = kJoyLeft | kJoyFire;
		f.joy.uwPort0 = kJoyDown;
		latchCommit(t, f, keys, 1);
		check(t.ulTick == 1 && t.ulClock == 1001, "latch-commit-1", "tick / clock after one VBL");
		check(t.sFrame.joy.uwPort1 == (kJoyLeft | kJoyFire) && t.sFrame.joy.uwPort0 == kJoyDown, "latch-frame", "frame stored");
		check(padBits(t.sPads, PAD_JOY1) == (kJoyLeft | kJoyFire) && padBits(t.sPads, PAD_JOY0) == kJoyDown, "latch-pads-joy", "joystick pads");
		check(padBits(t.sPads, PAD_KEYS_WASD) == kJoyUp && padBits(t.sPads, PAD_KEYS_ARROWS) == kJoyFire, "latch-pads-keys", "keyboard pads from the table");
		localFrameClear(f);
		latchCommit(t, f, keys, 5);
		check(t.ulTick == 2 && t.ulClock == 1006 && t.sFrame.joy.uwPort1 == 0, "latch-commit-5", "a tick standing for 5 VBLs");
		// the clock wraps like the 32-bit VBL counter
		latchReset(t, 0xFFFFFFFFu);
		latchCommit(t, f, keys, 2);
		check(t.ulClock == 1, "latch-wrap", "32-bit wrap");
	}
	// ---- tickIdle
	{
		g_pfnTickIdle = nullptr;
		tickIdle();
		check(g_idle == 0, "idle-none", "no hook, no call");
		g_pfnTickIdle = idleHook;
		tickIdle(); tickIdle();
		check(g_idle == 2, "idle-hook", "hook called");
		g_pfnTickIdle = nullptr;
	}
	// ---- cursor: the cases come from argv (Python model compares)
	for (int a = 1; a + 4 < argc; a += 5) {
		CursorState s = {(uint16_t)atoi(argv[a + 2]), (uint16_t)atoi(argv[a + 3]), 7};
		cursorStep(s, (uint16_t)atoi(argv[a]), (uint32_t)atoi(argv[a + 1]));
		printf("CUR %s %s %s %s -> %u %u %u\n", argv[a], argv[a + 1], argv[a + 2], argv[a + 3], s.uwX, s.uwY, s.uwLock);
		(void)argv[a + 4];
	}
	printf(g_fails ? "FAILED\n" : "DONE\n");
	return g_fails ? 1 : 0;
}
'''


def model_vbl(x, y, bits):
    """mog LAB_057D as src/rt/combat_ui.cpp cursorTick had it before ROADMAP 12.4 (cells are 16-bit, compares signed)."""
    def s16(v):
        v &= 0xFFFF
        return v - 0x10000 if v & 0x8000 else v
    lock = 1
    if bits & 1: x = (x + 2) & 0xFFFF
    if bits & 2: x = (x - 2) & 0xFFFF
    if bits & 4: y = (y + 2) & 0xFFFF
    if bits & 8: y = (y - 2) & 0xFFFF
    if bits & 0x10: lock = 0
    if s16(x) >= 0x13B: x = 0x13A
    if s16(x) < 0: x = 0
    if s16(y) >= 0xC4: y = 0xC3
    if s16(y) < 0: y = 0
    return x, y, lock


def model(x, y, bits, vbls):
    lock = 7                         # the driver starts with 7: only a real step writes it
    for _ in range(vbls):
        x, y, lock = model_vbl(x, y, bits)
    return x, y, lock


@unittest.skipUnless(CXX, 'clang++ not found')
class NetTick(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='nettick_')
        src = os.path.join(cls.tmp, 'd.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'd.exe')
        r = subprocess.run([CXX, '-std=c++17', '-Wall', '-Werror', '-fno-exceptions', '-fno-rtti',
                            '-I', os.path.join(ROOT, 'include'), src,
                            os.path.join(ROOT, 'src', 'engine', 'nettick.cpp'),
                            os.path.join(ROOT, 'src', 'engine', 'pad.cpp'),
                            os.path.join(ROOT, 'src', 'engine', 'input.cpp'), '-o', cls.exe],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError('compile failed: ' + r.stderr[-3000:])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_driver(self, args):
        r = subprocess.run([self.exe] + [str(a) for a in args], capture_output=True, text=True)
        return r.stdout.split('\n')

    def test_checks(self):
        out = self.run_driver([])
        fails = [l for l in out if l.startswith('FAIL')]
        self.assertEqual(fails, [], fails)
        self.assertGreaterEqual(len([l for l in out if l.startswith('OK')]), 14)
        self.assertIn('DONE', out)

    def test_cursor_matches_the_original_hook(self):
        rnd = random.Random(1204)
        cases = []
        for _ in range(400):
            bits = rnd.choice([0, 1, 2, 4, 8, 3, 12, 5, 6, 9, 10, 0x10, 0x15, 0x1F, rnd.randrange(32)])
            vbls = rnd.choice([1, 1, 1, 2, 3, 6])
            x = rnd.choice([0, 1, 0x138, 0x139, 0x13A, rnd.randrange(0x13B)])
            y = rnd.choice([0, 1, 0xC1, 0xC2, 0xC3, rnd.randrange(0xC4)])
            cases.append((bits, vbls, x, y))
        args = []
        for c in cases:
            args += list(c) + [0]
        out = [l for l in self.run_driver(args) if l.startswith('CUR')]
        self.assertEqual(len(out), len(cases))
        for c, line in zip(cases, out):
            got = tuple(int(v) for v in line.split('->')[1].split())
            self.assertEqual(got, model(c[2], c[3], c[0], c[1]), (c, line))

    def test_cursor_vbl_multiple_is_not_a_single_big_step(self):
        # at the right edge, right+left in one VBL ends where it started, and 6 VBLs equal six single steps
        self.assertEqual(model(0x13A, 5, 3, 6), (0x13A, 5, 1))
        out = self.run_driver([3, 6, 0x13A, 5, 0])
        self.assertIn('-> 314 5 1', '\n'.join(out))


if __name__ == '__main__':
    unittest.main()
