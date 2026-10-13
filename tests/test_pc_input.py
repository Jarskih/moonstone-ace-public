"""ROADMAP 11.9: the PC input backend.  engine/pcinput (pure: host keys -> Amiga raw codes, key-repeat filter, keyboard joystick, pad sticks,
JOYxDAT encoding, mouse counters) against independent Python models, plus the executable's scripted-input proof.

What the game expects (src/rt/input.cpp, include/engine/pcinput.hpp): raw Amiga key codes 0..$7F delivered as press / release edges and never
repeated; JOY0DAT mouse counters that the VBL handler differences (engine/input.cpp mouseStep, with its documented 255 - d wrap quirk);
JOYxDAT direction bits that ms::joyDirections decodes.  The models here know the Amiga keyboard from its layout (rows of key names), the
USB HID usage table (SDL scancode numbers) and the JOYxDAT bit meaning from the Hardware Reference Manual; engine/pcinput.cpp is a table of
numbers.  They share no code.

What is compared (tests/pcinput_driver.cpp is the C++ side):
  * the key map for every host key id 0..511 (and, when an ACE checkout is near, against ACE's KEY_* constants);
  * random sequences of host key edges (repeats, releases of keys never seen, both Ctrl keys, unmapped keys): the press/release stream,
    and the keys released when the window loses the focus;
  * the keyboard joystick (arrows + Right Ctrl / Space): bits after every edge, opposite directions cancelling, the empty set;
  * joyEncode for every direction combination, decoded by ms::joyDirections (the game's decoder) back to the same bits; the pad stick
    thresholds on both sides of the dead zone;
  * the mouse: random motion in many frames at several scales; the JOY0DAT counters and the pointer the game derives from them (mouseStep,
    wrap quirk included), fractions kept, rounding toward zero, the per-frame limit;
  * the PC executable (`moonstone.exe --smoke N --keys "..."`): the scripted events reach the platform input state at the frame the model
    predicts: key events as the key handler sees them, the ports as inputReadPorts shows them;
  * mutations: deliberate bugs in copies of pcinput.cpp must each make the comparison fail.
"""
import os
import random
import re
import shutil
import subprocess
import tempfile
import unittest
from fractions import Fraction

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++') or (r'C:\Program Files\LLVM\bin\clang++.exe' if os.path.exists(r'C:\Program Files\LLVM\bin\clang++.exe') else None)
ENGINE = os.path.join(ROOT, 'src', 'engine', 'pcinput.cpp')
INPUT_CPP = os.path.join(ROOT, 'src', 'engine', 'input.cpp')
DRIVER = os.path.join(ROOT, 'tests', 'pcinput_driver.cpp')

# ---------------------------------------------------------------------------------------------------------------------------------
# The models.
# ---------------------------------------------------------------------------------------------------------------------------------
# USB HID usage table (what SDL_Scancode numbers are): name -> usage.
HID = {}
for _i, _c in enumerate('ABCDEFGHIJKLMNOPQRSTUVWXYZ'):
    HID[_c] = 4 + _i
for _i, _c in enumerate('1234567890'):
    HID[_c] = 30 + _i
HID.update({'Enter': 40, 'Escape': 41, 'Backspace': 42, 'Tab': 43, 'Space': 44, 'Minus': 45, 'Equals': 46, 'LBracket': 47, 'RBracket': 48,
            'Backslash': 49, 'NonUSHash': 50, 'Semicolon': 51, 'Quote': 52, 'Grave': 53, 'Comma': 54, 'Period': 55, 'Slash': 56,
            'CapsLock': 57, 'PrintScreen': 70, 'ScrollLock': 71, 'Pause': 72, 'Insert': 73, 'Home': 74, 'PageUp': 75, 'Delete': 76,
            'End': 77, 'PageDown': 78, 'Right': 79, 'Left': 80, 'Down': 81, 'Up': 82, 'NumLock': 83, 'KPDiv': 84, 'KPMul': 85,
            'KPMinus': 86, 'KPPlus': 87, 'KPEnter': 88, 'KP0': 98, 'KPPeriod': 99, 'NonUSBackslash': 100, 'Application': 101,
            'Help': 117, 'LCtrl': 224, 'LShift': 225, 'LAlt': 226, 'LGUI': 227, 'RCtrl': 228, 'RShift': 229, 'RAlt': 230, 'RGUI': 231})
