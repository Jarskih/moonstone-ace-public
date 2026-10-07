"""Emulator check of the rt shims of src/rt/fighters.cpp (ROADMAP 6.4a): the m68k code the game build links (src/rt/fighters.cpp
and src/game/fighters.cpp, with src/game/creatures.cpp and src/engine/util.cpp they call, compiled with m68k-amiga-elf-g++
-m68020 and linked flat at a fixed address) runs in unicorn next to the ORIGINAL handler of the reassembled mog image, on the
same memory and the same registers.  Per handler (LAB_01CA, 0226, 0236, 0251, 027A, 0298, 029F, 02CB, 02D2) and per sound
picker (LAB_02DC..LAB_02E9) every byte of the game memory must agree afterwards (except the scratch cells the C++ does not keep
and the stack), the result registers (A0 = next script, D0.w / D1.w / D2.w = x / y / z, D3.b = facing) must be the original's,
and the shim may only change registers the original changes.

What tests/test_fighters.py cannot show: that the overlay structs (FightVars, MoveVars) and the script addresses of the rt
environment sit on the right symbols, the register marshalling of rt_fighter_* / rt_sound_*, the trampolines to the asm
callees (joystick LAB_00EE, wall probe LAB_0A71, sound LAB_0AA2, copper effect LAB_0427) and the code generation for the 68020.
The four callees are replaced in BOTH runs by the same 68000 stubs (patched into the image): LAB_00EE returns the case's two
joystick words, LAB_0A71 clears the bits of a constant mask in +63, LAB_0AA2 / LAB_0427 count their calls and keep the last
sound id in the test arena (the flyer's VHPOSR read, a custom chip access the harness refuses, is left to the host test).

ROADMAP 7.1h: the same for the four S_40 handlers (SECSTRT_40, LAB_0ED2, LAB_0EFF, LAB_0EC2: shims rt_fighter_snatcher /
demon / ai_knight / stalker) and for every routine the combat scripts call through opcode $B0 (rtFightOpRun on the tag, the
asm routine at its label with A1 = owner, A2 = frames, D0..D3): the fixed-channel sound starts (SECSTRT_16, LAB_0A9B..0A9D)
count like LAB_0AA2, the death fade (LAB_0D8A / LAB_03EE) counts like LAB_0427.

Needs: unicorn, the m68k toolchain of AGENTS.md, build/reasm/mog (py tools/reassemble.py).  Skipped otherwise.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
sys.path.insert(0, HERE)
import test_creatures_emu as E  # noqa: E402  (module import only)
import test_fighters as F  # noqa: E402
from test_mainloop import RT_LABELS  # noqa: E402  (7.1o: rt entry -> label whose oracle routine it is)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

TC = E.TC
A = TC.A
LOG = 0x19F000                         # test arena: call counters of the stubs (inside the DATA region of the harness)
LOG_SOUNDS, LOG_LAST, LOG_FX = LOG, LOG + 2, LOG + 4

SOURCES = ('src/game/creatures.cpp', 'src/game/fighters.cpp', 'src/game/fight_creatures.cpp', 'src/game/fight_ops.cpp',
           'src/rt/fighters.cpp', 'src/engine/util.cpp', 'src/game/rules/damage.cpp') + tuple('src/game/rules/' + n + '.cpp' for n in ['ai_fight', 'ai_fight_flyer', 'ai_fight_snatcher', 'ai_fight_demon', 'ai_fight_knight', 'ai_fight_dragon', 'ai_fight_brawler', 'ai_fight_caster', 'ai_fight_drake', 'ai_fight_stalker']) + (GAMEDATA_REL,)
SHIMS = {'01CA': 'rt_fighter_knight', '0226': 'rt_fighter_flyer', '0236': 'rt_fighter_brawler', '0251': 'rt_fighter_caster',
         '027A': 'rt_fighter_dragon', '0298': 'rt_fighter_dragon_part', '029F': 'rt_fighter_drake', '02CB': 'rt_fighter_dagger',
         '02D2': 'rt_fighter_idle',
         'T04': 'rt_fighter_snatcher', 'T08': 'rt_fighter_demon', 'T10': 'rt_fighter_ai_knight', 'T40': 'rt_fighter_stalker'}
NEW_LABEL = {'T04': 'SECSTRT_40', 'T08': 'LAB_0ED2', 'T10': 'LAB_0EFF', 'T40': 'LAB_0EC2'}   # the original handler of each case key
IGN = [(A('0A52'), 2), (A('0A53'), 2), (A('0A54'), 2), (A('0A56'), 2), (A('0635'), 2), (A('0636'), 2), (A('0637'), 4),
       (A('03B6'), 2), (A('01D8'), 2), (A('01EB'), 2), (A('0295'), 4), (A('062F'), 2), (A('0630'), 2), (A('0A77'), 2),
       (A('0A78'), 2)]

_BLOB = None

# The register entries of the nine handlers (A0 = owner, A1 = handler-table entry -> the register contract of the original handler).
# They used to live in src/rt/fighters.cpp (rt_fighter_*, rtFighterEntry); nothing in the game calls them (the creature dispatcher
# calls rtFighterRun), so ROADMAP 7.1f2 moved them here, into the test's own link unit.
# ROADMAP 7.1p: the handler identities are the bytes of rtFighterTag (src/rt/fighters.cpp) where the original has the labels of the handler bodies
TAG_INDEX = {'02D2': 0, '01CA': 1, '0226': 2, '0236': 3, '0251': 4, '027A': 5, '0298': 6, '029F': 7, '02CB': 8, 'SECSTRT_40': 9, 'LAB_0ED2': 10,
             'LAB_0EFF': 11, 'LAB_0EC2': 12}
SHIM_ASM = '''
asm(R"(
	.text
	.globl rtFighterEntry
rtFighterEntry:
	lea -12(%sp),%sp
	pea 0(%sp)
	move.l %a0,-(%sp)
	move.l %a1,-(%sp)
	jsr rtFighterRun
	lea 12(%sp),%sp
	move.l 0(%sp),%a0
	move.w 4(%sp),%d0
	move.w 6(%sp),%d1
	move.w 8(%sp),%d2
	move.b 10(%sp),%d3
	lea 12(%sp),%sp
	rts
''' + ''.join('''
	.globl %s
%s:
	lea rtFighterTag+%d,%%a1
	bra rtFighterEntry
''' % (name, name, TAG_INDEX[lab]) for lab, name in (('01CA', 'rt_fighter_knight'), ('0226', 'rt_fighter_flyer'), ('0236', 'rt_fighter_brawler'),
                                          ('0251', 'rt_fighter_caster'), ('027A', 'rt_fighter_dragon'),
                                          ('0298', 'rt_fighter_dragon_part'), ('029F', 'rt_fighter_drake'),
                                          ('02CB', 'rt_fighter_dagger'), ('02D2', 'rt_fighter_idle'))) + ''.join('''
	.globl %s
%s:
	lea rtFighterTag+%d,%%a1
	bra rtFighterEntry
''' % (name, name, TAG_INDEX[lab]) for lab, name in (('SECSTRT_40', 'rt_fighter_snatcher'), ('LAB_0ED2', 'rt_fighter_demon'),
                                          ('LAB_0EFF', 'rt_fighter_ai_knight'), ('LAB_0EC2', 'rt_fighter_stalker'))) + '''
	| the two patch entries that went with their dead patches (7.1 cleanup): the handler table fill and the knight defaults
	.globl rt_fight_tables_init
rt_fight_tables_init:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtFightTablesInit
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_knight_init_defaults
rt_knight_init_defaults:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %a1,-(%sp)
	jsr rtKnightDefaults
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	jsr mog_LAB_0167
	rts

	| the script routines: A1 = owner, A2 = frames, D0..D3 = x / y / z / facing, D4 = the tag (clobbered here)
rtFightOpEntry:
	move.l %d3,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a2,-(%sp)
	move.l %a1,-(%sp)
	move.l %d4,-(%sp)
	jsr rtFightOpRun
	lea 28(%sp),%sp
	rts
''' + ''.join('''
	.globl rt_fight_op_%s
rt_fight_op_%s:
	move.l #0xF000%s,%%d4
	bra rtFightOpEntry
''' % (op, op, op) for op in F.OPS) + ')");' + chr(10)


def build_blob():
    """Compile + link the sources flat at BLOB_BASE.  Returns (bytes, {symbol: address})."""
    global _BLOB
    if _BLOB:
        return _BLOB
    tmp = tempfile.mkdtemp(prefix='fighters_emu_')
    env = dict(os.environ, PATH=os.path.dirname(E.GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
             '-std=c++17', '-O1', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1', '-ffunction-sections',
             '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(E.ACE, 'mini_std'), '-I', E.ACE,
             '-I', E.GCC_SUPPORT]
    # rt::sfxRequest (src/rt/sfx.cpp, the C++ LAB_0AA2) is replaced by the counting stub the asm run uses at LAB_0AA2
    stub_cpp = os.path.join(tmp, 'sfx_stub.cpp')
    with open(stub_cpp, 'w') as f:
        f.write('#include "rt/sfx.hpp"\n'
                'namespace rt { UBYTE sfxRequest(UWORD uwSeq) {\n'
                '  *(volatile UWORD *)0x%X = *(volatile UWORD *)0x%X + 1; *(volatile UWORD *)0x%X = uwSeq; return 0; }\n'
                '  void sfxStartFixed(UBYTE ubChannel, UWORD uwSeq) {\n'
                '  *(volatile UWORD *)0x%X = *(volatile UWORD *)0x%X + 1; *(volatile UWORD *)0x%X = uwSeq; }\n'
                '  UBYTE sfxRelease(UBYTE) { return 0; } }\n'
                '// ROADMAP 7.1p: the tags rtFightOpRun runs in rt/fighters.cpp (fight died / end, pause all, kill current, the ritual sounds): not exercised here\n'
                'extern "C" { void rtFightDied() {} void rtFightEnd() {} void rtFightPauseAll() {} void rtFightKillCurrent() {}\n'
                '  unsigned long rtRngPercent(unsigned long *) { return 0; } }\n'
                % (LOG_SOUNDS, LOG_SOUNDS, LOG_LAST, LOG_SOUNDS, LOG_SOUNDS, LOG_LAST))
    objs = []
    shim_cpp = os.path.join(tmp, 'fighter_shims.cpp')
    with open(shim_cpp, 'w') as f:
        f.write(SHIM_ASM)
    for i, src in enumerate(SOURCES + (stub_cpp, shim_cpp)):
        o = os.path.join(tmp, '%d_%s.o' % (i, os.path.basename(src)))
        r = subprocess.run([E.GXX] + flags + ['-c', src if os.path.isabs(src) else os.path.join(ROOT, src), '-o', o], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError('compile %s failed: %s' % (src, r.stderr[-3000:]))
        objs.append(o)
    und, defs = set(), set()
    for o in objs:
        for ln in subprocess.run([E.NM, '-u', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 2 and f[0] == 'U':
                und.add(f[1])
        for ln in subprocess.run([E.NM, '--defined-only', o], capture_output=True, text=True, env=env).stdout.split('\n'):
            f = ln.split()
            if len(f) == 3:
                defs.add(f[2])
    cmd = [E.LD, '-Ttext=0x%X' % E.BLOB_BASE, '-e', 'rt_fighter_knight']
    for s in sorted(CN.legacy_set(und - defs)):
        if s.startswith('mog_LAB_'):
            cmd.append('--defsym=%s=%d' % (s, A(s[len('mog_LAB_'):])))
        elif s.startswith('mog_SECSTRT_'):
            cmd.append('--defsym=%s=%d' % (s, A(s[len('mog_'):])))
        elif s == 'rt_abs_a':
            cmd.append('--defsym=rt_abs_a=10')
        elif s == 'rt_abs_c':
            cmd.append('--defsym=rt_abs_c=12')
        elif s in RT_LABELS:     # 7.1o: rt entry the C++ calls instead of the label stub = the oracle routine of that label
            cmd.append('--defsym=%s=%d' % (s, A(RT_LABELS[s][4:])))
        else:
            raise RuntimeError('unresolved symbol ' + s)
    cmd += CN.defsym_aliases(cmd, und - defs)   # ROADMAP 7.1s: the C++ names of the cells resolve like their labels
    elf = os.path.join(tmp, 'blob.elf')
    r = subprocess.run(cmd + ['--gc-sections', '-o', elf] + objs, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError('link failed: ' + r.stderr[-3000:])
    binf = os.path.join(tmp, 'blob.bin')
    subprocess.run([E.OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
    syms = {}
    for ln in subprocess.run([E.NM, elf], capture_output=True, text=True, env=env).stdout.split('\n'):
        f = ln.split()
        if len(f) == 3 and f[1] in 'TtRr':
            syms[f[2]] = int(f[0], 16)
    blob = open(binf, 'rb').read()
    assert len(blob) < E.BLOB_SIZE - 0x4000, len(blob)         # the .bss (the rt environment) follows it
    _BLOB = (blob, syms)
    return _BLOB


def stubs(j0, j1, mask):
    """68000 stand-ins patched over the four asm callees (same bytes in both runs)."""
    return [
        (A('00EE'), b'\x30\x3c' + struct.pack('>H', j0) + b'\x32\x3c' + struct.pack('>H', j1) + b'\x4e\x75'),
        (A('0A71'), b'\x02\x28' + struct.pack('>H', mask) + b'\x00\x3f' + b'\x4e\x75'),            # ANDI.B #mask,63(A0) ; RTS
        (A('0AA2'), b'\x52\x79' + struct.pack('>I', LOG_SOUNDS) + b'\x33\xc0' + struct.pack('>I', LOG_LAST) + b'\x4e\x75'),
        (A('0427'), b'\x52\x79' + struct.pack('>I', LOG_FX) + b'\x4e\x75'),
        # ROADMAP 7.1h: the fixed-channel sound starts count like LAB_0AA2, the death fade like LAB_0427
        (A('SECSTRT_16'), b'\x52\x79' + struct.pack('>I', LOG_SOUNDS) + b'\x33\xc0' + struct.pack('>I', LOG_LAST) + b'\x4e\x75'),
        (A('0A9B'), b'\x52\x79' + struct.pack('>I', LOG_SOUNDS) + b'\x33\xc0' + struct.pack('>I', LOG_LAST) + b'\x4e\x75'),
        (A('0A9C'), b'\x52\x79' + struct.pack('>I', LOG_SOUNDS) + b'\x33\xc0' + struct.pack('>I', LOG_LAST) + b'\x4e\x75'),
        (A('0A9D'), b'\x52\x79' + struct.pack('>I', LOG_SOUNDS) + b'\x33\xc0' + struct.pack('>I', LOG_LAST) + b'\x4e\x75'),
        (A('0D8A'), b'\x52\x79' + struct.pack('>I', LOG_FX) + b'\x4e\x75'),
        (A('03EE'), b'\x52\x79' + struct.pack('>I', LOG_FX) + b'\x4e\x75'),
    ]


@unittest.skipUnless(E.HAVE_UC and E.HAVE_TOOLS and TC.CXX, 'needs unicorn, the m68k toolchain, build/reasm/mog')
class FightersEmuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        blob, syms = build_blob()
        regions = [(E.BLOB_BASE, E.BLOB_SIZE), (E.LOW_BASE, E.LOW_SIZE), (E.DATA_BASE, E.DATA_SIZE)]
        saved = E.H.CPU_MODEL
        E.H.CPU_MODEL = E.UM.UC_CPU_M68K_M68020                  # the shims are 68020 code (the original runs on it too)
        try:
            cls.h = E.H.Harness('mog', extra_regions=regions)
        finally:
            E.H.CPU_MODEL = saved
        cls.blob, cls.syms = blob, syms
        cls.image_lo, cls.image_hi = E.H.IMAGE_BASE, E.H.IMAGE_BASE + len(cls.h.image)
        cls.stack_lo, cls.stack_hi = E.H.STACK_BASE, E.H.STACK_BASE + E.H.STACK_SIZE
        rng = random.Random(1)
        cls.tabs = F.tables_image(rng)
        cls.base = F.base_image()

    # -- one run --
    def snapshot(self):
        uc = self.h.uc
        snap = (bytes(uc.mem_read(self.image_lo, self.image_hi - self.image_lo)), bytes(uc.mem_read(E.DATA_BASE, E.DATA_SIZE)),
                bytes(uc.mem_read(E.LOW_BASE, 0x20)))
        # ROADMAP 7.1p: the C++ stores the address of its handler tag (rtFighterTag + n) where the original stores the label of the handler body
        out = []
        for part in snap:
            for lab, n in TAG_INDEX.items():
                part = part.replace(struct.pack('>I', self.syms['rtFighterTag'] + n),
                                    struct.pack('>I', A(lab[4:] if lab.startswith('LAB_') else lab)))
            out.append(part)
        return tuple(out)

    def run_both(self, w, regs, extra, orig_label, shim):
        patches = [(a, bytes(b)) for img in (self.base, self.tabs, w.I) for a, b in img.segs]
        patches += extra + [(E.BLOB_BASE, self.blob)]
        rin = {'d': [regs['D%d' % i] for i in range(8)], 'a': [regs['A%d' % i] for i in range(7)] + [E.H.STACK_TOP], 'ccr': 0}
        ro = self.h.run(self.h.address(orig_label), rin, patches)
        so = self.snapshot()
        rs = self.h.run(self.syms[shim], rin, patches)
        ss = self.snapshot()
        return rin, ro.regs, rs.regs, so, ss

    def diff_mem(self, so, ss):
        ignore = IGN + [(self.stack_lo, self.stack_hi - self.stack_lo)]
        out = []
        for blo, a_, b_ in ((self.image_lo, so[0], ss[0]), (E.DATA_BASE, so[1], ss[1])):
            if a_ == b_:
                continue
            for k in range(len(a_)):
                if a_[k] != b_[k] and not any(lo <= blo + k < lo + ln for lo, ln in ignore):
                    out.append(blo + k)
        return out

    def check(self, fn, gen, n, outputs, regcheck=True):
        try:
            rng = random.Random(0x5A + int(fn, 16))
        except ValueError:                                       # the ROADMAP 7.1h families: 'T04', 'O02DC'
            rng = random.Random(F.case_seed(fn) + 0x40)
        oc_all, sc_all = set(), set()
        for i in range(n):
            w, regs, j0, j1, vh, salt = gen(rng, self.tabs, fn)
            if fn == '0226':
                # the flyer reads VHPOSR on every second turn-round (the harness treats custom chip reads as faults)
                w.I.w16(A('0234'), rng.choice((0, 2, 4)))
            extra = stubs(j0, j1, rng.choice((0xFF, 0xFF, 0xFA, 0xF5, 0xF7, 0xFE, 0xFD, 0xF3)) | 0x10)
            label = NEW_LABEL.get(fn) or 'LAB_' + (fn[1:] if fn[0] == 'O' else fn)
            shim = SHIMS.get(fn) or ('rt_fight_op_' + fn[1:] if fn[0] == 'O' else 'rt_sound_' + fn)
            rin, ro, rs, so, ss = self.run_both(w, regs, extra, label, shim)
            bad = self.diff_mem(so, ss)
            self.assertEqual(bad[:6], [], '%s case %d: memory differs at %s' % (fn, i, ['%06x' % b for b in bad[:6]]))
            din, dor, dsh = E.reg_vec(rin), E.reg_vec(ro), E.reg_vec(rs)
            for k, nm in enumerate(E.NAMES):
                mask = outputs.get(nm)
                if mask is not None:
                    self.assertEqual(dsh[k] & mask, dor[k] & mask, '%s case %d %s: shim %x original %x' % (fn, i, nm, dsh[k], dor[k]))
            self.assertEqual(rs['a'][7], rin['a'][7], fn + ' SP')
            oc_all |= {E.NAMES[k] for k in range(15) if dor[k] != din[k]}
            sc_all |= {E.NAMES[k] for k in range(15) if dsh[k] != din[k]}
        extra = sc_all - oc_all
        if regcheck:     # the pickers end in JMP LAB_0AA2, which clobbers what the (preserving) stub here does not
            self.assertEqual(extra, set(), '%s: the shim changes %s, which the original never does (it changes %s)' % (fn, sorted(extra), sorted(oc_all)))

    # -- the tests --
    def test_blob_exports_the_shims(self):
        for s in (list(SHIMS.values()) + ['rt_fight_op_' + o for o in F.OPS] +
                  ['rtFighterRun', 'rtFighterEntry', 'rtFightOpRun', 'rtFightTablesInit', 'rt_fight_tables_init',
                   'rt_knight_init_defaults']):
            self.assertIn(s, self.syms)

    def test_handlers(self):
        n = int(os.environ.get('FIGHTERS_EMU_N', '120'))
        for fn in [k for k in SHIMS if k not in NEW_LABEL]:
            self.check(fn, F.gen_case, n, {'A0': 0xFFFFFFFF, 'D0': 0xFFFF, 'D1': 0xFFFF, 'D2': 0xFFFF, 'D3': 0xFF})

    def test_s40_handlers(self):
        """ROADMAP 7.1h: SECSTRT_40, LAB_0ED2, LAB_0EFF and LAB_0EC2 against their C++ ports (the other callees stubbed alike)."""
        n = int(os.environ.get('FIGHTERS_EMU_N', '120'))
        for fn in NEW_LABEL:
            self.check(fn, F.gen_new, n, {'A0': 0xFFFFFFFF, 'D0': 0xFFFF, 'D1': 0xFFFF, 'D2': 0xFFFF, 'D3': 0xFF})

    def test_tables_shim(self):
        """rt_fight_tables_init (the test link unit's entry, was patched into LAB_01AE in place of sixteen MOVE.L #handler,n(A0)): the table holds the handlers of
        the asm fill, every register comes back as it went in."""
        want = F.handler_table_want()
        rng = random.Random(7)
        rin = {'d': [rng.getrandbits(32) for _ in range(8)], 'a': [rng.getrandbits(32) for _ in range(7)] + [E.H.STACK_TOP], 'ccr': 0}
        rin['a'][0] = A('08C7')
        r = self.h.run(self.syms['rt_fight_tables_init'], rin, [(E.BLOB_BASE, self.blob)])
        self.assertEqual(r.regs['d'][:8], rin['d'])
        self.assertEqual(r.regs['a'][:7], rin['a'][:7])
        table = bytes(self.h.uc.mem_read(A('08C7'), 72))
        for off, lab in want.items():
            if off != 68:
                # ROADMAP 7.1p: the slot holds the handler's tag (rtFighterTag + n), not the label of the asm body
                tag = self.syms['rtFighterTag'] + {k.replace('LAB_', ''): v for k, v in TAG_INDEX.items()}[lab.replace('LAB_', '')]
                self.assertEqual(int.from_bytes(table[off:off + 4], 'big'), tag, 'slot %d = %s' % (off, lab))

    def test_script_ops(self):
        """ROADMAP 7.1h: every routine opcode $B0 can call, the asm at its label against rtFightOpRun on its tag (memory only:
        the originals leave their registers as they please)."""
        for op in F.OPS:
            self.check('O' + op, F.gen_op, 40, {}, regcheck=False)


if __name__ == '__main__':
    unittest.main()
