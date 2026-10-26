"""PC audio quality: the band-limited (BLEP) renderer of the Paula model, the A500 / A1200 output filters and the audio settings file.

The BLEP renderer (src/engine/paula.cpp RENDER_BLEP, kernel in src/engine/paulablep.cpp) is checked against an independent model: the
Python Paula of tests/test_paula.py gives every channel's exact chip-time timeline (segments of constant byte, the volume per frame); from it
the model below builds the staircase of each channel, takes its steps, and sums step * S(x) where S is the band-limited unit step computed
here from the kernel's DEFINITION (a half-band sinc times a Kaiser window, beta 8, 16 samples each side, integrated).  It shares no code with
the C++ (which keeps a ring of residuals and a 1024-phase table).  Compared sample for sample, 1 LSB tolerance.

  * the interrupts (frame, channel) of a scenario are IDENTICAL in box and BLEP mode: the game hears the same Paula;
  * DC is exact once a step has passed, silence stays silence, the gain of a tone equals the box renderer's;
  * a sine on a channel: the aliased image of the staircase (28 kHz folded to 19.8 kHz) is -42 dB with the box and below -110 dB with BLEP;
  * mutations of the BLEP code (delay, sign, interpolation, ...) are caught;
  * the output filters against a Python model of the same equations; the settings file (render / separation / filter) and the command line.
"""
import bisect
import math
import os
import random
import subprocess
import tempfile
import unittest

from tests import test_paula as tp

KERNEL_HALF = 16
BETA = 8.0
GRID = 2048


def bessel_i0(x):
    s = term = 1.0
    for k in range(1, 60):
        term *= (x / (2.0 * k)) ** 2
        s += term
    return s


def kernel(x):
    u = x / KERNEL_HALF
    if abs(u) >= 1.0:
        return 0.0
    sinc = 1.0 if x == 0 else math.sin(math.pi * x) / (math.pi * x)
    return sinc * bessel_i0(BETA * math.sqrt(1.0 - u * u)) / bessel_i0(BETA)


_STEP = []


def step_response(x):
    """S(x): the band-limited unit step (running integral of the unit-sum kernel), linear interpolation on a fine grid."""
    if not _STEP:
        acc, prev = 0.0, kernel(-KERNEL_HALF)
        _STEP.append(0.0)
        for i in range(1, 2 * KERNEL_HALF * GRID + 1):
            cur = kernel(-KERNEL_HALF + i / GRID)
            acc += 0.5 * (prev + cur) / GRID
            _STEP.append(acc)
            prev = cur
        total = _STEP[-1]
        _STEP[:] = [v / total for v in _STEP]
    if x <= -KERNEL_HALF:
        return 0.0
    if x >= KERNEL_HALF:
        return 1.0
    pos = (x + KERNEL_HALF) * GRID
    i = int(pos)
    f = pos - i
    return _STEP[i] * (1.0 - f) + _STEP[i + 1] * f


def channel_steps(model, c, nframes):
    """[(time in clock units, level change)] of channel c: the level is the held byte times the volume of the frame."""
    K = tp.CLOCK
    pieces = []
    for (a, b, val) in model.segs[c]:
        f = a // K
        while f * K < b and f < nframes:
            lo, hi = max(a, f * K), min(b, (f + 1) * K)
            pieces.append((lo, hi, val * model.vols[c][f]))
            f += 1
    steps, level = [], 0
    for (lo, hi, lv) in sorted(pieces):
        if lv != level:
            steps.append((lo, lv - level))
            level = lv
    return steps