for _i in range(12):
    HID['F%d' % (_i + 1)] = 58 + _i
for _i in range(9):
    HID['KP%d' % (_i + 1)] = 89 + _i

# The Amiga keyboard, US layout, by rows: (first raw code, key names left to right).
AMIGA_ROWS = [
    (0x00, 'Grave 1 2 3 4 5 6 7 8 9 0 Minus Equals Backslash'),
    (0x10, 'Q W E R T Y U I O P LBracket RBracket'),
    (0x20, 'A S D F G H J K L Semicolon Quote NonUSHash'),
    (0x30, 'NonUSBackslash Z X C V B N M Comma Period Slash'),
    (0x1D, 'KP1 KP2 KP3'), (0x2D, 'KP4 KP5 KP6'), (0x3D, 'KP7 KP8 KP9'), (0x0F, 'KP0'), (0x3C, 'KPPeriod'),
    (0x40, 'Space Backspace Tab KPEnter Enter Escape Delete'),
    (0x4A, 'KPMinus'), (0x4C, 'Up Down Right Left'),
    (0x50, 'F1 F2 F3 F4 F5 F6 F7 F8 F9 F10'),
    (0x5A, 'NumLock ScrollLock KPDiv KPMul KPPlus'),
    (0x5F, 'Help'),
    (0x60, 'LShift RShift CapsLock LCtrl LAlt RAlt LGUI RGUI'),
]
# Amiga raw codes that more than one host key produces / extra host keys
EXTRA = {'RCtrl': 0x63, 'PageUp': 0x5F}   # one Ctrl on the Amiga; PageUp = Help as well


def key_model():
    """{usage: raw} of the expected key map."""
    out = {}
    for first, names in AMIGA_ROWS:
        for k, name in enumerate(names.split()):
            out[HID[name]] = first + k
    for name, raw in EXTRA.items():
        out[HID[name]] = raw
    return out


KEYS = key_model()


def build_driver(tmp, name='drv', mutate=None):
    src = ENGINE
    if mutate:
        with open(ENGINE, encoding='utf-8') as fh:
            text = fh.read()
        old, new = mutate
        if old not in text:
            raise AssertionError('mutation target not found: ' + old)
        src = os.path.join(tmp, name + '_pcinput.cpp')
        with open(src, 'w', encoding='utf-8') as fh:
            fh.write(text.replace(old, new, 1))
    exe = os.path.join(tmp, name + '.exe')
    subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra'] + ([] if mutate else ['-Werror']) + [
        '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), DRIVER, src, INPUT_CPP, '-o', exe],
        check=True, capture_output=True, text=True)
    return exe


def run_lines(exe, lines, timeout=60):
    r = subprocess.run([exe], input='\n'.join(lines) + '\n', check=True, capture_output=True, text=True, timeout=timeout)
    return r.stdout.splitlines()


def model_keyboard(events):
    """-> (list of 'P4E' / 'R4E' / 'I', list of keys released by a focus loss)"""
    held, count, out = set(), {}, []
    for usage, down in events:
        raw = KEYS.get(usage)
        if raw is None:
            out.append('I')
        elif down:
            if usage in held:
                out.append('I')          # OS key repeat
            else:
                held.add(usage)
                count[raw] = count.get(raw, 0) + 1
                out.append('P%02X' % raw if count[raw] == 1 else 'I')
        else:
            if usage not in held:
                out.append('I')          # release of a key never seen going down
            else:
                held.discard(usage)
                count[raw] -= 1
                out.append('R%02X' % raw if count[raw] == 0 else 'I')
    flush = []
    for usage in sorted(held):           # focus lost: every key still down goes up
        raw = KEYS[usage]
        count[raw] -= 1
        if count[raw] == 0:
            flush.append('%02X' % raw)
    return out, flush


JOY_RIGHT, JOY_LEFT, JOY_DOWN, JOY_UP, JOY_FIRE = 1, 2, 4, 8, 0x10
ARROWS = {82: 'up', 81: 'down', 80: 'left', 79: 'right', 228: 'fire', 44: 'fire'}


