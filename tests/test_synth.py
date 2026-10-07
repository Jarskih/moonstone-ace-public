"""Tests for mog's synth in C++ (ROADMAP 7.1g): src/engine/synth.cpp (pure), src/engine/synth_data.cpp (generated), src/rt/synth.cpp.

The acceptance test is the chip-register write log. The ORIGINAL S_44 code (build/reasm/mog, run in unicorn on a fake Paula,
tests/synth_oracle.py) and the C++ port get the same script (init, sequence starts, VBL ticks, INT4 entries, fades) and must write
the same registers, with the same values, in the same order, operation by operation:

  * every sequence id 0..167 (all music and every sound effect the game can request: the game indexes LAB_1098 with exactly these)
    started on each of the four channels, 1500 ticks each (long tunes loop; effects end and fall silent),
  * random multi-voice scenarios: starts at random times on random channels, INT4 services with random pending/enable masks
    between the ticks, fade steps, ticks skipped by the lock,
  * the INT4 handler against random voice states, the instrument relocation (LAB_0FD4) for random bank buffers.
Part 1 runs the engine on the host (clang++); part 2 (needs m68k-amiga-elf-g++) runs the COMPILED m68k code of engine + rt shims in
unicorn next to the original. The generated table file must be current (tools/gen_synth_tables.py --check). One deliberate mutation
proves the comparison is sensitive.
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2a: the synth tables are generated from the original mog, not in the repository)
import os
import random
import shutil
import subprocess
import sys
import struct
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import synth_oracle as O  # noqa: E402

CXX = shutil.which('clang++')
HAVE = bool(CXX) and O.have_image()
BANKS = (0x400000, 0x440000, 0x480000, 0x4C0000, 0x500000)
SEQ_COUNT = 168
AUD_BITS = (0x80, 0x100, 0x200, 0x400)


def build_driver(tmp, src_edit=None):
    srcs = []
    for rel in ('src/engine/synth.cpp', 'src/engine/synth_data.cpp', 'tests/synth_driver.cpp'):
        p = origskip.synth_data_cpp() if rel.endswith('synth_data.cpp') else os.path.join(ROOT, rel)
        if src_edit and rel == 'src/engine/synth.cpp':
            with open(p, encoding='utf-8') as f:
                text = f.read()
            new = src_edit(text)
            assert new != text, 'mutation did not apply'
            p = os.path.join(tmp, 'synth_mut.cpp')
            with open(p, 'w', encoding='utf-8') as f:
                f.write(new)
        srcs.append(p)
    exe = os.path.join(tmp, 'driver_mut%d.exe' % build_driver.n if src_edit else 'driver.exe')
    build_driver.n += 1
    cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), *srcs, '-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr[-3000:])
    return exe


build_driver.n = 0


def run_cpp(exe, script):
    r = subprocess.run([exe], input=O.script_text(script), capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return O.parse_driver_output(r.stdout, len(script))


def first_diff(script, a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return 'op %d %r: original %r ... C++ %r' % (i, script[i], x[:10], y[:10])
    return None


def random_scenario(rng, wave_base, n_ops):
    sc = [('Z', wave_base, BANKS)]
    for _ in range(n_ops):
        k = rng.random()
        if k < 0.07:
            sc.append(('S', rng.randrange(SEQ_COUNT), rng.randrange(4)))
        elif k < 0.12:
            sc.append(('F', rng.choice((0, 1, 0xFFFF))))
        elif k < 0.14:
            sc.append(('K', rng.choice((0, 0xFF00, 1))))
            sc.append(('T',))
        elif k < 0.30:
            req = 0
            for b in AUD_BITS:
                if rng.random() < 0.4:
                    req |= b
            enar = 0
            for b in AUD_BITS:
                if rng.random() < 0.8:
                    enar |= b
            sc.append(('I', req | rng.choice((0, 0x20, 0x4000)), enar | rng.choice((0, 0x4000))))
        else:
            sc.append(('T',))
    return sc


# --------------------------------------------------------------------------------------------------------------------------
# synthetic sequences: the game's own 167 sequences use only part of the command set (no envelopes, loops, calls, sums or
# transpose adds: tools/ scan in the commit notes). To compare the whole interpreter, a scenario can replace the vibrato rows
# 0..3, 16 envelopes and all sequences of the original with random but well-formed ones ('P' and 'Q' operations).
# --------------------------------------------------------------------------------------------------------------------------
VIB, ENV, SEQ_AREA, SEQ_END = 5046, 5106, 5234, 8788


def synth_note(rng):
    return rng.randrange(8, 80)


def synth_body(rng, depth, leaf_ids, budget, net=None):
    """A well-formed command stream (no terminator). net[0] tracks the transposition added by $B8 since the last $BC."""
    out = bytearray()
    net = net if net is not None else [0]
    for _ in range(rng.randrange(3, budget)):
        k = rng.random()
        if k < 0.35:
            out.append(synth_note(rng))
        elif k < 0.43:
            out += bytes([0x8C, rng.randrange(1, 7)])
        elif k < 0.47:
            n = rng.randrange(1, 4)
            out += bytes([0x98, n] + [rng.randrange(1, 4) for _ in range(n)])
        elif k < 0.52:
            out += bytes([0x80, rng.randrange(0, 70)])
        elif k < 0.58:
            out += bytes([0xD0, rng.randrange(0, 131)])
        elif k < 0.64:
            out += bytes([0x9C, rng.randrange(0, 4)])
        elif k < 0.70:
            out += bytes([0xC8, rng.randrange(0, 16)])
        elif k < 0.72:
            out += bytes([0xCC])
        elif k < 0.76:
            out += bytes([0xA8, rng.randrange(0, 4)])
        elif k < 0.80 and depth == 0:
            v = rng.randrange(0, 9) - 4
            out += bytes([0xBC, v & 0xFF])
            net[0] = 0
        elif k < 0.82 and depth == 0:
            v = rng.randrange(1, 5) - 2
            if v:
                out += bytes([0xB8, v & 0xFF])
                net[0] += v
        elif k < 0.88 and depth < 2:
            out += bytes([0xC0, rng.randrange(0, 4)]) + synth_body(rng, depth + 1, [], 6, net) + bytes([0xC4])
        elif k < 0.93 and leaf_ids and depth == 0:
            out += bytes([0xB0, rng.choice(leaf_ids)])
        elif k < 0.95:
            out += bytes([rng.choice((0x84, 0xA0, 0xA4)), rng.randrange(256)])
        elif k < 0.97:
            out += bytes([0x90])
        else:
            out += bytes([0x94, rng.randrange(40, 250)])
    return out


def synthetic_setup(rng, nseq=40):
    """Operations that install random vibrato rows, envelopes and nseq sequences (ids 1..nseq; the last 6 are call leaves)."""
    ops = []
    for r in range(4):
        row = bytearray(rng.randrange(0, 6) for _ in range(15))
        for i in (5, 6, 7, 8, 9):
            row[i] = rng.choice((0, 1, 2, 0xFF, 0xFE, 3))
        ops.append(('P', VIB + 15 * r, bytes(row)))
    for e in range(16):
        ops.append(('P', ENV + 8 * e, bytes([rng.randrange(1, 14), rng.randrange(0, 5), rng.randrange(1, 14), rng.randrange(0, 5),
                                             rng.randrange(0, 50), rng.randrange(0, 20), rng.randrange(1, 10), rng.randrange(0, 4)])))
    leaves = list(range(nseq - 5, nseq + 1))
    pos = SEQ_AREA
    for sid in range(1, nseq + 1):
        b = bytearray([0x94, rng.randrange(60, 250), 0x8C, rng.randrange(1, 5), 0xD0, rng.randrange(0, 131), 0x80, rng.randrange(5, 60)])
        if sid in leaves:
            b += bytes([synth_note(rng), synth_note(rng), 0xB4])
        else:
            net = [0]
            b += synth_body(rng, 0, leaves, 24, net)
            b += bytes([0x8C, rng.randrange(1, 5), synth_note(rng)])   # every pass plays a note (the original would spin otherwise)
            if net[0]:
                b += bytes([0xB8, -net[0] & 0xFF])             # the restart must not drift the transposition
            b += rng.choice((bytes([0x88]), bytes([0xAC]), bytes([0xD4, rng.randrange(1, nseq - 5)]), bytes([0x88])))
        assert pos + len(b) < SEQ_END
        ops.append(('P', pos, bytes(b)))
        ops.append(('Q', sid, pos))
        pos += len(b)
    return ops


@unittest.skipUnless(HAVE, 'needs clang++, unicorn and build/reasm/mog')
class SynthHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='synth_')
        cls.exe = build_driver(cls.tmp)
        cls.orig = O.Original()

    def compare(self, script):
        self.orig.reset()
        a = self.orig.run(script)
        b = run_cpp(self.exe, script)
        self.assertIsNone(first_diff(script, a, b))
        return a

    def test_every_sequence_on_every_channel(self):
        total = 0
        for seq in range(SEQ_COUNT):
            for ch in range(4):
                script = [('Z', self.orig.wave_base, BANKS), ('S', seq, ch)] + [('T',)] * 1500
                a = self.compare(script)
                total += sum(len(x) for x in a)
        self.assertGreater(total, 1_000_000)      # the sequences really write a lot (not a vacuous pass)

    def test_music_and_effects_play_something(self):
        script = [('Z', self.orig.wave_base, BANKS), ('S', 0x6E, 0)] + [('T',)] * 300
        a = self.compare(script)
        vols = [e[2] for ops in a for e in ops if e[0] == 'w' and e[1] == 0xA8]
        self.assertGreater(max(vols), 0)

    def test_random_scenarios(self):
        for seed in range(40):
            rng = random.Random(1000 + seed)
            self.compare(random_scenario(rng, self.orig.wave_base, 1200))

    def test_four_voices_together(self):
        # the area music: sequences $6E..$71 on channels 0..3, then effects over it, as LAB_0133 and LAB_0AA2 do
        rng = random.Random(7)
        sc = [('Z', self.orig.wave_base, BANKS)] + [('S', 0x6E + c, c) for c in range(4)]
        for t in range(3000):
            sc.append(('T',))
            if t % 97 == 0:
                sc.append(('S', rng.randrange(1, SEQ_COUNT), rng.randrange(4)))
            if t % 5 == 0:
                sc.append(('I', rng.choice(AUD_BITS), 0x780))
        self.compare(sc)

    def test_int4_against_random_voice_states(self):
        rng = random.Random(5)
        for case in range(60):
            seq = rng.randrange(1, SEQ_COUNT)
            sc = [('Z', self.orig.wave_base, BANKS)]
            for c in range(4):
                sc.append(('S', rng.randrange(1, SEQ_COUNT), c))
            for _ in range(rng.randrange(1, 40)):
                sc.append(('T',))
            for _ in range(30):
                req = sum(b for b in AUD_BITS if rng.random() < 0.5)
                sc.append(('I', req, rng.choice((0x780, 0x780, 0x080, 0x100, 0x200, 0x400, 0))))
                if rng.random() < 0.5:
                    sc.append(('T',))
            self.compare(sc)

    def test_synthetic_sequences_cover_the_whole_interpreter(self):
        wb = self.orig.wave_base
        for seed in range(30):
            rng = random.Random(500 + seed)
            sc = [('Z', wb, BANKS)] + synthetic_setup(rng)
            for c in range(4):
                sc.append(('S', rng.randrange(1, 35), c))
            sc += random_scenario(rng, wb, 1500)[1:]       # (its 'Z' dropped: the setup must survive)
            sc = [o if o[0] != 'S' else ('S', 1 + o[1] % 34, o[2]) for o in sc]      # not 0 (null), not the call leaves
            with self.subTest(seed=seed):
                self.compare(sc)

    def test_synthetic_sequences_use_every_command(self):
        # sanity of the generator itself: the streams contain each command at least once over the seeds
        seen = set()
        for seed in range(30):
            for op in synthetic_setup(random.Random(500 + seed)):
                if op[0] == 'P' and op[1] >= SEQ_AREA:
                    seen |= {b for b in op[2] if b >= 0x80}
        for cmd in (0x80, 0x88, 0x8C, 0x94, 0x98, 0x9C, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD4):
            self.assertIn(cmd, seen)

    def test_fade_and_lock(self):
        sc = [('Z', self.orig.wave_base, BANKS)] + [('S', 0x6E + c, c) for c in range(4)] + [('T',)] * 30
        sc += [('F', 1)] + [('T',)] * 10 + [('K', 0xFF00)] + [('T',)] * 3 + [('K', 0)] + [('T',)] * 5 + [('F', 0)] + [('T',)] * 10
        self.compare(sc)

    def test_instrument_relocation(self):
        rng = random.Random(11)
        for _ in range(10):
            banks = tuple(rng.randrange(0x100000, 0xF00000) & ~1 for _ in range(5))
            sc = [('Z', self.orig.wave_base, banks), ('D',)]
            a = self.compare(sc)
            self.assertEqual(len(a[1][0][1]), 131)

    def test_tables_are_current(self):
        # ROADMAP 10.2a: the tables are generated into the build (never committed); --check against the copy this run made
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gen_synth_tables.py'), '--check', '--out',
                            origskip.synth_data_cpp()], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_deliberate_mutation_is_detected(self):
        # (1) the start quirk DMACON <- $0002 becomes $0001: every real sequence start differs
        exe = build_driver(self.tmp, lambda t: t.replace('w16(hw, kSynthRegDmacon, 0x0002);', 'w16(hw, kSynthRegDmacon, 0x0001);'))
        script = [('Z', self.orig.wave_base, BANKS), ('S', 1, 0)] + [('T',)] * 10
        self.orig.reset()
        self.assertIsNotNone(first_diff(script, self.orig.run(script), run_cpp(exe, script)))
        # (2) the vibrato step uses the sign-extended byte: the real tunes (vibrato rows) notice
        exe = build_driver(self.tmp, lambda t: t.replace('(int16_t)(int8_t)v.auwRow[7 + j]', '(int16_t)(int8_t)v.auwRow[7 + j] + 1'))
        found = 0
        for seq in range(1, SEQ_COUNT):
            script = [('Z', self.orig.wave_base, BANKS), ('S', seq, seq % 4)] + [('T',)] * 200
            self.orig.reset()
            if first_diff(script, self.orig.run(script), run_cpp(exe, script)):
                found += 1
        self.assertGreater(found, 0)
        # (3) envelope attack clamp >= -> >: no real sequence uses an envelope, the synthetic scenarios do
        exe = build_driver(self.tmp, lambda t: t.replace('if(d0 >= v.uwBaseVol) {', 'if(d0 > v.uwBaseVol) {'))
        found = 0
        for seed in range(30):
            rng = random.Random(500 + seed)
            sc = [('Z', self.orig.wave_base, BANKS)] + synthetic_setup(rng) + [('S', 1 + seed % 30, seed % 4)] + [('T',)] * 600
            self.orig.reset()
            if first_diff(sc, self.orig.run(sc), run_cpp(exe, sc)):
                found += 1
        self.assertGreater(found, 0)


# --------------------------------------------------------------------------------------------------------------------------
# part 2: the compiled m68k code (engine + generated tables + rt shims, m68k-amiga-elf-g++ -m68020) in unicorn, entered through the
# ORIGINAL entry labels of the mog image after the five entry patches of asm/patches/mog.synth.json were applied (JMP rt_synth_*).
# --------------------------------------------------------------------------------------------------------------------------
try:
    import test_creatures_emu as E  # noqa: E402
    if not os.path.isdir(E.ACE):         # a git worktree has no sibling ace/: the main checkout's is three levels up
        alt = os.path.normpath(os.path.join(ROOT, '..', '..', '..', 'ace', 'include'))
        if os.path.isdir(alt):
            E.ACE = alt
    HAVE_M68K = bool(HAVE and E.HAVE_UC and E.GXX and E.LD and E.OBJCOPY and E.NM and os.path.isdir(E.ACE) and
                     os.path.isdir(E.GCC_SUPPORT) and os.path.exists(E.MOG_EXE))
except Exception:          # pragma: no cover
    HAVE_M68K = False

_BLOBS = {}
BLOB_SIZE = 0x20000


def build_blob(asm_mode):
    """Compile + link the port flat at E.BLOB_BASE. Returns (bytes, symbols)."""
    if asm_mode in _BLOBS:
        return _BLOBS[asm_mode]
    tmp = tempfile.mkdtemp(prefix='synth_m68k_')
    env = dict(os.environ, PATH=os.path.dirname(E.GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
             '-std=c++17', '-O1', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1', '-DMS_SYNTH_STANDALONE=1', '-DMS_SYNTH_ASM=%d' % asm_mode, '-ffunction-sections',
             '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(E.ACE, 'mini_std'), '-I', E.ACE,
             '-I', E.GCC_SUPPORT]
    objs = []
    for i, src in enumerate(('src/engine/synth.cpp', 'src/engine/synth_data.cpp', 'src/rt/synth.cpp', 'tests/synth_emu_support.cpp')):
        o = os.path.join(tmp, '%d.o' % i)
        sp = origskip.synth_data_cpp() if src.endswith('synth_data.cpp') else os.path.join(ROOT, src)
        r = subprocess.run([E.GXX] + flags + ['-c', sp, '-o', o], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-3000:]))
        objs.append(o)
    cmd = [E.LD, '-Ttext=0x%X' % E.BLOB_BASE, '-e', 'rt_synth_init']
    for lab in ('0FC4', '05C7', '05C8', '05C9', '05CA', '05CB', '0FCA', '0F89', '10A3', '0FD4', '0F8C', '0F73', '0FC2'):
        cmd.append('--defsym=mog_LAB_%s=%d' % (lab, E.TC.A(lab)))
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import cellnames  # ROADMAP 7.1s: the cell names of tools/cell_names.yaml resolve like their labels
        named = cellnames.symbol('mog', 'LAB_' + lab)
        if named != 'mog_LAB_' + lab:
            cmd.append('--defsym=%s=%d' % (named, E.TC.A(lab)))
    elf = os.path.join(tmp, 'blob.elf')
    r = subprocess.run(cmd + ['-o', elf] + objs, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError('link failed: ' + r.stderr[-3000:])
    binf = os.path.join(tmp, 'blob.bin')
    subprocess.run([E.OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
    syms = {}
    for ln in subprocess.run([E.NM, elf], capture_output=True, text=True, env=env).stdout.split(chr(10)):
        f = ln.split()
        if len(f) == 3:
            syms[f[2]] = int(f[0], 16)
    with open(binf, 'rb') as f:
        blob = f.read()
    assert len(blob) < BLOB_SIZE - 0x4000, len(blob)
    _BLOBS[asm_mode] = (blob, syms, objs, tmp)
    return _BLOBS[asm_mode]


def port_script(script):
    return [o for o in script if o[0] not in ('K', 'D')]


@unittest.skipUnless(HAVE_M68K, 'needs unicorn, the m68k toolchain and build/reasm/mog')
class SynthM68kTest(unittest.TestCase):
    asm_mode = 0

    @classmethod
    def setUpClass(cls):
        blob, syms, cls.objs, cls.tmp = build_blob(cls.asm_mode)
        cls.port = O.Port(blob, E.BLOB_BASE, BLOB_SIZE, syms, asm_mode=bool(cls.asm_mode))
        cls.orig = O.Original()

    def compare(self, script):
        script = port_script(script)
        self.orig.reset()
        self.port.reset()
        a = self.orig.run(script)
        b = self.port.run(script)
        self.assertIsNone(first_diff(script, a, b))
        return a

    def test_exports(self):
        for sym in ('rt_synth_init', 'rt_synth_reloc', 'rt_synth_start', 'rt_synth_tick', 'rt_synth_fade', 'rt_synth_int4',
                    'rtSynthTickC', '_ZN2ms10kSynthWaveE'):
            self.assertIn(sym, self.port.syms)
        for lab, shim in O.ENTRY_PATCHES:        # the patched entry is a JMP to the shim (the asm/patches/mog.synth.json encoding)
            raw = bytes(self.port.rig.uc.mem_read(self.port.addr(lab), 6))
            self.assertEqual(raw, bytes.fromhex('4EF9') + struct.pack('>I', self.port.syms[shim]))

    def test_the_compiled_code_is_what_runs(self):
        # sensitivity: turn the tick shim into `moveq #0,d0 ; rts` inside the blob; the write log must then differ from the original's
        script = [('Z', self.orig.wave_base, BANKS), ('S', 1, 0)] + [('T',)] * 20
        self.orig.reset()
        a = self.orig.run(script)
        self.port.reset()
        self.port.rig.uc.mem_write(self.port.syms['rt_synth_tick'], bytes.fromhex('70004E75'))
        self.port.rig.uc.ctl_flush_tb()
        self.assertIsNotNone(first_diff(script, a, self.port.run(script)))
        self.port.reset()

    def test_every_sequence(self):
        for seq in range(SEQ_COUNT):
            script = [('Z', self.orig.wave_base, BANKS), ('S', seq, seq % 4)] + [('T',)] * 700
            self.compare(script)

    def test_random_scenarios(self):
        for seed in range(15):
            self.compare(random_scenario(random.Random(2000 + seed), self.orig.wave_base, 1000))

    def test_synthetic_sequences(self):
        wb = self.orig.wave_base
        for seed in range(10):
            rng = random.Random(900 + seed)
            sc = [('Z', wb, BANKS)] + synthetic_setup(rng)
            for c in range(4):
                sc.append(('S', rng.randrange(1, 35), c))
            sc += random_scenario(rng, wb, 1000)[1:]
            self.compare([o if o[0] != 'S' else ('S', 1 + o[1] % 34, o[2]) for o in sc])

    def test_four_voice_music_with_fade(self):
        sc = [('Z', self.orig.wave_base, BANKS)] + [('S', 0x6E + c, c) for c in range(4)] + [('T',)] * 200
        sc += [('F', 1)] + [('T',)] * 20 + [('I', 0x780, 0x780)] * 3 + [('F', 0)] + [('T',)] * 20
        self.compare(sc)


@unittest.skipUnless(HAVE_M68K, 'needs unicorn, the m68k toolchain and build/reasm/mog')
class SynthM68kAsmModeTest(SynthM68kTest):
    """MS_SYNTH_ASM=1: the shims replay the replaced instruction and the ORIGINAL code runs; the log must be the original's."""
    asm_mode = 1

    def test_exports(self):
        for sym in ('rt_synth_init', 'rt_synth_reloc', 'rt_synth_start', 'rt_synth_tick', 'rt_synth_fade'):
            self.assertIn(sym, self.port.syms)
        self.assertNotIn('rtSynthTickC', self.port.syms)       # no C++ synth is linked in this mode
        for lab, shim in O.ENTRY_PATCHES:
            raw = bytes(self.port.rig.uc.mem_read(self.port.addr(lab), 6))
            self.assertEqual(raw, bytes.fromhex('4EF9') + struct.pack('>I', self.port.syms[shim]))

    def test_the_compiled_code_is_what_runs(self):
        pass


if __name__ == '__main__':
    unittest.main()
