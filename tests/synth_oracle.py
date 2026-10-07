"""Unicorn oracle for mog's synth (ROADMAP 7.1g): runs the ORIGINAL S_44 code (build/reasm/mog, the load-equivalent reassembly of
the original binary) on a fake Paula and returns the full chip-register write log per scripted operation.

The same script runs against the C++ port (tests/synth_driver.cpp on the host, or the compiled m68k code in unicorn); the logs
must be identical, element by element (a long write is normalised to its two word writes, hi then lo).

Script operations (one tuple each):
    ('Z', wave_base, (b1, b2, b3, b4, b5))   init (LAB_0F89) + instrument relocation (LAB_0FD4) with the bank buffers LAB_05C7..05CB
    ('S', seq, ch)                           start sequence seq on channel ch (LAB_0F8C)
    ('T',)                                   one VBL tick (the job LAB_0F73)
    ('I', intreqr, intenar)                  one INT4 entry: pending INTREQ bits and the INTENAR value the handler sees (LAB_0F69)
    ('F', flag)                              LAB_0FC4 := flag, then the fade step LAB_0FC2
    ('K', value)                             the lock word LAB_0FCA := value (a tick while it is non-zero skips the frame)
    ('P', ref, bytes)                        write bytes at S_44 offset ref (after 'Z'; vibrato rows, envelopes, sequences)
    ('Q', idx, ref)                          sequence table entry idx := S_44 offset ref
    ('D',)                                   dump the 131 instrument sample addresses (as 'dump' event)
Result: a list with one entry per operation: ('w', reg, value) tuples (word writes), or ('dump', [addresses]).
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diffharness'))

try:
    import harness as H
    from unicorn import Uc, UcError, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
    from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
    from unicorn import m68k_const as M
    HAVE_UC = True
except Exception:       # pragma: no cover
    HAVE_UC = False

HW_BASE = 0xDFF000
INST_COUNT = 131


def have_image():
    return HAVE_UC and os.path.exists(os.path.join(ROOT, 'build', 'reasm', 'mog'))


class Rig:
    """A unicorn machine with the mog image (or any code blob) and a fake custom-chip page.

    call(entry, d, a): runs a routine as if JSR'd (returns at the sentinel). Unicorn cannot execute RTE (unhandled exception), so
    interrupt handlers are run with their final RTE replaced by RTS (patch_rte); everything before it is unchanged."""

    SENT = H.SENTINEL if HAVE_UC else 0

    def __init__(self, image, image_base, regions=(), cpu=None, insn_limit=3_000_000):
        self.image, self.image_base = image, image_base
        self.regions = [(image_base, H._round(len(image))), (H.STACK_BASE, H.STACK_SIZE), (self.SENT, H.PAGE)] + list(regions)
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(cpu if cpu is not None else M.UC_CPU_M68K_M68020)
        for base, size in self.regions:
            self.uc.mem_map(base, size)
        self.uc.mem_map(HW_BASE, 0x1000)
        self.log = []
        self.pending = 0
        self.enar = 0
        self.insn_limit = insn_limit
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._on_hw_write, begin=HW_BASE, end=HW_BASE + 0xFFF)
        self.uc.hook_add(UC_HOOK_MEM_READ, self._on_hw_read, begin=HW_BASE, end=HW_BASE + 0xFFF)
        self.reset()

    def reset(self):
        uc = self.uc
        for base, size in self.regions:
            uc.mem_write(base, bytes(size))
        uc.mem_write(self.image_base, self.image)
        uc.mem_write(self.SENT, bytes.fromhex('4E71') * 8)
        uc.ctl_flush_tb()
        self.log = []
        self.pending = 0
        self.enar = 0

    # -- fake Paula ---------------------------------------------------------------------------------------------
    def _on_hw_write(self, uc, access, addr, size, value, _):
        reg = addr - HW_BASE
        if size == 4:
            self.log.append(('w', reg, (value >> 16) & 0xFFFF))
            self.log.append(('w', reg + 2, value & 0xFFFF))
        elif size == 2:
            self.log.append(('w', reg, value & 0xFFFF))
            if reg == 0x9C and not (value & 0x8000):      # INTREQ clear
                self.pending &= ~value & 0xFFFF
        else:
            raise AssertionError('byte write to the chip set at $%X' % addr)

    def _on_hw_read(self, uc, access, addr, size, value, _):
        reg = addr & 0xFFE
        if reg == 0x1E:
            uc.mem_write(HW_BASE + 0x1E, struct.pack('>H', self.pending))
        elif reg == 0x1C:
            uc.mem_write(HW_BASE + 0x1C, struct.pack('>H', self.enar))

    # -- running ------------------------------------------------------------------------------------------------
    def _go(self, entry, d, a):
        uc = self.uc
        uc.reg_write(M.UC_M68K_REG_SR, 0x2700)
        for i in range(8):
            uc.reg_write(H.DREG[i], d[i])
        sp = H.STACK_TOP - 4
        uc.mem_write(sp, struct.pack('>I', self.SENT))
        for i in range(7):
            uc.reg_write(H.AREG[i], a[i])
        uc.reg_write(H.AREG[7], sp)
        try:
            uc.emu_start(entry, self.SENT, count=self.insn_limit)
        except UcError as e:
            raise RuntimeError('unicorn: %s at pc=$%X' % (e, uc.reg_read(M.UC_M68K_REG_PC)))
        pc = uc.reg_read(M.UC_M68K_REG_PC)
        if pc != self.SENT:
            raise RuntimeError('did not return: pc=$%X' % pc)
        return [uc.reg_read(r) for r in H.DREG], [uc.reg_read(r) for r in H.AREG]

    def call(self, entry, d=None, a=None):
        return self._go(entry, d or [0] * 8, a or [0] * 7)

    def take_log(self):
        out, self.log = self.log, []
        return out


class Original:
    """The original synth in the reassembled mog image."""

    def __init__(self, regions=()):
        self.h = H.Harness('mog')              # only for load_image/symbols; the machine is our own
        syms = self.h.symbols
        self.base = H.IMAGE_BASE
        self.addr = lambda lab: self.h.hunk_bases[syms[lab]['hunk']] + syms[lab]['offset']
        self.rig = Rig(self.h.image, H.IMAGE_BASE, regions=regions)
        self.rte_orig = self.addr('LAB_0F6E') + 4     # MOVEM.L (A7)+,D0-D7/A0-A6 ; RTE
        assert bytes(self.rig.uc.mem_read(self.rte_orig, 2)) == bytes.fromhex('4E73')
        self._patch_orig()
        self.wave_base = self.h.hunk_bases[45]
        self.inst = self.addr('LAB_10A2')

    def _patch_orig(self):
        self.rig.uc.mem_write(self.rte_orig, bytes.fromhex('4E75'))   # RTE -> RTS
        self.rig.uc.ctl_flush_tb()

    def reset(self):
        self.rig.reset()
        self._patch_orig()

    def op(self, op):
        r, A = self.rig, self.addr
        k = op[0]
        if k == 'Z':
            banks = op[2]
            for lab, v in zip(('LAB_05C7', 'LAB_05C8', 'LAB_05C9', 'LAB_05CA', 'LAB_05CB'), banks):
                r.uc.mem_write(A(lab), struct.pack('>I', v))
            r.call(A('LAB_0F89'))
            r.call(A('LAB_0FD4'))
        elif k == 'S':
            r.call(A('LAB_0F8C'), d=[op[1], op[2], 0, 0, 0, 0, 0, 0])
        elif k == 'T':
            r.call(A('LAB_0F73'))
        elif k == 'I':
            r.pending, r.enar = op[1], op[2]
            r.call(A('LAB_0F69'))
        elif k == 'F':
            r.uc.mem_write(A('LAB_0FC4'), struct.pack('>H', op[1]))
            r.call(A('LAB_0FC2'))
        elif k == 'K':
            r.uc.mem_write(A('LAB_0FCA'), struct.pack('>H', op[1]))
        elif k == 'P':
            r.uc.mem_write(self.h.hunk_bases[44] + op[1], bytes(op[2]))
        elif k == 'Q':
            r.uc.mem_write(self.h.hunk_bases[44] + 8788 + 4 * op[1], struct.pack('>I', self.h.hunk_bases[44] + op[2]))
        elif k == 'D':
            raw = bytes(r.uc.mem_read(self.inst, 14 * INST_COUNT))
            return [('dump', [struct.unpack('>I', raw[14 * i + 6:14 * i + 10])[0] for i in range(INST_COUNT)])]
        else:
            raise ValueError(op)
        return r.take_log()

    def run(self, script):
        return [self.op(o) for o in script]


def script_text(script):
    """Serialise a script for tests/synth_driver.cpp."""
    out = []
    for o in script:
        k = o[0]
        if k == 'Z':
            out.append('Z %x %s' % (o[1], ' '.join('%x' % b for b in o[2])))
        elif k == 'S':
            out.append('S %d %d' % (o[1], o[2]))
        elif k in ('T', 'D'):
            out.append(k)
        elif k == 'I':
            out.append('I %x %x' % (o[1], o[2]))
        elif k == 'P':
            out.append('P %d %d %s' % (o[1], len(o[2]), ' '.join('%x' % b for b in o[2])))
        elif k == 'Q':
            out.append('Q %d %d' % (o[1], o[2]))
        elif k in ('F', 'K'):
            out.append('%s %x' % (k, o[1]))
    return '\n'.join(out) + '\n'


def parse_driver_output(text, nops):
    """Driver output: a '.' line before each operation's events; 'w reg val' (hex) or 'D a0 a1 ...'."""
    res, cur = [], None
    for ln in text.split('\n'):
        if not ln:
            continue
        if ln == '.':
            cur = []
            res.append(cur)
        elif ln[0] == 'w':
            _, reg, val = ln.split()
            cur.append(('w', int(reg, 16), int(val, 16)))
        elif ln[0] == 'D':
            cur.append(('dump', [int(x, 16) for x in ln.split()[1:]]))
    assert len(res) == nops, (len(res), nops)
    return res


