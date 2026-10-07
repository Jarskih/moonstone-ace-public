"""Host test for src/game/scene_places.cpp (ROADMAP 6.8).  The C++ is compiled with clang++ (no STL, with rules.cpp and the
engine RNG) and compared, over many random knight / inventory / generator states, with a literal Python model of the
mog.asm blocks it replaces: the node dispatcher LAB_007B and the town button chains, the stat pick LAB_0465/LAB_0469, the
black knights' stat purchase LAB_0E2B, the random gifts LAB_046C / LAB_0471, Math the wizard's roll LAB_045E, the mystic's
donation buttons LAB_0499 / LAB_049A and gamble LAB_0489 / LAB_047D..0483, Stonehenge LAB_00A1..00AE, Danu's blessing
LAB_00A5/00A6 and the Valley of the Gods LAB_009D..00A0 / LAB_0DCA.  The model is written from the asm text (labels in the
docstrings) on big-endian byte images; the driver byte-swaps the multi-byte fields into the host structs and back, so
every field of every record is compared, and stray writes show up (all unrelated bytes are random).

The tables the asm reads as data (the stat rows LAB_090A, the gift bands LAB_090E, the mystic bonus table LAB_048D with its
mis-disassembled half, the text tables LAB_095B/095C) are parsed from mog.asm, so the model and therefore the C++ constants
are checked against the source.

Needs clang++ on PATH and the moonshard tree (the asm source).
"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from test_engine_util import asm_rng, asm_percent  # noqa: E402
from test_game_rules import (g8, g16, g32, p8, p16, p32, s8, s16, h, m0013, m0019, rnd_knight, rnd_inv)  # noqa: E402

ROOT = os.path.dirname(HERE)
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
MOG_ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
CXX = shutil.which('clang++')
M32 = 0xFFFFFFFF
CAP = 4000          # the asm loops are unbounded; a case that does not finish in this many steps is dropped


DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/scene_places.hpp"
using namespace ms;
using namespace ms::game;

static const int KW[] = {4, 6, 8, 62, 64, 66, 68, 74, 78, 80, 84, 116, 118, 120, 122, 124, 126, 128};
static const int KL[] = {0, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 58, 88, 92, 96, 100, 108};
static void sw2(uint8_t *p) { uint8_t t = p[0]; p[0] = p[1]; p[1] = t; }
static void sw4(uint8_t *p) { uint8_t t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }
static void swapK(uint8_t *p) { for (int o : KW) sw2(p + o); for (int o : KL) sw4(p + o); }
static int unhex(const char *s, uint8_t *out, int max) {
    int n = 0;
    while (s[0] && s[1] && n < max) { unsigned v; sscanf(s, "%2x", &v); out[n++] = (uint8_t)v; s += 2; }
    return n;
}
static void phex(const uint8_t *p, int n) { for (int i = 0; i < n; ++i) printf("%02x", p[i]); }
static void loadK(Knight &k, const char *hex) { uint8_t b[132]; unhex(hex, b, 132); memcpy(&k, b, 132); swapK((uint8_t *)&k); }
static void dumpK(const Knight &k) { uint8_t b[132]; memcpy(b, &k, 132); swapK(b); phex(b, 132); }
static void loadI(Inventory &i, const char *hex) { uint8_t b[24]; unhex(hex, b, 24); memcpy(&i, b, 24); }
static void dumpI(const Inventory &i) { phex((const uint8_t *)&i, 24); }
static uint32_t hx(const char *s) { return (uint32_t)strtoul(s, 0, 16); }

int main() {
    static char line[16384];
    while (fgets(line, sizeof line, stdin)) {
        char *tok[16]; int nt = 0;
        for (char *t = strtok(line, " \r\n"); t && nt < 16; t = strtok(0, " \r\n")) tok[nt++] = t;
        if (!nt) continue;
        char op = tok[0][0];
        Knight k; Inventory inv;
        if (op == 'P') {                          // placeClassify id
            printf("P %d\n", placeClassify((uint16_t)hx(tok[1])));
        } else if (op == 'B') {                   // townButton id
            printf("B %d\n", townButton(hx(tok[1])));
        } else if (op == 'S') {                   // pickStat kb seed
            loadK(k, tok[1]); uint32_t seed = hx(tok[2]);
            StatPick p = pickStat(k, seed);
            printf("S %x %d %08x\n", p.ulSlot, p.ulSlot ? p.ubRow : 0, seed);
        } else if (op == 'A') {                   // aiBuyStat kb cost seed
            loadK(k, tok[1]); uint32_t seed = hx(tok[3]);
            bool r = aiBuyStat(k, (uint16_t)hx(tok[2]), seed);
            printf("A %d ", r); dumpK(k); printf(" %08x\n", seed);
        } else if (op == 'G') {                   // giftGold seed
            uint32_t seed = hx(tok[1]);
            uint32_t r = giftGold(seed);
            printf("G %u %08x\n", r, seed);
        } else if (op == 'I') {                   // giftItem kb inv bknight last seed
            loadK(k, tok[1]); loadI(inv, tok[2]); uint32_t last = hx(tok[4]), seed = hx(tok[5]);
            uint32_t r = giftItem(&k, inv, atoi(tok[3]) != 0, last, seed);
            printf("I %u ", r); dumpK(k); printf(" "); dumpI(inv); printf(" %08x %08x\n", last, seed);
        } else if (op == 'J') {                   // giftItem on a bare inventory (no knight): inv last seed
            loadI(inv, tok[1]); uint32_t last = hx(tok[2]), seed = hx(tok[3]);
            uint32_t r = giftItem(0, inv, false, last, seed);
            printf("J %u ", r); dumpI(inv); printf(" %08x %08x\n", last, seed);
        } else if (op == 'M') {                   // mathRoll kb inv last gold cycle item cycle seed
            loadK(k, tok[1]); loadI(inv, tok[2]);
            MathState st; st.ulLastGift = hx(tok[3]); st.uwGoldCycle = (uint16_t)hx(tok[4]); st.uwItemCycle = (uint16_t)hx(tok[5]);
            uint32_t seed = hx(tok[6]);
            MathResult r = mathRoll(k, inv, st, seed);
            printf("M %u %u %u %u %u ", r.uwKind, r.uwKind == MK_STAT ? r.uwStat : 0, r.ubMsg, r.ulAmount, r.ulItem);
            dumpK(k); printf(" "); dumpI(inv); printf(" %08x %04x %04x %08x\n", st.ulLastGift, st.uwGoldCycle, st.uwItemCycle, seed);
        } else if (op == 'D' || op == 'E') {      // donationBack / donationMore donation purse
            uint16_t d = (uint16_t)hx(tok[1]), p = (uint16_t)hx(tok[2]);
            bool r = op == 'D' ? donationBack(d, p) : donationMore(d, p);
            printf("%c %d %04x %04x\n", op, r, d, p);
        } else if (op == 'Y') {                   // mysticBonus donation beyond
            printf("Y %d\n", mysticBonus((uint16_t)hx(tok[1]), (int16_t)hx(tok[2])));
        } else if (op == 'Z') {                   // mysticGamble kb inv donation seed beyond stale
            loadK(k, tok[1]); loadI(inv, tok[2]); uint32_t seed = hx(tok[4]);
            uint8_t stale = (uint8_t)hx(tok[6]);
            int r = mysticGamble(k, inv, (uint16_t)hx(tok[3]), seed, (int16_t)hx(tok[5]), &stale);
            printf("Z %d ", r); dumpK(k); printf(" %02x %08x\n", stale, seed);
        } else if (op == 'T') {                   // stoneMatches frame stones
            printf("T %d\n", stoneMatches((uint16_t)hx(tok[1]), (uint8_t)hx(tok[2])));
        } else if (op == 'U') {                   // stoneEnding frame kind
            printf("U %04x\n", stoneEnding((uint16_t)hx(tok[1]), hx(tok[2])));
        } else if (op == 'N') {                   // danuBlessing kb inv
            loadK(k, tok[1]); loadI(inv, tok[2]);
            danuBlessing(k, inv);
            printf("N "); dumpK(k); printf("\n");
        } else if (op == 'V') {                   // valleyKeysComplete inv
            loadI(inv, tok[1]);
            printf("V %d\n", valleyKeysComplete(inv));
        } else if (op == 'W') {                   // valleyDefeat kb seed
            loadK(k, tok[1]); uint32_t seed = hx(tok[2]);
            valleyDefeat(k, seed);
            printf("W "); dumpK(k); printf(" %08x\n", seed);
        } else if (op == 'X') {                   // valleyVictory kb inv
            loadK(k, tok[1]); loadI(inv, tok[2]);
            valleyVictory(k, inv);
            printf("X "); dumpK(k); printf(" "); dumpI(inv); printf("\n");
        } else if (op == 'Q') {                   // valleyMoonstone inv seed
            loadI(inv, tok[1]); uint32_t seed = hx(tok[2]);
            uint8_t b = valleyMoonstone(inv, seed);
            printf("Q %d ", b); dumpI(inv); printf(" %08x\n", seed);
        }
    }
    return 0;
}
'''


def run_driver(lines):
    with tempfile.TemporaryDirectory() as tmp:
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(DRIVER)
        srcs = [p] + [os.path.join(ROOT, 'src', x) for x in ('game/scene_places.cpp', 'game/rules/stats.cpp', 'game/rules/rituals.cpp', 'game/rules/dice.cpp', 'game/rules/shops.cpp', 'game/rules/healing.cpp', 'game/rules/levelling.cpp', 'game/rules/settle.cpp', 'game/rules/clock.cpp', 'game/rules/turns.cpp', 'engine/util.cpp')] + [GAMEDATA_SOURCE]
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti',
               '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include')] + srcs + ['-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)
        out = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True, timeout=300)
        if out.returncode:
            raise RuntimeError('driver failed: %s %s' % (out.returncode, out.stdout[-300:]))
        return out.stdout.splitlines()


# ---------------------------------------------------------------------------------------------------------
# Tables parsed from mog.asm

def asm_lines():
    return open(MOG_ASM, encoding='latin-1').read().split('\n')


def parse_longs(label, stop=None, limit=None):
    """The DC.L values (ints, or label names) after `label:`; DS.L n gives n zero longs; stops at the next label."""
    lines = asm_lines()
    i = lines.index(label + ':') + 1
    out = []
    while i < len(lines) and (lines[i].startswith('\tDC.L') or lines[i].startswith('\tDS.L')):
        t = lines[i].split()
        if t[0] == 'DS.L':
            out += [0] * int(t[1])
        else:
            for x in t[1].split(','):
                out.append(int(x[1:], 16) if x.startswith('$') else x)
        i += 1
    return out[:limit] if limit else out


def parse_stat_rows():
    """LAB_090A: nine rows of {stat offset, text label}; returns the nine offsets."""
    v = parse_longs('LAB_090A', limit=18)
    assert all(isinstance(x, str) for x in v[1::2]) and len(v) == 18
    return v[0::2]


def parse_gift_bands():
    """LAB_090E: {upper bound, slot} x 10 (the first slot is a DS.L 1)."""
    v = parse_longs('LAB_090E', limit=20)
    return [(v[2 * i], v[2 * i + 1]) for i in range(10)]


def parse_mystic_words():
    """LAB_048D: the bonus table; the first half of it is disassembled as ORI.B, re-encoded here (ORI.B #imm,(An) = $0010+n,
    ORI.B #imm,(An)+ = $0018+n, then the immediate)."""
    lines = asm_lines()
    i = lines.index('LAB_048D:') + 1
    words = []
    while not lines[i].startswith('LAB_048E'):
        t = lines[i].split()
        if t[0] == 'DC.W':
            words.append(int(t[1][1:], 16))
        else:
            m = re.match(r'ORI\.B\t#\$([0-9a-f]+),\(A(\d)\)(\+?)', lines[i].strip())
            assert m, lines[i]
            words += [(0x18 if m.group(3) else 0x10) + int(m.group(2)), int(m.group(1), 16)]
        i += 1
    assert len(words) == 12
    return words


def parse_text_keys(label):
    """LAB_095B / LAB_095C: the stat keys of the first three {key, text} rows."""
    v = parse_longs(label, limit=6)
    return v[0::2]


# ---------------------------------------------------------------------------------------------------------
# Python model of the asm.  k = 132-byte Knight image, inv = 24-byte Inventory image (big-endian).

def add16(b, o, v):
    p16(b, o, g16(b, o) + v)


def rng_step(seed):
    return asm_rng(seed, 0)


def rng_percent(seed):
    """LAB_04A3: JSR LAB_04A1 ; ANDI.L #$7f,D0 ; CMP.L #100,D0 ; BLT ; SUBI.L #27,D0."""
    seed = asm_rng(seed, 0)
    return seed, asm_percent(seed)


def m_classify(d0):
    """LAB_007B: the CMP.W / BEQ chain, as the target label."""
    d0 &= 0xFFFF
    for val, tgt in ((0x15, 'B0'), (0x16, 'B0'), (0x17, 'B0'), (0x18, 'B0'), (0x19, '93'), (0x1A, '8A'), (0x1B, 'A1'),
                     (0x1C, '9D'), (0x1E, '7C'), (0x21, '4F')):
        if d0 == val:
            return tgt
    return None


PLACE_OF = {'B0': 1, '93': 2, '8A': 3, 'A1': 4, '9D': 5, '7C': 6, '4F': 7, None: 0}


def m_button(btn):
    """LAB_008C / LAB_0095: CMPI.L #1..#5,16(A0) in order."""
    for n in (1, 2, 3, 4, 5):
        if btn == n:
            return n
    return 0


def m_0469(k, seed, rows, cap=CAP):
    """LAB_0465 / LAB_0469.  Returns (D1, row index, seed) or None when the loop does not end."""
    for _ in range(cap):
        d0 = 0
        for o in (70, 71, 72):
            if not s8(k[o]) < 5:                          # CMPI.B #5,n(A0) ; BLT
                d0 += 1
        if d0 == 3:
            return 0, 0, seed                             # EOR.L D1,D1
        seed = rng_step(seed)
        d0 = seed & 0xF                                   # ANDI.L #$f,D0
        if not d0 <= 8:                                   # CMP.L #8,D0 ; BLE
            d0 -= 7
        d1 = rows[d0]                                     # LSL.L #3,D0 ; MOVE.L 0(A0,D0.L),D1
        if k[d1] != 5:                                    # CMPI.B #5,0(A1,D1.L) ; BEQ.W LAB_0469
            return d1, d0, seed
    return None


def m_ai(k, cost, seed, rows):
    """LAB_0E2B."""
    if s16(cost) > s16(g16(k, 78)):                       # CMP.W 78(A0),D0 ; BGT
        return 0, seed
    r = m_0469(k, seed, rows)
    if r is None:
        return None
    d1, _row, seed = r
    if d1 == 0:                                           # TST.W D1 ; BEQ
        return 0, seed
    k[d1] = (k[d1] + 1) & 0xFF
    p16(k, 78, g16(k, 78) - cost)
    return 1, seed


def m_gold(seed):
    """LAB_046C head."""
    seed = rng_step(seed)
    d0 = seed & 0x1F
    if not d0 <= 0x15:
        d0 -= 10
    return d0 + 10, seed


def m_item(k, inv, bk, last, seed, bands, cap=CAP):
    """LAB_0471 / LAB_0473..LAB_0476.  Returns (slot, last, seed) or None."""
    for _ in range(cap):
        seed, d0 = rng_percent(seed)
        slot = None
        for thr, sl in bands:                             # CMP.L (A0)+,D0 ; BLE LAB_0473 ; TST.L (A0)+ ; DBF D7
            if d0 <= thr:
                slot = sl
                break
        assert slot is not None
        if slot == last:                                  # CMP.L LAB_090D,D0 ; BEQ.W LAB_0471
            continue
        last = slot                                       # MOVE.L D0,LAB_090D
        if slot == 4:
            if inv[4] != 0:                               # TST.B 4(A0) ; BNE.W LAB_0471
                continue
            if bk:                                        # TST.W D3 ; BNE.W LAB_0476
                p32(k, 88, 0x19)
        inv[slot] = (inv[slot] + 1) & 0xFF                # LAB_0476
        if bk and slot == 6:
            add16(k, 80, 0x14)
            m0013(k, inv)
        return slot, last, seed
    return None


def m_math(k, inv, last, gc, ic, seed, rows, bands, cap=CAP):
    """LAB_045E.  Returns (kind, stat, msg, amount, item, last, gc, ic, seed) or None."""
    for _ in range(cap):
        seed, d0 = rng_percent(seed)
        d0 = (d0 + k[83]) & 0xFF                          # ADD.B 83(A0),D0
        if d0 <= 0x1E:                                    # LAB_0461
            r = m_item(k, inv, True, last, seed, bands)
            if r is None:
                return None
            slot, last, seed = r
            ic = (ic + 1) & 3
            return 1, 0, ic, 0, slot, last, gc, ic, seed
        if d0 <= 0x46:                                    # LAB_0462
            r = m_0469(k, seed, rows)
            if r is None:
                return None
            d1, row, seed = r
            if d1 == 0:                                   # BEQ.W LAB_045E
                continue
            k[d1] = (k[d1] + 1) & 0xFF
            if d1 == 0x47:
                m0013(k, inv)
                add16(k, 80, 0x0A)
            if d1 == 0x48:
                m0019(k)
            return 3, d1, row, 0, 0, last, gc, ic, seed
        if d0 <= 0x5A:                                    # LAB_045F
            amount, seed = m_gold(seed)
            add16(k, 74, amount)
            gc = (gc + 1) & 0xFFFF
            if not s16(gc) < 3:                           # CMPI.W #3 ; BLT
                gc = 0
            return 2, 0, gc, amount, 0, last, gc, ic, seed
        if k[83] == 0xFF:                                 # CMPI.B #$ff,83(A0) ; BEQ.W LAB_045E
            continue
        k[82] = 3
        return 4, 0, 0, 0, 0, last, gc, ic, seed
    return None


def m_donation(don, purse, back):
    """LAB_0499 (back) / LAB_049A (more)."""
    if back:
        if don == 0:
            return 0, don, purse
        return 1, (don - 1) & 0xFFFF, (purse + 1) & 0xFFFF
    if purse == 0:
        return 0, don, purse
    return 1, (don + 1) & 0xFFFF, (purse - 1) & 0xFFFF


def m_bonus(don, beyond, words):
    """LAB_0489: CMP.W (A0),D1 ; BLE ; ADDQ.L #4,A0 ; DBF D2 (six entries), then MOVE.W 2(A0),D2."""
    for i in range(6):
        if s16(don) <= s16(words[2 * i]):
            return words[2 * i + 1]
    return beyond & 0xFFFF


def m_mystic(k, inv, don, seed, beyond, stale, rows, words, raise_keys, lower_keys):
    """LAB_0469 + LAB_0489 + LAB_047D..LAB_0483; stale = bytearray(1) = the byte A1 points at when D1 = 0.
    Returns MysticMsg (0 nothing, 1 unchanged, 2.. raise rows, 5.. lower rows) and the new seed."""
    r = m_0469(k, seed, rows)
    if r is None:
        return None
    d1, _row, seed = r
    seed, roll = rng_percent(seed)                        # LAB_0489: JSR LAB_04A3
    d0 = (roll + m_bonus(don, beyond, words)) & 0xFFFF    # ADD.W D2,D0
    win = not s16(d0) < 0x32                              # CMP.W #$32,D0 ; BLT LAB_048C
    mem, off = (k, d1) if d1 else (stale, 0)              # 0(A1,D1.L): A1 = the knight only when a stat was picked
    if win:
        if d1 == 0:                                       # TST.W D1 ; BNE LAB_047D ; LEA LAB_0956
            return 0, seed
        mem[off] = (mem[off] + 1) & 0xFF
        if d1 == 0x47:
            add16(k, 80, 0x0A)
        m0013(k, inv)                                     # MOVEA.L A1,A0 ; JSR LAB_0013
        m0019(k)                                          # JSR LAB_0019
        tbl, base = raise_keys, 2
    else:
        if mem[off] == 1:                                 # LAB_047F: CMPI.B #1 ; BEQ.W LAB_0486
            return 1, seed
        mem[off] = (mem[off] - 1) & 0xFF
        if d1 == 0x47:
            p16(k, 80, g16(k, 80) - 10)                   # SUBI.W #$a,80(A0)
            if not s16(g16(k, 80)) > 0:                   # BGT LAB_0480
                p16(k, 80, 1)
        m0019(k)                                          # LAB_0480: JSR LAB_0019
        m0013(k, inv)                                     # JSR LAB_0013
        tbl, base = lower_keys, 5
    for i in range(3):                                    # LAB_0481: MOVEQ #2,D0 ; CMP.L (A0),D1 ; ADDA.L #8 ; DBF
        if tbl[i] == d1:
            return base + i, seed
    return 1, seed                                        # LAB_0486


def m_stone_match(d0, d1):
    """LAB_00A1..LAB_00A4 (D0 = frame word, D1 = Inventory +22)."""
    for bit, frame in ((2, 0x2E), (3, 0x2E), (1, 0x2D), (0, 0x31)):
        if d1 & (1 << bit) and d0 == frame:               # BTST #bit,D1 ; BEQ ; CMP.W #frame,D0 ; BEQ.W LAB_00A8
            return 1
    return 0


def m_stone_code(d0, kind):
    """LAB_00A8..LAB_00AE."""
    d7 = 0
    if d0 == 0x2E:
        d7 |= 1 << 0
    if d0 == 0x2D:
        d7 |= 1 << 2
    if d0 == 0x31:
        d7 |= 1 << 1
    for val, bit in ((3, 3), (0, 4), (1, 5), (2, 6)):
        if kind == val:
            d7 |= 1 << bit
    return d7


def m_danu(k, inv):
    """LAB_00A5/LAB_00A6: CMPI.B #5,73(A0) ; BEQ ; ADDI.B #1,73(A0) ; BSR.W LAB_0013 ; MOVE.W 84(A0),80(A0) ; MOVE.B #0,130(A0)."""
    if k[73] != 5:
        k[73] = (k[73] + 1) & 0xFF
    m0013(k, inv)
    p16(k, 80, g16(k, 84))
    k[130] = 0


def m_defeat(k, seed, rows):
    """LAB_009E..LAB_009F: JSR LAB_0469 ; SUBI.B #2,73(A0) ; CMPI.B #1,0(A0,D1.W) ; BEQ ; SUBI.B #1,0(A0,D1.W)."""
    r = m_0469(k, seed, rows)
    if r is None:
        return None
    d1, _row, seed = r
    k[73] = (k[73] - 2) & 0xFF
    if k[d1] != 1:
        k[d1] = (k[d1] - 1) & 0xFF
    return seed


def m_victory(k, inv):
    """LAB_00A0."""
    add16(k, 78, 3)
    inv[20] = 0


def m_moonstone(inv, seed):
    """LAB_0DCA tail: JSR LAB_04A1 ; ANDI.L #3,D0 ; BSET D0,22(A0)."""
    seed = rng_step(seed)
    bit = seed & 3
    inv[22] |= 1 << bit
    return bit, seed


# ---------------------------------------------------------------------------------------------------------
STATS = [0, 1, 2, 3, 4, 5, 5, 5, 6, 7, 8, 0x7F, 0x80, 0xFF]
GOLDS = [0, 1, 9, 10, 31, 100, 149, 150, 0x7FFF, 0x8000, 0xFFFF]


def rk(r):
    """A random knight with the fields the place code reads biased to the interesting values."""
    k = rnd_knight(r)
    for o in (70, 71, 72):
        k[o] = r.choice(STATS)
    k[73] = r.choice([0, 1, 2, 3, 4, 5, 6, 7, 0x7F, 0x80, 0xFF])
    k[82] = r.choice([0, 1, 3, 0x80, 0xFF])
    k[83] = r.choice([0xFF, 0xFF, 0, 5, 10, 0x1E, 0x46, 0x50, 0x8A, r.getrandbits(8)])
    p16(k, 74, r.choice(GOLDS + [r.getrandbits(16)]))
    p16(k, 78, r.choice([0, 1, 2, 3, 4, 5, 0x7FFF, 0x8000, 0xFFFF, r.getrandbits(16)]))
    p16(k, 80, r.choice([0, 1, 2, 9, 10, 11, 20, 0x7FFF, 0x8000, 0xFFFF, r.getrandbits(16)]))
    p32(k, 88, r.choice([0x16, 0x17, 0x18, 0x19, r.getrandbits(32)]))
    p32(k, 0, r.choice([0, 0, 1, 0x01000000, 0xFF000000, r.getrandbits(32)]))
    return k


def ri(r):
    inv = rnd_inv(r, dense=r.random() < .5)
    inv[4] = r.choice([0, 0, 0, 1, 255])
    inv[20] = r.choice([0, 3, 7, 0x0F, 0x0F, 0xFF, r.getrandbits(8)])
    return inv


def seed_of(r):
    """The game's own start seed ($1B, LAB_0973) stepped a few times, or a random state.  Degenerate states (1, $FFFFFFFF, ...)
    are left out: the generator then walks a short cycle and the asm's unbounded redraw loops would not end either."""
    if r.random() < .4:
        s = 0x1B
        for _ in range(r.randrange(0, 40)):
            s = asm_rng(s, 0)
        return s
    return r.getrandbits(32) or 0x1B


LASTS = [0xFFFFFFFF, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18]


@unittest.skipUnless(CXX and os.path.isfile(MOG_ASM), 'needs clang++ and the moonshard asm source')
class ScenePlaces(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        r = random.Random(0x6800)
        cls.rows = parse_stat_rows()
        cls.bands = parse_gift_bands()
        cls.words = parse_mystic_words()
        cls.raise_keys = parse_text_keys('LAB_095B')
        cls.lower_keys = parse_text_keys('LAB_095C')
        rows, bands, words = cls.rows, cls.bands, cls.words
        lines, exp = [], []

        def case(line, expected):
            lines.append(line)
            exp.append(expected)

        # dispatcher and buttons
        for node in range(0, 0x30):
            case('P %x' % node, 'P %d' % PLACE_OF[m_classify(node)])
        for node in (0x10015, 0x1B0000, 0xFFFF, 0x2001A):          # only the low word counts (CMP.W)
            case('P %x' % node, 'P %d' % PLACE_OF[m_classify(node)])
        for btn in list(range(0, 9)) + [0x10001, 0xFFFFFFFF, 0x100000005]:
            btn &= M32
            case('B %x' % btn, 'B %d' % m_button(btn))
        # stat pick, AI purchase
        for _ in range(3000):
            k, seed = rk(r), seed_of(r)
            res = m_0469(bytearray(k), seed, rows)
            if res is None:
                continue
            d1, row, s2 = res
            case('S %s %x' % (h(k), seed), 'S %x %d %08x' % (d1, row if d1 else 0, s2))
        for _ in range(2500):
            k, seed = rk(r), seed_of(r)
            cost = r.choice([3, 3, 3, 0, 1, 0x8000, 0xFFFF, r.getrandbits(16)])
            e = bytearray(k)
            res = m_ai(e, cost, seed, rows)
            if res is None:
                continue
            ret, s2 = res
            case('A %s %x %x' % (h(k), cost, seed), 'A %d %s %08x' % (ret, h(e), s2))
        # gifts
        for _ in range(1500):
            seed = seed_of(r)
            amount, s2 = m_gold(seed)
            case('G %x' % seed, 'G %d %08x' % (amount, s2))
        for _ in range(4000):
            k, inv, seed, last = rk(r), ri(r), seed_of(r), r.choice(LASTS)
            bk = r.random() < .6
            e, ei = bytearray(k), bytearray(inv)
            res = m_item(e, ei, bk, last, seed, bands)
            if res is None:
                continue
            slot, last2, s2 = res
            case('I %s %s %d %x %x' % (h(k), h(inv), bk, last, seed), 'I %d %s %s %08x %08x' % (slot, h(e), h(ei), last2, s2))
        for _ in range(1500):
            inv, seed, last = ri(r), seed_of(r), r.choice(LASTS)
            ei = bytearray(inv)
            res = m_item(None, ei, False, last, seed, bands)
            if res is None:
                continue
            slot, last2, s2 = res
            case('J %s %x %x' % (h(inv), last, seed), 'J %d %s %08x %08x' % (slot, h(ei), last2, s2))
        # Math
        for _ in range(6000):
            k, inv, seed, last = rk(r), ri(r), seed_of(r), r.choice(LASTS)
            gc, ic = r.choice([0, 1, 2, 0xFFFF, 3, 7]), r.choice([0, 1, 2, 3, 0xFFFF, 6])
            if r.random() < .5:
                k[83] = r.choice([0, 0, 5, 10, 0x1E, 0x46])
            e, ei = bytearray(k), bytearray(inv)
            res = m_math(e, ei, last, gc, ic, seed, rows, bands)
            if res is None:
                continue
            kind, stat, msg, amount, item, last2, gc2, ic2, s2 = res
            case('M %s %s %x %x %x %x' % (h(k), h(inv), last, gc, ic, seed),
                 'M %d %d %d %d %d %s %s %08x %04x %04x %08x' % (kind, stat, msg, amount, item, h(e), h(ei), last2, gc2, ic2, s2))
        # mystic
        for _ in range(1500):
            don, purse = r.choice([0, 1, 2, 0xFFFF, r.getrandbits(16)]), r.choice([0, 1, 2, 0xFFFF, r.getrandbits(16)])
            for op, back in (('D', True), ('E', False)):
                ok, d2, p2 = m_donation(don, purse, back)
                case('%s %x %x' % (op, don, purse), '%s %d %04x %04x' % (op, ok, d2, p2))
        for don in list(range(0, 300)) + [0x7FFF, 0x8000, 0x8001, 0xFFFF]:
            beyond = r.getrandbits(16)
            b = m_bonus(don, beyond, words)
            case('Y %x %x' % (don, beyond), 'Y %d' % (b - 0x10000 if b & 0x8000 else b))
        for _ in range(8000):
            k, inv, seed = rk(r), ri(r), seed_of(r)
            don = r.choice([1, 5, 9, 10, 15, 19, 20, 29, 30, 39, 40, 49, 50, 100, 150, 250, 251, 300, 0x7FFF, 0x8000, r.randrange(1, 150)])
            beyond, stale = r.getrandbits(16), r.choice([0, 1, 2, 0xFF, r.getrandbits(8)])
            if r.random() < .3:
                for o in (70, 71, 72):                    # all stats at the limit: the slot-0 quirk
                    k[o] = r.choice([5, 6, 0x7F])
            e, ei, es = bytearray(k), bytearray(inv), bytearray([stale])
            res = m_mystic(e, ei, don, seed, beyond, es, rows, words, cls.raise_keys, cls.lower_keys)
            if res is None:
                continue
            msg, s2 = res
            case('Z %s %s %x %x %x %x' % (h(k), h(inv), don, seed, beyond, stale), 'Z %d %s %02x %08x' % (msg, h(e), es[0], s2))
        # Stonehenge
        for frame in [0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0, 0xFFFF, 0x1002E]:
            for stones in range(256):
                case('T %x %x' % (frame, stones), 'T %d' % m_stone_match(frame & 0xFFFF, stones))
            for kind in (0, 1, 2, 3, 4, 5, 0x100000003, 0xFFFFFFFF):
                kind &= M32
                case('U %x %x' % (frame, kind), 'U %04x' % m_stone_code(frame & 0xFFFF, kind))
        for _ in range(1500):
            k, inv = rk(r), ri(r)
            if r.random() < .3:
                k[73] = 5
            e = bytearray(k)
            m_danu(e, inv)
            case('N %s %s' % (h(k), h(inv)), 'N %s' % h(e))
        # Valley of the Gods
        for _ in range(300):
            inv = ri(r)
            case('V %s' % h(inv), 'V %d' % (inv[20] == 0x0F))
        for _ in range(3000):
            k, seed = rk(r), seed_of(r)
            e = bytearray(k)
            s2 = m_defeat(e, seed, rows)
            if s2 is None:
                continue
            case('W %s %x' % (h(k), seed), 'W %s %08x' % (h(e), s2))
        for _ in range(500):
            k, inv = rk(r), ri(r)
            e, ei = bytearray(k), bytearray(inv)
            m_victory(e, ei)
            case('X %s %s' % (h(k), h(inv)), 'X %s %s' % (h(e), h(ei)))
        for _ in range(800):
            inv, seed = ri(r), seed_of(r)
            ei = bytearray(inv)
            bit, s2 = m_moonstone(ei, seed)
            case('Q %s %x' % (h(inv), seed), 'Q %d %s %08x' % (bit, h(ei), s2))
        cls.lines, cls.exp = lines, exp
        cls.out = run_driver(lines)

    def _check(self, kind):
        n = 0
        self.assertEqual(len(self.out), len(self.exp))
        for line, exp, got in zip(self.lines, self.exp, self.out):
            if line[0] not in kind:
                continue
            n += 1
            if exp != got:
                ea, ga = exp.split(), got.split()
                diffs = [i for i, (x, y) in enumerate(zip(ea, ga)) if x != y]
                self.fail('case %r\n field %s\n expected %s\n got      %s' % (
                    line[:200], diffs, ea[diffs[0]] if diffs else exp, ga[diffs[0]] if diffs else got))
        self.assertGreater(n, 0)

    def test_dispatch(self):
        self._check('PB')

    def test_stat_pick_and_ai(self):
        self._check('SA')

    def test_gifts(self):
        self._check('GIJ')

    def test_math(self):
        self._check('M')

    def test_mystic(self):
        self._check('DEYZ')

    def test_stonehenge_danu(self):
        self._check('TUN')

    def test_valley(self):
        self._check('VWXQ')

    def test_tables_from_the_asm(self):
        """The constants the C++ carries are the ones in the source: stat rows, gift bands, mystic bonus and text keys."""
        self.assertEqual(self.rows, [0x46, 0x48, 0x47, 0x48, 0x46, 0x48, 0x47, 0x47, 0x46])
        self.assertEqual(self.bands, [(25, 0), (35, 2), (45, 4), (55, 6), (65, 8), (75, 10), (80, 14), (85, 12), (94, 16), (100, 18)])
        self.assertEqual(self.words, [9, 20, 19, 10, 29, 0, 39, 0xFFF6, 49, 0xFFEC, 250, 0xFFE2])
        self.assertEqual(self.raise_keys, [0x46, 0x47, 0x48])
        self.assertEqual(self.lower_keys, [0x46, 0x47, 0x48])
        # the data hunk keeps a copy of the first five bonus pairs after LAB_095C (the last bound differs: 100 there)
        data = parse_longs('LAB_095C')
        words = []
        for v in data[12:18]:
            words += [(v >> 16) & 0xFFFF, v & 0xFFFF]
        self.assertEqual(words[:10], self.words[:10])


if __name__ == '__main__':
    unittest.main()