def model_joykeys(events, arrows=True):
    held = {}
    out = []
    for usage, down in events:
        role = ARROWS.get(usage) if arrows else None
        if role is not None:
            held[usage] = down
        names = {ARROWS[u] for u, d in held.items() if d}
        bits = 0
        if 'up' in names and 'down' not in names:
            bits |= JOY_UP
        if 'down' in names and 'up' not in names:
            bits |= JOY_DOWN
        if 'left' in names and 'right' not in names:
            bits |= JOY_LEFT
        if 'right' in names and 'left' not in names:
            bits |= JOY_RIGHT
        if 'fire' in names:
            bits |= JOY_FIRE
        out.append('%x' % bits)
    return out


def joy_decode(dat):
    """ms::joyDirections written from the Hardware Reference Manual: right = bit 1, left = bit 9, down = bit 1 xor bit 0, up = bit 9 xor bit 8."""
    b = lambda n: (dat >> n) & 1
    bits = 0
    if b(1):
        bits |= JOY_RIGHT
    if b(9):
        bits |= JOY_LEFT
    if b(1) ^ b(0):
        bits |= JOY_DOWN
    if b(9) ^ b(8):
        bits |= JOY_UP
    return bits


def joy_encode_model(bits):
    """The JOYxDAT value of a stick position: the one with only bits 0, 1, 8, 9 that decodes to the direction bits."""
    sols = [v for v in range(0x400) if (v & ~0x303) == 0 and joy_decode(v) == bits & 0xF]
    assert len(sols) == 1
    return sols[0]


def pad_model(x, y, dead=12000):
    bits = 0
    if x < -dead:
        bits |= JOY_LEFT
    if x > dead:
        bits |= JOY_RIGHT
    if y < -dead:
        bits |= JOY_UP
    if y > dead:
        bits |= JOY_DOWN
    return bits


def trunc(f):
    return int(f)          # Fraction -> int truncates toward zero


def game_delta(prev, now):
    """The game's counter difference (engine/input.cpp mouseDelta, verified against the original in tests/test_input.py), the 255 - d quirk included."""
    if prev >= now:
        d = (prev - now) & 0xFF
        return (0xFF - d) if d >= 0x80 else -d
    n = (now - prev) & 0xFF
    return -(0xFF - n) if n >= 0x80 else n


def model_mouse(sx, sy, frames):
    """-> ['M dat x y', ...]; sx, sy in 16.16 counts per pixel."""
    ax = ay = Fraction(0)
    h = v = 0
    out = []
    for moves in frames:
        fx = fy = 0
        for dx, dy in moves:
            ax += Fraction(dx * sx, 65536)
            cx = trunc(ax)
            ax -= cx
            cx = max(-127 - fx, min(127 - fx, cx))
            fx += cx
            ay += Fraction(dy * sy, 65536)
            cy = trunc(ay)
            ay -= cy
            cy = max(-127 - fy, min(127 - fy, cy))
            fy += cy
            h = (h + cx) & 0xFF
            v = (v + cy) & 0xFF
        dat = (v << 8) | h
        out.append((dat, h, v))
    # the game samples the counters once per frame
    res = []
    gx = gy = 100
    prev_h = prev_v = 0
    for dat, h, v in out:
        gx = max(-7, min(0x13F, gx + game_delta(prev_h, h)))
        gy = max(-7, min(0xC7, gy + game_delta(prev_v, v)))
        prev_h, prev_v = h, v
        res.append('M %04X %d %d' % (dat, gx, gy))
    return res


# ---------------------------------------------------------------------------------------------------------------------------------
# Cases and the comparison.
# ---------------------------------------------------------------------------------------------------------------------------------
POOL = [4, 20, 26, 44, 79, 80, 81, 82, 224, 228, 225, 40, 41, 100, 117, 75, 83, 71, 2, 3, 200, 300, 511]   # mapped, both Ctrls, unmapped, out of range


