"""Host test of the network clock logic (ROADMAP 12.8b, include/engine/netclock.hpp).

  * netLineAccumStep: forward steps of the beam line, a wrap of the frame counts the wrap, the first read is the origin;
  * netLinesToMs / netMsToLines: no overflow for large line counts, agree with 64 us per line;
  * netClockRead: tracks simulated real time within a frame whether the VBL count lags the beam line (the interrupt latency near the
    frame wrap, the case where TOD * 312 + line went backwards), leads it, or misses VBLs entirely (a lean window); never goes back;
    the start is kNetClockOriginMs; the VBL counter may wrap through 2^32;
  * a long gap (several frames) between two reads is covered by the VBL count, a stalled VBL count (OS owns the interrupt) by the beam."""
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <stdint.h>
#include "engine/netclock.hpp"
using namespace ms;

static int g_fails;
static void check(bool b, const char *name, const char *detail) {
	if (b) printf("OK %s\n", name);
	else { printf("FAIL %s: %s\n", name, detail); ++g_fails; }
}
static uint32_t g_rng = 12345;
static uint32_t rnd(uint32_t n) { g_rng = g_rng * 1664525u + 1013904223u; return (g_rng >> 8) % n; }

// Simulated machine: time in lines since power-on; the beam line is time % 312; the VBL counter counts frames, incremented
// 'lat' lines after the wrap (interrupt latency), and may be frozen (window).
struct Sim { uint64_t t; uint32_t vbl0; };

int main() {
	{
		NetLineAccum a;
		netLineAccumInit(a, 100);
		bool ok = netLineAccumStep(a, 100) == 0 && netLineAccumStep(a, 150) == 50 && netLineAccumStep(a, 5) == 50 + 167 &&
		          netLineAccumStep(a, 5) == 217;
		check(ok, "accum-wrap", "forward steps and the frame wrap");
		NetLineAccum b = {0, 0, false};
		check(netLineAccumStep(b, 200) == 0 && netLineAccumStep(b, 201) == 1, "accum-first", "first read is the origin");
	}
	{
		check(netLinesToMs(0) == 0 && netLinesToMs(125) == 8 && netLinesToMs(15625) == 1000, "lines-ms", "64 us per line");
		check(netLinesToMs(0xFFFFFFFFu) == 0xFFFFFFFFu / 125 * 8 + (0xFFFFFFFFu % 125) * 8 / 125, "lines-ms-big", "no overflow");
		check(netMsToLines(2) == 31 || netMsToLines(2) == 32, "ms-lines", "2 ms is about 31 lines");
	}
	// tracking: polls every 1..300 lines, VBL latency 0..20 lines after the wrap
	for (int mode = 0; mode < 4; ++mode) {
		NetClockState s = {};
		uint64_t t = 777;
		uint32_t vbl0 = mode == 3 ? 0xFFFFFF00u : 5;
		const uint32_t lat = mode == 1 ? 20 : 0;
		uint32_t prev = 0;
		uint64_t t0 = 0;
		bool mono = true, close = true, first = true;
		int32_t worst = 0;
		for (int i = 0; i < 20000; ++i) {
			t += 1 + rnd(300);
			uint64_t tv = t >= lat ? t - lat : 0;
			uint32_t vbl = vbl0 + (uint32_t)(tv / 312);
			uint32_t line = (uint32_t)(t % 312);
			if (mode == 2) vbl = vbl0;   // the VBL belongs to the OS: frozen
			uint32_t ms = netClockRead(s, vbl, line);
			if (first) { first = false; t0 = t; if (ms != kNetClockOriginMs) mono = false; prev = ms; continue; }
			if ((int32_t)(ms - prev) < 0) mono = false;
			prev = ms;
			int32_t real = (int32_t)((t - t0) * 64 / 1000);   // microseconds / 1000
			int32_t d = (int32_t)(ms - kNetClockOriginMs) - real;
			if (d < 0) d = -d;
			if (d > worst) worst = d;
			// the beam alone is exact while polls are < 1 frame apart, the VBL alone is within a frame: both fine
			if (d > 25 + real / 200) close = false;   // 20 ms per VBL vs 312 lines: 0.16% fast
		}
		char buf[96];
		snprintf(buf, sizeof(buf), "mode %d worst error %d ms", mode, worst);
		check(mono, mode == 0 ? "clock-monotonic-0" : mode == 1 ? "clock-monotonic-1" : mode == 2 ? "clock-monotonic-2" : "clock-monotonic-3", buf);
		check(close, mode == 0 ? "clock-tracks-0" : mode == 1 ? "clock-tracks-1" : mode == 2 ? "clock-tracks-2" : "clock-tracks-3", buf);
	}
	{   // gap of many frames between two reads: the VBL count carries it
		NetClockState s = {};
		netClockRead(s, 10, 50);
		uint32_t ms = netClockRead(s, 10 + 500, 50);
		check(ms == kNetClockOriginMs + 500 * 20, "long-gap", "500 frames = 10 s");
	}
	{   // the old failure: line small, VBL counter not yet incremented -> must not go back
		NetClockState s = {};
		netClockRead(s, 100, 300);
		uint32_t a = netClockRead(s, 100, 311);
		uint32_t b = netClockRead(s, 100, 3);     // wrapped, VBL late
		uint32_t c = netClockRead(s, 101, 9);
		check(a <= b && b <= c, "vbl-late", "no step back at the frame wrap");
	}
	printf(g_fails ? "FAILED\n" : "DONE\n");
	return g_fails;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class NetClock(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='netclock_')
        src = os.path.join(cls.tmp, 'd.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.tmp, 'd.exe')
        r = subprocess.run([CXX, '-std=c++17', '-Wall', '-Werror', '-fno-exceptions', '-fno-rtti',
                            '-I', os.path.join(ROOT, 'include'), src, '-o', cls.exe],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError('compile failed: ' + r.stderr[-3000:])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_checks(self):
        r = subprocess.run([self.exe], capture_output=True, text=True)
        out = r.stdout.split('\n')
        fails = [l for l in out if l.startswith('FAIL')]
        self.assertEqual(fails, [], fails)
        self.assertGreaterEqual(len([l for l in out if l.startswith('OK')]), 15)
        self.assertIn('DONE', out)


if __name__ == '__main__':
    unittest.main()