def py_blep(scn):
    """The BLEP output of a scenario (list of (left, right)), one entry per rendered frame: output frame f is the sample of frame f - 16."""
    m = tp.PyPaula(scn.rate, scn.sep, 0, scn.mem, scn.origin, scn.size, scn.actions)
    m.run(scn.n, scn.events)
    K = tp.CLOCK
    own, other = (100 + scn.sep) / 100.0, (100 - scn.sep) / 100.0
    side_steps = ([], [])      # per speaker: (time, delta) with the speaker weights applied, for the left (0) and right (1) output
    for c in range(4):
        left = c in (0, 3)
        for (t, d) in channel_steps(m, c, scn.n):
            side_steps[0].append((t, d * (own if left else other)))
            side_steps[1].append((t, d * (other if left else own)))
    tabs = []
    for steps in side_steps:
        steps.sort()
        times = [t for t, _ in steps]
        prefix = [0.0]
        for _, d in steps:
            prefix.append(prefix[-1] + d)
        tabs.append((steps, times, prefix))
    out = []
    for f in range(scn.n):
        T = (f - KERNEL_HALF + 1) * K          # the sample instant of output frame f: the end of frame f - 16
        pair = []
        for (steps, times, prefix) in tabs:
            lo = bisect.bisect_right(times, T - KERNEL_HALF * K)          # steps this far back are complete: S = 1
            hi = bisect.bisect_left(times, T + KERNEL_HALF * K)           # steps this far ahead have not started: S = 0
            y = prefix[lo]
            for i in range(lo, hi):
                y += steps[i][1] * step_response((T - steps[i][0]) / K)
            pair.append(max(-32768, min(32767, math.floor(y + 0.5))))
        out.append(tuple(pair))
    return out


def blep(scn):
    """A copy of the scenario rendered by the band-limited steps (no low-pass: the Python side has none for BLEP)."""
    s = tp.Scn(scn.n, scn.mem, scn.origin, scn.rate, scn.sep, 0, scn.size, blep=True)
    s.events, s.actions = scn.events, scn.actions
    return s


def box(scn):
    s = tp.Scn(scn.n, scn.mem, scn.origin, scn.rate, scn.sep, 0, scn.size, blep=False)
    s.events, s.actions = scn.events, scn.actions
    return s


def goertzel_db(samples, freq, rate=48000):
    """Power of a Hann-windowed block at freq, in dB (arbitrary reference: only differences are used)."""
    n = len(samples)
    w = 2.0 * math.pi * freq / rate
    re = im = 0.0
    for i, x in enumerate(samples):
        win = 0.5 - 0.5 * math.cos(2.0 * math.pi * i / n)
        re += x * win * math.cos(w * i)
        im -= x * win * math.sin(w * i)
    return 10.0 * math.log10((re * re + im * im) / (n * n) + 1e-30)


