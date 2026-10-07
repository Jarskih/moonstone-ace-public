"""Host test for src/game/creatures.cpp (ROADMAP 6.6): the hit / contact family of mog.asm (LAB_03CA, LAB_03DB, LAB_03BE,
LAB_03A9), the damage routine LAB_021B, the dagger flight (LAB_02D3, LAB_02F6, LAB_02FD, LAB_02CA), the creature plumbing
(LAB_0161, LAB_0171, LAB_02D0, LAB_0310, LAB_0322) and the AI predicates (LAB_02BC, LAB_02BF, LAB_02C2, LAB_02C4).

Two independent references per routine:
  * the LIFTED asm (tools/lift.py output, a literal 68k transliteration with exact CCR semantics) is run on a big-endian
    guest arena in a host driver, next to the C++ on a copy of the same arena (struct fields converted to native
    order).  After every case the whole arena and the result registers must be identical (scratch cells the C++
    deliberately does not keep, and the lifted code's stack, are masked);
  * a Python model written from the listing, with its own flag arithmetic (CMP / SUB / branch conditions evaluated
    from N, Z, V, C like the 68000 does).  Its result registers and the memory regions the routine writes must match what
    the driver reports.
LAB_0322 has no lifted twin (JSR (A1)): its reference is the Python model alone, with a deterministic stand-in for the
AI handlers.

Deliberate differences of the C++ (creatures.hpp) are excluded by the generators: plane masks 0 or above 15, attacker
cel tables without a registered hit set, divisions by zero in the dagger set-up.

Needs clang++ on PATH, PyYAML-free; the lifted sources are generated (build/creatures_test/) from the moonshard asm.
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no the IRA listing; the whole module skips)
origskip.require_listing()
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
MOG_ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
SYMS_HPP = os.path.join(ROOT, 'include', 'ms', 'gen', 'mog_syms.hpp')
PLANAR_C = os.path.join(ROOT, 'reference', 'moonshard', 'planar_contact.c')
CXX = shutil.which('clang++')
WORK_DIR = os.path.join(ROOT, 'build', 'creatures_test')

# Routines the oracle needs (lifted into WORK_DIR); callees that are already lifted in src/lifted/mog are linked from there.
LIFT = ['LAB_03DB', 'LAB_03BE', 'LAB_03A9', 'LAB_03AC', 'LAB_03B3', 'LAB_03CA', 'LAB_021B', 'LAB_02D3', 'LAB_02F6',
        'LAB_02FD', 'LAB_0310', 'LAB_02D0', 'LAB_02CA', 'LAB_0161', 'LAB_0171', 'LAB_02BC', 'LAB_02BF', 'LAB_02C2',
        'LAB_02C4', 'LAB_0BB3', 'LAB_03C7']


# ---- symbols ----------------------------------------------------------------------------------------------------
def load_syms():
    text = open(SYMS_HPP, encoding='utf-8').read()
    hunks = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'HUNK_([0-9A-F]{2}) = 0x([0-9A-F]+)u', text)}
    syms = {}
    for m in re.finditer(r'constexpr uint32_t (LAB_[0-9A-F]+|SECSTRT_[0-9]+) = HUNK_([0-9A-F]{2}) \+ 0x([0-9A-F]+)u;', text):
        syms[m.group(1)] = hunks[m.group(2)] + int(m.group(3), 16)
    return syms


SYM = load_syms()


def A(label):
    """Address of mog's LAB_<label>; a SECSTRT_<n> label is taken as it is (ROADMAP 7.1h)."""
    return SYM[label if label.startswith('SECSTRT_') else 'LAB_' + label]


# ---- arena layout (everything below ARENA) -----------------------------------------------------------------------------
ARENA = 0x180000
STACK_LO, STACK_TOP = 0x178000, 0x17F000
KNIGHT0 = A('0613')                 # five records: four knights + the dragon (LAB_0617)
REC = 132
HEAP = 0x140000                     # creature heap (LAB_05C3's value): 20 records + 2 spare
HEAP_N = 22
JOBS, WORKS, ATKL, HURTL = A('0649'), A('064B'), A('064F'), A('0650')
CELS, HITS, PLANES, MISC = 0x150000, 0x158000, 0x160000, 0x170000
BLOCK, SLOTS = A('062E'), A('0301')
PAIRS, PAIRS_END = A('0A51'), A('0A4E')
IGNORE = [(A('0A52'), 2), (A('0A53'), 2), (A('0A54'), 2), (A('0A56'), 2), (A('0635'), 2), (A('0636'), 2),
          (A('0637'), 4), (A('03B6'), 2), (STACK_LO, STACK_TOP - STACK_LO + 64)]
assert max(SYM.values()) < HEAP - 0x2000, 'image grew into the test arena'


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def w16(v):
    return v & 0xFFFF


def setw(reg, v):
    return (reg & 0xFFFF0000) | (v & 0xFFFF)


def setb(reg, v):
    return (reg & 0xFFFFFF00) | (v & 0xFF)


def ext_l(reg):
    return s16(reg) & 0xFFFFFFFF


PAGE = 4096


