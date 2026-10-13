"""ROADMAP 11.6a: planar bitplanes -> RGB (src/engine/planar.cpp) against an independent Python model, plus the PC smoke frame.

The model works pixel by pixel from the Hardware Reference Manual's description of the playfield: for pixel (x, y) and plane p it
addresses the word 'ptr[p] + y * (rowbytes + mod) + 2 * (x // 16)', takes bit 15 - x % 16 of it, and weights it 2^p.  planar.cpp works
a word at a time over the whole row; the two share no code.

What is compared (tests/planar_driver.cpp is the C++ side):
  * random modes: 1..8 planes, widths from 1 to 320 (odd, word-aligned, one word), heights, positive/negative/odd modulos and
    pointers, checked and unchecked chip views, random palettes with and without extra-halfbrite;
  * the game's own modes: 320x200 with 5 planes at the $1F40 stride and the 6-plane enhanced mode, modulo 0;
  * out-of-range fetches (read as zero bits, status BAD_ADDRESS), refused modes (0 or 9 planes, empty picture);
  * the colour conversions (OCS 12-bit, AGA hi/lo nibble banks) and the presentation geometry (fitRect);
  * the PC executable: `moonstone.exe --smoke N --dump-frame f.ppm|f.png` draws a pattern into chip RAM through the software blitter
    and the palette calls, converts it, and the dump's pixels are checked against the pattern computed here;
  * mutations: deliberate bugs in a copy of planar.cpp must each make the comparison fail (the test is sensitive).
"""
import os
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++') or (r'C:\Program Files\LLVM\bin\clang++.exe' if os.path.exists(r'C:\Program Files\LLVM\bin\clang++.exe') else None)
ENGINE = os.path.join(ROOT, 'src', 'engine', 'planar.cpp')
DRIVER = os.path.join(ROOT, 'tests', 'planar_driver.cpp')
OK, BAD_MODE, BAD_ADDRESS = 0, 1, 2
MEM = 1 << 16


# ---------------------------------------------------------------------------------------------------------------------------------
# The reference model.
# ---------------------------------------------------------------------------------------------------------------------------------
def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Case:
    def __init__(self, planes, width, height, mod1, mod2, ptr, origin, mem, checked, palette, ehb=False):
        self.planes, self.width, self.height, self.mod1, self.mod2 = planes, width, height, mod1, mod2
        self.ptr = list(ptr) + [0] * (8 - len(ptr))
        self.origin, self.mem, self.checked, self.palette, self.ehb = origin, mem, checked, palette, ehb

    def text(self, cmd):
        return '%s %x %x %x %x %x %s %x %x %x %s %s %d' % (
            cmd, self.planes, self.width, self.height, self.mod1 & 0xFFFF, self.mod2 & 0xFFFF,
            ' '.join('%x' % p for p in self.ptr), self.origin, len(self.mem), 1 if self.checked else 0, self.mem.hex(),
            ' '.join('%06x' % c for c in self.palette), 1 if self.ehb else 0)