class BlepRenderer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = tp.build_driver(cls.tmp.name, 'base')
        cls.cases = [tp.rnd_scenario(random.Random(2610 + i), n=400) for i in range(40)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_interrupts_are_identical_in_box_and_blep_mode(self):
        box_run = tp.run_scenarios_full(self.exe, [box(s) for s in self.cases])
        blep_run = tp.run_scenarios_full(self.exe, [blep(s) for s in self.cases])
        total = 0
        for i, (a, b) in enumerate(zip(box_run, blep_run)):
            self.assertEqual(a[0], b[0], 'interrupts of scenario %d' % i)
            self.assertEqual(a[2], b[2])
            total += len(a[0])
        self.assertGreater(total, 100, 'the scenarios must raise interrupts')

    def test_random_scenarios_match_the_independent_model(self):
        got = tp.run_scenarios_full(self.exe, [blep(s) for s in self.cases[:14]])
        for i, (s, g) in enumerate(zip(self.cases, got)):
            want = py_blep(blep(s))
            worst = max((max(abs(a - c), abs(b - d)) for (a, b), (c, d) in zip(g[1], want)), default=0)
            self.assertLessEqual(worst, 1, 'scenario %d: largest difference %d' % (i, worst))
        self.assertTrue(any(any(l or r for l, r in g[1]) for g in got))

    def test_dc_is_exact_after_the_step_has_passed_and_silence_stays_silent(self):
        for ch, side in ((0, 0), (1, 1), (2, 1), (3, 0)):
            s = blep(tp.dc_scn(ch, n=60))
            (irq, smp), = tp.run_scenarios(self.exe, [s])
            self.assertEqual(set(smp[48:]), {(8192, 0)} if side == 0 else {(0, 8192)}, 'channel %d' % ch)
        s = blep(tp.Scn(80, bytes(16), 0x1000))
        (irq, smp), = tp.run_scenarios(self.exe, [s])
        self.assertEqual(set(smp), {(0, 0)})

    def test_a_step_is_the_band_limited_step(self):
        """The step of a DC channel (at the start of frame 0, the instant of sample 15): exactly half of it there, a small Gibbs overshoot after
        it, ringing that dies away, and the held level exactly from 16 frames on."""
        (irq, smp), = tp.run_scenarios(self.exe, [blep(tp.dc_scn(0, n=60))])
        left = [l for l, _ in smp]
        self.assertEqual(left[15], 4096)
        self.assertTrue(all(abs(v) < 800 for v in left[:15]), left[:15])
        self.assertTrue(8192 < max(left[16:31]) < 8192 * 1.15, max(left[16:31]))
        self.assertEqual(left[31:], [8192] * 29)

    def test_tone_gain_equals_the_box_renderers(self):
        mem = bytes(int(round(100 * math.sin(2 * math.pi * i / 64))) & 0xFF for i in range(64))
        scn = tp.Scn(6000, mem, 0x1000)
        scn.chan(0, 0x1000, 32, 250, 64)
        scn.w(0, tp.REG_DMACON, 0x8001)
        (_, a), (_, b) = tp.run_scenarios(self.exe, [box(scn), blep(scn)])
        rms = lambda v: math.sqrt(sum(x * x for x in v) / len(v))
        ra, rb = rms([l for l, _ in a[1000:5000]]), rms([l for l, _ in b[1000:5000]])
        self.assertAlmostEqual(20 * math.log10(rb / ra), 0.0, delta=0.15)

    def test_aliasing_of_the_staircase_is_gone(self):
        """period 124: the staircase runs at 28604 Hz; its image at 28604 - 447 = 28157 Hz folds to 19843 Hz in a 48 kHz output."""
        mem = bytes(int(round(100 * math.sin(2 * math.pi * i / 64))) & 0xFF for i in range(64))
        scn = tp.Scn(6000, mem, 0x1000)
        scn.chan(0, 0x1000, 32, 124, 64)
        scn.w(0, tp.REG_DMACON, 0x8001)
        (_, a), (_, b) = tp.run_scenarios(self.exe, [box(scn), blep(scn)])
        f0 = tp.CLOCK / 124 / 64
        fp = tp.CLOCK / 124
        res = {}
        for name, smp in (('box', a), ('blep', b)):
            blk = [l for l, _ in smp[1000:5096]]
            fund = goertzel_db(blk, f0)
            res[name] = [goertzel_db(blk, 48000 - (fp - f0)) - fund, goertzel_db(blk, 48000 - (fp + f0)) - fund]
        self.assertGreater(max(res['box']), -50.0, res)      # the box filter folds the image back (measured -42 dB)
        self.assertLess(max(res['blep']), -90.0, res)        # the band-limited steps do not (measured -113 dB)

    def test_blep_mutations_are_caught(self):
        scns = [blep(s) for s in self.cases[:6]] + [blep(tp.dc_scn(0, n=60))]
        wants = [py_blep(s) for s in scns]

        def bad(exe):
            try:
                got = tp.run_scenarios_full(exe, scns)
            except (RuntimeError, subprocess.TimeoutExpired):
                return True
            return any(max((max(abs(a - c), abs(b - d)) for (a, b), (c, d) in zip(g[1], w)), default=0) > 1 for g, w in zip(got, wants))

        self.assertFalse(bad(self.exe))
        mutations = [
            ('output one frame late', 'const uint32_t ulOut = (p.ulFrame - kBlepDelay) & (kBlepRing - 1);', 'const uint32_t ulOut = (p.ulFrame - kBlepDelay + 1) & (kBlepRing - 1);'),
            ('step weights swapped between speakers', 'addBlepStep(p, pKernel, asSteps[c].aulAt[s], llDelta * (isLeft ? llOwn : llOther), llDelta * (isLeft ? llOther : llOwn));',
             'addBlepStep(p, pKernel, asSteps[c].aulAt[s], llDelta * (isLeft ? llOther : llOwn), llDelta * (isLeft ? llOwn : llOther));'),
            ('no interpolation between phases', 'if(llFrac != 0) {', 'if(false) {'),
            ('step at the frame start dropped', 'addStep(pSteps, 0, lNow - lLevel);', 'addStep(pSteps, 0, 0);'),
            ('step time off by a frame', 'addStep(pSteps, static_cast<uint32_t>(kClockPal - ullLeft), lNow - lLevel);', 'addStep(pSteps, 0, lNow - lLevel);'),
            ('held level not added', 'p.allRing[0][ulNow] += (llLevelL', 'p.allRing[0][ulNow] += 0 * (llLevelL'),
            ('volume not in the level', 'static_cast<int32_t>(ch.bOut) * static_cast<int32_t>(llVol);\n\t\t\t\taddStep(pSteps, static_cast<uint32_t>',
             'static_cast<int32_t>(ch.bOut) * 64;\n\t\t\t\taddStep(pSteps, static_cast<uint32_t>'),
        ]
        survivors = []
        for i, (name, old, new) in enumerate(mutations):
            try:
                exe = tp.build_driver(self.tmp.name, 'bm%d' % i, ('paula.cpp', old, new))
            except AssertionError:
                self.fail('mutation text not found: ' + name)
            if not bad(exe):
                survivors.append(name)
        self.assertEqual(survivors, [])


# ---------------------------------------------------------------------------------------------------------------------------------
# Output filters and settings, through the PC executable.
# ---------------------------------------------------------------------------------------------------------------------------------
def py_filter(frames, model, rate=48000):
    """The post filter of src/engine/outfilter.cpp written from its description: (A500: 4421 Hz | A1200: 34.4 kHz) one-pole low-pass, then
    5.2 Hz high-pass, then (LED) the 3090.53 Hz Q 0.66 biquad."""
    a500 = model in ('a500', 'a500led')
    led = model.endswith('led')
    la = 1.0 - math.exp(-2 * math.pi * (4420.97 if a500 else 34400.0) / rate)
    ha = 1.0 / (1.0 + 2 * math.pi * 5.2 / rate)
    w0 = 2 * math.pi * 3090.53 / rate
    alpha = math.sin(w0) / (2 * 0.660225)
    a0 = 1 + alpha
    b0 = (1 - math.cos(w0)) / 2 / a0
    b1 = (1 - math.cos(w0)) / a0
    a1 = -2 * math.cos(w0) / a0
    a2 = (1 - alpha) / a0
    out = []
    st = [[0.0, 0.0, 0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 0.0, 0.0]]       # lp, hp x, hp y, z1, z2
    for fr in frames:
        o = []
        for s in range(2):
            lp, hx, hy, z1, z2 = st[s]
            lp += (fr[s] - lp) * la
            x = lp
            y = ha * (hy + x - hx)
            hx, hy = x, y
            x = y
            if led:
                yo = b0 * x + z1
                z1 = b1 * x - a1 * yo + z2
                z2 = b0 * x - a2 * yo
                x = yo
            st[s] = [lp, hx, hy, z1, z2]
            o.append(max(-32768, min(32767, math.floor(x + 0.5))))
        out.append(tuple(o))
    return out


@unittest.skipUnless(tp.sdl_exe(), 'build-sdl/moonstone.exe missing')
class OutputFilters(unittest.TestCase):
    def run_exe(self, *args, pref=None):
        return subprocess.run([tp.sdl_exe(), *args], cwd=tp.BUILD, capture_output=True, text=True, timeout=120)

    def dump(self, tmp, *args, name='t.wav'):
        out = os.path.join(tmp, name)
        r = self.run_exe('--smoke', '14', '--fast', '--dump-audio', out, *args)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return tp.read_wav(out)[3], r.stderr

    def test_filters_match_the_python_model(self):
        with tempfile.TemporaryDirectory() as tmp:
            raw, _ = self.dump(tmp, '--audio-render', 'box', '--audio-separation', '100', '--audio-filter', 'off')
            for model in ('a500', 'a500led', 'a1200', 'a1200led'):
                got, _ = self.dump(tmp, '--audio-render', 'box', '--audio-separation', '100', '--audio-filter', model)
                want = py_filter(raw, model)
                worst = max(max(abs(a - c), abs(b - d)) for (a, b), (c, d) in zip(got, want))
                self.assertLessEqual(worst, 1, model)
                self.assertNotEqual(got, raw)

    def test_a1200_stage_is_nearly_transparent_and_a500_is_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            raw, _ = self.dump(tmp, '--audio-render', 'box', '--audio-separation', '100', '--audio-filter', 'off')
            rms = lambda v: math.sqrt(sum(a * a + b * b for a, b in v) / (2 * len(v)))
            a1200, _ = self.dump(tmp, '--audio-render', 'box', '--audio-separation', '100', '--audio-filter', 'a1200')
            a500, _ = self.dump(tmp, '--audio-render', 'box', '--audio-separation', '100', '--audio-filter', 'a500')
            self.assertLess(abs(20 * math.log10(rms(a1200[2000:]) / rms(raw[2000:]))), 0.5)
            self.assertLess(rms(a500[2000:]), rms(a1200[2000:]) - 20)       # the 4.4 kHz stage takes a visible bite of the tone

    def test_blep_defaults_match_the_independent_model(self):
        """Without options: BLEP, separation 70, A1200 filter.  The smoke tone through py_blep and py_filter."""
        with tempfile.TemporaryDirectory() as tmp:
            got, err = self.dump(tmp)
            self.assertIn('audio: render blep, separation 70, filter a1200', err)
            scn = tp.smoke_tone_model(14, sep=70)
            want = py_filter(py_blep(blep(scn)), 'a1200')
            worst = max(max(abs(a - c), abs(b - d)) for (a, b), (c, d) in zip(got, want))
            self.assertLessEqual(worst, 2)


@unittest.skipUnless(tp.sdl_exe(), 'build-sdl/moonstone.exe missing')
class AudioSettingsFile(unittest.TestCase):
    def run_exe(self, pref, *args):
        r = subprocess.run([tp.sdl_exe(), '--smoke', '3', '--fast', '--pref', pref, *args], cwd=tp.BUILD, capture_output=True, text=True, timeout=120)
        return r

    def line(self, r):
        for l in r.stderr.splitlines():
            if l.startswith('audio: render '):
                return l
        self.fail('no audio: render line in ' + r.stderr)

    def write(self, pref, text):
        with open(os.path.join(pref, 'audio.txt'), 'w') as f:
            f.write(text)

    def test_file_command_line_and_save(self):
        with tempfile.TemporaryDirectory() as pref:
            r = self.run_exe(pref)
            self.assertEqual(self.line(r), 'audio: render blep, separation 70, filter a1200')              # no file: the defaults
            self.assertFalse(os.path.exists(os.path.join(pref, 'audio.txt')), 'a plain run must not create the file')
            self.write(pref, '# mine\nRender = BOX\nseparation=40\nfilter=a500led\n')
            self.assertEqual(self.line(self.run_exe(pref)), 'audio: render box, separation 40, filter a500led')
            self.assertEqual(self.line(self.run_exe(pref, '--audio-separation', '100')), 'audio: render box, separation 100, filter a500led')   # the command line wins
            self.assertEqual(self.line(self.run_exe(pref, '--audio-render', 'blep', '--audio-filter', 'off')), 'audio: render blep, separation 40, filter off')

    def test_malformed_file_falls_back_per_setting(self):
        with tempfile.TemporaryDirectory() as pref:
            self.write(pref, 'render=banana\nseparation=abc\nfilter=a500\nseparation=250\nnonsense\n=\nfilter\n')
            self.assertEqual(self.line(self.run_exe(pref)), 'audio: render blep, separation 70, filter a500')
            self.write(pref, '\x00\xff\xfe garbage \x01')
            self.assertEqual(self.line(self.run_exe(pref)), 'audio: render blep, separation 70, filter a1200')

    def test_audio_save_remembers_the_options(self):
        with tempfile.TemporaryDirectory() as pref:
            r = self.run_exe(pref, '--audio-render', 'box', '--audio-separation', '20', '--audio-filter', 'a1200led', '--audio-save')
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(os.path.join(pref, 'audio.txt')) as f:
                text = f.read()
            self.assertIn('render=box\n', text)
            self.assertIn('separation=20\n', text)
            self.assertIn('filter=a1200led\n', text)
            self.assertEqual(self.line(self.run_exe(pref)), 'audio: render box, separation 20, filter a1200led')

    def test_bad_option_value_is_refused(self):
        with tempfile.TemporaryDirectory() as pref:
            for opt in (('--audio-filter', 'cheese'), ('--audio-render', 'sinc')):
                r = self.run_exe(pref, *opt)
                self.assertNotEqual(r.returncode, 0)
                self.assertIn('expected', r.stderr)

    def test_smoke_without_pref_ignores_the_file(self):
        r = subprocess.run([tp.sdl_exe(), '--smoke', '3', '--fast'], cwd=tp.BUILD, capture_output=True, text=True, timeout=120)
        self.assertEqual(self.line(r), 'audio: render blep, separation 70, filter a1200')


if __name__ == '__main__':
    unittest.main()