# --------------------------------------------------------------------------------------------------------------------
# the compiled port in unicorn: the entry points of the ORIGINAL image are patched exactly as asm/patches/mog.synth.json does
# (JMP rt_synth_* over the first instruction) and the scripts call the original labels, so the shims, the C++ and the patch
# encoding run for real.
# --------------------------------------------------------------------------------------------------------------------
ENTRY_PATCHES = (('LAB_0F89', 'rt_synth_init'), ('LAB_0F8C', 'rt_synth_start'), ('LAB_0F73', 'rt_synth_tick'),
                 ('LAB_0FC2', 'rt_synth_fade'), ('LAB_0FD4', 'rt_synth_reloc'))


class Port(Original):
    """The mog image with the synth entry patches and the compiled C++ (blob = flat binary at blob_base, syms = its symbols)."""

    def __init__(self, blob, blob_base, blob_size, syms, asm_mode=False):
        super().__init__(regions=[(blob_base, blob_size)])
        self.blob, self.blob_base, self.syms, self.asm_mode = blob, blob_base, syms, asm_mode
        self.int4 = syms.get('rt_synth_int4')
        self.wave = syms.get('_ZN2ms10kSynthWaveE')
        self.tab = syms.get('_ZN2ms10kSynthBlobE')
        self.rte_blob = self.int4 + 14 if self.int4 else None
        self.reset()

    def _patch(self):
        uc = self.rig.uc
        uc.mem_write(self.blob_base, self.blob)
        if self.rte_blob is not None:
            assert bytes(uc.mem_read(self.rte_blob, 2)) == bytes.fromhex('4E73'), 'rt_synth_int4 does not end in RTE where expected'
            uc.mem_write(self.rte_blob, bytes.fromhex('4E75'))
        for lab, shim in ENTRY_PATCHES:
            uc.mem_write(self.addr(lab), bytes.fromhex('4EF9') + struct.pack('>I', self.syms[shim]))
        uc.ctl_flush_tb()

    def reset(self):
        self.rig.reset()
        self._patch_orig()
        if hasattr(self, 'blob'):
            self._patch()

    def op(self, op):
        r = self.rig
        k = op[0]
        if k == 'I' and not self.asm_mode:
            r.pending, r.enar = op[1], op[2]
            r.call(self.int4)
            return self._norm(r.take_log())
        if k in ('P', 'Q') and not self.asm_mode:
            if k == 'P':
                r.uc.mem_write(self.tab + op[1] - 3570, bytes(op[2]))
            else:
                r.uc.mem_write(self.tab + 8788 - 3570 + 4 * op[1], struct.pack('>I', op[2]))
            return []
        if k in ('K', 'D'):
            raise ValueError('%s is a host-only operation' % k)
        return self._norm(Original.op(self, op))

    def _norm(self, log):
        """AUDxLC pairs that point into the C++ waveform array are rewritten to the S_45 address the original uses."""
        if self.asm_mode or self.wave is None:
            return log
        out, i = [], 0
        while i < len(log):
            e = log[i]
            if e[0] == 'w' and e[1] in (0xA0, 0xB0, 0xC0, 0xD0) and i + 1 < len(log) and log[i + 1][:2] == ('w', e[1] + 2):
                addr = (e[2] << 16) | log[i + 1][2]
                if self.wave <= addr < self.wave + 170:
                    addr = self.wave_base + addr - self.wave
                out += [('w', e[1], addr >> 16), ('w', e[1] + 2, addr & 0xFFFF)]
                i += 2
            else:
                out.append(e)
                i += 1
        return out