def model_palette(palette, ehb):
    pal = list(palette)
    if ehb:
        for i in range(32):
            r, g, b = (pal[i] >> 16) & 255, (pal[i] >> 8) & 255, pal[i] & 255
            pal[32 + i] = ((r // 2) << 16) | ((g // 2) << 8) | (b // 2)
    return pal


def model_word(case, addr):
    off = addr - case.origin
    if case.checked and (addr < case.origin or off > len(case.mem) - 2):
        return 0, False
    return (case.mem[off] << 8) | case.mem[off + 1], True


def model_indices(case):
    """-> (list of rows of colour numbers, all fetches inside the view)"""
    rowbytes = 2 * ((case.width + 15) // 16)
    rows, ok = [], True
    for y in range(case.height):
        row = []
        for x in range(case.width):
            colour = 0
            for p in range(case.planes):
                mod = case.mod1 if p % 2 == 0 else case.mod2
                addr = (case.ptr[p] & ~1) + y * (rowbytes + s16(mod)) + 2 * (x // 16)
                word, inside = model_word(case, addr & 0xFFFFFFFF)
                ok = ok and inside
                colour |= ((word >> (15 - x % 16)) & 1) << p
            row.append(colour)
        rows.append(row)
    return rows, ok


def model_render(case):
    rows, ok = model_indices(case)
    pal = model_palette(case.palette, case.ehb)
    return [[pal[c] for c in row] for row in rows], OK if ok else BAD_ADDRESS


def model_aga(hi, lo):
    r = ((hi >> 8) & 15) * 16 + ((lo >> 8) & 15)
    g = ((hi >> 4) & 15) * 16 + ((lo >> 4) & 15)
    b = (hi & 15) * 16 + (lo & 15)
    return (r << 16) | (g << 8) | b


def model_fit(winw, winh, imgw, imgh, num, den):
    """Largest whole scale whose stretched height fits, centred; else the real-valued fit (floor)."""
    best = 0
    s = 1
    while imgw * s <= winw:
        h = int(imgh * num * s / den + 0.5)
        if h <= winh:
            best = s
        s += 1
    if best:
        w, h = imgw * best, int(imgh * num * best / den + 0.5)
    else:
        sh = imgh * num / den                   # stretched image height in source pixels
        if winw * sh <= winh * imgw:
            w, h = winw, int(winw * sh / imgw)
        else:
            h, w = winh, int(winh * imgw / sh)
        w, h = max(w, 1), max(h, 1)
    return ((winw - w) // 2, (winh - h) // 2, w, h)


# ---------------------------------------------------------------------------------------------------------------------------------
# Driver I/O.
# ---------------------------------------------------------------------------------------------------------------------------------
def build_driver(tmp, name='drv', mutate=None):
    src = ENGINE
    if mutate:
        with open(ENGINE, encoding='utf-8') as fh:
            text = fh.read()
        old, new = mutate
        if old not in text:
            raise AssertionError('mutation target not found: ' + old)
        src = os.path.join(tmp, name + '_planar.cpp')
        with open(src, 'w', encoding='utf-8') as fh:
            fh.write(text.replace(old, new, 1))
    exe = os.path.join(tmp, name + '.exe')
    subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra'] + ([] if mutate else ['-Werror']) + [
        '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), DRIVER, src, '-o', exe],
        check=True, capture_output=True, text=True)
    return exe


def run_lines(exe, lines):
    r = subprocess.run([exe], input='\n'.join(lines) + '\n', check=True, capture_output=True, text=True)
    return r.stdout.splitlines()


def parse_pixels(hexstr, width, height, digits):
    vals = [int(hexstr[i:i + digits], 16) for i in range(0, len(hexstr), digits)]
    return [vals[y * width:(y + 1) * width] for y in range(height)]


def run_cases(exe, cases):
    out = run_lines(exe, [c.text('R') for c in cases])
    res = []
    for i, c in enumerate(cases):
        s, p = out[2 * i].split(), out[2 * i + 1]
        status = int(s[1])
        res.append((status, parse_pixels(p[1:], c.width, c.height, 6) if status != BAD_MODE else None))
    return res


def count_mismatches(exe, cases, first=None):
    bad = 0
    got = run_cases(exe, cases)
    for c, (status, pix) in zip(cases, got):
        if c.planes < 1 or c.planes > 8 or c.width < 1 or c.height < 1:
            if status != BAD_MODE:
                bad += 1
            continue
        want, wstatus = model_render(c)
        if status != wstatus or pix != want:
            bad += 1
            if first is not None and not first:
                first.append((c.planes, c.width, c.height, c.mod1, c.mod2, status, wstatus))
    return bad


def rnd_palette(rng):
    return [rng.getrandbits(24) for _ in range(256)]


def rnd_case(rng, checked=None):
    planes = rng.randint(1, 8)
    width = rng.choice([1, 2, 15, 16, 17, 31, 32, 33, 47, 48, 63, 64, 100, 160, 320, rng.randint(1, 200)])
    height = rng.randint(1, 6)
    rowbytes = 2 * ((width + 15) // 16)
    mem = bytes(rng.getrandbits(8) for _ in range(MEM))
    origin = rng.choice([0, 0x1000, 0x40000])
    checked = rng.random() < 0.5 if checked is None else checked
    # modulos: even, negative, zero (the chip ignores bit 0 of what it adds only for blitter channels; here they are added as given)
    mods = [rng.choice([0, 0, 2, 4, 40, -2, -rowbytes, -rowbytes - 2, rng.randint(-64, 64) & ~1]) for _ in range(2)]
    base = 0x800 + rng.choice([0, 1, 2, 3]) if rng.random() < 0.3 else 0x800
    ptr = []
    for p in range(8):
        ptr.append(origin + base + p * 0x400 + rng.choice([0, 0, 0, 2, 6]))
    if checked and rng.random() < 0.15:
        ptr[rng.randint(0, 7)] = origin + MEM - 4 - rng.randint(0, 40)   # near the end: runs off in a checked view
    return Case(planes, width, height, mods[0], mods[1], ptr, origin, mem, checked, rnd_palette(rng), ehb=planes == 6 and rng.random() < 0.5)


class Conversion(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = build_driver(cls.tmp.name, 'base')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_random_modes_match_the_model(self):
        rng = random.Random(1161)
        cases = [rnd_case(rng) for _ in range(500)]
        first = []
        self.assertEqual(count_mismatches(self.exe, cases, first), 0, first)

    def test_every_plane_count_and_width_class(self):
        rng = random.Random(1162)
        cases = []
        for planes in range(1, 9):
            for width in (1, 15, 16, 17, 33, 64, 319, 320):
                c = rnd_case(rng, checked=True)
                c.planes, c.width, c.height = planes, width, 3
                c.ptr = [c.origin + 0x400 + p * 0x400 for p in range(8)]
                c.mod1 = c.mod2 = 0
                cases.append(c)
        self.assertEqual(count_mismatches(self.exe, cases), 0)

    def test_game_modes(self):
        """320x200, 5 planes (the original) and 6 (enhanced, KILLEHB: 64 real colours), planes contiguous at the $1F40 stride."""
        rng = random.Random(1163)
        for planes in (5, 6):
            mem = bytes(rng.getrandbits(8) for _ in range(0x1F40 * 6 + 64))
            pal = rnd_palette(rng)
            c = Case(planes, 320, 200, 0, 0, [0x2000 + p * 0x1F40 for p in range(planes)], 0x2000, mem, True, pal)
            self.assertEqual(count_mismatches(self.exe, [c]), 0, planes)

    def test_status_of_out_of_range_and_refused_modes(self):
        rng = random.Random(1164)
        mem = bytes(rng.getrandbits(8) for _ in range(256))
        pal = rnd_palette(rng)
        # runs off the end of a checked view: still rendered, status BAD_ADDRESS, missing words read as zero
        c = Case(3, 64, 4, 0, 0, [0x100 + 120 * p for p in range(3)], 0x100, mem, True, pal)
        status, pix = run_cases(self.exe, [c])[0]
        want, wstatus = model_render(c)
        self.assertEqual((status, pix), (BAD_ADDRESS, want))
        self.assertEqual(wstatus, BAD_ADDRESS)
        # below the origin
        c = Case(1, 16, 1, 0, 0, [0x80], 0x100, mem, True, pal)
        self.assertEqual(run_cases(self.exe, [c])[0][0], BAD_ADDRESS)
        # exactly at the last word: fine; one word further: not
        c = Case(1, 16, 1, 0, 0, [0x100 + 254], 0x100, mem, True, pal)
        self.assertEqual(run_cases(self.exe, [c])[0][0], OK)
        c = Case(1, 16, 1, 0, 0, [0x100 + 256], 0x100, mem, True, pal)
        self.assertEqual(run_cases(self.exe, [c])[0][0], BAD_ADDRESS)
        # unchecked view: never an address error
        c = Case(2, 32, 2, 0, 0, [0x100, 0x180], 0x100, mem, False, pal)
        self.assertEqual(run_cases(self.exe, [c])[0][0], OK)
        for planes, w, h in ((0, 16, 1), (9, 16, 1), (1, 0, 1), (1, 16, 0)):
            c = Case(planes, w, h, 0, 0, [0x100] * 8, 0x100, mem, True, pal)
            self.assertEqual(run_cases(self.exe, [c])[0][0], BAD_MODE, (planes, w, h))

    def test_indices_equal_the_model(self):
        rng = random.Random(1165)
        cases = [rnd_case(rng) for _ in range(150)]
        out = run_lines(self.exe, [c.text('I') for c in cases])
        for i, c in enumerate(cases):
            rows, ok = model_indices(c)
            got = parse_pixels(out[2 * i][1:], c.width, c.height, 2)
            self.assertEqual(got, rows, (c.planes, c.width, c.height))
            self.assertEqual(int(out[2 * i + 1].split()[1]), OK if ok else BAD_ADDRESS)

    def test_modulo_pairs_are_odd_and_even_planes(self):
        """BPL1MOD takes planes 1, 3, 5, 7 and BPL2MOD planes 2, 4, 6, 8: with different values the rows of the two sets diverge."""
        rng = random.Random(1166)
        mem = bytes(rng.getrandbits(8) for _ in range(MEM))
        c = Case(8, 48, 5, 6, -2, [0x2000 + 0x600 * p for p in range(8)], 0, mem, True, rnd_palette(rng))
        self.assertEqual(count_mismatches(self.exe, [c]), 0)

    def test_colour_conversions(self):
        rng = random.Random(1167)
        pairs = [(rng.getrandbits(12), rng.getrandbits(12)) for _ in range(200)] + [(0, 0), (0xFFF, 0xFFF), (0x123, 0x456), (0xF00, 0x0F0)]
        out = run_lines(self.exe, ['A %x %x' % p for p in pairs])
        for (hi, lo), line in zip(pairs, out):
            t = line.split()
            self.assertEqual(int(t[1], 16), model_aga(hi, lo), (hi, lo))
            self.assertEqual(int(t[2], 16), model_aga(hi, hi), hi)          # OCS 12-bit: each nibble n -> n * 17
            self.assertEqual(int(t[2], 16), ((hi >> 8 & 15) * 17 << 16) | ((hi >> 4 & 15) * 17 << 8) | (hi & 15) * 17)

    def test_extra_halfbrite(self):
        rng = random.Random(1168)
        mem = bytes(rng.getrandbits(8) for _ in range(MEM))
        pal = rnd_palette(rng)
        c = Case(6, 64, 3, 0, 0, [0x1000 + 0x300 * p for p in range(6)], 0, mem, True, pal, ehb=True)
        want, _ = model_render(c)
        got = run_cases(self.exe, [c])[0][1]
        self.assertEqual(got, want)
        used = {v for row in model_indices(c)[0] for v in row}
        self.assertTrue(any(v >= 32 for v in used), 'the case must exercise colours 32..63')

    def test_fit_rect(self):
        rng = random.Random(1169)
        cases = [(640, 512), (1280, 1024), (1920, 1080), (320, 200), (320, 240), (321, 241), (100, 80), (50, 400), (1000, 100), (1, 1)]
        cases += [(rng.randint(1, 2500), rng.randint(1, 1600)) for _ in range(200)]
        lines = ['F %d %d 320 200 6 5' % c for c in cases]
        out = run_lines(self.exe, lines)
        for c, line in zip(cases, out):
            self.assertEqual(tuple(int(v) for v in line.split()[1:]), model_fit(c[0], c[1], 320, 200, 6, 5), c)
        rng2 = random.Random(1172)
        odd = [(rng2.randint(1, 2000), rng2.randint(1, 1500), rng2.randint(1, 400), rng2.randint(1, 300), rng2.randint(1, 9), rng2.randint(1, 9))
               for _ in range(300)]
        out2 = run_lines(self.exe, ['F %d %d %d %d %d %d' % o for o in odd])
        for o, line in zip(odd, out2):
            self.assertEqual(tuple(int(v) for v in line.split()[1:]), model_fit(*o), o)
        # properties the model could share a mistake in: inside the window, whole scale when it fits
        for c, line in zip(cases, out):
            x, y, w, h = (int(v) for v in line.split()[1:])
            self.assertTrue(x >= 0 and y >= 0 and x + w <= c[0] and y + h <= c[1], (c, x, y, w, h))
            if c[0] >= 320 and c[1] >= 240:
                self.assertEqual(w % 320, 0, c)


# ---------------------------------------------------------------------------------------------------------------------------------
# Mutations.
# ---------------------------------------------------------------------------------------------------------------------------------
class Mutations(unittest.TestCase):
    MUTATIONS = [
        ('leftmost pixel is the low bit', '(auwBits[ubPlane] >> bBit) & 1u', '(auwBits[ubPlane] >> (15 - bBit)) & 1u'),
        ('plane 1 and 2 weights swapped', '<< ubPlane))', '<< (ubPlane ^ 1)))'),
        ('modulos of odd and even planes swapped', '(ubPlane & 1) ? sMode.mod2 : sMode.mod1', '(ubPlane & 1) ? sMode.mod1 : sMode.mod2'),
        ('modulo ignored', '(lRowBytes + lMod)', '(lRowBytes)'),
        ('row fetched in whole words rounded down', '(static_cast<uint32_t>(sMode.width) + 15u) / 16u', 'static_cast<uint32_t>(sMode.width) / 16u'),
        ('last pixel of the row dropped', 'ulX < sMode.width;', 'ulX + 1 < sMode.width;'),
        ('odd pointer bit kept', '(sMode.ptr[ubPlane] & ~1u)', '(sMode.ptr[ubPlane])'),
        ('view end off by one word', 'ulOff > sMem.size - 2u', 'ulOff > sMem.size'),
        ('address error not reported', 'eStatus = PLANAR_BAD_ADDRESS;', ''),
        ('missing words read as ones', 'auwBits[ubPlane] = 0;', 'auwBits[ubPlane] = 0xFFFF;'),
        ('eight planes refused', 'sMode.planes <= kMaxPlanes', 'sMode.planes < kMaxPlanes'),
        ('zero planes accepted', 'sMode.planes >= 1 &&', ''),
        ('empty height accepted', 'sMode.height >= 1;', 'true;'),
        ('colour number masked to 5 bits', 'sPalette.rgb[aubRow[uwX]]', 'sPalette.rgb[aubRow[uwX] & 31]'),
        ('output pitch taken as the width', 'static_cast<uint32_t>(uwY) * ulPitch', 'static_cast<uint32_t>(uwY) * sMode.width'),
        ('halved colours not masked', '& 0x7F7F7Fu', '& 0xFFFFFFu'),
        ('halfbrite copies the wrong entry', 'sPalette.rgb[32 + i] =', 'sPalette.rgb[33 + i] ='),
        ('halfbrite not halved', '(ulRgb >> 1)', '(ulRgb)'),
        ('AGA red low nibble from green', '| ((uwLo12 >> 8) & 0xF)', '| ((uwLo12 >> 4) & 0xF)'),
        ('AGA blue high nibble missing', '(static_cast<uint32_t>(uwHi12 & 0xF) << 4)', '(static_cast<uint32_t>(uwHi12 & 0xF) << 0)'),
        ('12-bit colours not replicated', 'return rgbAga(uwRgb12, uwRgb12);', 'return rgbAga(uwRgb12, 0);'),
        ('fit: image must be smaller than the window', 'lImgW * s <= lWinW', 'lImgW * s < lWinW'),
        ('fit: height may touch the window', 'if(lH > lWinH)', 'if(lH >= lWinH)'),
        ('fit: height not rounded', '(lImgH * lAspNum * s + lAspDen / 2) / lAspDen;\n\t\tif', '(lImgH * lAspNum * s) / lAspDen;\n\t\tif'),
        ('fit: not centred vertically', 'sRect.y = (lWinH - sRect.h) / 2;', 'sRect.y = 0;'),
        ('fit: aspect ignored', 'const int32_t lH = (lImgH * lAspNum * s', 'const int32_t lH = (lImgH * lAspDen * s'),
    ]

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        rng = random.Random(1170)
        cls.cases = [rnd_case(rng) for _ in range(300)]
        # targeted: refused modes, and a checked boundary triple, so each mutation above has something to trip on
        mem = bytes(rng.getrandbits(8) for _ in range(256))
        pal = rnd_palette(rng)
        for planes, w, h in ((0, 16, 1), (9, 16, 1), (1, 16, 0), (8, 16, 2)):
            cls.cases.append(Case(planes, w, h, 0, 0, [0x100 + 16 * p for p in range(8)], 0x100, mem, True, pal))
        cls.cases.append(Case(1, 16, 1, 0, 0, [0x100 + 256], 0x100, mem, True, pal))
        cls.cases.append(Case(1, 16, 1, 0, 0, [0x100 + 254], 0x100, mem, True, pal))
        cls.cases.append(Case(1, 16, 1, 0, 0, [0x80], 0x100, mem, True, pal))
        cls.cases.append(Case(6, 32, 4, 0, 0, [0x100 + 20 * p for p in range(6)], 0x100, mem, True, pal, ehb=True))
        cls.base = build_driver(cls.tmp.name, 'base')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fit_and_colour_mismatch(self, exe):
        """The colour and geometry checks of Conversion, reduced to a yes/no."""
        rng = random.Random(1171)
        pairs = [(rng.getrandbits(12), rng.getrandbits(12)) for _ in range(60)]
        wins = [(640, 512), (1920, 1080), (321, 241), (100, 80), (700, 333), (1000, 100), (320, 240), (640, 480), (960, 720)]
        wins += [(rng.randint(1, 2500), rng.randint(1, 1600)) for _ in range(60)]
        odd = [(rng.randint(1, 2000), rng.randint(1, 1500), rng.randint(1, 400), rng.randint(1, 300), rng.randint(1, 9), rng.randint(1, 9))
               for _ in range(150)] + [(4, 1, 4, 3, 1, 2), (4, 2, 4, 3, 1, 2), (9, 4, 3, 3, 3, 2)]   # the first: 1.5 rows round up to 2
        out = run_lines(exe, ['A %x %x' % p for p in pairs] + ['F %d %d 320 200 6 5' % w for w in wins] + ['F %d %d %d %d %d %d' % o for o in odd])
        for (hi, lo), line in zip(pairs, out):
            t = line.split()
            if int(t[1], 16) != model_aga(hi, lo) or int(t[2], 16) != model_aga(hi, hi):
                return True
        for w, line in zip(wins, out[len(pairs):]):
            if tuple(int(v) for v in line.split()[1:]) != model_fit(w[0], w[1], 320, 200, 6, 5):
                return True
        for o, line in zip(odd, out[len(pairs) + len(wins):]):
            if tuple(int(v) for v in line.split()[1:]) != model_fit(*o):
                return True
        return False

    def test_unmutated_build_is_clean(self):
        self.assertEqual(count_mismatches(self.base, self.cases), 0)
        self.assertFalse(self.fit_and_colour_mismatch(self.base))

    def test_every_mutation_is_caught(self):
        survivors = []
        for i, (name, old, new) in enumerate(self.MUTATIONS):
            exe = build_driver(self.tmp.name, 'mut%d' % i, (old, new))
            if count_mismatches(exe, self.cases) == 0 and not self.fit_and_colour_mismatch(exe):
                survivors.append(name)
        self.assertEqual(survivors, [], 'mutations the tests do not notice')


# ---------------------------------------------------------------------------------------------------------------------------------
# The PC executable's smoke frame.
# ---------------------------------------------------------------------------------------------------------------------------------
BUILD = os.path.join(ROOT, 'build-sdl')
SMOKE_W, SMOKE_H = 320, 200


def smoke_palette12(c):
    """The test pattern's palette (src/rt/sdl/smoke.cpp): colour c of 0..31 as an OCS register value."""
    r, g, b = (c * 5) & 15, (c * 3 + 4) & 15, (c * 7 + 9) & 15
    return (r << 8) | (g << 4) | b


def expected_smoke_frame():
    """Top half (rows 0..99): 20 bars of 16 pixels, bar k has colour k; bottom half: the same inverted (colour 31 - k)."""
    rows = []
    for y in range(SMOKE_H):
        row = []
        for x in range(SMOKE_W):
            k = x // 16
            c = k if y < 100 else 31 - k
            row.append(model_aga(smoke_palette12(c), smoke_palette12(c)))
        rows.append(row)
    return rows


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    parts = data.split(b'\n', 3)
    assert parts[0] == b'P6', parts[0]
    w, h = (int(v) for v in parts[1].split())
    assert parts[2] == b'255'
    px = parts[3]
    assert len(px) == w * h * 3
    return [[(px[(y * w + x) * 3] << 16) | (px[(y * w + x) * 3 + 1] << 8) | px[(y * w + x) * 3 + 2] for x in range(w)] for y in range(h)]


def read_png(path):
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, idat = 8, b''
    w = h = 0
    while pos < len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        crc = struct.unpack('>I', data[pos + 8 + n:pos + 12 + n])[0]
        assert zlib.crc32(typ + body) & 0xFFFFFFFF == crc, 'bad chunk crc ' + typ.decode()
        if typ == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            assert (depth, ctype) == (8, 2)
        elif typ == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    rows = []
    for y in range(h):
        line = raw[y * (1 + 3 * w):(y + 1) * (1 + 3 * w)]
        assert line[0] == 0, 'filter type'
        rows.append([(line[1 + 3 * x] << 16) | (line[2 + 3 * x] << 8) | line[3 + 3 * x] for x in range(w)])
    return rows


def sdl_exe():
    p = os.path.join(BUILD, 'moonstone.exe')
    return p if os.path.isfile(p) else None


@unittest.skipUnless(sdl_exe(), 'build-sdl/moonstone.exe missing (py -m unittest tests.test_sdl_build builds it)')
class SmokeFrame(unittest.TestCase):
    def run_exe(self, *args):
        return subprocess.run([sdl_exe(), *args], cwd=BUILD, capture_output=True, text=True, timeout=120)

    def test_ppm_dump_has_the_pattern(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, 'frame.ppm')
            r = self.run_exe('--smoke', '3', '--dump-frame', out)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertEqual(read_ppm(out), expected_smoke_frame())

    def test_png_dump_has_the_pattern(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, 'frame.png')
            r = self.run_exe('--smoke', '3', '--dump-frame', out)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertEqual(read_png(out), expected_smoke_frame())

    def test_fast_mode_runs_many_frames_quickly(self):
        import time
        t0 = time.time()
        r = self.run_exe('--smoke', '2000', '--fast')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertLess(time.time() - t0, 20.0, '2000 frames unthrottled would take 40 s at 50 Hz')
        self.assertIn('2000 frames', r.stderr)

    def test_self_tests_ran(self):
        r = self.run_exe('--smoke', '2')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('timer self-test: ok', r.stderr)
        self.assertIn('chip self-test: ok', r.stderr)

    def test_bad_dump_name_is_refused(self):
        r = self.run_exe('--smoke', '1', '--dump-frame', os.path.join(tempfile.gettempdir(), 'frame.xyz'))
        self.assertNotEqual(r.returncode, 0)


if __name__ == '__main__':
    unittest.main()