def make_cases(seed=11):
    rnd = random.Random(seed)
    cases = {'K': [], 'E': [], 'J': [], 'D': [], 'P': [], 'M': []}
    cases['K'] = list(range(512))
    for _ in range(1500):
        n = rnd.randint(1, 14)
        cases['E'].append([(rnd.choice(POOL), rnd.random() < 0.55) for _ in range(n)])
    cases['E'].append([(228, True), (224, True), (224, False), (228, False)])        # two Ctrl keys: one press, one release
    cases['E'].append([(228, True), (224, True), (228, False), (224, False)])
    cases['E'].append([(82, True), (82, True), (82, True), (82, False)])             # OS repeat
    cases['E'].append([(82, False), (82, True), (82, False)])                        # release of a key never seen
    cases['E'].append([(228, True), (224, True)])                                    # focus lost with both held
    for _ in range(800):
        n = rnd.randint(1, 16)
        cases['J'].append((rnd.random() < 0.8, [(rnd.choice([79, 80, 81, 82, 228, 44, 224, 4, 100]), rnd.random() < 0.55) for _ in range(n)]))
    cases['D'] = list(range(32))
    for x, y in [(0, 0), (12000, 0), (12001, 0), (-12000, 0), (-12001, 0), (0, 12000), (0, 12001), (0, -12000), (0, -12001),
                 (32767, 32767), (-32768, -32768), (-32768, 32767), (5000, -20000), (-30000, 11999)]:
        cases['P'].append((x, y))
    for _ in range(300):
        cases['P'].append((rnd.randint(-32768, 32767), rnd.randint(-32768, 32767)))
    scales = [0x10000, 0x8000, 0x6666, 0x19999, 0x28000, 0x1B58, 0x30000, 0x14000]
    for sx in scales:
        for _ in range(12):
            frames = []
            for _ in range(rnd.randint(2, 40)):
                moves = [(rnd.randint(-9, 9) if rnd.random() < 0.7 else rnd.randint(-200, 200), rnd.randint(-9, 9) if rnd.random() < 0.7 else rnd.randint(-200, 200))
                         for _ in range(rnd.randint(0, 4))]
                frames.append(moves)
            cases['M'].append((sx, rnd.choice(scales), frames))
    # directed: fractions kept (3 x 0.4 = 1.2 counts), symmetric rounding, wrap of the counters, the per-frame limit
    cases['M'].append((0x6666, 0x6666, [[(1, 1)], [(1, 1)], [(1, 1)], [(1, 1)], [(1, 1)]]))
    cases['M'].append((0x6666, 0x6666, [[(-1, -1)], [(-1, -1)], [(-1, -1)], [(-1, -1)], [(-1, -1)]]))
    cases['M'].append((0x10000, 0x10000, [[(100, 100)]] * 6))                         # counters wrap past 255 (one count lost per wrap)
    cases['M'].append((0x10000, 0x10000, [[(-100, -100)]] * 6))
    cases['M'].append((0x10000, 0x10000, [[(300, -300)], [(127, 127), (127, 127)], [(-127, -127), (-127, -127)], []]))
    return cases


def fmt_moves(frames):
    return ' '.join(';'.join('%d,%d' % m for m in f) if f else '-' for f in frames)


def lines_for(cases):
    lines = []
    lines += ['K %d' % u for u in cases['K']]
    lines += ['E ' + ' '.join('%d %d' % (u, 1 if d else 0) for u, d in ev) for ev in cases['E']]
    lines += ['J %s %s' % ('A' if arrows else 'N', ' '.join('%d %d' % (u, 1 if d else 0) for u, d in ev)) for arrows, ev in cases['J']]
    lines += ['D %x' % b for b in cases['D']]
    lines += ['P %d %d' % xy for xy in cases['P']]
    lines += ['M %x %x %s' % (sx, sy, fmt_moves(fr)) for sx, sy, fr in cases['M']]
    return lines


def mismatches(exe, cases):
    """Number of cases whose output differs from the models; the first few are described in self.detail."""
    out = run_lines(exe, lines_for(cases))
    pos, bad, detail = 0, 0, []

    def check(got, want, what):
        nonlocal bad
        if got != want:
            bad += 1
            if len(detail) < 8:
                detail.append('%s: got %r want %r' % (what, got, want))

    for u in cases['K']:
        check(out[pos], 'K %d %s' % (u, '--' if u not in KEYS else '%02X' % KEYS[u]), 'key map %d' % u)
        pos += 1
    for ev in cases['E']:
        presses, flush = model_keyboard(ev)
        check(out[pos], 'E ' + ' '.join(presses) + ' F' + ''.join(' ' + f for f in flush), 'keyboard %r' % (ev,))
        pos += 1
    for arrows, ev in cases['J']:
        check(out[pos], 'J ' + ' '.join(model_joykeys(ev, arrows)), 'joykeys %r' % (ev,))
        pos += 1
    for b in cases['D']:
        enc = joy_encode_model(b)
        check(out[pos], 'D %x %x %x' % (b, enc, b & 0xF), 'joyEncode %x' % b)
        pos += 1
    for x, y in cases['P']:
        check(out[pos], 'P %x' % pad_model(x, y), 'pad %d %d' % (x, y))
        pos += 1
    for sx, sy, fr in cases['M']:
        want = model_mouse(sx, sy, fr)
        got = out[pos:pos + len(want)]
        check(got, want, 'mouse %x %x %s' % (sx, sy, fmt_moves(fr)[:80]))
        pos += len(want)
    mismatches.detail = detail
    return bad


