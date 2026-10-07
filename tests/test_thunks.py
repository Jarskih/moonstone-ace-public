"""Tests for tools/thunks.py, the swap generation in tools/resource.py and the thunk marshalling
(ROADMAP 3.2a-3.2d).  Run: py -m unittest discover -s tests -p "test_*.py"

Layers
  contracts / swap list / reference classification   pure Python (needs build/inventory/routines.json)
  generated asm                                        vasm -Felf of the swapped program/mog
  thunk asm in unicorn                                 the REAL generated thunk against a stub ms_thunk_run
                                                       that records what it receives and mutates the frame
  thunkRun in C++                                      include/ms/thunk_run.hpp on the host (clang++)
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no the IRA listing; the whole module skips)
origskip.require_listing()
import importlib.util, os, re, shutil, struct, subprocess, sys, tempfile, textwrap, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import thunks  # noqa: E402

_spec = importlib.util.spec_from_file_location('ms_resource', os.path.join(ROOT, 'tools', 'resource.py'))
res = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(res)

HAVE_INV = os.path.exists(thunks.INVENTORY)
HAVE_ASM = (os.path.exists(os.path.join(res.REASM, 'program.lst')) and os.path.exists(res.R.VASM)
            and os.path.exists(os.path.join(res.R.ASM_DIR, 'program.asm')))


@unittest.skipUnless(HAVE_INV, 'run tools/reassemble.py, callgraph.py, routines.py first')
class Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.routines = thunks.load_routines()
        cls.hand = thunks.load_contracts(routines=cls.routines)
        cls.lifted = thunks.lifted_functions()

    def test_contracts_yaml_is_valid(self):
        for (b, lab), c in self.hand.items():
            self.assertIn(lab, self.routines[b])
            self.assertTrue(c['reason'].strip())

    def test_automatic_contract_comes_from_routines_json(self):
        r = self.routines['mog']['LAB_03CA']
        c = thunks.contract_for('mog', 'LAB_03CA', self.routines, self.hand, self.lifted)
        self.assertEqual((c.origin, c.regs_out, c.ccr_out, c.sp_delta), ('auto', ['d5'], False, 0))
        self.assertEqual(c.regs_in, sorted(r['reads_before_write'], key=thunks.REGS.index))
        self.assertEqual(c.out_mask, 1 << thunks.REGS.index('d5'))

    def test_risky_routines_need_a_hand_contract(self):
        # ccr_live_out, uses_sp_tricks: refused without an entry, accepted with it
        for b, lab, why in (('program', 'LAB_01A1', 'ccr_live_out'), ('mog', 'LAB_0CC1', 'uses_sp_tricks'),
                            ('program', 'LAB_049B', 'uses_sp_tricks')):
            with self.assertRaises(thunks.Refused) as cm:
                thunks.contract_for(b, lab, self.routines, {}, self.lifted)
            self.assertIn(why, str(cm.exception))
            self.assertEqual(thunks.contract_for(b, lab, self.routines, self.hand, self.lifted).origin, 'hand')

    def test_approx_routine_is_refused(self):
        approx = next(l for l, r in self.routines['program'].items() if r['approx'])
        with self.assertRaises(thunks.Refused) as cm:
            thunks.contract_for('program', approx, self.routines, {}, None)
        self.assertIn('approx', str(cm.exception))

    def test_unlifted_routine_is_refused(self):
        free = next(l for l in self.routines['mog'] if ('mog', l) not in self.lifted)
        with self.assertRaises(thunks.Refused) as cm:
            thunks.contract_for('mog', free, self.routines, {}, self.lifted)
        self.assertIn('no lifted body', str(cm.exception))

    def test_hand_contract_validation(self):
        bad = textwrap.dedent('''\
            mog:
              LAB_0CC1:
                regs_in: [d9]
                regs_out: a1
                ccr_out: maybe
                sp_delta: 3
                reason: ""
                extra: 1
            nope:
              LAB_0001: {}
            ''')
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'c.yaml')
            with open(p, 'w') as f:
                f.write(bad)
            with self.assertRaises(ValueError) as cm:
                thunks.load_contracts(p, self.routines)
        msg = str(cm.exception)
        for frag in ('unknown keys', 'regs_in', 'regs_out', 'ccr_out', 'sp_delta', 'reason', 'unknown binary'):
            self.assertIn(frag, msg)

    def test_swap_list_parsing_and_refusals(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 's.txt')

            def write(t):
                with open(p, 'w') as f:
                    f.write(t)
            write('# c\nmog:lab_03ca  # pilot\nprogram LAB_0014\n')
            self.assertEqual(thunks.parse_swap_list(p), [('mog', 'LAB_03CA'), ('program', 'LAB_0014')])
            write('mog:LAB_03CA\nmog:LAB_03CA\n')
            with self.assertRaises(thunks.Refused):
                thunks.parse_swap_list(p)
            write('amiga:LAB_0001\n')
            with self.assertRaises(thunks.Refused):
                thunks.parse_swap_list(p)
            free = next(l for l in self.routines['mog'] if ('mog', l) not in self.lifted)
            write(f'mog:{free}\n')
            with self.assertRaises(thunks.Refused) as cm:
                thunks.build_swap(p, self.routines, self.hand, self.lifted)
            self.assertIn('swap list refused', str(cm.exception))

    def test_checked_in_swap_lists_are_all_safe(self):
        for name in ('none', 'pilot', 'pure'):
            sw = thunks.build_swap(os.path.join(thunks.SWAP_DIR, name + '.txt'), self.routines, self.hand, self.lifted)
            n = sum(len(v) for v in sw.values())
            self.assertEqual(n == 0, name == 'none')
        pure = thunks.build_swap(os.path.join(thunks.SWAP_DIR, 'pure.txt'), self.routines, self.hand, self.lifted)
        for b, d in pure.items():
            for lab in d:
                self.assertTrue(self.routines[b][lab]['pure'], f'{b}:{lab}')

    def test_every_lifted_routine_has_a_contract_or_a_reason(self):
        for (b, lab) in self.lifted:
            try:
                thunks.contract_for(b, lab, self.routines, self.hand, self.lifted)
            except thunks.Refused as e:
                self.assertIn('hand-written contract', str(e))


@unittest.skipUnless(HAVE_INV, 'needs build/inventory')
class StaticFiles(unittest.TestCase):
    def test_hunk_tab_is_current(self):
        with open(thunks.HUNK_TAB, newline='') as f:
            self.assertEqual(f.read(), thunks.gen_hunk_tab())

    def test_thunk_frame_constants(self):
        self.assertEqual(thunks.F_SIZE, 72)
        self.assertEqual(thunks.FRAME, thunks.MARGIN + 72)
        with open(os.path.join(ROOT, 'include', 'ms', 'thunk_run.hpp')) as f:
            t = f.read()
        self.assertIn('sizeof(ThunkFrame) == 72', t)
        self.assertIn('offsetof(ThunkFrame, vsp) == %d' % thunks.F_VSP, t)


@unittest.skipUnless(HAVE_INV and HAVE_ASM, 'needs build/reasm + inventory + vasm')
class SwappedAsm(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='swap_test_')

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def gen(self, name, swapfile):
        out = os.path.join(self.tmp, os.path.basename(swapfile)[:-4])
        buf = __import__('io').StringIO()
        with __import__('contextlib').redirect_stdout(buf):
            ok = res.generate_swapped([name], swapfile, out)
        self.assertTrue(ok, buf.getvalue())
        return os.path.join(out, name + '.s')

    def test_empty_swap_equals_the_normal_generation(self):
        for name in ('program', 'mog'):
            with open(self.gen(name, os.path.join(thunks.SWAP_DIR, 'none.txt')), encoding='latin-1', newline='') as f:
                swapped = f.read()
            self.assertEqual(swapped, res.Gen(name, 'elf').generate())

    def test_pilot_renames_the_label_and_assembles(self):
        path = self.gen('mog', os.path.join(thunks.SWAP_DIR, 'pilot.txt'))
        with open(path, encoding='latin-1') as f:
            text = f.read()
        self.assertIn('\nmog_LAB_03CA__asm:\n', text)
        self.assertIn('\tXDEF\tmog_LAB_03CA__asm', text)
        self.assertEqual(text.count('\nmog_LAB_03CA:\n'), 1)       # the thunk
        ends = [m.start() for m in re.finditer(r'(?m)^[ \t]*END[ \t]*$', text)]   # the generated asm may have no END any more
        if ends:                                                                # (the hunk that held it is C++-owned data)
            self.assertLess(text.index('\nmog_LAB_03CA:\n'), ends[-1])          # not after END
        self.assertIn('\tpea\tlab_LAB_03CA\n', text)
        obj = os.path.join(self.tmp, 'mog.o')
        r = subprocess.run([res.R.VASM, '-Felf', '-m68020', '-devpac', '-no-opt', '-quiet', '-o', obj, path],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        try:
            nm = subprocess.run(['m68k-amiga-elf-nm', obj], capture_output=True, text=True)
        except FileNotFoundError:        # toolchain PATH not set: the vasm assembly above is the check
            return
        if nm.returncode == 0:
            syms = {l.split()[-1]: l.split()[-2] for l in nm.stdout.splitlines() if len(l.split()) >= 2}
            self.assertEqual(syms.get('mog_LAB_03CA'), 'T')
            self.assertEqual(syms.get('mog_LAB_03CA__asm'), 'T')
            self.assertEqual(syms.get('lab_LAB_03CA'), 'U')
            self.assertEqual(syms.get('ms_thunk_run'), 'U')

    def test_only_code_pointer_uses_are_redirected(self):
        plan = thunks.SwapPlan('mog', {'LAB_03CA': thunks.Contract([], ['d5'], False, 0, 'auto')},
                               thunks.load_routines()['mog'], res.image_sections('mog'), [0x10000] * 64)
        h = plan.rows['LAB_03CA']['hunk']
        near = (h, plan.hunk_sizes[h] - 100)
        far = (h, 0) if plan.hunk_sizes[h] > 40000 else near
        def c(text, caller):
            i = text.index('LAB_03CA')
            return plan.classify(text, i, i + 8, caller)
        self.assertEqual(c('\tJSR\tLAB_03CA', None), '')
        self.assertEqual(c('\tDC.L\tLAB_03CA', None), '')
        self.assertEqual(c('\tMOVE.L\t#LAB_03CA,(A0)', None), '')
        self.assertEqual(c('\tBSR.W\tLAB_03CA', near), '')
        self.assertEqual(c('\tBSR.S\tLAB_03CA', near), '__asm')        # 8-bit displacement
        self.assertEqual(c('\tBRA.W\tLAB_03CA+4', near), '__asm')      # into the body
        self.assertEqual(c('\tDC.W\tLAB_03CA-LAB_0001', None), '__asm')
        self.assertEqual(c('\tMOVE.W\tLAB_03CA,D0', None), '__asm')    # reads the code bytes
        self.assertEqual(c('\tBSR.W\tLAB_03CA', None), '__asm')        # caller position unknown
        if far is not near:
            self.assertEqual(c('\tBSR.W\tLAB_03CA', far), '__asm')     # PC16 out of range


# --------------------------------------------------------------------------------------------
# the real generated thunk, run in unicorn against a stub callee
# --------------------------------------------------------------------------------------------
THUNK_ORG, STUB_ORG, FN_ORG, RET_ORG, RET2_ORG = 0x10000, 0x11000, 0x12000, 0x20000, 0x20100
REC, STACK_TOP = 0x30000, 0x42000
STACK_BASE_ADDR, STACK_SIZE_BYTES = 0x40000, 0x4000
SCRATCH = 0xDEADBEEF

STUB = """
	org	$%X