class Mem:
    """Big-endian guest memory (sparse: 4 KB pages, unwritten bytes read 0) with the touched ranges remembered (what the
    driver is sent)."""

    def __init__(self):
        self.pages = {}
        self.touched = []

    def copy(self):
        o = Mem()
        o.pages = {k: bytearray(v) for k, v in self.pages.items()}
        o.touched = list(self.touched)
        return o

    def touch(self, a, n):
        self.touched.append((a, a + n))

    def _page(self, a):
        pg = self.pages.get(a // PAGE)
        if pg is None:
            pg = self.pages[a // PAGE] = bytearray(PAGE)
        return pg

    def r8(self, a):
        pg = self.pages.get(a // PAGE)
        return pg[a % PAGE] if pg is not None else 0

    def r16(self, a):
        return (self.r8(a) << 8) | self.r8(a + 1)

    def r32(self, a):
        return (self.r16(a) << 16) | self.r16(a + 2)

    def _set(self, a, v):
        self._page(a)[a % PAGE] = v & 0xFF

    def w8(self, a, v):
        self._set(a, v)
        self.touch(a, 1)

    def w16(self, a, v):
        self._set(a, v >> 8)
        self._set(a + 1, v)
        self.touch(a, 2)

    def w32(self, a, v):
        self.w16(a, v >> 16)
        self.w16(a + 2, v)

    def put(self, a, data):
        for i, b in enumerate(data):
            self._set(a + i, b)
        self.touch(a, len(data))

    def zero(self, a, n):
        self.put(a, bytes(n))

    def get(self, a, n):
        return bytes(self.r8(a + i) for i in range(n))

    def fill(self, a, n, rng, lo=0, hi=255):
        self.put(a, bytes(rng.randint(lo, hi) for _ in range(n)))

    def regions(self):
        spans = sorted(self.touched)
        out = []
        for lo, hi in spans:
            if out and lo <= out[-1][1]:
                out[-1][1] = max(out[-1][1], hi)
            else:
                out.append([lo, hi])
        return out


# ---- flag arithmetic (what the 68000 computes) -------------------------------------------------------------------------
def sub(a, b, bits):
    """a - b -> (result, N, Z, V, C) on `bits` wide operands (CMP / SUB)."""
    mask = (1 << bits) - 1
    a &= mask
    b &= mask
    r = (a - b) & mask
    top = bits - 1
    n = (r >> top) & 1
    z = int(r == 0)
    v = (((a ^ b) & (a ^ r)) >> top) & 1
    c = int(a < b)
    return r, n, z, v, c


def cmp_w(src, dst):             # CMP.W src,dst -> flags of dst - src
    return sub(dst, src, 16)


def cmp_l(src, dst):
    return sub(dst, src, 32)


def BGE(f):
    return f[1] == f[3]


def BLT(f):
    return f[1] != f[3]


def BGT(f):
    return (not f[2]) and f[1] == f[3]


def BLE(f):
    return bool(f[2]) or f[1] != f[3]


def BMI(f):
    return bool(f[1])


def BPL(f):
    return not f[1]


def divs(dn, src):
    """DIVS.W src,Dn (dn 32 bit).  Returns the new Dn, or None for a zero divisor."""
    s = s16(src)
    if s == 0:
        return None
    d = s32(dn)
    q = abs(d) // abs(s)
    if (d < 0) != (s < 0):
        q = -q
    rem = d - q * s
    if q > 32767 or q < -32768:
        return dn & 0xFFFFFFFF
    return ((rem & 0xFFFF) << 16) | (q & 0xFFFF)


def divu(dn, src):
    s = src & 0xFFFF
    if s == 0:
        return None
    dn &= 0xFFFFFFFF
    q = dn // s
    if q > 0xFFFF:
        return dn
    return ((dn % s) << 16) | q


# ---- the model: mog.asm, label by label --------------------------------------------------------------------------------
def m_03CA(d0, d1, d2, d3, d5):
    """LAB_03CA: returns D5."""
    f = cmp_l(d0, d2)                    # CMP.L D0,D2
    if BMI(f):                           # BMI.S LAB_03CC
        f = cmp_l(d0, d3)                # LAB_03CC: CMP.L D0,D3
        return d5 + 1 if BGE(f) else d5  # BGE.S LAB_03CB / LAB_03CD
    f = cmp_l(d1, d2)                    # CMP.L D1,D2
    if BMI(f):
        return d5 + 1
    f = cmp_l(d0, d3)                    # CMP.L D0,D3
    if BMI(f):
        return d5                        # BMI.S LAB_03CD
    f = cmp_l(d1, d3)                    # CMP.L D1,D3
    if BGE(f):
        return d5
    return d5 + 1


class Trap(Exception):
    pass


def m_03DB(M, a0, a1, d0, d1, d2, d3, d4, d5):
    """LAB_03DB (mog.asm 8354-8508).  Returns (D0, D1, D2)."""
    mirror = 0                                                   # CLR.W LAB_0A54
    d6 = (d5 & 0xFFFF) * 10                                      # MOVE.W D5,D6 ; MULU #10,D6
    d7 = M.r8((a1 + s16(d6) + 18) & 0xFFFFFFFF)                  # MOVE.B 18(A1,D6.W),D7
    if not (d7 & 1):                                             # BTST #0,D7 ; BNE.S LAB_03DC
        mirror = M.r16((a1 + s16(d6) + 14) & 0xFFFFFFFF)         # MOVE.W 14(A1,D6.W),LAB_0A54
    a2 = PAIRS                                                   # LEA LAB_0A51,A2
    while M.r32(a2) != a1:                                       # LAB_03DD: CMPA.L (A2),A1 ; BEQ.S LAB_03DE
        a2 += 8
    a1 = M.r32(a2 + 4)                                           # LAB_03DE
    if d5 & 0xFFFF:                                              # TST.W D5 ; BEQ.S LAB_03E1
        d5 = setw(d5, d5 - 1)
        while True:                                              # LAB_03DF
            d6 = M.r8(a1)                                        # MOVEQ #0,D6 ; MOVE.B (A1)+,D6
            a1 += 1
            d6 = w16(d6 + d6)                                    # ADD.W D6,D6
            if d6 != 0:                                          # BEQ.S LAB_03E0
                d6 = w16(d6 + 3)                                 # ADDQ.W #3,D6
                a1 = (a1 + s16(d6)) & 0xFFFFFFFF                 # LEA 0(A1,D6.W),A1
            d5 = setw(d5, d5 - 1)                                # DBF D5,LAB_03DF
            if (d5 & 0xFFFF) == 0xFFFF:
                break
    if M.r8(a1) == 0:                                            # LAB_03E1: TST.B (A1) ; BEQ.W LAB_03E3
        return 0, d1, d2
    d0 = w16(d0 + d0)                                            # ADD.W D0,D0
    d5 = setw(d5, d0)                                            # MOVE.W D0,D5
    d0 = w16(d0 << 2)                                            # LSL.W #2,D0
    d0 = w16(d0 + d5)                                            # ADD.W D5,D0
    a2 = (a0 + 10 + s16(d0)) & 0xFFFFFFFF                        # LEA 10(A0,D0.W),A2
    d5 = 0                                                       # MOVEQ #0,D5
    st = [d1 & 0xFFFF, d2 & 0xFFFF, d3 & 0xFFFF, d4 & 0xFFFF]    # MOVEM.W D1-D4,-(A7): (A7)..6(A7)
    d0 = setw(d0, st[0])                                         # MOVE.W D1,D0   (D1 = def x)
    d1 = setw(d1, d0)                                            # MOVE.W D0,D1
    d1 = setw(d1, d1 + M.r16(a2 + 4))                            # ADD.W 4(A2),D1
    d3 = M.r8(a1 + 2)                                            # MOVEQ #0,D3 ; MOVE.B 2(A1),D3
    d2 = setw(d2, st[2])                                         # MOVE.W 4(A7),D2   (attacker x)
    if mirror != 0:                                              # TST.W LAB_0A54 ; BEQ.S LAB_03E2
        d2 = setw(d2, d2 + mirror)                               # ADD.W LAB_0A54,D2
        d2 = setw(d2, d2 - d3)                                   # SUB.W D3,D2
    d3 = setw(d3, d3 + d2)                                       # ADD.W D2,D3
    d5 = m_03CA(d0, d1, d2, d3, d5)                              # JSR LAB_03CA
    d0 = setw(d0, st[1])                                         # MOVE.W 2(A7),D0   (def y)
    d1 = setw(d1, d0)                                            # MOVE.W D0,D1
    d1 = setw(d1, d1 + M.r16(a2 + 6))                            # ADD.W 6(A2),D1
    d3 = M.r8(a1 + 3)                                            # MOVEQ #0,D3 ; MOVE.B 3(A1),D3
    d2 = setw(d2, st[3])                                         # MOVE.W 6(A7),D2   (attacker y)
    d3 = setw(d3, d3 + d2)                                       # ADD.W D2,D3
    d5 = m_03CA(d0, d1, d2, d3, d5)
    d1, d2, d3, d4 = (setw(d1, st[0]), setw(d2, st[1]), setw(d3, st[2]), setw(d4, st[3]))   # MOVEM.W (A7)+,D1-D4
    if (d5 & 0xFFFF) != 2:                                       # CMP.W #2,D5 ; BEQ.S LAB_03E4
        return 0, d1, d2
    a3 = (M.r32(a0 + 2) + M.r32(a2)) & 0xFFFFFFFF                # MOVEA.L 2(A0),A3 ; LEA 0(A3,D0.L),A3  (D0 = (A2).L)
    d7 = setw(d7, M.r16(a2 + 4))                                 # MOVE.W 4(A2),D7
    d7 = setw(d7, d7 + 15)                                       # ADDI.W #$f,D7
    d7 = setw(d7, (d7 & 0xFFFF) >> 4)                            # LSR.W #4,D7
    d7 = setw(d7, d7 + d7)                                       # ADD.W D7,D7
    d6 = setw(d6, M.r16(a2 + 6))                                 # MOVE.W 6(A2),D6
    d7 = (d7 & 0xFFFF) * (d6 & 0xFFFF)                           # MULU D6,D7
    stride = d7 & 0xFFFF                                         # MOVE.W D7,LAB_0A56
    d7 = M.r8(a1)                                                # MOVEQ #0,D7 ; MOVE.B (A1),D7
    d7 = setw(d7, d7 - 1)                                        # SUBQ.W #1,D7
    a1 += 4                                                      # ADDQ.L #4,A1
    while True:                                                  # LAB_03E5
        d5 = M.r8(a1)                                            # MOVEQ #0,D5 ; MOVE.B (A1)+,D5
        a1 += 1
        if mirror != 0:
            d5 = setw(d5, -d5)                                   # NEG.W D5
            d5 = setw(d5, d5 + mirror)                           # ADD.W LAB_0A54,D5
        d5 = setw(d5, d5 + d3)                                   # ADD.W D3,D5
        skip_y_byte = True
        f = cmp_w(d1, d5)                                        # CMP.W D1,D5 ; BLT.W LAB_03E9
        if not BLT(f):
            d5 = setw(d5, d5 - d1)                               # SUB.W D1,D5
            f = sub(d5, M.r16(a2 + 4), 16)                       # SUB.W 4(A2),D5 ; BGE.W LAB_03E9
            d5 = setw(d5, f[0])
            if not BGE(f):
                skip_y_byte = False
                d5 = M.r8(a1)                                    # MOVEQ #0,D5 ; MOVE.B (A1)+,D5
                a1 += 1
                d5 = setw(d5, d5 + d4)                           # ADD.W D4,D5
                f = cmp_w(d2, d5)                                # CMP.W D2,D5 ; BLT.W LAB_03EA
                if not BLT(f):
                    d5 = setw(d5, d5 - d2)                       # SUB.W D2,D5
                    f = sub(d5, M.r16(a2 + 6), 16)               # SUB.W 6(A2),D5 ; BGE.W LAB_03EA
                    d5 = setw(d5, f[0])
                    if not BGE(f):
                        d5 = M.r8(a1 - 2)                        # MOVEQ #0,D5 ; MOVE.B -2(A1),D5
                        d5 = setw(d5, d5 + d3)                   # ADD.W D3,D5
                        x_abs = d5 & 0xFFFF                      # MOVE.W D5,LAB_0A52
                        d5 = setw(d5, d5 - d1)                   # SUB.W D1,D5
                        d6 = M.r8(a1 - 1)                        # MOVEQ #0,D6 ; MOVE.B -1(A1),D6
                        d6 = setw(d6, d6 + d4)                   # ADD.W D4,D6
                        y_abs = d6 & 0xFFFF                      # MOVE.W D6,LAB_0A53
                        d6 = setw(d6, d6 - d2)                   # SUB.W D2,D6
                        sv7 = d7 & 0xFFFF                        # MOVE.W D7,-(A7)
                        d7 = setw(d7, M.r16(a2 + 4))             # MOVE.W 4(A2),D7
                        d7 = setw(d7, d7 + 15)
                        d7 = setw(d7, (d7 & 0xFFFF) >> 4)
                        d7 = setw(d7, d7 + d7)
                        d6 = (d6 & 0xFFFF) * (d7 & 0xFFFF)       # MULU D7,D6
                        d7 = setw(d7, sv7)                       # MOVE.W (A7)+,D7
                        d0 = setw(d0, d5)                        # MOVE.W D5,D0
                        d0 = setw(d0, (d0 & 0xFFFF) >> 4)        # LSR.W #4,D0
                        d0 = setw(d0, d0 + d0)                   # ADD.W D0,D0
                        d0 = setw(d0, d0 + d6)                   # ADD.W D6,D0
                        d5 = setw(d5, d5 & 0xF)                  # ANDI.W #$f,D5
                        d5 = setw(d5, d5 ^ 0xF)                  # EORI.W #$f,D5
                        a4 = a3                                  # MOVEA.L A3,A4
                        d6 = M.r16(a2 + 8) & 0xFF                # MOVE.W 8(A2),D6 ; ANDI.W #$ff,D6
                        d6 = w16(d6 + d6)                        # ADD.W D6,D6
                        d6 = POP4[(d6 >> 1)] - 1                 # LEA LAB_0A55,A5 ; MOVE.W 0(A5,D6.W),D6 ; SUBQ.W #1,D6
                        pl = d6 + 1
                        for _ in range(pl):                      # LAB_03E7 (DBF D6)
                            word = M.r16((a4 + s16(d0)) & 0xFFFFFFFF)    # MOVE.W 0(A4,D0.W),D6
                            if (word >> (d5 & 31)) & 1:          # BTST D5,D6 ; BNE.S LAB_03E8
                                return 1, x_abs, y_abs
                            a4 = (a4 + s16(stride)) & 0xFFFFFFFF # LEA 0(A4,D6.W),A4
        if skip_y_byte:
            a1 += 1                                              # LAB_03E9: ADDQ.L #1,A1
        d7 = setw(d7, d7 - 1)                                    # LAB_03EA: DBF D7,LAB_03E5
        if (d7 & 0xFFFF) == 0xFFFF:
            return 0, d1, d2


POP4 = [0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4]       # LAB_0A55 (word per mask 0..15)


def m_0161(M):
    """LAB_0161: clears +14 / +18 of the dragon, 21 heap records, the four knights."""
    heap = M.r32(A('05C3'))
    for base in [KNIGHT0 + 4 * REC] + [heap + i * REC for i in range(21)] + [KNIGHT0 + i * REC for i in range(4)]:
        M.w32(base + 14, 0)
        M.w32(base + 18, 0)


def m_03BE(M):
    """LAB_03BE (mog.asm 8161-8238)."""
    m_0161(M)
    a6 = JOBS
    for _ in range(10):                                          # D7 = 9
        if M.r8(a6) != 0:                                        # TST.B 0(A6) ; BEQ.W LAB_03C6
            a4 = M.r32(a6 + 40)
            done = False
            while not done and M.r32(a4) != 0:                   # LAB_03C0
                a5 = JOBS
                for _ in range(10):                              # D6 = 9
                    if a5 != a6 and M.r8(a5) != 0:               # CMPA.L A6,A5 ; TST.B 0(A5)
                        d0 = M.r16(a6 + 10)
                        d1 = w16(M.r16(a5 + 10) - d0)
                        if d1 & 0x8000:                          # SUB.W D0,D1 ; BPL.S LAB_03C2
                            d1 = w16(-d1)
                        if not BGT(cmp_w(10, d1)):               # CMP.W #10,D1 ; BGT.W LAB_03C5
                            a3 = M.r32(a5 + 44)
                            while M.r32(a3) != 0:                # LAB_03C3
                                r0, _x, _y = m_03DB(M, M.r32(a3), M.r32(a4), M.r16(a3 + 4), M.r16(a3 + 6),
                                                    M.r16(a3 + 8), M.r16(a4 + 6), M.r16(a4 + 8), M.r16(a4 + 4))
                                if r0 != 0:
                                    ao, ah = M.r32(a6 + 24), M.r32(a5 + 24)
                                    M.w32(ao + 14, ah)           # MOVE.L 24(A5),14(A0)
                                    M.w32(ah + 18, ao)           # MOVE.L 24(A6),18(A1)
                                    M.w16(ah + 122, _x)          # MOVE.W LAB_0A52,122(A1)
                                    M.w16(ah + 124, _y)
                                    done = True
                                    break
                                a3 += 10
                    if done:
                        break
                    a5 += 50
                if not done:
                    a4 += 10
        a6 += 50
    M.zero(ATKL, 800)                                            # LAB_03C7
    M.zero(HURTL, 800)


def m_03A9(M, a0, d0, d2):
    """LAB_03A9 (mog.asm 8022-8160).  Returns the mask word."""
    step = d0 & 0xFFFF
    facing = d2 & 3
    mask = 0x1F
    a6 = JOBS
    for _ in range(10):
        if M.r8(a6) != 0 and M.r32(a6 + 24) != a0:               # TST.B 0(A6) ; CMPA.L A0,A1
            a1 = M.r32(a6 + 24)
            if M.r16(a1 + 58) != 0 and BGT(sub(M.r16(a1 + 80), 0, 16)):      # TST.W 58(A1) ; TST.W 80(A1) ; BLE
                mask = m_03AC(M, a0, a1, step, facing, mask)
        a6 += 50
    return mask


def m_03B3(M, a0, a1):
    d1 = M.r16(a1 + 8)                                           # MOVE.W 8(A1),D1
    d2 = w16(M.r16(a0 + 8) - d1)                                 # MOVE.W 8(A0),D2 ; SUB.W D1,D2
    if d2 & 0x8000:                                              # BPL.S LAB_03B4
        d2 = w16(-d2)
    return 0 if BGT(cmp_w(10, d2)) else 1


def m_03AC(M, a0, a1, step, facing, mask):
    d5 = 0
    d6 = 0
    ran_x = False
    if m_03B3(M, a0, a1) != 0:                                   # JSR LAB_03B3 ; TST.L D0 ; BEQ.W LAB_03AF
        d6 = 0
        if facing == 1:                                          # CMPI.W #1,LAB_0636 ; BEQ.S LAB_03AD
            d4 = M.r16(a0 + 4)
            go = not BGT(cmp_w(M.r16(a1 + 4), d4))               # CMP.W 4(A1),D4 ; BGT.W LAB_03AF
        else:
            d6 = 1
            d4 = M.r16(a0 + 4)
            go = not BLT(cmp_w(M.r16(a1 + 4), d4))               # CMP.W 4(A1),D4 ; BLT.W LAB_03AF
        if go:
            d0 = w16(M.r16(a0 + 58) + step)
            d1 = w16(M.r16(a0 + 60) + step)
            d5 = m_03CA(d0, d1, M.r16(a1 + 58), M.r16(a1 + 60), 0)
            d5 = m_03CA(M.r16(a0 + 112), M.r16(a0 + 114), M.r16(a1 + 112), M.r16(a1 + 114), d5)
            if d5 == 2:                                          # CMP.L #2,D5 ; BNE.S LAB_03AF
                mask &= ~(1 << d6)                               # BCLR D6,LAB_03B6+1
    # LAB_03AF
    d5 = m_03CA(M.r16(a0 + 58), M.r16(a0 + 60), M.r16(a1 + 58), M.r16(a1 + 60), 0)
    d1 = M.r16(a1 + 8)
    d2 = w16(M.r16(a0 + 8) - d1)
    if BPL(sub(M.r16(a0 + 8), d1, 16)):                          # SUB.W D1,D2 ; BPL.S LAB_03B0
        d6 = 3
    else:
        d6 = 2
        d2 = w16(-d2)                                            # NEG.W D2
    if not BGT(cmp_w(20, d2)):                                   # CMP.W #20,D2 ; BGT.S LAB_03B2
        d5 = m_03CA(M.r16(a0 + 112), M.r16(a0 + 114), M.r16(a1 + 112), M.r16(a1 + 114), d5)
        if d5 == 2:
            mask &= ~(1 << d6)
    return mask


def m_021B(M, a0):
    """LAB_021B -> D0 (long)."""
    a1 = M.r32(a0 + 42)
    d2 = M.r16(a0 + 64)
    d0 = M.r32((a1 + s16(d2)) & 0xFFFFFFFF)                      # MOVE.L 0(A1,D2.W),D0
    d1 = M.r8(a0 + 70)                                           # MOVEQ #0,D1 ; MOVE.B 70(A0),D1
    d0 = setw(d0, d0 + d1)                                       # ADD.W D1,D0
    if M.r32(a0 + 88) == 0x17:
        d0 = setw(d0, d0 + 2)
    if M.r32(a0 + 88) == 0x18:
        d0 = setw(d0, d0 + 3)
    if M.r32(a0 + 88) == 0x19:
        d0 = setw(d0, d0 + 5)
    if d2 == 0x20:                                               # CMP.W #$20,D2 ; BNE.S LAB_021F
        d0 = setw(d0, d0 << 1)
    d2 = M.r16(A('05E4') + 18)
    a2 = M.r32(a0 + 96)
    d1 = M.r8(a2 + 22)
    dbl = False
    if d1 != 0:
        if (d1 & 1) and d2 == 0x2E:
            dbl = True
        elif (d1 & 8) and d2 == 0x2E:
            dbl = True
        elif (d1 & 2) and d2 == 0x2D:
            dbl = True
        elif (d1 & 4) and d2 == 0x31:
            dbl = True
    if dbl:
        d0 = setw(d0, d0 << 1)                                   # LAB_0223
    return d0


def m_02D3(M, a0_in, d7):
    """LAB_02D3 (mog.asm 6535-6592): block, cells.  Returns (LAB_0628, LAB_0629, LAB_02DA)."""
    a1 = M.r32(A('0633'))
    a2 = M.r32(A('0634'))
    d0 = M.r16(a1 + 4)
    d1 = w16(M.r16(a2 + 4) - d0)                                 # MOVE.W 4(A2),D1 ; SUB.W D0,D1
    if not BMI(sub(M.r16(a2 + 4), d0, 16)):                      # BMI.S LAB_02D4
        d7 = w16(-d7)                                            # NEG.W D7
    M.w32(BLOCK, a1)
    M.w16(BLOCK + 4, M.r16(a1 + 4))
    M.w16(BLOCK + 6, M.r16(a1 + 8))
    M.w16(BLOCK + 8, M.r16(a1 + 6))
    M.w16(BLOCK + 10, w16(M.r16(a2 + 4) + d7))                   # (A3) = 4(A2) ; ADD.W D7,(A3)+
    M.w16(a1 + 126, w16(M.r16(a2 + 4) + d7))                     # MOVE.W 4(A2),126(A1) ; ADD.W D7,126(A1)
    M.w16(BLOCK + 12, M.r16(a2 + 8))
    M.w16(a0_in + 128, M.r16(a2 + 8))                            # MOVE.W 8(A2),128(A0)  (A0 as passed in)
    M.w16(BLOCK + 14, M.r16(a2 + 6))
    d0 = M.r16(BLOCK + 4)
    d1 = w16(M.r16(BLOCK + 10) - d0)
    if not BPL(sub(M.r16(BLOCK + 10), d0, 16)):                  # BPL.S LAB_02D5
        d1 = w16(-d1)
    dist = d1                                                    # MOVE.W D1,LAB_02DA
    d0 = M.r16(BLOCK + 6)
    d1 = w16(M.r16(BLOCK + 12) - d0)
    if not BPL(sub(M.r16(BLOCK + 12), d0, 16)):
        d1 = w16(-d1)
    if not BLT(cmp_w(dist, d1)):                                 # CMP.W LAB_02DA,D1 ; BLT.S LAB_02D7
        dist = d1
    d0 = dist >> 1                                               # LSR.W #1,D0
    d1 = dist >> 3                                               # LSR.W #3,D1
    c29, c28 = d0, d1
    if not BGT(cmp_w(2, c29)):                                   # CMPI.W #2,LAB_0629 ; BGT.S LAB_02D8
        c29 = 3
    if not BGT(cmp_w(4, c28)):
        c28 = 4
        c29 = 10
    M.w16(BLOCK + 16, c28)
    M.w16(BLOCK + 18, c29)
    M.w16(a1 + 106, c28)                                         # MOVE.W LAB_0628,106(A1)
    M.w16(A('0628'), c28)
    M.w16(A('0629'), c29)
    M.w16(A('02DA'), dist)
    return c28, c29, dist


def m_02F6(M):
    """LAB_02F6 (mog.asm 6722-6817).  Returns D0 (0 / -1), or 'trap' for a divide by zero."""
    d0 = M.r32(BLOCK)
    a0 = SLOTS
    for _ in range(6):                                           # D7 = 5
        if M.r32(a0) == d0 or M.r32(a0) == 0:                    # CMP.L (A0),D0 ; BEQ ; TST.L (A0) ; BEQ
            break
        a0 += 20
    else:
        return -1
    a2 = M.r32(A('0633'))
    a1 = BLOCK
    M.w16(a2 + 126, M.r16(0x0A))                                 # MOVE.W EXT_0000,126(A2)  ($A)
    M.w16(a2 + 128, M.r16(0x0C))                                 # MOVE.W ADR_ERROR,128(A2) ($C)
    d0 = setw(d0, M.r16(a1 + 8))
    d0 = setw(d0, d0 - M.r16(a1 + 14))                           # SUB.W 14(A1),D0
    if (d0 & 0xFFFF) & 0x8000:                                   # TST.W D0 ; BGE.S LAB_02F9
        d0 = setw(d0, -d0)
    if BLE(cmp_w(5, d0 & 0xFFFF)):                               # CMP.W #5,D0 ; BLE.S LAB_02FB
        # LAB_02FB
        M.w32(a0, M.r32(a1))
        M.w16(a0 + 4, M.r16(a1 + 16))
        d0 = setw(d0, M.r16(a1 + 16))
        d0 = setw(d0, d0 + 1)                                    # ADDQ.W #1,D0
        d0 = setw(d0, (d0 & 0xFFFF) >> 1)                        # LSR.W #1,D0
        d1 = M.r16(a1 + 18)                                      # MOVEQ #0,D1 ; MOVE.W 18(A1),D1
        d1 = setw(d1, d1 << 8)                                   # LSL.W #8,D1
        d1 = divu(d1, d0)                                        # DIVU D0,D1
        if d1 is None:
            return 'trap'
        d1 = setw(d1, d1 + d1)                                   # ADD.W D1,D1
        M.w16(a0 + 6, d1)
        d1 = ext_l(d1)                                           # EXT.L D1
        d0 = setw(d0, d0 - 1)                                    # SUBQ.W #1,D0
        d1 = divu(d1, d0)
        if d1 is None:
            return 'trap'
        M.w16(a0 + 8, d1)
    else:
        M.w32(a0, M.r32(a1))                                     # MOVE.L (A1),(A0)+
        M.w16(a0 + 4, M.r16(a1 + 16))
        d0 = setw(d0, M.r16(a1 + 16))
        d1 = M.r16(a1 + 8)                                       # MOVEQ #0,D1 ; MOVE.W 8(A1),D1
        f = sub(d1, M.r16(a1 + 14), 16)                          # SUB.W 14(A1),D1
        d1 = setw(d1, f[0])
        if not BLT(f):                                           # BLT.S LAB_02FA
            d1 = ext_l(d1)
            d1 = (d1 << 8) & 0xFFFFFFFF                          # ASL.L #8,D1
            d1 = divs(d1, d0)
            if d1 is None:
                return 'trap'
            d1 = setw(d1, d1 + d1)
            M.w16(a0 + 6, d1)
            d1 = ext_l(d1)
            d0 = setw(d0, d0 - 1)
            d1 = divs(d1, d0)
            if d1 is None:
                return 'trap'
            M.w16(a0 + 8, d1)
        else:
            M.w16(a0 + 6, 0)                                     # CLR.W (A0)+
            d1 = ext_l(d1)
            d1 = (d1 << 8) & 0xFFFFFFFF
            d1 = divs(d1, d0)
            if d1 is None:
                return 'trap'
            d1 = setw(d1, d1 + d1)
            d1 = ext_l(d1)
            d0 = setw(d0, d0 - 1)
            d1 = divs(d1, d0)
            if d1 is None:
                return 'trap'
            d1 = setw(d1, -d1)                                   # NEG.W D1
            M.w16(a0 + 8, d1)
    # LAB_02FC
    d0 = setw(d0, M.r16(a1 + 16))
    d1 = setw(0, M.r16(a1 + 10) - M.r16(a1 + 4))
    d1 = setw(d1, d1 << 6)                                       # ASL.W #6,D1
    d1 = divs(ext_l(d1), d0)
    if d1 is None:
        return 'trap'
    M.w16(a0 + 10, d1)
    d1 = setw(d1, M.r16(a1 + 12) - M.r16(a1 + 6))
    d1 = setw(d1, d1 << 6)
    d1 = divs(ext_l(d1), d0)
    if d1 is None:
        return 'trap'
    M.w16(a0 + 12, d1)
    M.w16(a0 + 14, M.r16(a1 + 4) << 6)
    M.w16(a0 + 16, M.r16(a1 + 6) << 6)
    M.w16(a0 + 18, M.r16(a1 + 8) << 8)
    return 0


def asr_w(v, n):
    return w16(s16(v) >> n)


def m_02FD(M, d0):
    """LAB_02FD.  Returns (D0, D1, D2, D3) words or (-1,) when no slot."""
    a0 = SLOTS
    for _ in range(6):
        if M.r32(a0) == d0:
            break
        a0 += 20
    else:
        return (-1,)
    d1 = M.r16(a0 + 6)
    d0 = M.r16(a0 + 8)
    M.w16(a0 + 6, M.r16(a0 + 6) - d0)
    M.w16(a0 + 18, M.r16(a0 + 18) - d1)
    M.w16(a0 + 14, M.r16(a0 + 14) + M.r16(a0 + 10))
    M.w16(a0 + 16, M.r16(a0 + 16) + M.r16(a0 + 12))
    d1 = asr_w(M.r16(a0 + 14), 6)
    d2 = asr_w(M.r16(a0 + 16), 6)
    d3 = asr_w(M.r16(a0 + 18), 8)
    M.w16(a0 + 4, M.r16(a0 + 4) - 1)
    if M.r16(a0 + 4) != 0:                                       # BNE.S LAB_0300
        return (0, d1, d2, d3)
    M.w32(a0, 0)
    return (1, d1, d2, d3)


def m_0310(M, a0, a1, a2, d0, d1, d2, d3, d5):
    """LAB_0310 -> D0."""
    a6 = JOBS
    for _ in range(10):
        if M.r8(a6) == 0:
            a5 = M.r32(a6 + 36)
            for i in range(36):
                M.w8(a5 + i, 0)
            M.w32(a6 + 2, a0)
            M.w32(a6 + 24, a1)
            M.w32(a6 + 28, a2)
            M.w16(a6 + 6, d0)
            M.w16(a6 + 8, d1)
            M.w16(a6 + 10, d2)
            M.w8(a6 + 22, d3)
            M.w8(a6 + 32, d5)
            M.w8(a6 + 0, 1)
            M.w8(a6 + 1, 1)
            return 0
        a6 += 50
    return 1


def m_0171(M):
    a1 = M.r32(A('05C3'))
    for _ in range(20):
        if M.r32(a1) == 0:
            M.w32(a1, 1)
            return a1
        a1 += REC
    return a1


def m_02D0(M, a0, a2, d0, d1, d2, d3, d5):
    """LAB_02D0 -> (A1, D0)."""
    a1 = m_0171(M)
    M.w16(a1 + 4, d0)
    M.w16(a1 + 6, d1)
    M.w16(a1 + 8, d2)
    M.w8(a1 + 10, d3)
    M.w32(a1 + 38, a2)
    M.w32(a1 + 14, 0)
    M.w32(a1 + 18, 0)
    M.w32(a1, 1)
    M.w8(a1 + 77, d5)
    return a1, m_0310(M, a0, a1, a2, d0, d1, d2, d3, d5)


def m_02CA(M, a1, a2, d0, d1, d2, d3):
    """LAB_02CA -> (A1, D0)."""
    M.w8(a1 + 76, M.r8(a1 + 76) - 1)
    a1n, st = m_02D0(M, A('07EB'), a2, d0, d1, d2, d3, 52)
    M.w32(a1n + 42, A('0302'))
    M.w16(a1n + 64, 0x0C)
    M.w8(a1n + 77, 0x34)
    return a1n, st


def handler_model(fn, owner):
    """The stand-in for an AI handler (LAB_0322's JSR (A1)); the C++ driver implements the same rule."""
    h = (fn * 2654435761 + owner * 40503) & 0xFFFFFFFF
    kind = h & 7
    if kind < 2:
        return (0xFFFFFFFF, 0, 0, 0, 0)
    if kind == 2:
        return (0, 0, 0, 0, 0)
    return (0x00400000 + (h >> 8 & 0xFFFF) * 2, (h >> 3) & 0x1FF, (h >> 12) & 0xFF, (h >> 20) & 0xFF, (h >> 5) & 3)


def m_0322(M, handlers):
    a6 = JOBS
    for _ in range(10):
        if M.r8(a6) != 0:
            a0 = M.r32(a6 + 24)
            if M.r32(a0 + 14) != 0 or M.r32(a0 + 18) != 0 or M.r8(a6 + 1) == 0:
                d0 = M.r8(a6 + 32)
                fn = M.r32(handlers + d0)
                s, x, y, z, f = handler_model(fn, a0)
                if s == 0xFFFFFFFF:
                    pass
                elif s == 0:
                    M.w16(a6, 0)
                    M.w32(M.r32(a6 + 24), 0)
                else:
                    M.w32(a6 + 2, s)
                    M.w16(a6 + 6, x)
                    M.w16(a6 + 8, y)
                    M.w16(a6 + 10, z)
                    M.w8(a6 + 22, f)
                    M.w8(a6 + 1, 1)
        a6 += 50


# ---- case generators -----------------------------------------------------------------------------------------------
class Case:
    def __init__(self, fn, mem, regs, dumps=(), note=''):
        self.fn, self.mem, self.regs, self.dumps, self.note = fn, mem, dict(regs), list(dumps), note


def base_mem(rng):
    """The constants of the arena: popcount table, junk words, the heap pointer cell, the hit-set end pointer."""
    M = Mem()
    for i, c in enumerate(POP4):
        M.w16(A('0A55') + 2 * i, c)
    M.w16(0x0A, rng.randint(0, 0xFFFF))
    M.w16(0x0C, rng.randint(0, 0xFFFF))
    M.w32(A('05C3'), HEAP)
    return M


def pos16(rng, lo=0, hi=330):
    """A coordinate: mostly on screen, sometimes near the 16-bit wrap."""
    r = rng.random()
    if r < 0.04:
        return rng.randint(0xFF00, 0xFFFF)
    return rng.randint(lo, hi)


def build_cel(M, rng, addr, planes_base, nframes, wmax=64, hmax=48, mask_choices=(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
              density=0.2, flags_choices=(0, 1)):
    """A cel table at addr: 10-byte header (+2 = plane base) and nframes 10-byte entries.  Returns the entries."""
    hdr = bytes(rng.randint(0, 255) for _ in range(10))
    M.put(addr, hdr)
    M.w32(addr + 2, planes_base)
    ents = []
    off = 0
    for f in range(nframes):
        w = rng.randint(1, wmax)
        h = rng.randint(1, hmax)
        mask = rng.choice(mask_choices)
        flag = rng.choice(flags_choices)
        rowb = ((w + 15) >> 4) << 1
        stride = rowb * h
        planes = bin(mask).count('1')
        M.w32(addr + 10 + 10 * f, off)
        M.w16(addr + 14 + 10 * f, w)
        M.w16(addr + 16 + 10 * f, h)
        fbyte = (rng.randint(0, 255) & 0xFE) | flag             # only bit 0 of the flag byte means anything
        M.w16(addr + 18 + 10 * f, (fbyte << 8) | mask)
        # random bits of the planes of this frame
        total = stride * planes + 4
        data = bytearray(total)
        for i in range(total):
            b = 0
            for k in range(8):
                if rng.random() < density:
                    b |= 1 << k
            data[i] = b
        M.put(planes_base + off, bytes(data))
        ents.append((off, w, h, mask, flag, stride))
        off += total + rng.randint(0, 6)
    return ents


def build_hit_set(M, rng, addr, nframes, maxdx=40, maxdy=40):
    """A hit set: nframes records ({count, ?, maxdx, maxdy, points} or the single byte 0).  Returns its size."""
    p = addr
    for f in range(nframes):
        if rng.random() < 0.15:
            M.w8(p, 0)
            p += 1
            continue
        n = rng.randint(1, 6)
        mdx = rng.randint(1, maxdx)
        mdy = rng.randint(1, maxdy)
        M.put(p, bytes([n, rng.randint(0, 255), mdx, mdy]))
        pts = []
        for _ in range(n):
            pts.append(rng.randint(0, mdx))
            pts.append(rng.randint(0, mdy))
        M.put(p + 4, bytes(pts))
        p += 4 + 2 * n
    return p - addr


def regs0(**kw):
    r = {'D%d' % i: 0 for i in range(8)}
    r.update({'A%d' % i: 0 for i in range(7)})
    r.update(kw)
    return r


def gen_03DB(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        nd = rng.randint(1, 4)
        na = rng.randint(1, 5)
        deft = CELS
        att = CELS + 0x400
        dents = build_cel(M, rng, deft, PLANES, nd, density=rng.choice((0.1, 0.3, 0.6, 0.9)))
        aents = build_cel(M, rng, att, PLANES + 0x8000, na)
        hit = HITS
        build_hit_set(M, rng, hit, na)
        M.w32(PAIRS, att)
        M.w32(PAIRS + 4, hit)
        M.w32(PAIRS_END, PAIRS + 8)
        # junk pair first sometimes so the search has to step
        if rng.random() < 0.4:
            M.w32(PAIRS, 0x00123456)
            M.w32(PAIRS + 4, 0x00654321)
            M.w32(PAIRS + 8, att)
            M.w32(PAIRS + 12, hit)
            M.w32(PAIRS_END, PAIRS + 16)
        df = rng.randrange(nd)
        af = rng.randrange(na)
        ox = pos16(rng, 0, 200)
        oy = pos16(rng, 0, 120)
        dx = ox if rng.random() < 0.5 else w16(ox + rng.randint(-30, 30))
        dy = oy if rng.random() < 0.5 else w16(oy + rng.randint(-30, 30))
        ax = w16(dx + rng.randint(-20, 40))
        ay = w16(dy + rng.randint(-20, 40))
        cases.append(Case('03DB', M, regs0(A0=deft, A1=att, D0=df, D1=dx, D2=dy, D3=ax, D4=ay, D5=af)))
    return cases


def setup_records(M, rng):
    """Random bytes in all five static records and the heap (so stray writes show), links cleared where the test wants."""
    M.fill(KNIGHT0, 5 * REC, rng)
    M.fill(HEAP, HEAP_N * REC, rng)


def setup_jobs(M, rng, owners, active_p=0.7):
    """Ten job slots with the work / list pointers; owners = list of record addresses to draw from."""
    for i in range(10):
        j = JOBS + 50 * i
        M.fill(j, 50, rng)
        M.w8(j + 0, 1 if rng.random() < active_p else 0)
        M.w32(j + 24, rng.choice(owners))
        M.w32(j + 36, WORKS + 36 * i)
        M.w32(j + 40, ATKL + 80 * i)
        M.w32(j + 44, HURTL + 80 * i)
        M.fill(WORKS + 36 * i, 36, rng)
        M.fill(ATKL + 80 * i, 80, rng, 0, 0)
        M.fill(HURTL + 80 * i, 80, rng, 0, 0)


def gen_03BE(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        owners = [KNIGHT0 + REC * i for i in range(5)] + [HEAP + REC * i for i in range(20)]
        rng.shuffle(owners)
        setup_jobs(M, rng, owners[:10] if rng.random() < 0.8 else owners[:4])
        ncel = rng.randint(2, 5)
        cels = []
        base_planes = PLANES
        for k in range(ncel):
            addr = CELS + 0x400 * k
            ents = build_cel(M, rng, addr, base_planes, rng.randint(1, 3), wmax=48, hmax=32, density=rng.choice((0.1, 0.3, 0.7)))
            base_planes += 0x4000
            cels.append((addr, len(ents)))
        # hit sets for each cel table (as attackers): frames up to its count
        pairs = []
        hp = HITS
        for addr, nf in cels:
            if rng.random() < 0.9:
                size = build_hit_set(M, rng, hp, nf, 30, 30)
                pairs.append((addr, hp))
                hp += size + 8
            # else: no hit set; the generator keeps such cels out of the attack lists below
        pk = {a for a, _ in pairs}
        npairs = min(len(pairs), 10)
        for i, (a, h) in enumerate(pairs[:npairs]):
            M.w32(PAIRS + 8 * i, a)
            M.w32(PAIRS + 8 * i + 4, h)
        M.w32(PAIRS_END, PAIRS + 8 * npairs)
        atk_cels = [(a, nf) for a, nf in cels if a in {p[0] for p in pairs[:npairs]}]
        zbase = rng.randint(0, 40)
        for i in range(10):
            j = JOBS + 50 * i
            M.w16(j + 10, zbase + rng.randint(0, 14) if rng.random() < 0.85 else rng.randint(0, 60000))
            for lst, base, usek in ((ATKL, 40, 'a'), (HURTL, 44, 'h')):
                cnt = rng.randint(0, 3)
                p = M.r32(j + base)
                for e in range(cnt):
                    pool = atk_cels if usek == 'a' else cels
                    if not pool:
                        break
                    addr, nf = rng.choice(pool)
                    M.w32(p, addr)
                    M.w16(p + 4, rng.randrange(nf))
                    M.w16(p + 6, rng.randint(0, 120))
                    M.w16(p + 8, rng.randint(0, 80))
                    p += 10
        dumps = [(KNIGHT0, 5 * REC), (HEAP, HEAP_N * REC), (ATKL, 800), (HURTL, 800)]
        cases.append(Case('03BE', M, regs0(), dumps))
    return cases


def gen_03A9(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        recs = [KNIGHT0 + REC * i for i in range(5)] + [HEAP + REC * i for i in range(6)]
        setup_jobs(M, rng, rng.sample(recs, 10) if rng.random() < 0.9 else recs[:3] * 3 + [recs[4]], active_p=0.7)
        for r in recs:
            def box(lo_hi):
                a = rng.randint(0, 200)
                return a, a + rng.randint(0, 40)
            x0, x1 = box(0)
            y0, y1 = box(0)
            if rng.random() < 0.15:
                x0 = 0
            M.w16(r + 58, x0)
            M.w16(r + 60, x1)
            M.w16(r + 112, y0)
            M.w16(r + 114, y1)
            M.w16(r + 80, rng.choice((-5, 0, 3, 20, 100)) & 0xFFFF)
            M.w16(r + 4, rng.randint(0, 250) if rng.random() < 0.9 else rng.randint(0, 0xFFFF))
            M.w16(r + 8, rng.randint(0, 60) if rng.random() < 0.9 else rng.randint(0, 0xFFFF))
        a0 = rng.choice(recs)
        step = rng.randint(-12, 12) & 0xFFFF
        facing = rng.choice((1, 3, 1, 3, 0, 2, 5, 0x0101))
        cases.append(Case('03A9', M, regs0(A0=a0, D0=step, D2=facing | (rng.randint(0, 255) << 8 if rng.random() < 0.3 else 0))))
    return cases


def gen_021B(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        atk = rng.choice([KNIGHT0 + REC * i for i in range(5)] + [HEAP + REC * i for i in range(4)])
        tab = MISC
        M.put(tab, bytes(rng.randint(0, 255) if rng.random() < 0.3 else 0 for _ in range(48)))
        for k in range(9):
            M.w32(tab + 4 * k, rng.choice((0, 1, 3, 6, 10, 14, 0x00010007, 0xFFFF0005, rng.randint(0, 0xFFFFFFFF))))
        inv = MISC + 0x100
        M.fill(inv, 24, rng)
        M.w8(inv + 22, rng.choice((0, 1, 2, 4, 8, 3, 5, 9, 15, rng.randint(0, 255))))
        M.w32(atk + 42, tab)
        M.w32(atk + 96, inv)
        M.w16(atk + 64, rng.choice((0, 4, 8, 12, 16, 20, 24, 28, 32, 32, 32)))
        M.w8(atk + 70, rng.randint(0, 255) if rng.random() < 0.2 else rng.randint(0, 12))
        M.w32(atk + 88, rng.choice((0x16, 0x17, 0x18, 0x19, 0x1A, 0)))
        M.w16(A('05E4') + 18, rng.choice((0x2D, 0x2E, 0x2F, 0x30, 0x31, rng.randint(0, 0xFFFF))))
        cases.append(Case('021B', M, regs0(A0=atk, D1=rng.randint(0, 2**32 - 1), D2=rng.randint(0, 2**32 - 1), A1=rng.randint(0, 2**20), D0=rng.randint(0, 2**32 - 1))))
    return cases


def gen_02D3(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        a1 = rng.choice([KNIGHT0 + REC * i for i in range(4)] + [HEAP + REC * i for i in range(4)])
        a2 = rng.choice([KNIGHT0 + REC * i for i in range(4)] + [HEAP + REC * i for i in range(4)])
        M.w32(A('0633'), a1)
        M.w32(A('0634'), a2)
        for r in (a1, a2):
            M.w16(r + 4, pos16(rng, 0, 330))
            M.w16(r + 6, rng.randint(0, 80) if rng.random() < 0.8 else rng.randint(0, 0xFFFF))
            M.w16(r + 8, pos16(rng, 20, 160))
        a0 = a1 if rng.random() < 0.8 else rng.choice([a2, HEAP + 5 * REC])
        sp = rng.choice((80, 50, 25, 5, 0, 0x7FFF, -40, 100))
        cases.append(Case('02D3', M, regs0(A0=a0, D7=sp & 0xFFFF), [(BLOCK, 20), (A('0628'), 6), (A('02DA'), 2), (a1, REC), (a0, REC)]))
    return cases


def gen_02F6(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        a1 = rng.choice([KNIGHT0 + REC * i for i in range(4)] + [HEAP + REC * i for i in range(4)])
        M.w32(A('0633'), a1)
        M.fill(SLOTS, 120, rng)
        # slots: some free, some owned by others, maybe the owner's own
        owner = a1
        for s in range(6):
            r = rng.random()
            M.w32(SLOTS + 20 * s, 0 if r < 0.3 else (owner if r < 0.4 else rng.randint(1, 0xFFFFFF) | 0x100))
        if rng.random() < 0.12:
            for s in range(6):
                M.w32(SLOTS + 20 * s, 0x100 + s)
        M.w32(BLOCK, owner)
        h = rng.randint(0, 120) if rng.random() < 0.8 else rng.randint(0, 0xFFFF)
        th = h + rng.choice((0, 0, 1, 3, 5, 6, 9, -4, -6, -30, 40, 90)) if rng.random() < 0.85 else rng.randint(0, 0xFFFF)
        steps = rng.choice((3, 4, 5, 8, 12, 14, 17, 20, 40, 100, 255, rng.randint(2, 400)))
        M.w16(BLOCK + 4, pos16(rng, 0, 330))
        M.w16(BLOCK + 6, rng.randint(20, 150))
        M.w16(BLOCK + 8, h)
        M.w16(BLOCK + 10, pos16(rng, 0, 330))
        M.w16(BLOCK + 12, rng.randint(20, 150))
        M.w16(BLOCK + 14, th)
        M.w16(BLOCK + 16, steps)
        M.w16(BLOCK + 18, rng.choice((0, 2, 0x78, rng.randint(0, 0xFFFF))))
        cases.append(Case('02F6', M, regs0(), [(SLOTS, 120), (a1, REC)]))
    return cases


def gen_02FD(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        M.fill(SLOTS, 120, rng)
        key = rng.randint(1, 0xFFFFFF) | 0x1000
        slot = rng.randrange(7)
        for s in range(6):
            M.w32(SLOTS + 20 * s, 0 if rng.random() < 0.3 else rng.randint(1, 0xFFFFFF) | 0x2000)
        if slot < 6:
            M.w32(SLOTS + 20 * slot, key)
            M.w16(SLOTS + 20 * slot + 4, rng.choice((1, 1, 2, 5, 14, 0, 0xFFFF)))
        cases.append(Case('02FD', M, regs0(D0=key, D1=rng.randint(0, 2**32 - 1), D2=rng.randint(0, 2**32 - 1), D3=rng.randint(0, 2**32 - 1)),
                          [(SLOTS, 120)]))
    return cases


def gen_jobs_common(M, rng, owners):
    setup_jobs(M, rng, owners, active_p=rng.choice((0.0, 0.3, 0.7, 1.0)))


def gen_0310(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        gen_jobs_common(M, rng, [HEAP + REC * i for i in range(10)])
        r = regs0(A0=rng.randint(0, 2**24), A1=HEAP + REC * rng.randrange(20), A2=rng.randint(0, 2**24),
                  D0=rng.randint(0, 0xFFFF), D1=rng.randint(0, 0xFFFF), D2=rng.randint(0, 0xFFFF), D3=rng.randint(0, 255),
                  D5=rng.choice((0x0C, 0x10, 0x34, rng.randint(0, 255))))
        cases.append(Case('0310', M, r, [(JOBS, 500), (WORKS, 360)]))
    return cases


def gen_02D0(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        for i in range(20):
            M.w32(HEAP + REC * i, 0 if rng.random() < rng.choice((0.1, 0.5, 0.9)) else rng.randint(1, 0xFFFF))
        if rng.random() < 0.15:                       # a full heap: the record lands one past the 20 (the asm writes there)
            for i in range(20):
                M.w32(HEAP + REC * i, 1)
        gen_jobs_common(M, rng, [HEAP + REC * i for i in range(10)])
        r = regs0(A0=rng.randint(0, 2**24), A2=rng.randint(0, 2**24),
                  D0=rng.randint(0, 0xFFFF), D1=rng.randint(0, 0xFFFF), D2=rng.randint(0, 0xFFFF), D3=rng.randint(0, 255),
                  D5=rng.choice((0x0C, 0x28, 0x34, rng.randint(0, 255))))
        cases.append(Case('02D0', M, r, [(JOBS, 500), (WORKS, 360), (HEAP, HEAP_N * REC)]))
    return cases


def gen_02CA(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        for i in range(20):
            M.w32(HEAP + REC * i, 0 if rng.random() < 0.6 else 5)
        gen_jobs_common(M, rng, [HEAP + REC * i for i in range(10)])
        th = KNIGHT0 + REC * rng.randrange(4)
        M.w8(th + 76, rng.choice((0, 1, 5, 10)))
        r = regs0(A1=th, A2=rng.randint(0, 2**24), D0=rng.randint(0, 0xFFFF), D1=rng.randint(0, 0xFFFF),
                  D2=rng.randint(0, 0xFFFF), D3=rng.randint(0, 255))
        cases.append(Case('02CA', M, r, [(JOBS, 500), (WORKS, 360), (HEAP, HEAP_N * REC), (th, REC)]))
    return cases


HANDLERS = MISC + 0x400                    # a stand-in handler table (LAB_08C7): 256 bytes of longs


def gen_0322(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        gen_jobs_common(M, rng, [HEAP + REC * i for i in range(10)] if rng.random() < 0.8 else [HEAP, HEAP + REC])
        for i in range(10):
            j = JOBS + 50 * i
            M.w8(j + 32, rng.choice((0x0C, 0x10, 0x14, 0x18, 0x20, 0x28, 0x34, rng.randint(0, 255))))
            M.w8(j + 1, rng.randint(0, 1))
        for i in range(21):
            own = HEAP + REC * i
            if rng.random() < 0.7:
                M.w32(own + 14, 0)
                M.w32(own + 18, 0)
        for k in range(64):
            M.w32(HANDLERS + 4 * k, rng.randint(0x400000, 0x4FFFFF))
        cases.append(Case('0322', M, regs0(), [(JOBS, 500), (HEAP, HEAP_N * REC)]))
    return cases


def gen_pred(rng, n):
    cases = []
    for ci in range(n):
        M = base_mem(rng)
        setup_records(M, rng)
        a = rng.choice([KNIGHT0 + REC * i for i in range(4)])
        b = rng.choice([HEAP + REC * i for i in range(4)] + [KNIGHT0 + 4 * REC])
        M.w32(A('0633'), a)
        M.w32(A('0634'), b)
        for r in (a, b):
            M.w16(r + 4, pos16(rng, 0, 330) if rng.random() < 0.8 else rng.randint(0, 0xFFFF))
            M.w16(r + 8, pos16(rng, 0, 160) if rng.random() < 0.8 else rng.randint(0, 0xFFFF))
        M.w16(a + 120, rng.choice((4, 5, 10, 40, rng.randint(0, 0xFFFF))))
        fn = rng.choice(('02BC', '02BF', '02C2', '02C4'))
        regs = regs0(A0=b, A1=a, D7=rng.choice((0, 10, 40, 100, rng.randint(0, 0xFFFF))), D0=rng.randint(0, 2**32 - 1), D1=rng.randint(0, 2**32 - 1))
        cases.append(Case(fn, M, regs, [(a, REC)]))
    return cases


# ---- the driver ----------------------------------------------------------------------------------------------------------
DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ms/regs.hpp"
#include "ms/gen/lift_ops.hpp"
#include "ms/gen/mog_syms.hpp"
#include "game/creatures.hpp"
extern "C" {
#include "planar_contact.h"
}
using namespace ms;
using namespace ms::game;
static char g_plan[256];                     // set by the "PLAN" cases: where moonshard's planar_contact.c disagrees

static const uint32_t ARENA = @ARENA@;
static const uint32_t STACK_LO = @STACK_LO@, STACK_TOP = @STACK_TOP@;
static uint8_t g_o[ARENA];                   // oracle arena: big-endian everywhere
static uint8_t g_s[ARENA];                   // C++ arena: structs native
static int g_fault;
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - g_s); }
void *ms::jobHostPtr(uint32_t a) { return g_s + a; }

struct OMem : Mem {
    uint8_t r8(uint32_t a) override { if(a >= ARENA) { g_fault = 1; return 0; } return g_o[a]; }
    void w8(uint32_t a, uint8_t v) override { if(a >= ARENA) { g_fault = 1; return; } g_o[a] = v; }
};
static OMem g_mem;

typedef void (*LiftFn)(Regs &, Mem &);
struct Tab { uint32_t addr; LiftFn fn; };
#include "dispatch.inc"
void tcallAsm(Regs &R, Mem &M, uint32_t addr) {
    for(unsigned i = 0; i < sizeof kTab / sizeof kTab[0]; ++i) {
        if(kTab[i].addr == addr) { kTab[i].fn(R, M); return; }
    }
    fprintf(stderr, "tcallAsm: no routine at %08x\n", addr);
    g_fault = 2;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void sw2(uint8_t *p) { uint8_t t = p[0]; p[0] = p[1]; p[1] = t; }
static void sw4(uint8_t *p) { uint8_t t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }

// byte order of the structs the C++ reads natively (offsets of the words / longs)
static const int K_W[] = {4, 6, 8, 58, 60, 62, 64, 66, 68, 74, 78, 80, 84, 112, 114, 116, 118, 120, 122, 124, 126, 128};
static const int K_L[] = {0, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 88, 92, 96, 100, 108};
static const int J_W[] = {6, 8, 10, 12, 14, 16, 18, 48};
static const int J_L[] = {2, 24, 28, 36, 40, 44};
static const int W_L[] = {2, 8, 12, 20, 32};
static const int B_W[] = {4, 6, 8, 10, 12, 14, 16, 18};
static const int B_L[] = {0};
static void swapRec(uint8_t *p, const int *w, int nw, const int *l, int nl) {
    for(int i = 0; i < nw; ++i) sw2(p + w[i]);
    for(int i = 0; i < nl; ++i) sw4(p + l[i]);
}
#define NA(a) ((int)(sizeof(a) / sizeof((a)[0])))
static void convertAll(uint8_t *g) {          // involution: BE <-> native
    for(int i = 0; i < 5; ++i) swapRec(g + @KNIGHT0@ + 132 * i, K_W, NA(K_W), K_L, NA(K_L));
    for(int i = 0; i < @HEAP_N@; ++i) swapRec(g + @HEAP@ + 132 * i, K_W, NA(K_W), K_L, NA(K_L));
    for(int i = 0; i < 10; ++i) swapRec(g + @JOBS@ + 50 * i, J_W, NA(J_W), J_L, NA(J_L));
    for(int i = 0; i < 10; ++i) swapRec(g + @WORKS@ + 36 * i, 0, 0, W_L, NA(W_L));
    swapRec(g + @BLOCK@, B_W, NA(B_W), B_L, NA(B_L));
    for(int i = 0; i < 6; ++i) swapRec(g + @SLOTS@ + 20 * i, B_W, NA(B_W), B_L, NA(B_L));
}

static uint32_t cell32(uint32_t a) { return rd32(g_s + a); }
static uint16_t cell16(uint32_t a) { return rd16(g_s + a); }

// the deterministic AI-handler stand-in of LAB_0322's test (tests/test_creatures.py handler_model)
static void testHandler(uint32_t fn, uint32_t owner, HandlerResult *r) {
    uint32_t h = fn * 2654435761u + owner * 40503u;
    uint32_t kind = h & 7;
    r->ulScript = 0xFFFFFFFFu; r->uwX = r->uwY = r->uwZ = 0; r->ubFacing = 0;
    if(kind < 2) return;
    if(kind == 2) { r->ulScript = 0; return; }
    r->ulScript = 0x00400000u + ((h >> 8) & 0xFFFF) * 2;
    r->uwX = (uint16_t)((h >> 3) & 0x1FF); r->uwY = (uint16_t)((h >> 12) & 0xFF); r->uwZ = (uint16_t)((h >> 20) & 0xFF);
    r->ubFacing = (uint8_t)((h >> 5) & 3);
}

static ContactEnv contactEnv() {
    ContactEnv e;
    e.pJobs = (CombatJob *)(g_s + @JOBS@);
    e.pHitPairs = g_s + @PAIRS@;
    e.ulHitCount = (cell32(@PAIRS_END@) - @PAIRS@) / 8;
    e.pDragon = (Knight *)(g_s + @KNIGHT0@ + 4 * 132);
    e.pKnights = (Knight *)(g_s + @KNIGHT0@);
    e.pCreatures = (Knight *)(g_s + cell32(@L05C3@));
    e.pAttackLists = g_s + @ATKL@;
    e.pHurtLists = g_s + @HURTL@;
    return e;
}

// ---- the C++ side of one case: the register contract of the rt shim (src/rt/creatures.cpp) -------------------------
static void subject(const char *fn, Regs &R) {
    CombatJob *jobs = (CombatJob *)(g_s + @JOBS@);
    DaggerSlot *slots = (DaggerSlot *)(g_s + @SLOTS@);
    DaggerBlock *blk = (DaggerBlock *)(g_s + @BLOCK@);
    if(!strcmp(fn, "03DB")) {
        uint32_t set = hitSetFind(g_s + @PAIRS@, (cell32(@PAIRS_END@) - @PAIRS@) / 8, R.a[1]);
        ContactPoint pt = {0, 0};
        bool hit = set && contactTest(g_s + R.a[0], (uint16_t)R.d[0], (uint16_t)R.d[1], (uint16_t)R.d[2], g_s + R.a[1], g_s + set,
                                      (uint16_t)R.d[5], (uint16_t)R.d[3], (uint16_t)R.d[4], &pt);
        R.d[0] = hit; R.d[1] = pt.uwX; R.d[2] = pt.uwY;
    } else if(!strcmp(fn, "PLAN")) {
        // the same arena through moonshard/planar_contact.c: attacker record + mirror from the cel table, defender rect
        const uint8_t *defTab = g_s + R.a[0], *attTab = g_s + R.a[1];
        uint32_t set = hitSetFind(g_s + @PAIRS@, (cell32(@PAIRS_END@) - @PAIRS@) / 8, R.a[1]);
        int16_t attOfs = (int16_t)(uint16_t)((uint16_t)R.d[5] * 10u), defOfs = (int16_t)(uint16_t)((uint16_t)R.d[0] * 10u);
        uint16_t mirror = (attTab[18 + attOfs] & 1) ? 0 : rd16(attTab + 14 + attOfs);
        const uint8_t *rec = hitRecordAt(g_s + set, (uint16_t)R.d[5]);
        const uint8_t *cel = defTab + 10 + defOfs;
        ContactPoint pt = {0, 0};
        bool mine = contactTest(defTab, (uint16_t)R.d[0], (uint16_t)R.d[1], (uint16_t)R.d[2], attTab, g_s + set, (uint16_t)R.d[5],
                                (uint16_t)R.d[3], (uint16_t)R.d[4], &pt);
        g_plan[0] = 0;
        R.d[0] = mine;
        if(rec[0] == 0) return;
        ms_contact_points pts = {rec, (size_t)(4 + 2 * rec[0])};
        ms_contact_rect rr = {(int16_t)R.d[1], (int16_t)R.d[2], rd32(cel), rd16(cel + 4), rd16(cel + 6), (uint8_t)(rd16(cel + 8) & 0xFF)};
        const uint8_t *planes = g_s + rd32(defTab + 2);
        int16_t ox = 0, oy = 0;
        int r = ms_contact_test(&pts, (int16_t)R.d[3], (int16_t)R.d[4], (int16_t)mirror, &rr, planes, (size_t)(g_s + ARENA - planes), &ox, &oy);
        if(r < 0 || (r == 1) != mine || (mine && ((uint16_t)ox != pt.uwX || (uint16_t)oy != pt.uwY)))
            snprintf(g_plan, sizeof g_plan, "mine=%d (%u,%u) planar=%d (%d,%d) mirror=%u", (int)mine, pt.uwX, pt.uwY, r, ox, oy, mirror);
    } else if(!strcmp(fn, "03B3")) {
        R.d[0] = depthClose(((const Knight *)(g_s + R.a[0]))->uwY, ((const Knight *)(g_s + R.a[1]))->uwY) ? 1 : 0;
    } else if(!strcmp(fn, "03BE")) {
        ContactEnv e = contactEnv();
        contactScan(e);
    } else if(!strcmp(fn, "03A9")) {
        R.d[0] = blockedMask(jobs, (const Knight *)(g_s + R.a[0]), (uint16_t)R.d[0], (uint16_t)R.d[2]);
    } else if(!strcmp(fn, "021B")) {
        const Knight *k = (const Knight *)(g_s + R.a[0]);
        R.d[0] = contactDamage(*k, g_s + k->ulDamageTable, *(const Inventory *)(g_s + k->ulInventory), cell16(@L05E4@ + 18));
    } else if(!strcmp(fn, "02D3")) {
        Knight *self = (Knight *)(g_s + cell32(@L0633@));
        const Knight *tgt = (const Knight *)(g_s + cell32(@L0634@));
        DaggerAim r = daggerAim(*blk, *self, *tgt, (Knight *)(g_s + R.a[0]), (int16_t)R.d[7]);
        wr16(g_s + @L02DA@, r.uwDist); wr16(g_s + @L0628@, r.uwSteps); wr16(g_s + @L0629@, r.uwArc);
        R.a[0] = @BLOCK@; R.a[1] = cell32(@L0633@); R.a[2] = cell32(@L0634@); R.a[3] = @BLOCK@ + 16;
    } else if(!strcmp(fn, "02F6")) {
        uint32_t slot;
        int32_t r = daggerStart(slots, *blk, (Knight *)(g_s + cell32(@L0633@)), rd16(g_s + 0x0A), rd16(g_s + 0x0C), &slot);
        R.d[0] = (uint32_t)(r == -2 ? -9 : r);
        R.a[0] = @SLOTS@ + 20 * slot + (r == 0 ? 20 : 0);
        if(r == 0) { R.a[1] = @BLOCK@; R.a[2] = cell32(@L0633@); }
    } else if(!strcmp(fn, "02FD")) {
        uint16_t out[3] = {(uint16_t)R.d[1], (uint16_t)R.d[2], (uint16_t)R.d[3]};
        uint32_t slot;
        int32_t r = daggerStep(slots, R.d[0], out, &slot);
        R.d[0] = (uint32_t)r; R.d[1] = out[0]; R.d[2] = out[1]; R.d[3] = out[2];
        R.a[0] = @SLOTS@ + 20 * slot;
    } else if(!strcmp(fn, "0310")) {
        R.d[0] = jobCreate(jobs, R.a[0], R.a[1], R.a[2], (uint16_t)R.d[0], (uint16_t)R.d[1], (uint16_t)R.d[2], (uint8_t)R.d[3], (uint8_t)R.d[5]);
    } else if(!strcmp(fn, "02D0")) {
        SpawnResult s = creatureSpawn(jobs, (Knight *)(g_s + cell32(@L05C3@)), R.a[0], R.a[2], (uint16_t)R.d[0], (uint16_t)R.d[1],
                                      (uint16_t)R.d[2], (uint8_t)R.d[3], (uint8_t)R.d[5]);
        R.a[1] = jobAddr(s.pRecord); R.d[0] = s.ulStatus;
    } else if(!strcmp(fn, "02CA")) {
        SpawnResult s = daggerRelease(jobs, (Knight *)(g_s + cell32(@L05C3@)), *(Knight *)(g_s + R.a[1]), @L07EB@, @L0302@, R.a[2],
                                      (uint16_t)R.d[0], (uint16_t)R.d[1], (uint16_t)R.d[2], (uint8_t)R.d[3]);
        R.a[1] = jobAddr(s.pRecord); R.d[0] = s.ulStatus; R.a[0] = @L07EB@; R.d[5] = 52;
    } else if(!strcmp(fn, "0322")) {
        DispatchEnv e;
        e.pJobs = jobs; e.pHandlerTable = g_s + @HANDLERS@; e.callHandler = testHandler;
        creatureDispatch(e);
    } else if(!strcmp(fn, "02BC")) {
        R.d[0] = depthNear(*(const Knight *)(g_s + R.a[1]), *(const Knight *)(g_s + R.a[0])) ? 1 : 0;
    } else if(!strcmp(fn, "02BF")) {
        R.d[0] = xNear(*(const Knight *)(g_s + cell32(@L0633@)), *(const Knight *)(g_s + cell32(@L0634@)), (int16_t)R.d[7]) ? 1 : 0;
    } else if(!strcmp(fn, "02C2")) {
        bool left;
        uint16_t d = xDelta(*(const Knight *)(g_s + cell32(@L0633@)), *(const Knight *)(g_s + cell32(@L0634@)), left);
        R.d[0] = d; R.d[1] = left ? 1 : 0;
    } else if(!strcmp(fn, "02C4")) {
        faceTarget(*(Knight *)(g_s + cell32(@L0633@)), *(const Knight *)(g_s + cell32(@L0634@)));
    }
}

static LiftFn oracleFn(const char *fn) {
@ORACLE@
    return 0;
}

static unsigned long hexv(const char *s) { return strtoul(s, 0, 16); }
static int unhex(const char *s, uint8_t *out, int max) {
    int n = 0;
    while(s[0] && s[1] && s[0] != '\n' && n < max) { unsigned v; sscanf(s, "%2x", &v); out[n++] = (uint8_t)v; s += 2; }
    return n;
}
static void phex(const uint8_t *p, int n) { for(int i = 0; i < n; ++i) printf("%02x", p[i]); }

struct Range { uint32_t a, n; };
static const Range kIgnore[] = {
@IGNORE@
};

int main() {
    static char line[1 << 20];
    static uint8_t g_init[ARENA];
    static Range dumps[16];
    int nd = 0, ncase = 0;
    char fn[16] = "";
    Regs in;
    memset(&in, 0, sizeof in);
    memset(g_init, 0, sizeof g_init);
    while(fgets(line, sizeof line, stdin)) {
        if(!strncmp(line, "CASE ", 5)) {
            char *t = strtok(line + 5, " \r\n");
            strncpy(fn, t, sizeof fn - 1);
            for(int i = 0; i < 8; ++i) in.d[i] = (uint32_t)hexv(strtok(0, " \r\n"));
            for(int i = 0; i < 7; ++i) in.a[i] = (uint32_t)hexv(strtok(0, " \r\n"));
            in.a[7] = STACK_TOP; in.sr = 0;
            memset(g_init, 0, sizeof g_init);
            nd = 0;
        } else if(!strncmp(line, "M ", 2)) {
            uint32_t a = (uint32_t)hexv(line + 2);
            const char *h = strchr(line + 2, ' ') + 1;
            unhex(h, g_init + a, ARENA - a);
        } else if(!strncmp(line, "DUMP ", 5)) {
            char *t = strtok(line + 5, " \r\n");
            dumps[nd].a = (uint32_t)hexv(t);
            dumps[nd].n = (uint32_t)hexv(strtok(0, " \r\n"));
            ++nd;
        } else if(!strncmp(line, "GO", 2)) {
            ++ncase;
            memcpy(g_o, g_init, ARENA);
            memcpy(g_s, g_init, ARENA);
            convertAll(g_s);
            g_fault = 0;
            Regs ro = in, rs = in;
            bool haveOracle = strcmp(fn, "0322") != 0 && strcmp(fn, "PLAN") != 0;
            if(haveOracle) {
                LiftFn f = oracleFn(fn);
                if(!f) { printf("RES BAD no oracle for %s\n", fn); fflush(stdout); continue; }
                f(ro, g_mem);
            }
            subject(fn, rs);
            convertAll(g_s);
            const char *status = "OK";
            char detail[512] = "";
            if(!strcmp(fn, "PLAN") && g_plan[0]) { status = "DIFF"; snprintf(detail, sizeof detail, "%s", g_plan); }
            if(g_fault) status = "FAULT";
            else if(haveOracle) {
                // memory (masked), then the registers the routine returns
                for(uint32_t a = 0; a < ARENA && !strcmp(status, "OK"); ++a) {
                    if(!(a & 255) && !memcmp(g_o + a, g_s + a, 256)) { a += 255; continue; }
                    if(g_o[a] == g_s[a]) continue;
                    bool ign = false;
                    for(unsigned k = 0; k < sizeof kIgnore / sizeof kIgnore[0]; ++k)
                        if(a >= kIgnore[k].a && a < kIgnore[k].a + kIgnore[k].n) ign = true;
                    if(!ign) { status = "DIFF"; snprintf(detail, sizeof detail, "mem %06x oracle %02x C++ %02x", a, g_o[a], g_s[a]); }
                }
                if(!strcmp(fn, "03DB")) {
                    if(ro.d[0] != rs.d[0] || (ro.d[0] == 1 && (ro.d[1] & 0xFFFF) != (rs.d[1] & 0xFFFF || 0) )) {}
                    if(ro.d[0] != rs.d[0] || (ro.d[0] == 1 && ((ro.d[1] & 0xFFFF) != (rs.d[1] & 0xFFFF) || (ro.d[2] & 0xFFFF) != (rs.d[2] & 0xFFFF))))
                        { status = "DIFF"; snprintf(detail, sizeof detail, "regs oracle d0=%x d1=%x d2=%x C++ d0=%x d1=%x d2=%x", ro.d[0], ro.d[1], ro.d[2], rs.d[0], rs.d[1], rs.d[2]); }
                } else if(!strcmp(fn, "03A9")) {
                    if((ro.d[0] & 0xFFFF) != (rs.d[0] & 0xFFFF)) { status = "DIFF"; snprintf(detail, sizeof detail, "mask oracle %x C++ %x", ro.d[0], rs.d[0]); }
                    if(ro.a[0] != in.a[0]) { status = "DIFF"; snprintf(detail, sizeof detail, "oracle changed A0"); }   // the shim keeps A0
                } else if(!strcmp(fn, "021B")) {
                    if(ro.d[0] != rs.d[0]) { status = "DIFF"; snprintf(detail, sizeof detail, "D0 oracle %x C++ %x", ro.d[0], rs.d[0]); }
                    // the routine keeps every register but D0 and A2 (QUIRK: A2 = the attacker's inventory pointer is not saved)
                    for(int i = 1; i < 8; ++i) if(ro.d[i] != in.d[i]) { status = "DIFF"; snprintf(detail, sizeof detail, "oracle changed D%d", i); }
                    for(int i = 0; i < 7; ++i) {
                        uint32_t want = i == 2 ? rd32(g_init + in.a[0] + 96) : in.a[i];
                        if(ro.a[i] != want) { status = "DIFF"; snprintf(detail, sizeof detail, "oracle changed A%d to %x", i, ro.a[i]); }
                    }
                } else if(!strcmp(fn, "02D3")) {
                    for(int i = 0; i < 4; ++i)
                        if(ro.a[i] != rs.a[i]) { status = "DIFF"; snprintf(detail, sizeof detail, "A%d oracle %x C++ %x", i, ro.a[i], rs.a[i]); }
                } else if(!strcmp(fn, "02F6")) {
                    if(ro.d[0] != rs.d[0]) { status = "DIFF"; snprintf(detail, sizeof detail, "D0 oracle %x C++ %x", ro.d[0], rs.d[0]); }
                    for(int i = 0; i < 3; ++i)
                        if(ro.a[i] != rs.a[i]) { status = "DIFF"; snprintf(detail, sizeof detail, "A%d oracle %x C++ %x", i, ro.a[i], rs.a[i]); }
                } else if(!strcmp(fn, "02FD")) {
                    bool ok = ro.d[0] == rs.d[0] && ro.a[0] == rs.a[0] && ro.a[1] == in.a[1];
                    if(ok && (int32_t)ro.d[0] == -1) ok = ro.d[1] == in.d[1] && ro.d[2] == in.d[2] && ro.d[3] == in.d[3];
                    else if(ok) ok = (ro.d[1] & 0xFFFF) == (rs.d[1] & 0xFFFF) && (ro.d[2] & 0xFFFF) == (rs.d[2] & 0xFFFF) && (ro.d[3] & 0xFFFF) == (rs.d[3] & 0xFFFF);
                    if(!ok) { status = "DIFF"; snprintf(detail, sizeof detail, "oracle d0=%x %x %x %x C++ d0=%x %x %x %x", ro.d[0], ro.d[1], ro.d[2], ro.d[3], rs.d[0], rs.d[1], rs.d[2], rs.d[3]); }
                } else if(!strcmp(fn, "0310")) {
                    if(ro.d[0] != rs.d[0]) { status = "DIFF"; snprintf(detail, sizeof detail, "D0 oracle %x C++ %x", ro.d[0], rs.d[0]); }
                } else if(!strcmp(fn, "02D0") || !strcmp(fn, "02CA")) {
                    if(ro.d[0] != rs.d[0] || ro.a[1] != rs.a[1]) { status = "DIFF"; snprintf(detail, sizeof detail, "oracle d0=%x a1=%x C++ d0=%x a1=%x", ro.d[0], ro.a[1], rs.d[0], rs.a[1]); }
                    if(!strcmp(fn, "02CA") && (ro.a[0] != rs.a[0] || ro.d[5] != rs.d[5])) { status = "DIFF"; snprintf(detail, sizeof detail, "02CA A0 / D5 oracle %x %x C++ %x %x", ro.a[0], ro.d[5], rs.a[0], rs.d[5]); }
                    if(!strcmp(fn, "02D0") && (ro.a[0] != in.a[0] || ro.d[1] != in.d[1])) { status = "DIFF"; snprintf(detail, sizeof detail, "02D0 changed A0 / D1"); }
                } else if(!strcmp(fn, "02BC") || !strcmp(fn, "02BF") || !strcmp(fn, "03B3")) {
                    if((ro.d[0] & 0xFFFF) != (rs.d[0] & 0xFFFF)) { status = "DIFF"; snprintf(detail, sizeof detail, "D0 oracle %x C++ %x", ro.d[0], rs.d[0]); }
                } else if(!strcmp(fn, "02C2")) {
                    if((ro.d[0] & 0xFFFF) != (rs.d[0] & 0xFFFF) || ro.d[1] != rs.d[1]) { status = "DIFF"; snprintf(detail, sizeof detail, "oracle %x %x C++ %x %x", ro.d[0], ro.d[1], rs.d[0], rs.d[1]); }
                }
            }
            // what the Python model compares: the oracle's result registers and the dumped regions (the C++ arena for LAB_0322)
            const uint8_t *src = haveOracle ? g_o : g_s;
            Regs &rr = haveOracle ? ro : rs;
            unsigned chg = 0;                       // registers the routine changed (documents the shim contracts)
            for(int i = 0; i < 8; ++i) { if(rr.d[i] != in.d[i]) chg |= 1u << i; if(i < 7 && rr.a[i] != in.a[i]) chg |= 1u << (8 + i); }
            printf("RES %s %s %s | d %08x %08x %08x %08x a %08x %08x chg %x\n", fn, status, detail, rr.d[0], rr.d[1], rr.d[2], rr.d[3], rr.a[0], rr.a[1], chg);
            for(int i = 0; i < nd; ++i) { printf("D %x %x ", dumps[i].a, dumps[i].n); phex(src + dumps[i].a, dumps[i].n); printf("\n"); }
            printf("END\n");
            fflush(stdout);
        }
    }
    printf("DONE %d\n", ncase);
    return 0;
}
'''


def ensure_lifted():
    """Lift the oracle routines into WORK_DIR (cached), return (list of source files, label -> source path)."""
    os.makedirs(WORK_DIR, exist_ok=True)
    stamp = os.path.join(WORK_DIR, 'stamp')
    newest = max(os.path.getmtime(p) for p in (MOG_ASM, os.path.join(ROOT, 'tools', 'lift.py'), os.path.join(ROOT, 'tools', 'lift_ops.py'),
                                              SYMS_HPP, __file__))
    fresh = os.path.exists(stamp) and os.path.getmtime(stamp) >= newest and \
        all(os.path.exists(os.path.join(WORK_DIR, 'lab_%s.cpp' % l[4:].lower())) for l in LIFT)
    if not fresh:
        for f in os.listdir(WORK_DIR):
            if f.startswith('lab_'):
                os.remove(os.path.join(WORK_DIR, f))
        for lab in LIFT:
            r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'lift.py'), 'mog', lab, '--out-dir', WORK_DIR,
                                '--allow-calls'], capture_output=True, text=True, cwd=ROOT)
            if r.returncode != 0:
                raise RuntimeError('lift %s failed: %s%s' % (lab, r.stdout, r.stderr))
        for f in os.listdir(WORK_DIR):
            if f.startswith('lab_') and f.endswith('.cpp'):
                p = os.path.join(WORK_DIR, f)
                s = open(p, encoding='utf-8').read().replace('lift::callAsm(', 'tcallAsm(')
                open(p, 'w', encoding='utf-8', newline='\n').write(s)
        open(stamp, 'w').write('ok')
    files = {}
    todo = list(LIFT)
    while todo:
        lab = todo.pop()
        if lab in files:
            continue
        p = os.path.join(WORK_DIR, 'lab_%s.cpp' % lab[4:].lower())
        if not os.path.exists(p):
            p = os.path.join(ROOT, 'src', 'lifted', 'mog', 'lab_%s.cpp' % lab[4:].lower())
        files[lab] = p
        s = open(p, encoding='utf-8').read()
        for m in re.finditer(r'void lab_(LAB_[0-9A-F]+)\(Regs&, Mem&\);', s):
            if m.group(1) != lab:
                todo.append(m.group(1))
    return files


_DRIVER_EXE = None


def build_driver():
    global _DRIVER_EXE
    if _DRIVER_EXE and os.path.exists(_DRIVER_EXE):
        return _DRIVER_EXE
    files = ensure_lifted()
    tmp = tempfile.mkdtemp(prefix='creatures_')
    disp = ''.join('extern "C" void lab_%s(ms::Regs&, ms::Mem&);\n' % l for l in sorted(files))
    disp += 'static const Tab kTab[] = {\n' + ''.join('    {ms::sym_mog::%s, &lab_%s},\n' % (l, l) for l in sorted(files)) + '};\n'
    open(os.path.join(tmp, 'dispatch.inc'), 'w').write(disp)
    oracle = ''
    for fnname in ('03B3', '03DB', '03BE', '03A9', '021B', '02D3', '02F6', '02FD', '0310', '02D0', '02CA', '02BC', '02BF', '02C2', '02C4'):
        oracle += '    if(!strcmp(fn, "%s")) return &lab_LAB_%s;\n' % (fnname, fnname)
    subs = {
        'ARENA': '0x%X' % ARENA, 'STACK_LO': '0x%X' % STACK_LO, 'STACK_TOP': '0x%X' % STACK_TOP, 'KNIGHT0': '0x%X' % KNIGHT0,
        'HEAP': '0x%X' % HEAP, 'HEAP_N': str(HEAP_N), 'JOBS': '0x%X' % JOBS, 'WORKS': '0x%X' % WORKS, 'BLOCK': '0x%X' % BLOCK,
        'SLOTS': '0x%X' % SLOTS, 'PAIRS': '0x%X' % PAIRS, 'PAIRS_END': '0x%X' % PAIRS_END, 'ATKL': '0x%X' % ATKL,
        'HURTL': '0x%X' % HURTL, 'HANDLERS': '0x%X' % HANDLERS, 'ORACLE': oracle,
        'IGNORE': ',\n'.join('    {0x%X, %d}' % (a, n) for a, n in IGNORE),
    }
    for lab in ('05C3', '05E4', '0633', '0634', '02DA', '0628', '0629', '07EB', '0302'):
        subs['L' + lab] = '0x%X' % A(lab)
    src = DRIVER
    for k, v in subs.items():
        src = src.replace('@%s@' % k, v)
    assert '@' not in src.replace('@ORACLE@', ''), re.findall(r'@\w+@', src)
    drv = os.path.join(tmp, 'driver.cpp')
    open(drv, 'w').write(src)
    exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
    pc = os.path.join(tmp, 'planar_contact.o')
    r = subprocess.run([CXX, '-x', 'c', '-std=c11', '-O1', '-w', '-c', PLANAR_C, '-o', pc], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('planar_contact.c compile failed: ' + r.stderr[-2000:])
    cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions',
           '-fno-rtti', '-w', '-include', os.path.join(tmp, 'tcall.hpp'), '-I', os.path.join(ROOT, 'include'), '-I', tmp,
           '-I', os.path.dirname(PLANAR_C), drv, os.path.join(ROOT, 'src', 'game', 'creatures.cpp'), os.path.join(ROOT, 'src', 'game', 'rules', 'damage.cpp'), GAMEDATA_SOURCE, pc] + sorted(set(files.values())) + ['-o', exe]
    open(os.path.join(tmp, 'tcall.hpp'), 'w').write('#pragma once\n#include "ms/regs.hpp"\nvoid tcallAsm(ms::Regs &, ms::Mem &, unsigned);\n')
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('driver compile failed:\n%s\n%s' % (' '.join(cmd), r.stderr[-4000:]))
    _DRIVER_EXE = exe
    return exe


def case_text(c):
    out = ['CASE %s %s' % (c.fn, ' '.join('%x' % c.regs['D%d' % i] for i in range(8)) + ' ' + ' '.join('%x' % c.regs['A%d' % i] for i in range(7)))]
    for lo, hi in c.mem.regions():
        out.append('M %x %s' % (lo, c.mem.get(lo, hi - lo).hex()))
    for a, n in c.dumps:
        out.append('DUMP %x %x' % (a, n))
    out.append('GO')
    return '\n'.join(out) + '\n'


def run_cases(cases):
    exe = build_driver()
    text = ''.join(case_text(c) for c in cases)
    r = subprocess.run([exe], input=text, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('driver exit %d: %s' % (r.returncode, r.stderr[-2000:]))
    results = []
    cur = None
    for ln in r.stdout.split('\n'):
        if ln.startswith('RES '):
            cur = {'line': ln, 'dumps': {}}
            head, regs = ln.split(' | ')
            parts = head.split(' ', 3)
            cur['fn'], cur['status'], cur['detail'] = parts[1], parts[2], parts[3] if len(parts) > 3 else ''
            f = regs.split()
            cur['d'] = [int(x, 16) for x in f[1:5]]
            cur['a'] = [int(x, 16) for x in f[6:8]]
            cur['chg'] = int(f[9], 16)
        elif ln.startswith('D '):
            _, a, n, hx = ln.split(' ')
            cur['dumps'][int(a, 16)] = bytes.fromhex(hx)
        elif ln == 'END':
            results.append(cur)
    assert len(results) == len(cases), (len(results), len(cases), r.stdout[-500:])
    return results


def model_dumps(M, c):
    return {a: M.get(a, n) for a, n in c.dumps}


# ---- the tests -------------------------------------------------------------------------------------------------------------
@unittest.skipUnless(CXX, 'clang++ not on PATH')
class CreaturesTest(unittest.TestCase):
    def check_driver(self, cases, what):
        res = run_cases(cases)
        bad = [(i, r) for i, r in enumerate(res) if r['status'] != 'OK']
        if bad:
            i, r = bad[0]
            self.fail('%s: %d of %d cases differ from the lifted asm; first #%d (%s): %s' % (what, len(bad), len(cases), i, r['status'], r['detail']))
        return res

    def compare_dumps(self, what, i, M, c, r):
        exp = model_dumps(M, c)
        for a, got in r['dumps'].items():
            if got != exp[a]:
                k = next(j for j in range(len(got)) if got[j] != exp[a][j])
                self.fail('%s case %d: memory at %#x differs from the Python model: asm/C++ %s model %s' % (
                    what, i, a + k, got[k:k + 8].hex(), exp[a][k:k + 8].hex()))

    def test_00_popcount_table_in_tables_yaml(self):
        # LAB_0A55 is the table contact_mask_popcount of tools/tables.yaml: the C++ popcount and the model agree with it
        text = open(MOG_ASM, encoding='latin-1').read().split('\n')
        i = next(k for k, l in enumerate(text) if l.startswith('LAB_0A55:'))
        vals = []
        for l in text[i + 1:i + 3]:
            for tok in re.findall(r'\$([0-9a-f]{8})', l):
                vals += [int(tok[:4], 16), int(tok[4:], 16)]
        self.assertEqual(vals, POP4)

    def test_01_contact_test(self):
        rng = random.Random(1)
        cases = gen_03DB(rng, 2000)
        res = self.check_driver(cases, 'LAB_03DB')
        hits = 0
        for i, (c, r) in enumerate(zip(cases, res)):
            R = c.regs
            d0, d1, d2 = m_03DB(c.mem.copy(), R['A0'], R['A1'], R['D0'], R['D1'], R['D2'], R['D3'], R['D4'], R['D5'])
            self.assertEqual(r['d'][0], d0, 'case %d D0' % i)
            if d0:
                hits += 1
                self.assertEqual((r['d'][1] & 0xFFFF, r['d'][2] & 0xFFFF), (d1 & 0xFFFF, d2 & 0xFFFF), 'case %d point' % i)
        self.assertGreater(hits, 150)
        self.assertLess(hits, 2000)

    def test_02_contact_overlap(self):
        # the closed-interval decision tree on full 32-bit signed operands (the asm's CMP.L / BMI / BGE), incl. negatives
        rng = random.Random(2)
        vals = [0, 1, 2, 5, 100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFF0, -1 & 0xFFFFFFFF]
        from_cpp = self.overlap_via_cpp
        pts = [tuple(rng.choice(vals) if rng.random() < 0.5 else rng.randint(0, 40) for _ in range(4)) for _ in range(1500)]
        got = from_cpp(pts)
        for p, g in zip(pts, got):
            self.assertEqual(int(m_03CA(*p, 0)), g, p)

    def overlap_via_cpp(self, pts):
        src = r'''
#include <stdio.h>
#include "game/creatures.hpp"
uint32_t ms::jobHostAddr(const void *p) { return 0; }
void *ms::jobHostPtr(uint32_t a) { return 0; }
int main() { unsigned a, b, c, d; while(scanf("%x %x %x %x", &a, &b, &c, &d) == 4) printf("%d\n", ms::game::contactOverlap((int32_t)a, (int32_t)b, (int32_t)c, (int32_t)d) ? 1 : 0); return 0; }
'''
        tmp = tempfile.mkdtemp(prefix='creatures_ov_')
        open(os.path.join(tmp, 'm.cpp'), 'w').write(src)
        exe = os.path.join(tmp, 'm.exe')
        r = subprocess.run([CXX, '-std=c++17', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-w', '-I', os.path.join(ROOT, 'include'),
                            os.path.join(tmp, 'm.cpp'), os.path.join(ROOT, 'src', 'game', 'creatures.cpp'), '-o', exe], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        out = subprocess.run([exe], input=''.join('%x %x %x %x\n' % tuple(v & 0xFFFFFFFF for v in p) for p in pts), capture_output=True, text=True).stdout
        return [int(x) for x in out.split()]

    def test_03_contact_scan(self):
        rng = random.Random(3)
        cases = gen_03BE(rng, 700)
        res = self.check_driver(cases, 'LAB_03BE')
        hits = 0
        for i, (c, r) in enumerate(zip(cases, res)):
            M = c.mem.copy()
            m_03BE(M)
            self.compare_dumps('LAB_03BE', i, M, c, r)
            hits += sum(1 for k in range(5) if r['dumps'][KNIGHT0][REC * k + 14:REC * k + 18] != b'\0\0\0\0')
        self.assertGreater(hits, 50)

    def test_04_blocked_mask(self):
        rng = random.Random(4)
        cases = gen_03A9(rng, 1500)
        res = self.check_driver(cases, 'LAB_03A9')
        seen = set()
        for i, (c, r) in enumerate(zip(cases, res)):
            R = c.regs
            mask = m_03A9(c.mem.copy(), R['A0'], R['D0'], R['D2'])
            self.assertEqual(r['d'][0] & 0xFFFF, mask, 'case %d' % i)
            seen.add(mask)
        self.assertGreater(len(seen), 4)

    def test_05_damage(self):
        rng = random.Random(5)
        cases = gen_021B(rng, 1200)
        res = self.check_driver(cases, 'LAB_021B')
        for i, (c, r) in enumerate(zip(cases, res)):
            self.assertEqual(r['d'][0], m_021B(c.mem.copy(), c.regs['A0']), 'case %d' % i)

    def test_06_dagger_aim(self):
        rng = random.Random(6)
        cases = gen_02D3(rng, 900)
        res = self.check_driver(cases, 'LAB_02D3')
        for i, (c, r) in enumerate(zip(cases, res)):
            M = c.mem.copy()
            m_02D3(M, c.regs['A0'], c.regs['D7'])
            self.compare_dumps('LAB_02D3', i, M, c, r)

    def test_07_dagger_start(self):
        rng = random.Random(7)
        cases = []
        # division by zero (steps 0 / 1 / 2 / 0xFFFF...) is a trap in the asm: the lifted code traps, so those are model-only
        for c in gen_02F6(rng, 2000):
            cases.append(c)
        ok_cases = [c for c in cases if m_02F6(c.mem.copy()) != 'trap']
        self.assertGreater(len(ok_cases), 1500)
        res = self.check_driver(ok_cases, 'LAB_02F6')
        flavours = set()
        for i, (c, r) in enumerate(zip(ok_cases, res)):
            M = c.mem.copy()
            d0 = m_02F6(M)
            self.assertEqual(r['d'][0], d0 & 0xFFFFFFFF, 'case %d D0' % i)
            self.compare_dumps('LAB_02F6', i, M, c, r)
            flavours.add(d0)
        self.assertEqual(flavours, {0, -1})
        # the C++ reports a divide by zero as -2 (checked through a one-off case: steps 0 -> DIVU by zero in the arc setup)
        rng2 = random.Random(70)
        c = gen_02F6(rng2, 1)[0]
        c.mem.w16(BLOCK + 16, 0)
        c.mem.w16(BLOCK + 8, c.mem.r16(BLOCK + 14))        # short arc path: DIVU by (0 + 1) >> 1 = 0
        c.mem.w32(SLOTS, 0)
        self.assertEqual(m_02F6(c.mem.copy()), 'trap')

    def test_08_dagger_step(self):
        rng = random.Random(8)
        cases = gen_02FD(rng, 1500)
        res = self.check_driver(cases, 'LAB_02FD')
        kinds = set()
        for i, (c, r) in enumerate(zip(cases, res)):
            M = c.mem.copy()
            out = m_02FD(M, c.regs['D0'])
            kinds.add(out[0])
            self.assertEqual(r['d'][0], out[0] & 0xFFFFFFFF, 'case %d status' % i)
            if out[0] != -1:
                self.assertEqual([x & 0xFFFF for x in r['d'][1:4]], list(out[1:]), 'case %d xyz' % i)
            self.compare_dumps('LAB_02FD', i, M, c, r)
        self.assertEqual(kinds, {-1, 0, 1})

    def test_09_job_create(self):
        rng = random.Random(9)
        cases = gen_0310(rng, 500)
        res = self.check_driver(cases, 'LAB_0310')
        got = set()
        for i, (c, r) in enumerate(zip(cases, res)):
            R = c.regs
            M = c.mem.copy()
            st = m_0310(M, R['A0'], R['A1'], R['A2'], R['D0'], R['D1'], R['D2'], R['D3'], R['D5'])
            got.add(st)
            self.assertEqual(r['d'][0], st, 'case %d' % i)
            self.compare_dumps('LAB_0310', i, M, c, r)
        self.assertEqual(got, {0, 1})

    def test_10_spawn_and_release(self):
        rng = random.Random(10)
        cases = gen_02D0(rng, 500)
        res = self.check_driver(cases, 'LAB_02D0')
        for i, (c, r) in enumerate(zip(cases, res)):
            R = c.regs
            M = c.mem.copy()
            a1, st = m_02D0(M, R['A0'], R['A2'], R['D0'], R['D1'], R['D2'], R['D3'], R['D5'])
            self.assertEqual((r['a'][1], r['d'][0]), (a1, st), 'case %d' % i)
            self.compare_dumps('LAB_02D0', i, M, c, r)
        cases = gen_02CA(rng, 400)
        res = self.check_driver(cases, 'LAB_02CA')
        for i, (c, r) in enumerate(zip(cases, res)):
            R = c.regs
            M = c.mem.copy()
            a1, st = m_02CA(M, R['A1'], R['A2'], R['D0'], R['D1'], R['D2'], R['D3'])
            self.assertEqual((r['a'][1], r['d'][0]), (a1, st), 'case %d' % i)
            self.compare_dumps('LAB_02CA', i, M, c, r)

    def test_11_creature_dispatch(self):
        # no lifted twin (JSR (A1)): the C++ against the Python model, the handler a deterministic stand-in
        rng = random.Random(11)
        cases = gen_0322(rng, 600)
        res = run_cases(cases)
        killed = ran = 0
        for i, (c, r) in enumerate(zip(cases, res)):
            self.assertEqual(r['status'], 'OK', r['detail'])
            M = c.mem.copy()
            m_0322(M, HANDLERS)
            self.compare_dumps('LAB_0322', i, M, c, r)
            killed += 1 if any(M.r8(JOBS + 50 * k) == 0 and c.mem.r8(JOBS + 50 * k) != 0 for k in range(10)) else 0
            ran += 1 if any(M.r32(JOBS + 50 * k + 2) != c.mem.r32(JOBS + 50 * k + 2) for k in range(10)) else 0
        self.assertGreater(killed, 30)
        self.assertGreater(ran, 100)

    def test_12_predicates(self):
        rng = random.Random(12)
        cases = gen_pred(rng, 1200)
        self.check_driver(cases, 'predicates')

    def test_14_planar_contact_c_cross_check(self):
        # moonshard/planar_contact.c is the same contact test as a pure function.  Where its inputs are the ones the game
        # has (planes inside the buffer, masks 1..15, positions on screen) it agrees with LAB_03DB; the differences are the
        # corner cases listed in the module docstring of creatures.hpp / the report (depth_ok at 0x8000, BGE vs
        # (int16)(x - w) >= 0 at 16-bit overflow, an unsigned byte offset where the 68k sign-extends it).
        if not os.path.exists(PLANAR_C):
            self.skipTest('moonshard/planar_contact.c missing')
        rng = random.Random(14)
        cases = [Case('PLAN', c.mem, c.regs) for c in gen_03DB(rng, 1500)]
        res = run_cases(cases)
        bad = [(i, r) for i, r in enumerate(res) if r['status'] != 'OK']
        self.assertEqual(bad, [], 'planar_contact.c disagrees with the C++ port, first: %s' % (bad[0][1]['detail'] if bad else ''))

    def test_15_depth_gate(self):
        # LAB_03B3 against the C++ over every depth difference around the gate and the 16-bit corners; moonshard's
        # ms_contact_depth_ok differs at exactly one value (the difference 0x8000: NEG.W leaves it negative, CMP.W #10
        # resets the overflow flag NEG set, so the 68k says "close"; the C module says "far").
        cases = []
        vals = [0, 1, 9, 10, 11, 12, 100, 0x7FFF, 0x8000, 0x8001, 0xFFF5, 0xFFF6, 0xFFFF, 0x4000, 0xC000]
        rng = random.Random(15)
        for a in vals + [rng.randint(0, 0xFFFF) for _ in range(60)]:
            for d in (0, 1, 10, 11, 0x8000, 0xFFFF, 0xFFF6, 0xFFF5, rng.randint(0, 0xFFFF)):
                M = base_mem(rng)
                M.fill(KNIGHT0, 2 * REC, rng)
                M.w16(KNIGHT0 + 8, a)
                M.w16(KNIGHT0 + REC + 8, (a + d) & 0xFFFF)
                cases.append(Case('03B3', M, regs0(A0=KNIGHT0, A1=KNIGHT0 + REC)))
        res = self.check_driver(cases, 'LAB_03B3')
        for c, r in zip(cases, res):
            self.assertEqual(r['d'][0] & 0xFFFF, m_03B3(c.mem.copy(), KNIGHT0, KNIGHT0 + REC))
        if os.path.exists(PLANAR_C):
            src = (b'#include <stdio.h>' + chr(10).encode() + b'#include "planar_contact.h"' + chr(10).encode() +
                   b'int main() { int v[4] = {0x7FFF, 0x8000, 10, 11}; for(int i = 0; i < 4; ++i) printf("%d ", ms_contact_depth_ok(0, (int16_t)v[i])); return 0; }')
            tmp = tempfile.mkdtemp(prefix='creatures_pl_')
            open(os.path.join(tmp, 'p.c'), 'wb').write(src)
            exe = os.path.join(tmp, 'p.exe')
            r = subprocess.run([CXX, '-x', 'c', '-w', '-I', os.path.dirname(PLANAR_C), os.path.join(tmp, 'p.c'), '-x', 'c', PLANAR_C, '-o', exe],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[-1500:])
            out = subprocess.run([exe], capture_output=True, text=True).stdout.split()
            self.assertEqual(out, ['0', '0', '1', '0'])        # 0x7FFF far, 0x8000 "far" (the 68k: close), 10 close, 11 far
            m = Mem()
            m.w16(KNIGHT0 + 8, 0)
            m.w16(KNIGHT0 + REC + 8, 0x8000)
            self.assertEqual(m_03B3(m, KNIGHT0, KNIGHT0 + REC), 1)

    def test_16_patch_table(self):
        # asm/patches/mog.creatures.json: every patched line is the text the table says, each patch is a single JMP into an
        # rt_* name defined in src/rt/creatures.cpp, and no range overlaps another mog*.json
        import glob
        import json
        lines = open(MOG_ASM, encoding='latin-1').read().split(chr(10))
        mine = json.load(open(os.path.join(ROOT, 'asm', 'patches', 'mog.creatures.json')))
        self.assertEqual(mine['binary'], 'mog')
        rt = open(os.path.join(ROOT, 'src', 'rt', 'creatures.cpp'), encoding='utf-8').read()
        ranges = []
        for p in mine['patches']:
            for k, text in enumerate(p['orig']):
                self.assertEqual(' '.join(lines[p['line'] - 1 + k].split()), ' '.join(text.split()), p['id'])
            self.assertEqual(len(p['new']), 1)
            m = re.match(r'\s*JMP\s+(rt_\w+)$', p['new'][0])
            self.assertTrue(m, p['id'])
            self.assertIn('.globl ' + m.group(1), rt)
            ranges.append((p['line'], p['line'] + len(p['orig']) - 1, p['id']))
        for f in glob.glob(os.path.join(ROOT, 'asm', 'patches', 'mog*.json')):
            if os.path.basename(f) == 'mog.creatures.json':
                continue
            for q in json.load(open(f)).get('patches', []):
                if 'line' not in q:
                    continue
                a, b = q['line'], q['line'] + len(q.get('orig', [1])) - 1
                for lo, hi, pid in ranges:
                    self.assertFalse(lo <= b and a <= hi, '%s overlaps %s in %s' % (pid, q['id'], os.path.basename(f)))

    def test_13_clear_links_overruns_the_heap_by_one(self):
        rng = random.Random(13)
        M = base_mem(rng)
        setup_records(M, rng)
        for i in range(HEAP_N):
            M.w32(HEAP + REC * i + 14, 0xDEADBEEF)
            M.w32(HEAP + REC * i + 18, 0xDEADBEEF)
        m_0161(M)
        self.assertEqual(M.r32(HEAP + 20 * REC + 14), 0)        # the 21st record is cleared too
        self.assertEqual(M.r32(HEAP + 21 * REC + 14), 0xDEADBEEF)


if __name__ == '__main__':
    unittest.main()