@unittest.skipUnless(CXX, 'clang++ not found')
class PcInput(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.cases = make_cases()
        cls.base = build_driver(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_models_agree_with_the_game_decoders(self):
        """The model of JOYxDAT is the game's decoder (ms::joyDirections) seen from the other side: every encoded stick position decodes back."""
        for b in range(16):
            self.assertEqual(joy_decode(joy_encode_model(b)), b)

    def test_key_map_is_the_amiga_keyboard(self):
        self.assertLess(max(KEYS.values()), 0x68)
        self.assertEqual(KEYS[HID['Up']], 0x4C)
        self.assertEqual(KEYS[HID['Right']], 0x4E)
        self.assertEqual(KEYS[HID['Space']], 0x40)
        self.assertEqual(KEYS[HID['Enter']], 0x44)

    def test_everything_matches_the_models(self):
        self.assertEqual(mismatches(self.base, self.cases), 0, '\n'.join(getattr(mismatches, 'detail', [])))

    def test_codes_the_coop_key_sets_expect(self):
        """The co-op keyboard set A of engine/pad.hpp reads the translated keys of the cursor block and Ctrl / Right Shift: the raw codes
        it documents ($4C $4D $4F $4E, Ctrl $63, RShift $61) are what this map delivers."""
        self.assertEqual([KEYS[HID[n]] for n in ('Up', 'Down', 'Left', 'Right')], [0x4C, 0x4D, 0x4F, 0x4E])
        self.assertEqual((KEYS[HID['LCtrl']], KEYS[HID['RCtrl']], KEYS[HID['RShift']]), (0x63, 0x63, 0x61))
        self.assertEqual([KEYS[HID[n]] for n in ('W', 'S', 'A', 'D')], [0x11, 0x21, 0x20, 0x22])      # set B, engine/pad.hpp
        self.assertEqual((KEYS[HID['LAlt']], KEYS[HID['RAlt']]), (0x64, 0x65))

    def test_key_map_agrees_with_ace_constants_when_ace_is_near(self):
        header = None
        for base in (ROOT, os.path.dirname(ROOT), os.path.join(ROOT, '..', '..'), os.path.join(ROOT, '..', '..', '..')):
            p = os.path.normpath(os.path.join(base, 'ace', 'include', 'ace', 'managers', 'key.h'))
            if os.path.isfile(p):
                header = p
                break
        if header is None:
            self.skipTest('no ACE checkout near the project')
        consts = {}
        with open(header, encoding='utf-8') as fh:
            for m in re.finditer(r'#define\s+KEY_(\w+)\s+0x([0-9A-Fa-f]+)', fh.read()):
                consts[m.group(1)] = int(m.group(2), 16)
        names = {'F%d' % i: 'F%d' % i for i in range(1, 11)}
        names.update({c: c for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'})
        names.update({c: c for c in '0123456789'})
        names.update({'Up': 'UP', 'Down': 'DOWN', 'Left': 'LEFT', 'Right': 'RIGHT', 'Space': 'SPACE', 'Enter': 'RETURN', 'Escape': 'ESCAPE',
                      'Tab': 'TAB', 'Backspace': 'BACKSPACE', 'Delete': 'DEL', 'Grave': 'ACCENT', 'Minus': 'MINUS', 'Equals': 'EQUALS',
                      'Backslash': 'BACKSLASH', 'LBracket': 'LBRACKET', 'RBracket': 'RBRACKET', 'Semicolon': 'SEMICOLON', 'Quote': 'APOSTROPHE',
                      'Comma': 'COMMA', 'Period': 'PERIOD', 'Slash': 'SLASH', 'LShift': 'LSHIFT', 'RShift': 'RSHIFT', 'LCtrl': 'CONTROL',
                      'CapsLock': 'CAPSLOCK', 'Help': 'HELP', 'KPEnter': 'NUMENTER', 'KPPeriod': 'NUMPERIOD', 'LAlt': 'LALT', 'LGUI': 'LAMIGA',
                      'RGUI': 'RAMIGA', 'NonUSHash': 'REGIONAL1', 'NonUSBackslash': 'REGIONAL2'})
        for i in range(10):
            names['KP%d' % i if i else 'KP0'] = 'NUM%d' % i
        for host, ace in names.items():
            self.assertIn(ace, consts, ace)
            self.assertEqual(KEYS[HID[host]], consts[ace], '%s vs KEY_%s' % (host, ace))

    MUTATIONS = [
        ('cursor up is the down key', '{82, 0x4C}', '{82, 0x4D}'),
        ('Right Ctrl unmapped', '{228, 0x63}, ', ''),
        ('letter A on the wrong key', '{4, 0x20}', '{4, 0x21}'),
        ('keypad 7 moved', '{95, 0x3D}', '{95, 0x3C}'),
        ('NumLock is not (', '{83, 0x5A}', '{83, 0x5B}'),
        ('releases of unseen keys pass', 'if(isDown == isHeld) {', 'if(isDown && isHeld) {'),
        ('OS key repeat passes', 'if(isDown == isHeld) {', 'if(!isDown && !isHeld) {'),
        ('second Ctrl press passes', 'return sState.aubCount[ubRaw]++ == 0 ? KEY_PRESS : KEY_IGNORE;', 'return ++sState.aubCount[ubRaw] ? KEY_PRESS : KEY_IGNORE;'),
        ('first Ctrl release passes', 'return --sState.aubCount[ubRaw] == 0 ? KEY_RELEASE : KEY_IGNORE;', 'return --sState.aubCount[ubRaw] >= 0 ? KEY_RELEASE : KEY_IGNORE;'),
        ('focus loss forgets the second Ctrl', 'if(--sState.aubCount[ubRaw] == 0) {\n\t\t\t*pubRaw = ubRaw;', 'if(--sState.aubCount[ubRaw] >= 0) {\n\t\t\t*pubRaw = ubRaw;'),
        ('keyboard fire key is Return', 'kUsageSpace}};', '40}};'),
        ('arrows up and down swapped', '{{kUsageUp, kUsageDown,', '{{kUsageDown, kUsageUp,'),
        ('left and right do not cancel', '(uwBits & (kJoyLeft | kJoyRight)) == (kJoyLeft | kJoyRight)', '(uwBits & (kJoyLeft | kJoyRight)) == 0x7F'),
        ('up and down do not cancel', '(uwBits & (kJoyUp | kJoyDown)) == (kJoyUp | kJoyDown)', '(uwBits & (kJoyUp | kJoyDown)) == 0x7F'),
        ('second fire key ignored', '((1u << JK_FIRE) | (1u << JK_FIRE2))', '(1u << JK_FIRE)'),
        ('joyEncode down bit', '(uwRight ^ uwDown)', 'uwDown'),
        ('joyEncode up bit', '((uwLeft ^ uwUp) << 8)', '(uwUp << 8)'),
        ('joyEncode left bit', '(uwLeft << 9)', '(uwLeft << 8)'),
        ('pad dead zone inclusive', 'if(wX > kPadDeadzone)', 'if(wX >= kPadDeadzone)'),
        ('pad up and down swapped', 'if(wY < -kPadDeadzone) uwBits |= kJoyUp;', 'if(wY < -kPadDeadzone) uwBits |= kJoyDown;'),
        ('mouse rounds toward minus infinity', 'int32_t lCounts = lFrac / 65536;', 'int32_t lCounts = lFrac >> 16;'),
        ('mouse remainder lost', 'lFrac -= lCounts * 65536;', 'lFrac = 0;'),
        ('mouse remainder kept twice', 'lFrac -= lCounts * 65536;', 'lFrac -= lCounts * 65535;'),
        ('mouse frame limit missing', 'if(lFrame + lCounts > kMouseMaxPerFrame) {', 'if(false) {'),
        ('mouse lower frame limit missing', 'else if(lFrame + lCounts < -kMouseMaxPerFrame) {', 'else if(false) {'),
        ('mouse frame limit not per frame', 'sMouse.lFrameX = sMouse.lFrameY = 0;\n}\n\nvoid mouseMove', 'sMouse.lFrameX = 0;\n}\n\nvoid mouseMove'),
        ('mouse axes swapped', 'sMouse.ubH = (uint8_t)(sMouse.ubH + axisCounts(sMouse.lFracX, sMouse.lFrameX, lDx, lScaleX16));',
         'sMouse.ubH = (uint8_t)(sMouse.ubH + axisCounts(sMouse.lFracX, sMouse.lFrameX, lDy, lScaleX16));'),
    ]

    def test_unmutated_build_is_clean(self):
        self.assertEqual(mismatches(self.base, self.cases), 0)

    def test_every_mutation_is_caught(self):
        survivors = []
        for i, (name, old, new) in enumerate(self.MUTATIONS):
            exe = build_driver(self.tmp.name, 'mut%d' % i, (old, new))
            try:
                caught = mismatches(exe, self.cases) > 0
            except (subprocess.TimeoutExpired, subprocess.CalledProcessError, IndexError):
                caught = True            # hangs, crashes or wrong output length are caught too
            if not caught:
                survivors.append(name)
        self.assertEqual(survivors, [], 'mutations the tests do not notice')


# ---------------------------------------------------------------------------------------------------------------------------------
# The PC executable: scripted input reaches the platform input state.
# ---------------------------------------------------------------------------------------------------------------------------------
BUILD = os.path.join(ROOT, 'build-sdl')

# script token -> list of (frame offset, kind, arg); kinds: 'key' (usage, down), 'repeat' (usage), 'mouse' (dx, dy), 'lmb', 'rmb'
NAMES = {'Right': 79, 'Left': 80, 'Up': 82, 'Down': 81, 'Space': 44, 'Left_Ctrl': 224, 'Right_Ctrl': 228, 'Left_Shift': 225, 'A': 4, 'Return': 40,
         'Escape': 41, 'F1': 58}


def model_script(script, frames):
    """-> (expected key lines, expected port lines) of `--smoke frames --keys script`."""
    events = {}
    at = 1

    def add(frame, ev):
        events.setdefault(frame, []).append(ev)

    for tok in script.split():
        if tok.startswith('@'):
            at += int(tok[1:])
        elif tok in ('lmb+', 'lmb-', 'rmb+', 'rmb-'):
            add(at, ('lmb' if tok[0] == 'l' else 'rmb', tok[3] == '+'))
        elif tok.startswith('mouse='):
            dx, dy = (int(v) for v in tok[6:].split(','))
            add(at, ('mouse', dx, dy))
        elif tok[0] == '+':
            add(at, ('key', NAMES[tok[1:]], True))
        elif tok[0] == '-':
            add(at, ('key', NAMES[tok[1:]], False))
        elif tok[0] == '~':
            add(at, ('repeat', NAMES[tok[1:]]))
        else:
            add(at, ('key', NAMES[tok], True))
            add(at + 1, ('key', NAMES[tok], False))
    held, count = set(), {}
    keys, ports = [], []
    pressed = set()          # host keys currently down for the joystick (arrows + fire keys)
    h = v = 0
    lmb = rmb = False
    last = None
    for frame in range(1, frames + 1):
        for ev in events.get(frame, []):
            if ev[0] == 'key':
                _, usage, down = ev
                if usage in ARROWS:
                    (pressed.add if down else pressed.discard)(usage)
                presses, _ = model_keyboard_step(held, count, usage, down)
                if presses:
                    keys.append((frame, presses))
            elif ev[0] == 'repeat':
                pass                                     # dropped before the state machine
            elif ev[0] == 'mouse':
                h = (h + ev[1]) & 0xFF
                v = (v + ev[2]) & 0xFF
            elif ev[0] == 'lmb':
                lmb = ev[1]
            else:
                rmb = ev[1]
        names = {ARROWS[u] for u in pressed}
        bits = 0
        bits |= JOY_UP if ('up' in names and 'down' not in names) else 0
        bits |= JOY_DOWN if ('down' in names and 'up' not in names) else 0
        bits |= JOY_LEFT if ('left' in names and 'right' not in names) else 0
        bits |= JOY_RIGHT if ('right' in names and 'left' not in names) else 0
        fire1 = 'fire' in names
        state = ((v << 8) | h, joy_encode_model(bits), lmb, fire1, rmb)
        if state != last:
            ports.append((frame, state))
            last = state
    kl = ['input: frame %d key %s 0x%02X' % (f, 'down' if d else 'up', r) for f, (d, r) in keys]
    pl = ['input: frame %d ports joy0=0x%04X joy1=0x%04X fire0=%d fire1=%d right0=%d right1=0' % (f, s[0], s[1], s[2], s[3], s[4]) for f, s in ports]
    return kl, pl


def model_keyboard_step(held, count, usage, down):
    raw = KEYS.get(usage)
    if raw is None:
        return None, None
    if down:
        if usage in held:
            return None, None
        held.add(usage)
        count[raw] = count.get(raw, 0) + 1
        return ((True, raw) if count[raw] == 1 else None), None
    if usage not in held:
        return None, None
    held.discard(usage)
    count[raw] -= 1
    return ((False, raw) if count[raw] == 0 else None), None


def sdl_exe():
    p = os.path.join(BUILD, 'moonstone.exe')
    return p if os.path.isfile(p) else None


@unittest.skipUnless(sdl_exe(), 'build-sdl/moonstone.exe missing (py -m unittest tests.test_sdl_build builds it)')
class ScriptedInput(unittest.TestCase):
    def run_exe(self, *args):
        return subprocess.run([sdl_exe(), *args], cwd=BUILD, capture_output=True, text=True, timeout=120)

    def check_script(self, script, frames=12):
        r = self.run_exe('--smoke', str(frames), '--fast', '--keys', script)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        keys, ports = model_script(script, frames)
        got = [l for l in r.stderr.splitlines() if l.startswith('input: frame')]
        got_keys = [l for l in got if ' key ' in l]
        got_ports = [l for l in got if ' ports ' in l]
        return got_keys, got_ports, keys, ports

    def test_keys_reach_the_platform_at_the_predicted_frames(self):
        script = '@2 Right ~Right +Left_Ctrl +Right_Ctrl -Left_Ctrl @2 -Right_Ctrl Space mouse=3,-2 lmb+ @1 lmb- rmb+ rmb- A F1'
        gk, gp, wk, wp = self.check_script(script)
        self.assertEqual(gk, wk)
        self.assertEqual(gp, wp)
        self.assertGreater(len(wk), 8)

    def test_joystick_follows_the_cursor_keys_and_fire(self):
        script = '+Up +Right @1 +Down -Up @1 +Left @1 -Right -Down -Left Right_Ctrl Space'
        gk, gp, wk, wp = self.check_script(script)
        self.assertEqual(gk, wk)
        self.assertEqual(gp, wp)

    def test_opposite_directions_cancel(self):
        gk, gp, wk, wp = self.check_script('+Left +Right @2 -Left @2 -Right')
        self.assertEqual(gp, wp)
        self.assertEqual(len(gp), 3, gp)                     # frame 1: both down, nothing; 3: Right alone; 5: nothing
        self.assertIn('joy1=0x0003', gp[1])

    def test_mouse_counters_wrap(self):
        script = 'mouse=100,100 @1 mouse=100,100 @1 mouse=100,100 @1 mouse=-100,-100 @1 mouse=-100,-100'
        gk, gp, wk, wp = self.check_script(script)
        self.assertEqual(gp, wp)

    def test_joykeys_none_leaves_port_1_alone(self):
        r = self.run_exe('--smoke', '6', '--fast', '--keys', 'Right Up Space', '--joykeys', 'none')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        ports = [l for l in r.stderr.splitlines() if ' ports ' in l]
        self.assertEqual(len(ports), 1, ports)                       # only the first sample: nothing ever changed
        self.assertIn('key down 0x4E', r.stderr)                     # the keys themselves still reach the game

    def test_unknown_key_name_is_a_usage_error(self):
        r = self.run_exe('--smoke', '3', '--keys', 'NoSuchKey')
        self.assertEqual(r.returncode, 64)
        self.assertIn('unknown key name', r.stderr)

    def test_scripted_run_is_deterministic(self):
        a = self.run_exe('--smoke', '10', '--fast', '--keys', '@1 Right mouse=5,5 @2 Space')
        b = self.run_exe('--smoke', '10', '--fast', '--keys', '@1 Right mouse=5,5 @2 Space')
        pick = lambda r: [l for l in r.stderr.splitlines() if l.startswith('input:')]
        self.assertEqual(pick(a), pick(b))
        self.assertTrue(pick(a))


if __name__ == '__main__':
    unittest.main()