ms_thunk_run:
	move.l	4(sp),a0
	move.l	a0,$30000
	move.l	8(sp),$30004
	move.l	12(sp),$30008
	move.l	16(sp),$3000C
	lea	$30010,a1
	moveq	#17,d0
.c:	move.l	(a0)+,(a1)+
	dbra	d0,.c
	move.l	4(sp),a0
	move.l	60(a0),a1
	move.l	#$DEADBEEF,-4(a1)
	move.l	#$DEADBEEF,-8(a1)
	move.l	#$DEADBEEF,-32(a1)
	move.l	#$11111111,(a0)
	move.l	#$22222222,32(a0)
	move.w	#$0015,64(a0)
	rts
	org	$%X
lab_TEST:
	rts
	org	$%X
	move.w	ccr,$30100
	nop
	org	$%X
	move.w	ccr,$30102
	nop
"""


def _have_unicorn():
    try:
        import unicorn  # noqa: F401
    except ImportError:
        return False
    return os.path.exists(res.R.VASM)


@unittest.skipUnless(_have_unicorn(), 'needs unicorn and vasm')
class ThunkInUnicorn(unittest.TestCase):
    def build(self, tmp, sp_delta, mask):
        c = thunks.Contract([], [], False, sp_delta, 'hand')
        lines = thunks.thunk_lines('t_', 'mog', 'TEST', c)
        text = [f'\torg\t${THUNK_ORG:X}'] + [l.replace('lab_LAB_TEST', 'lab_TEST') for l in lines]
        src = '\n'.join(text) + STUB % (STUB_ORG, FN_ORG, RET_ORG, RET2_ORG)
        # the mask is baked into the thunk: patch the generated immediate to the one under test
        src = src.replace('#$%04X' % c.out_mask, '#$%04X' % mask, 1)
        s = os.path.join(tmp, 'thunk.s')
        with open(s, 'w', newline='\n') as f:
            f.write(src + '\n')
        b = os.path.join(tmp, 'thunk.bin')
        r = subprocess.run([res.R.VASM, '-Fbin', '-m68020', '-devpac', '-no-opt', '-quiet', '-o', b, s],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(b, 'rb') as f:
            return f.read()

    def run_thunk(self, sp_delta, mask=0x8001, ccr_in=0x0A):
        import unicorn
        from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
        from unicorn import m68k_const as M
        with tempfile.TemporaryDirectory() as tmp:
            blob = self.build(tmp, sp_delta, mask)
        uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(M.UC_CPU_M68K_M68020)
        uc.mem_map(0x10000, 0x30000)
        uc.mem_map(STACK_BASE_ADDR, STACK_SIZE_BYTES)
        # blob = vasm -Fbin output for the whole org range, starting at the lowest org
        uc.mem_write(THUNK_ORG, blob)
        S = STACK_TOP - 0x100                       # SP at entry: [S]=ret, [S+4..]=stack args
        uc.mem_write(S, struct.pack('>IIII', RET_ORG, RET2_ORG, 0xA0A0A0A0, 0xB0B0B0B0))
        entry = [0x1000 + i * 0x111 for i in range(8)] + [0x2000 + i * 0x101 for i in range(7)]
        uc.reg_write(M.UC_M68K_REG_SR, 0x2700 | ccr_in)
        for i in range(8):
            uc.reg_write(M.UC_M68K_REG_D0 + i, entry[i])
        for i in range(7):
            uc.reg_write(M.UC_M68K_REG_A0 + i, entry[8 + i])
        uc.reg_write(M.UC_M68K_REG_A7, S)
        # the return markers record the exit CCR; stop right behind them
        end = RET_ORG + 6 if sp_delta == 0 else RET2_ORG + 6
        uc.emu_start(THUNK_ORG, end, count=5000)
        rd = lambda a, n: bytes(uc.mem_read(a, n))
        u32 = lambda a: struct.unpack('>I', rd(a, 4))[0]
        got = {
            'F': u32(REC), 'fn': u32(REC + 4), 'mask': u32(REC + 8), 'delta': u32(REC + 12),
            'frame': struct.unpack('>15IIHHI', rd(REC + 16, 72)),
            'd': [uc.reg_read(M.UC_M68K_REG_D0 + i) for i in range(8)],
            'a': [uc.reg_read(M.UC_M68K_REG_A0 + i) for i in range(7)],
            'sp': uc.reg_read(M.UC_M68K_REG_A7),
            'ccr': struct.unpack('>H', rd(REC + (0x100 if sp_delta == 0 else 0x102), 2))[0] & 0x1F,
            'ret_slot': u32(S), 'S': S, 'entry': entry,
        }
        return got

    def test_frame_seen_by_the_callee(self):
        g = self.run_thunk(0)
        f = g['frame']
        self.assertEqual(list(f[:15]), g['entry'])                  # every register is an input
        self.assertEqual(f[15], g['S'] + 4)                        # vsp: SP after the return-address pop
        self.assertEqual(f[16], 0x0A)                              # entry CCR
        self.assertEqual(f[18], RET_ORG)                           # return address copy
        self.assertEqual(g['fn'], FN_ORG)
        self.assertEqual(g['delta'], 0)
        self.assertEqual(g['mask'], 0x8001)
        # the frame (and the C++ above it) lives below the lifted code's virtual stack margin
        self.assertLessEqual(g['F'] + 72, g['S'] - thunks.MARGIN + 8)

    def test_writeback_ccr_and_return(self):
        g = self.run_thunk(0)
        self.assertEqual(g['d'][0], 0x11111111)                    # the stub's "output" d0
        self.assertEqual(g['a'][0], 0x22222222)                    # a0 as the stub wrote it
        self.assertEqual(g['d'][1:], g['entry'][1:8])              # untouched registers restored
        self.assertEqual(g['a'][1:], g['entry'][9:15])
        self.assertEqual(g['ccr'], 0x15)                           # CCR the callee left is returned
        self.assertEqual(g['sp'], g['S'] + 4)                      # balanced: SP after RTS
        # the lifted code scribbled on the return slot; the thunk put the real address back
        self.assertEqual(g['ret_slot'], RET_ORG)

    def test_sp_delta_returns_through_the_next_stack_word(self):
        g = self.run_thunk(4)
        self.assertEqual(g['delta'], 4)
        self.assertEqual(g['sp'], g['S'] + 8)                      # popped own slot + one more
        self.assertEqual(g['ccr'], 0x15)                           # execution reached [S+4] = RET2


# --------------------------------------------------------------------------------------------
# thunkRun (include/ms/thunk_run.hpp) on the host
# --------------------------------------------------------------------------------------------
HOST_TEST = r'''
#include <stdio.h>
#include "ms/thunk_run.hpp"
using namespace ms;
struct HM : Mem { uint8_t r8(uint32_t) override { return 0; } void w8(uint32_t, uint8_t) override {} };
static void body(Regs& R, Mem&) {            // reads d0,d1; "clobbers" d2, writes d3, a1 and the CCR
    R.d[2] = 0xDEAD; R.d[3] = R.d[0] + R.d[1]; R.a[1] = 0x1234;
    R.setCCR(CCR_Z | CCR_C); if (R.a[7] != 0x1000) R.d[7] = 1;
}
static void popper(Regs& R, Mem&) { R.a[7] += 4; }
int main() {
    HM m; ThunkFrame F{}; int bad = 0;
    for (int i = 0; i < 15; i++) F.reg[i] = 0x100 + i;
    F.vsp = 0x1000; F.ccr = CCR_X | CCR_N;
    uint32_t mask = (1u << 3) | (1u << 9);                       // d3, a1; no CCR
    bool ok = thunkRun(F, body, mask, 0, m);
    if (!ok) bad |= 1;
    if (F.reg[3] != 0x100 + 0x101) bad |= 2;                      // output copied
    if (F.reg[9] != 0x1234) bad |= 4;                             // a1 output
    if (F.reg[2] != 0x102) bad |= 8;                              // non-output keeps entry value
    if (F.reg[7] != 0x107) bad |= 16;
    if (F.ccr != (CCR_X | CCR_N)) bad |= 32;                      // CCR not an output: entry CCR
    mask |= THUNK_OUT_CCR;
    thunkRun(F, body, mask, 0, m);
    if (F.ccr != (CCR_Z | CCR_C)) bad |= 64;                      // CCR output
    if (thunkRun(F, popper, 0, 4, m) != true) bad |= 128;         // sp_delta matches
    if (thunkRun(F, popper, 0, 0, m) != false) bad |= 256;        // unbalanced vs contract
    printf("%d\n", bad);
    return bad;
}
'''


@unittest.skipUnless(shutil.which('clang++'), 'needs clang++')
class ThunkRunHost(unittest.TestCase):
    def test_marshalling_core(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = os.path.join(tmp, 't.cpp'), os.path.join(tmp, 't.exe')
            with open(src, 'w') as f:
                f.write(HOST_TEST)
            r = subprocess.run(['clang++', '-std=c++17', '-fno-exceptions', '-fno-rtti', '-I',
                                os.path.join(ROOT, 'include'), src, '-o', exe], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, f'failure bits {r.stdout.strip()}')


if __name__ == '__main__':
    unittest.main()
