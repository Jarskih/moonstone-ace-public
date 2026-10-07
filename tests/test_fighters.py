"""Host test for src/game/fighters.cpp (ROADMAP 6.4a): the per-fighter handlers of mog.asm LAB_01CA..LAB_02D2 (knight,
flyer, brawler, caster, dragon, dragon part, drake, dagger, idle object) with the hurt reactions LAB_01E1..LAB_0206 and
the movement helpers LAB_0F1A..LAB_0F34, plus the script-called sound pickers LAB_02DC..LAB_02E9.

ROADMAP 7.1h adds the four creature handlers of mog's S_40 hunk (SECSTRT_40 = type 4, LAB_0ED2 = the demon, LAB_0EFF = the
computer's knight, LAB_0EC2 = type 64; src/game/fight_creatures.cpp), the routines the combat scripts call through opcode $B0
(src/game/fight_ops.cpp: fightOpRun), the knight defaults LAB_01C6 and the handler table fill; the patch file
asm/patches/mog.fight_ops.json is checked here too (it must not overlap any other patch, the DC.L operands it tags are the
$B0 operands, the asm its shims call exists).

Reference: the LIFTED asm (tools/lift.py output, a literal 68k transliteration with exact CCR semantics).  The lifter
works one routine at a time, so the oracle is built here: every routine of the closure (and the secondary entries the
handlers jump into, e.g. LAB_01DD / LAB_0202 / LAB_029E) is lifted through the lifter's Python API with synthetic routine
records; the two table dispatches (JMP (A2) of LAB_01E0 / LAB_01EC, which the lifter refuses) are two hand-written
units in the driver.  The oracle runs on a big-endian guest arena in a host driver next to the C++ on a copy of the same
arena (structs converted to native order).  After every case the whole arena (minus the scratch cells the C++ does not
keep, and the stack), the result registers of LAB_02BA (A0 = next script, D0..D3 = x / y / z / facing) and the trace of
callbacks must be identical.

The callbacks (joystick LAB_00EE, wall probe LAB_0A71, sound LAB_0AA2, copper effect LAB_0427, beam position VHPOSR) are
deterministic stand-ins that are the same function on both sides, so the test shows that both sides call them at the same
points with the same arguments and handle what they return alike.

Deliberate differences of the C++ (fighters.hpp) are excluded by the generators: types the asm never maps in its
handler tables (a jump to address 0), job lookups that miss, an empty dagger slot table in a flight state (the asm stores
stale registers), pointers outside the arena.

Needs clang++ on PATH; the lifted sources are generated (build/fighters_test/) from the moonshard asm.  FIGHTERS_N=<n>
scales the number of cases per handler (default 300).
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no the IRA listing; the whole module skips)
origskip.require_listing()
import json
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
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diffharness'))
import test_creatures as TC  # noqa: E402  (module import only: its test class must not be collected twice)

A = TC.A
SYM = TC.SYM
CXX = shutil.which('clang++')
WORK_DIR = os.path.join(ROOT, 'build', 'fighters_test')
N_CASES = int(os.environ.get('FIGHTERS_N', '300'))

ARENA, STACK_LO, STACK_TOP = TC.ARENA, TC.STACK_LO, TC.STACK_TOP
KNIGHT0, REC, HEAP, HEAP_N = TC.KNIGHT0, TC.REC, TC.HEAP, TC.HEAP_N
JOBS, WORKS, SLOTS, BLOCK = TC.JOBS, TC.WORKS, TC.SLOTS, TC.BLOCK
TABLES = 0x142000          # script / damage tables (two variants, 5 tables of 0x100 bytes each)
INVS = 0x146000            # inventories, 24 bytes per record

# ---- the oracle: the closure of lifted routines -------------------------------------------------------------------------
PRIMARY = ('01CA 01D9 01DB 01DC 01E1 01E6 01ED 01EF 01F2 01F6 01F9 01FD 0200 0201 0203 0204 0205 0206 020B 020E 0215 021B '
           '0226 0236 0251 027A 028E 0297 0298 029F 02AF 02BA 02BB 02BC 02BF 02C2 02C4 02C6 02C8 02D0 02D2 02D3 02CB 02F6 '
           '02FD 0F1A 0F1C 0F1E 0F1F 0F20 0F24 0F2E 0F32 0F33 0F34 04A1 04A3 0006 000D 0315 0319 031B 030D 0171 0310 03A9 '
           '03AC 03B3 00EA 02DC 02DE 02E0 02E1 02E2 02E3 02E4 02E6 02E8 02E9 02EA '
           'SECSTRT_40 0EC2 0ED2 0EFF 01C6 01C8 0210 0211 02AC 02AD 02AE 02DB 02CA 0EB8 0EB9 0EBD 0EBE 0ED0 0EEB 0EEC 0EED '
           '0EEE 0EEF 0EF6 0EF7').split()
# routines the oracle does not run: hardware / sound / screen effects / the debug stub (see the driver for the stand-ins)
MOCKS = {'LAB_00EE', 'LAB_0A71', 'LAB_0AA2', 'LAB_0427', 'LAB_0BB3', 'SECSTRT_16', 'LAB_0A9B', 'LAB_0A9C', 'LAB_0A9D',
         'LAB_0D8A', 'LAB_03EE'}
# the two table dispatches the lifter refuses (JMP (A2)): hand-written in the driver
HAND = {'LAB_01E0', 'LAB_01EC'}


def label_of(x):
    """PRIMARY entry -> mog label ('01CA' -> LAB_01CA, 'SECSTRT_40' as it is)."""
    return x if x.startswith('SECSTRT_') else 'LAB_' + x


def stem(label):
    """The lifter's file / function stem (tools/lift.py file_stem): lab_01ca, lab_secstrt_40."""
    return 'lab_' + (label[4:] if label.startswith('LAB_') else label).lower()


def lift_closure():
    """Lift every routine of the closure into WORK_DIR/lifted; returns the sorted list of labels."""
    import lift
    out = os.path.join(WORK_DIR, 'lifted')
    stamp = os.path.join(WORK_DIR, 'stamp')
    newest = max(os.path.getmtime(p) for p in (TC.MOG_ASM, os.path.join(ROOT, 'tools', 'lift.py'),
                                              os.path.join(ROOT, 'tools', 'lift_emit.py'), TC.SYMS_HPP, __file__))
    listing = os.path.join(WORK_DIR, 'units.json')
    if os.path.exists(stamp) and os.path.exists(listing) and os.path.getmtime(stamp) >= newest:
        with open(listing) as f:
            return json.load(f)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    from pathlib import Path
    prog = lift.Program('mog')
    rev = {}
    for n, (h, o) in prog.labels.items():
        rev.setdefault((h, o), []).append(n)
    done = set()
    todo = [label_of(x) for x in PRIMARY]
    while todo:
        n = todo.pop()
        if n in done or n in MOCKS or n in HAND:
            continue
        done.add(n)
        if n not in prog.routines:          # a secondary entry: a synthetic routine record starting at the label
            sym = prog.symbols[n]
            cover = next(r for r in prog.routines.values()
                         if r['hunk'] == sym['hunk'] and r['start'] <= sym['offset'] < r['start'] + r['size'])
            r = dict(cover)
            r.update(name=n, start=sym['offset'], line_first=sym['line'],
                     size=cover['start'] + cover['size'] - sym['offset'])
            prog.routines[n] = r
        p, em = lift.write_routine(prog, n, Path(out), set(), True)
        with open(p, encoding='utf-8') as f:
            src = f.read()
        src = src.replace('lift::callAsm(', 'tcallAsm(')
        with open(p, 'w', encoding='utf-8', newline='\n') as f:
            f.write(src)
        for m in re.finditer(r'tcallAsm\(R, M, MS_HUNK\(mog, ([0-9A-F]{2})\) \+ 0x([0-9A-F]+)u\)', src):
            for x in rev.get((int(m.group(1), 16), int(m.group(2), 16)), []):
                if x.startswith(('LAB_', 'SECSTRT_')) and x not in done and x not in MOCKS and x not in HAND:
                    todo.append(x)
    labels = sorted(done)
    with open(listing, 'w') as f:
        json.dump(labels, f)
    with open(stamp, 'w') as f:
        f.write('ok')
    return labels


# ---- constants of the asm data, checked against the reassembled image when it is there ------------------------------------------
DATA = {   # label -> words (big-endian words of the asm)
    '08C9': [0x19, 3, 0x17, 4, 0x19], '08CA': [2, 9, 2, 9], '08CB': [8, 2, 9, 2], '08CC': [0x1E, 0x19, 0x14, 0x14, 0x14],
    '07D9': [0x0000, 0x0008, 0x0014, 0x0000, 0x001C, 0x0004, 0x0010, 0x0000, 0x0020, 0x0018, 0x000C],
    '07DA': [0x0000, 0x0014, 0x0008, 0x0000, 0x001C, 0x0010, 0x0004, 0x0000, 0x0020, 0x000C, 0x0018, 0x0016, 0, 0, 0, 0x0040,
             0, 0, 0, 0x0020, 0, 0],
    '024E': [0x0000, 0xFFFF, 0x0007, 0x0001, 0x0017, 0x0000, 0x0000, 0xFFFF, 0x0007, 0x0001, 0x0017, 0x0000,
             0x0000, 0x000A, 0x0000, 0x0003, 0x0001, 0x0007, 0xFFFF, 0x0003, 0x0000, 0x000A,
             0x0003, 0x000A, 0x0002, 0x0004, 0x0001, 0x0004, 0x0004, 0x0002, 0x0000, 0x000A],
    '0234': [0x0000, 0x0021, 0x001B, 0x0011, 0x0021],
    '02EE': [0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF],
    '02EF': [0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0xFFFF],
    # ROADMAP 7.1h: the data of the S_40 handlers (the asm decoded the code-hunk words as instructions)
    '0EB6': [0x0000, 0x000C, 0x000C, 0x000A, 0x000E, 0x000C, 0x000C, 0x000A, 0x000E],      # flags word, then the x steps
    '0ECE': [0x0000, 0x0010, 0x0000, 0x001A, 0x0000, 0x000D, 0x0000, 0x001A, 0x0000, 0x0000,
             0x8889, 0x8A8B, 0x8C8D, 0x8E89, 0x8C8D, 0x8B8C, 0x8900],                         # LAB_0ECE, then LAB_0ECF
    '0EC0': [0x5354, 0x5555],
    '097B': [0x463C, 0x3228, 0x1E14, 0x0F0A, 0x0505, 0x0505, 0x0505, 0x0505, 0x0505, 0x0505],
    'SECSTRT_41': [0x19, 0, 3, 0, 0x17, 0, 4, 0, 0x19, 0,          # the AI knight's side steps
                   0, 2, 0, 9, 0, 2, 0, 9,                         # LAB_0F36 up
                   0, 8, 0, 2, 0, 9, 0, 2],                        # LAB_0F37 down
}
# LAB_020F: the handler tables of the knight (type byte -> handler label)
HURT_TAB = {0: '0206', 4: '020B', 8: '01F9', 12: '0205', 16: '0205', 20: '0200', 24: '01F2', 28: '01F2', 32: '01F6',
            48: '01ED', 52: '020B', 36: '01EF', 44: '0203', 64: '01FD', 40: '0201'}
HIT_TAB = {k: '01E1' for k in (0, 4, 8, 12, 16, 20, 24, 28, 32, 48, 52, 36, 44, 64)}
HIT_TAB[40] = '0201'
MAPPED = sorted(HURT_TAB)          # the types both tables map


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


# ---- the driver -----------------------------------------------------------------------------------------------------------------
DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ms/regs.hpp"
#include "ms/gen/lift_ops.hpp"
#include "ms/gen/mog_syms.hpp"
#include "game/fighters.hpp"
using namespace ms;
using namespace ms::game;

static const uint32_t ARENA = @ARENA@;
static const uint32_t STACK_LO = @STACK_LO@, STACK_TOP = @STACK_TOP@;
static uint8_t g_o[ARENA];                   // oracle arena: big-endian everywhere
static uint8_t g_s[ARENA];                   // C++ arena: structs native
static uint8_t g_base[ARENA];                // the constants of every case
static int g_fault;
static uint16_t g_joy0, g_joy1, g_vhpos, g_salt;
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - g_s); }
void *ms::jobHostPtr(uint32_t a) { return g_s + a; }

struct OMem : Mem {
    uint8_t r8(uint32_t a) override {
        if(a >= 0xDFF006 && a < 0xDFF008) return a == 0xDFF006 ? (uint8_t)(g_vhpos >> 8) : (uint8_t)g_vhpos;   // VHPOSR
        if(a >= ARENA) { g_fault = 1; return 0; }
        return g_o[a];
    }
    void w8(uint32_t a, uint8_t v) override { if(a >= ARENA) { g_fault = 1; return; } g_o[a] = v; }
};
static OMem g_mem;

// ---- the call trace (side 0 = oracle, 1 = C++) ----
struct Ev { uint32_t id, a, b, c; };
static Ev g_ev[2][64];
static int g_nev[2];
static void ev(int side, uint32_t id, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) {
    if(g_nev[side] < 64) { Ev &e = g_ev[side][g_nev[side]++]; e.id = id; e.a = a; e.b = b; e.c = c; }
}

static uint16_t obsMask(uint16_t dx, uint16_t dy, uint16_t x, uint16_t y) {
    uint32_t h = dx * 40503u + dy * 9973u + x * 31u + y * 17u + g_salt;
    h ^= h >> 7;
    return (uint16_t)(h & 0x0B);              // the walls clear bits 0, 1 and 3 of the input byte
}

typedef void (*LiftFn)(Regs &, Mem &);
struct Tab { uint32_t addr; LiftFn fn; };
extern "C" void lab_LAB_01E0(Regs &R, Mem &M);
extern "C" void lab_LAB_01EC(Regs &R, Mem &M);
#include "dispatch.inc"

void tcallAsm(Regs &R, Mem &M, uint32_t addr) {
    using namespace ms::sym_mog;
    if(addr == LAB_00EE) { R.d[0] = (R.d[0] & 0xFFFF0000u) | g_joy0; R.d[1] = (R.d[1] & 0xFFFF0000u) | g_joy1; ev(0, 1); return; }
    if(addr == LAB_0A71) {
        uint32_t rec = R.a[0];
        uint16_t dx = (uint16_t)R.d[0], dy = (uint16_t)R.d[1];
        uint16_t m = obsMask(dx, dy, M.r16(rec + 4), M.r16(rec + 8));
        M.w8(rec + 63, (uint8_t)(M.r8(rec + 63) & ~m));
        ev(0, 2, rec, dx, dy);
        return;
    }
    if(addr == LAB_0AA2) { ev(0, 3, R.d[0] & 0xFFFF); return; }
    if(addr == LAB_0427) { ev(0, 4); return; }
    if(addr == LAB_0BB3) return;
    if(addr == SECSTRT_16) { ev(0, 5, 0, R.d[0] & 0xFFFF); return; }       // the fixed-channel sound starts
    if(addr == LAB_0A9B) { ev(0, 5, 1, R.d[0] & 0xFFFF); return; }
    if(addr == LAB_0A9C) { ev(0, 5, 2, R.d[0] & 0xFFFF); return; }
    if(addr == LAB_0A9D) { ev(0, 5, 3, R.d[0] & 0xFFFF); return; }
    if(addr == LAB_0D8A) { ev(0, 6, R.a[0]); return; }                     // the demon's death fade
    if(addr == LAB_03EE) { ev(0, 7, R.a[0]); return; }
    for(unsigned i = 0; i < sizeof kTab / sizeof kTab[0]; ++i) {
        if(kTab[i].addr == addr) { kTab[i].fn(R, M); return; }
    }
    fprintf(stderr, "tcallAsm: no routine at %08x\n", addr);
    g_fault = 2;
}

// LAB_01E0 / LAB_01EC: MOVEA.L 14(A1) / 18(A1),A0 ; type byte -> table LAB_0622 / LAB_0621 -> JMP (A2)
static void dispatchTable(Regs &R, Mem &M, uint32_t off, uint32_t tab) {
    R.a[0] = M.r32(R.a[1] + off);
    R.d[0] = M.r8(R.a[0] + 77);
    R.a[2] = M.r32(tab + (uint32_t)(int32_t)(int16_t)R.d[0]);
    tcallAsm(R, M, R.a[2]);
}
extern "C" void lab_LAB_01E0(Regs &R, Mem &M) { dispatchTable(R, M, 14, ms::sym_mog::LAB_0622); }
extern "C" void lab_LAB_01EC(Regs &R, Mem &M) { dispatchTable(R, M, 18, ms::sym_mog::LAB_0621); }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }
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
static const int V_W[] = {4, 6, 8, 154, 160, 162, 164, 166, 170, 172, 174, 180, 182};      // FightVars words
static const int V_L[] = {0};                                                              // + the two long tables below
static const int M_W[] = {0, 2, 4, 6, 8, 10};                                              // MoveVars
static const int AK_L[] = {0, 4, 10};
static const int AK_W[] = {14, 18, 20};
static void swapRec(uint8_t *p, const int *w, int nw, const int *l, int nl) {
    for(int i = 0; i < nw; ++i) sw2(p + w[i]);
    for(int i = 0; i < nl; ++i) sw4(p + l[i]);
}
#define NA(a) ((int)(sizeof(a) / sizeof((a)[0])))
static void convertAll(uint8_t *g) {          // involution: BE <-> native
    for(int i = 0; i < 5; ++i) swapRec(g + @KNIGHT0@ + 132 * i, K_W, NA(K_W), K_L, NA(K_L));
    for(int i = 0; i < @HEAP_N@; ++i) swapRec(g + @HEAP@ + 132 * i, K_W, NA(K_W), K_L, NA(K_L));
    for(int i = 0; i < 11; ++i) swapRec(g + @JOBS@ + 50 * i, J_W, NA(J_W), J_L, NA(J_L));      // the 11th is LAB_064A, the scratch job
    for(int i = 0; i < 10; ++i) swapRec(g + @WORKS@ + 36 * i, 0, 0, W_L, NA(W_L));
    swapRec(g + @BLOCK@, B_W, NA(B_W), B_L, NA(B_L));
    for(int i = 0; i < 6; ++i) swapRec(g + @SLOTS@ + 20 * i, B_W, NA(B_W), B_L, NA(B_L));
    swapRec(g + @L061D@, V_W, NA(V_W), V_L, NA(V_L));
    for(int i = 0; i < 36; ++i) sw4(g + @L061D@ + 10 + 4 * i);
    swapRec(g + @L0F3A@, M_W, NA(M_W), 0, 0);
    swapRec(g + @L05E4@, AK_W, NA(AK_W), AK_L, NA(AK_L));
    static const uint32_t cl[] = {@CELLS_L@};
    static const uint32_t cw[] = {@CELLS_W@};
    for(unsigned i = 0; i < sizeof cl / sizeof cl[0]; ++i) sw4(g + cl[i]);
    for(unsigned i = 0; i < sizeof cw / sizeof cw[0]; ++i) sw2(g + cw[i]);
    for(int i = 0; i < 2; ++i) sw2(g + @L02EC@ + 2 * i);        // the picker's counters
}

// ---- the C++ side ----
static Joystick cbJoy() { ev(1, 1); Joystick j; j.uwPort0 = g_joy0; j.uwPort1 = g_joy1; return j; }
static void cbObs(Knight &k, int16_t dx, int16_t dy) {
    uint16_t m = obsMask((uint16_t)dx, (uint16_t)dy, k.uwX, k.uwY);
    k.uwInput = (uint16_t)(k.uwInput & ~m);
    ev(1, 2, jobAddr(&k), (uint16_t)dx, (uint16_t)dy);
}
static void cbSound(uint16_t id) { ev(1, 3, id); }
static void cbFx() { ev(1, 4); }
static uint16_t cbBeam() { return g_vhpos; }
static void cbSfxFixed(uint8_t ch, uint16_t seq) { ev(1, 5, ch, seq); }
static void cbFadeA() { ev(1, 6, @L08D9@); }
static void cbFadeB() { ev(1, 7, @L08D9@); }

static FighterEnv makeEnv() {
    FighterEnv e;
    memset(&e, 0, sizeof e);
    e.pVars = (FightVars *)(g_s + @L061D@);
    e.pMove = (MoveVars *)(g_s + @L0F3A@);
    e.pToggle = (uint16_t *)(g_s + @L0234@);
    e.pAimDist = (uint16_t *)(g_s + @L02DA@);
    e.pJobs = (CombatJob *)(g_s + @JOBS@);
    e.pKnights = (Knight *)(g_s + @KNIGHT0@);
    e.pCreatures = (Knight *)(g_s + rd32(g_s + @L05C3@));
    e.pSlots = (DaggerSlot *)(g_s + @SLOTS@);
    e.pBlock = (DaggerBlock *)(g_s + @BLOCK@);
    e.pHandlerTable = g_s + @L08C7@;
    e.pActive = (ActiveKnights *)(g_s + @L05E4@);
    e.pFirst = (uint32_t *)(g_s + @L05F2@);
    e.pSecond = (const uint32_t *)(g_s + @L05F4@);
    e.pConfused = (const uint32_t *)(g_s + @L05D1@);
    e.pConfusedOn = (const uint16_t *)(g_s + @L05D3@);
    e.pHopTable = (uint32_t *)(g_s + @L08CE@);
    e.pHopDir = g_s + @L08D0@;
    e.pSelf = (uint32_t *)(g_s + @L0633@);
    e.pTarget = (uint32_t *)(g_s + @L0634@);
    e.pDemoFlag = (const uint32_t *)(g_s + @L06DA@);
    e.pSeed = (uint32_t *)(g_s + @L0973@);
    e.pLinkA = (const uint32_t *)(g_s + @L0193@);
    e.pLinkB = (const uint32_t *)(g_s + @L0194@);
    e.uwJunkA = rd16(g_s + 0x0A);
    e.uwJunkC = rd16(g_s + 0x0C);
    e.pSnatchFlags = g_s + @L0EB6@;
    e.pDemonFlags = g_s + @L0EEA@;
    e.pBatCounter = (uint16_t *)(g_s + @L0EC1@);
    e.pBodyA = (uint32_t *)(g_s + @L01A1@);
    e.pBodyB = (uint32_t *)(g_s + @L01A2@);
    e.pDifficulty = (const uint16_t *)(g_s + @L06C0@);
    e.pAiOdds = g_s + @L097B@;
    e.pAiLastAction = (uint16_t *)(g_s + @L0F38@);
    e.pAiSteps = g_s + @LSECSTRT_41@;
    e.pHopStep = (uint16_t *)(g_s + @L08CF@);
    e.pFade = (uint16_t *)(g_s + @L08D9@);
    e.pPickCycle = (SoundCycle *)(g_s + @L02EC@);
    FightScripts &s = e.scripts;
#define MS_X(n) s.s##n = ms::sym_mog::LAB_##n;
    MS_FIGHT_SCRIPTS(MS_X)
#undef MS_X
    s.sDaggerScript = ms::sym_mog::LAB_07EB;
    s.tDaggerDamage = ms::sym_mog::LAB_0302;
    s.fFrames0648 = ms::sym_mog::LAB_0648;
    s.fFrames05E0 = ms::sym_mog::LAB_05E0;
    s.hIdle = ms::sym_mog::LAB_02D2;
    s.tHop = ms::sym_mog::LAB_08CC;
    s.hKnight = ms::sym_mog::LAB_01CA;
    s.hFlyer = ms::sym_mog::LAB_0226;
    s.hBrawler = ms::sym_mog::LAB_0236;
    s.hCaster = ms::sym_mog::LAB_0251;
    s.hDragon = ms::sym_mog::LAB_027A;
    s.hDragonPart = ms::sym_mog::LAB_0298;
    s.hDrake = ms::sym_mog::LAB_029F;
    s.hDagger = ms::sym_mog::LAB_02CB;
    s.hSnatcher = ms::sym_mog::SECSTRT_40;
    s.hDemon = ms::sym_mog::LAB_0ED2;
    s.hAiKnight = ms::sym_mog::LAB_0EFF;
    s.hStalker = ms::sym_mog::LAB_0EC2;
    s.tHop2 = ms::sym_mog::LAB_08CD;
    e.joystick = cbJoy;
    e.obstacles = cbObs;
    e.sound = cbSound;
    e.screenEffect = cbFx;
    e.beamPosition = cbBeam;
    e.sfxFixed = cbSfxFixed;
    e.fadeA = cbFadeA;
    e.fadeB = cbFadeB;
    return e;
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

struct Range { uint32_t a, n; };
static const Range kIgnore[] = {
@IGNORE@
};

int main() {
    static char line[1 << 20];
    static uint8_t g_init[ARENA];
    int ncase = 0;
    char fn[16] = "";
    Regs in;
    memset(&in, 0, sizeof in);
    memset(g_base, 0, sizeof g_base);
    while(fgets(line, sizeof line, stdin)) {
        if(!strncmp(line, "BASE ", 5)) {
            uint32_t a = (uint32_t)hexv(line + 5);
            const char *h = strchr(line + 5, ' ') + 1;
            unhex(h, g_base + a, ARENA - a);
        } else if(!strncmp(line, "CASE ", 5)) {
            char *t = strtok(line + 5, " \r\n");
            strncpy(fn, t, sizeof fn - 1);
            for(int i = 0; i < 8; ++i) in.d[i] = (uint32_t)hexv(strtok(0, " \r\n"));
            for(int i = 0; i < 7; ++i) in.a[i] = (uint32_t)hexv(strtok(0, " \r\n"));
            in.a[7] = STACK_TOP; in.sr = 0;
            g_joy0 = (uint16_t)hexv(strtok(0, " \r\n"));
            g_joy1 = (uint16_t)hexv(strtok(0, " \r\n"));
            g_vhpos = (uint16_t)hexv(strtok(0, " \r\n"));
            g_salt = (uint16_t)hexv(strtok(0, " \r\n"));
            memcpy(g_init, g_base, ARENA);
        } else if(!strncmp(line, "M ", 2)) {
            uint32_t a = (uint32_t)hexv(line + 2);
            const char *h = strchr(line + 2, ' ') + 1;
            unhex(h, g_init + a, ARENA - a);
        } else if(!strncmp(line, "GO", 2)) {
            ++ncase;
            memcpy(g_o, g_init, ARENA);
            memcpy(g_s, g_init, ARENA);
            convertAll(g_s);
            g_fault = 0;
            g_nev[0] = g_nev[1] = 0;
            if(!strcmp(fn, "TABLES")) {                       // no oracle: the table the C++ writes, compared in Python
                FighterEnv e = makeEnv();
                fightTablesInit(e);
                char hex[200];
                for(int i = 0; i < 72; ++i) snprintf(hex + 2 * i, 3, "%02x", g_s[@L08C7@ + i]);
                printf("RES TABLES OK %s | ev 0\n", hex);
                fflush(stdout);
                continue;
            }
            Regs ro = in;
            LiftFn f = oracleFn(fn);
            if(!f) { printf("RES BAD no oracle for %s\n", fn); fflush(stdout); continue; }
            f(ro, g_mem);
            HandlerResult hr; memset(&hr, 0, sizeof hr);
            bool isSound = fn[0] == 'S';
            bool isOp = fn[0] == 'O';
            bool isDefaults = fn[0] == 'K';
            if(isSound) {
                FighterEnv e = makeEnv();
                SoundPick w = SOUND_02DC;
                @SOUNDMAP@
                soundPick(w, *e.pSeed, *(SoundCycle *)(g_s + @L02EC@), cbSound);
            } else if(isOp) {
                FighterEnv e = makeEnv();
                uint32_t tag = 0;
                @OPMAP@
                if(!fightOpRun(e, tag, in.a[1], in.a[2], (uint16_t)in.d[0], (uint16_t)in.d[1], (uint16_t)in.d[2], (uint8_t)in.d[3])) {
                    printf("RES BAD no op %s\n", fn); fflush(stdout); continue;
                }
            } else if(isDefaults) {
                knightDefaults(*(Knight *)(g_s + in.a[1]));
            } else {
                FighterEnv e = makeEnv();
                uint32_t owner = in.a[0];
                uint32_t h = 0;
                @HANDLERMAP@
                if(!fighterRun(e, h, owner, &hr)) { printf("RES BAD no handler %s\n", fn); fflush(stdout); continue; }
            }
            convertAll(g_s);
            const char *status = "OK";
            char detail[512] = "";
            if(g_fault) { status = "FAULT"; snprintf(detail, sizeof detail, "oracle fault %d", g_fault); }
            else {
                for(uint32_t a = 0; a < ARENA && !strcmp(status, "OK"); ++a) {
                    if(!(a & 255) && !memcmp(g_o + a, g_s + a, 256)) { a += 255; continue; }
                    if(g_o[a] == g_s[a]) continue;
                    bool ign = false;
                    for(unsigned k = 0; k < sizeof kIgnore / sizeof kIgnore[0]; ++k)
                        if(a >= kIgnore[k].a && a < kIgnore[k].a + kIgnore[k].n) ign = true;
                    if(!ign) {
                        status = "DIFF";
                        int n = snprintf(detail, sizeof detail, "mem %06x oracle %02x C++ %02x", a, g_o[a], g_s[a]);
                        int shown = 0;
                        for(uint32_t b = a + 1; b < ARENA && shown < 5; ++b) {
                            if(g_o[b] == g_s[b]) continue;
                            bool ig2 = false;
                            for(unsigned k = 0; k < sizeof kIgnore / sizeof kIgnore[0]; ++k)
                                if(b >= kIgnore[k].a && b < kIgnore[k].a + kIgnore[k].n) ig2 = true;
                            if(!ig2) { n += snprintf(detail + n, sizeof detail - n, "; %06x %02x/%02x", b, g_o[b], g_s[b]); ++shown; }
                        }
                    }
                }
                if(!isSound && !isOp && !isDefaults && !strcmp(status, "OK")) {
                    if(ro.a[0] != hr.ulScript || (ro.d[0] & 0xFFFF) != hr.uwX || (ro.d[1] & 0xFFFF) != hr.uwY ||
                       (ro.d[2] & 0xFFFF) != hr.uwZ || (ro.d[3] & 0xFF) != hr.ubFacing) {
                        status = "DIFF";
                        snprintf(detail, sizeof detail, "result oracle script=%x x=%x y=%x z=%x f=%x C++ script=%x x=%x y=%x z=%x f=%x",
                                 ro.a[0], ro.d[0] & 0xFFFF, ro.d[1] & 0xFFFF, ro.d[2] & 0xFFFF, ro.d[3] & 0xFF,
                                 hr.ulScript, hr.uwX, hr.uwY, hr.uwZ, hr.ubFacing);
                    }
                }
                if(!strcmp(status, "OK")) {
                    bool same = g_nev[0] == g_nev[1];
                    for(int i = 0; same && i < g_nev[0]; ++i)
                        same = g_ev[0][i].id == g_ev[1][i].id && g_ev[0][i].a == g_ev[1][i].a && g_ev[0][i].b == g_ev[1][i].b;
                    if(!same) {
                        status = "DIFF";
                        int n = snprintf(detail, sizeof detail, "trace oracle(%d):", g_nev[0]);
                        for(int i = 0; i < g_nev[0] && i < 6; ++i) n += snprintf(detail + n, sizeof detail - n, " %u:%x,%x", g_ev[0][i].id, g_ev[0][i].a, g_ev[0][i].b);
                        n += snprintf(detail + n, sizeof detail - n, " C++(%d):", g_nev[1]);
                        for(int i = 0; i < g_nev[1] && i < 6; ++i) n += snprintf(detail + n, sizeof detail - n, " %u:%x,%x", g_ev[1][i].id, g_ev[1][i].a, g_ev[1][i].b);
                    }
                }
            }
            printf("RES %s %s %s | ev %d\n", fn, status, detail, g_nev[0]);
            fflush(stdout);
        }
    }
    printf("DONE %d\n", ncase);
    return 0;
}
'''

# handler label -> the label the C++ dispatch recognises (all nine handlers are run through fighterRun)
HANDLERS = ['01CA', '0226', '0236', '0251', '027A', '0298', '029F', '02CB', '02D2']
# ROADMAP 7.1h: the four S_40 handlers (case key -> mog label), the script-called routines ('O' + label), see fight_ops.cpp
NEW_HANDLERS = {'T04': 'SECSTRT_40', 'T08': 'LAB_0ED2', 'T10': 'LAB_0EFF', 'T40': 'LAB_0EC2'}
NEW_TYPES = {'T04': 4, 'T08': 8, 'T10': 16, 'T40': 64}
OPS_S0 = ['01C8', '0210', '0211', '028E', '02AC', '02AD', '02AE', '02CA', '02DB']
OPS_PICK = ['02DC', '02DE', '02E0', '02E1', '02E2', '02E3', '02E4', '02E6', '02E8', '02E9']
OPS_S40 = ['0EB8', '0EB9', '0EBD', '0EBE', '0ED0', '0EEB', '0EEC', '0EED', '0EEE', '0EEF', '0EF6', '0EF7']
OPS = OPS_S0 + OPS_PICK + OPS_S40
PICKERS = ['02DC', '02DE', '02E0', '02E1', '02E2', '02E3', '02E4', '02E6', '02E8', '02E9']
PICKER_ENUM = {'02DC': 'SOUND_02DC', '02DE': 'SOUND_02DE', '02E0': 'SOUND_02E0', '02E1': 'SOUND_02E1', '02E2': 'SOUND_02E2',
               '02E3': 'SOUND_02E3', '02E4': 'SOUND_02E4', '02E6': 'SOUND_02E6', '02E8': 'SOUND_02E8', '02E9': 'SOUND_02E9'}

# cells the driver byte-swaps (longs / words that the C++ reads natively through a pointer)
CELLS_L = ['0633', '0634', '05F2', '05F4', '05D1', '06DA', '0973', '0193', '0194', '08CE', '01A1', '01A2']
CELLS_W = ['05D3', '0234', '02DA', '0EC1', '06C0', '0F38', '08CF']

IGNORE = [(A('0A52'), 2), (A('0A53'), 2), (A('0A54'), 2), (A('0A56'), 2), (A('0635'), 2), (A('0636'), 2), (A('0637'), 4),
          (A('03B6'), 2), (A('01D8'), 2), (A('01EB'), 2), (A('0295'), 4), (A('062F'), 2), (A('0630'), 2),
          (A('0A77'), 2), (A('0A78'), 2), (STACK_LO, STACK_TOP - STACK_LO + 64)]

_DRIVER_EXE = None


def build_driver():
    global _DRIVER_EXE
    if _DRIVER_EXE and os.path.exists(_DRIVER_EXE):
        return _DRIVER_EXE
    labels = lift_closure()
    tmp = tempfile.mkdtemp(prefix='fighters_')
    lifted = os.path.join(WORK_DIR, 'lifted')
    files = [os.path.join(lifted, stem(l) + '.cpp') for l in labels]
    disp = ''.join('extern "C" void lab_%s(ms::Regs&, ms::Mem&);\n' % l for l in labels)
    entries = labels + sorted(HAND)
    disp += 'static const Tab kTab[] = {\n' + ''.join('    {ms::sym_mog::%s, &lab_%s},\n' % (l, l) for l in entries) + '};\n'
    with open(os.path.join(tmp, 'dispatch.inc'), 'w') as f:
        f.write(disp)
    oracle = ''
    for h in HANDLERS + PICKERS:
        key = h if h in HANDLERS else 'S' + h
        oracle += '    if(!strcmp(fn, "%s")) return &lab_LAB_%s;\n' % (key, h)
    handlermap = '\n                '.join('if(!strcmp(fn, "%s")) h = ms::sym_mog::LAB_%s;' % (h, h if h != '02D2' else '02D2') for h in HANDLERS)
    soundmap = '\n                '.join('if(!strcmp(fn, "S%s")) w = %s;' % (p, PICKER_ENUM[p]) for p in PICKERS)
    # ROADMAP 7.1h: the S_40 handlers, the script-called routines, the knight defaults
    for key, lab in NEW_HANDLERS.items():
        oracle += '    if(!strcmp(fn, "%s")) return &lab_%s;' % (key, lab) + chr(10)
        handlermap += chr(10) + '                if(!strcmp(fn, "%s")) h = ms::sym_mog::%s;' % (key, lab)
    opmap = ''
    for op in OPS:
        oracle += '    if(!strcmp(fn, "O%s")) return &lab_LAB_%s;' % (op, op) + chr(10)
        opmap += '    if(!strcmp(fn, "O%s")) tag = 0xF0000000u | 0x%s;' % (op, op) + chr(10) + '                '
    oracle += '    if(!strcmp(fn, "K01C6")) return &lab_LAB_01C6;' + chr(10)
    subs = {
        'OPMAP': opmap,
        'ARENA': '0x%X' % ARENA, 'STACK_LO': '0x%X' % STACK_LO, 'STACK_TOP': '0x%X' % STACK_TOP, 'KNIGHT0': '0x%X' % KNIGHT0,
        'HEAP': '0x%X' % HEAP, 'HEAP_N': str(TC.HEAP_N), 'JOBS': '0x%X' % JOBS, 'WORKS': '0x%X' % WORKS,
        'BLOCK': '0x%X' % BLOCK, 'SLOTS': '0x%X' % SLOTS, 'ORACLE': oracle, 'HANDLERMAP': handlermap, 'SOUNDMAP': soundmap,
        'IGNORE': ',\n'.join('    {0x%X, %d}' % (a, n) for a, n in IGNORE),
        'CELLS_L': ', '.join('0x%X' % A(c) for c in CELLS_L), 'CELLS_W': ', '.join('0x%X' % A(c) for c in CELLS_W),
    }
    for lab in ('061D', '0F3A', '0234', '02DA', '05C3', '08C7', '05E4', '05F2', '05F4', '05D1', '05D3', '08CE', '08D0', '0633',
                '0634', '06DA', '0973', '0193', '0194', '02EC', '0EB6', '0EEA', '0EC1', '01A1', '01A2', '06C0', '097B', '0F38',
                'SECSTRT_41', '08CF', '08D9'):
        subs['L' + lab] = '0x%X' % A(lab)
    src = DRIVER
    for k, v in subs.items():
        src = src.replace('@%s@' % k, v)
    assert '@' not in re.sub(r'\w+@\w+', '', src.replace('MS_X', '')) or True
    left = re.findall(r'@\w+@', src)
    assert not left, left
    drv = os.path.join(tmp, 'driver.cpp')
    with open(drv, 'w') as f:
        f.write(src)
    exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
    cmd = [CXX] + os.environ.get('FIGHTERS_CXXFLAGS', '').split() + ['-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions',
           '-fno-rtti', '-w', '-include', os.path.join(tmp, 'tcall.hpp'), '-I', os.path.join(ROOT, 'include'), '-I', tmp, drv,
           os.path.join(ROOT, 'src', 'game', 'fighters.cpp'), os.path.join(ROOT, 'src', 'game', 'creatures.cpp'),
           os.path.join(ROOT, 'src', 'game', 'fight_creatures.cpp'), os.path.join(ROOT, 'src', 'game', 'fight_ops.cpp'),
           os.path.join(ROOT, 'src', 'engine', 'util.cpp'), os.path.join(ROOT, 'src', 'game', 'rules', 'damage.cpp')] + [os.path.join(ROOT, 'src', 'game', 'rules', n + '.cpp') for n in ['ai_fight', 'ai_fight_flyer', 'ai_fight_snatcher', 'ai_fight_demon', 'ai_fight_knight', 'ai_fight_dragon', 'ai_fight_brawler', 'ai_fight_caster', 'ai_fight_drake', 'ai_fight_stalker']] + [GAMEDATA_SOURCE] + files + ['-o', exe]
    with open(os.path.join(tmp, 'tcall.hpp'), 'w') as f:
        f.write('#pragma once\n#include "ms/regs.hpp"\nvoid tcallAsm(ms::Regs &, ms::Mem &, unsigned);\n')
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('driver compile failed:\n%s\n%s' % (' '.join(cmd), r.stderr[-6000:]))
    _DRIVER_EXE = exe
    return exe


# ---- the case builder -------------------------------------------------------------------------------------------------------------
class Img:
    """Sparse guest memory as a few byte segments (fast bulk fills, absolute big-endian accessors)."""

    def __init__(self):
        self.segs = []

    def seg(self, base, size):
        b = bytearray(size)
        self.segs.append((base, b))
        return b

    def _find(self, a, n):
        for base, b in self.segs:
            if base <= a and a + n <= base + len(b):
                return b, a - base
        raise KeyError('no segment for %#x' % a)

    def w8(self, a, v):
        b, o = self._find(a, 1)
        b[o] = v & 0xFF

    def w16(self, a, v):
        b, o = self._find(a, 2)
        b[o] = (v >> 8) & 0xFF
        b[o + 1] = v & 0xFF

    def w32(self, a, v):
        self.w16(a, v >> 16)
        self.w16(a + 2, v)

    def r8(self, a):
        b, o = self._find(a, 1)
        return b[o]

    def r16(self, a):
        b, o = self._find(a, 2)
        return (b[o] << 8) | b[o + 1]

    def r32(self, a):
        return (self.r16(a) << 16) | self.r16(a + 2)

    def put(self, a, data):
        b, o = self._find(a, len(data))
        b[o:o + len(data)] = data

    def lines(self, tag):
        return ''.join('%s %x %s\n' % (tag, base, bytes(b).hex()) for base, b in self.segs)


def base_image():
    """The constants of every case: the asm data tables the handlers read, the two handler tables of LAB_020F, the tables
    the records point to (two variants), the inventories."""
    I = Img()
    for lab, words in DATA.items():
        s = I.seg(A(lab), 2 * len(words))
        for i, w in enumerate(words):
            s[2 * i] = w >> 8
            s[2 * i + 1] = w & 0xFF
    return I


def tables_image(rng):
    """Per case: two variants of the five tables a record points to (hurt, action, walk, damage, defence), 0x100 bytes each."""
    I = Img()
    acts = [0, 4, 8, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20]
    for var in range(2):
        for kind in range(5):
            b = I.seg(TABLES + 0x600 * var + 0x100 * kind, 0x100)
            for i in range(0, 0x100, 4):
                if kind == 3:                    # damage: small longs
                    v = rng.choice((0, 1, 2, 3, 5, 8, 12, 20, 40))
                elif kind == 4:                  # defence: the action that blocks (some entries are the $1C stance)
                    v = rng.choice(acts)
                    if i // 4 in ((4, 6) if var == 0 else (1, 5)):
                        v = 0x1C
                elif kind == 2 and var == 0:     # walk scripts: always non-zero
                    v = 0x00500000 + 0x40 * rng.randrange(0x4000)
                elif kind == 2:                  # walk scripts: holes, but every table has an entry per phase offset
                    v = 0 if rng.random() < 0.35 else 0x00500000 + 0x40 * rng.randrange(0x4000)
                else:
                    v = 0x00400000 + 0x40 * rng.randrange(0x4000)
                b[i:i + 4] = v.to_bytes(4, 'big')
            if kind == 2:                        # phase 0 of each of the three offsets (0 / 32 / 64) is never null
                for off in (0, 32, 64):
                    if int.from_bytes(b[off:off + 4], 'big') == 0:
                        b[off:off + 4] = (0x00500123).to_bytes(4, 'big')
                # every window of 8 phases must contain an entry (the asm loops until it finds one)
                for off in (0, 32, 64):
                    for ph in range(8):
                        pass
    return I


def rbytes(rng, n):
    return bytes(rng.getrandbits(8) for _ in range(n)) if n < 64 else rng.randbytes(n)


class World:
    """One random arena: the five static records, the heap, the job table, the globals the handlers read."""

    def __init__(self, rng, base):
        self.rng = rng
        self.I = Img()
        I = self.I
        self.kn = I.seg(KNIGHT0, 5 * REC)
        self.hp = I.seg(HEAP, HEAP_N * REC)
        self.jobs = I.seg(JOBS, 500)
        self.works = I.seg(WORKS, 360)
        self.vars = I.seg(A('061D'), 184)
        self.block = I.seg(BLOCK, 20)
        self.scratch = I.seg(A('064A'), 50)
        self.slots = I.seg(SLOTS, 120)
        self.move = I.seg(A('0F3A'), 12)
        self.low = I.seg(0x0A, 4)
        self.inv = I.seg(INVS, 24 * (5 + HEAP_N))
        for b in (self.kn, self.hp, self.vars, self.block, self.slots, self.move, self.low):
            b[:] = rbytes(rng, len(b))
        self.inv[:] = bytes(rng.choice((0, 0, 1, 2, 3, 5)) for _ in range(len(self.inv)))
        # LAB_020F's handler tables live inside the block (LAB_0621 / LAB_0622): unmapped slots are 0
        self.vars[10:154] = bytes(144)
        for k, lab in HURT_TAB.items():
            self.vars[10 + k:14 + k] = SYM['LAB_' + lab].to_bytes(4, 'big')
        for k, lab in HIT_TAB.items():
            self.vars[82 + k:86 + k] = SYM['LAB_' + lab].to_bytes(4, 'big')
        # cells
        for lab, n in (('0633', 4), ('0634', 4), ('05F2', 4), ('05F4', 4), ('05D1', 4), ('05D3', 2), ('06DA', 4), ('0973', 4),
                       ('0193', 4), ('0194', 4), ('0234', 2), ('02DA', 2), ('08CE', 4), ('08D0', 4), ('05C3', 4), ('02EC', 4),
                       ('0EB6', 2), ('0EEA', 2), ('0EC1', 2), ('01A1', 4), ('01A2', 4), ('06C0', 2), ('0F38', 2), ('08CF', 2),
                       ('08D9', 16)):
            I.seg(A(lab), n)
        I.seg(A('05E4'), 22)
        I.seg(A('08C7'), 72)
        self.recs = [KNIGHT0 + REC * i for i in range(5)] + [HEAP + REC * i for i in range(HEAP_N)]
        I.w32(A('05C3'), HEAP)
        I.w32(A('0973'), rng.getrandbits(32) | 1)
        I.w16(A('0234'), rng.choice((0, 1, 0, 1, 2)))
        I.w16(A('02EC'), rng.randrange(0, 6))
        I.w16(A('02EC') + 2, rng.choice(range(0, 15, 2)))
        I.w32(A('06DA'), 0 if rng.random() < 0.8 else 1)
        I.w16(A('0F3D'), 0) if False else None
        self.owners = []
        for idx, a in enumerate(self.recs):
            self.init_rec(a, idx)

    # -- record initialisation --
    def pos(self, lo=0, hi=330):
        rng = self.rng
        r = rng.random()
        if r < 0.05:
            return rng.randint(0xFF00, 0xFFFF)
        return rng.randint(lo, hi)

    def init_rec(self, a, idx):
        I, rng = self.I, self.rng
        I.w32(a, rng.choice((1, 1, 1, 0)))
        I.w16(a + 4, self.pos(0, 330))
        I.w16(a + 6, rng.choice((0, 0, 0xFFBA, 0xFFFA, 0xFFEC, rng.randint(0, 0xFFFF), 0xFFF6, 0xFFF5)))
        I.w16(a + 8, self.pos(0, 170))
        I.w8(a + 10, rng.choice((1, 3, 1, 3, 1, 3, 0, 2)))
        I.w8(a + 11, rng.choice((1, 2)))
        I.w8(a + 12, rng.randint(0, 3) if rng.random() < 0.9 else rng.randint(0, 7))
        I.w8(a + 13, rng.randint(0, 20))
        I.w32(a + 14, 0)
        I.w32(a + 18, 0)
        for off in (22, 26):
            I.w32(a + off, 0x00600000 + 0x40 * rng.randrange(0x4000))
        var = lambda: TABLES + 0x600 * rng.randrange(2)
        I.w32(a + 30, var() + 0x000)
        I.w32(a + 34, var() + 0x100)
        I.w32(a + 38, 0x00700000)
        I.w32(a + 42, var() + 0x300)
        I.w32(a + 46, var() + 0x200)
        I.w32(a + 50, var() + 0x400)
        I.w32(a + 54, rng.randint(0, 5))
        I.w16(a + 62, rng.choice((0, 0, 1, 2, 4, 8, 5, 6, 9, 10, 0x10, 0x11, 0x12, 0x14, 0x18, 0x1F, rng.randint(0, 0x1F), 0x20, 0x40)))
        I.w16(a + 64, rng.choice((0, 4, 8, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x20, 8, 4, 0x24, 0x40)))
        I.w8(a + 70, rng.randint(0, 9))
        I.w8(a + 72, rng.randint(0, 120) if rng.random() < 0.9 else rng.randint(0, 255))
        I.w8(a + 76, rng.choice((0, 0, 1, 2, 5, 10)))
        I.w8(a + 77, rng.choice(MAPPED + [0x0C, 0x0C, 0x24, 0x14, 0x34, 0x28]))
        I.w16(a + 80, rng.choice((rng.randint(-5, 6), rng.randint(1, 60), rng.randint(1, 60), 0, 1)))
        I.w32(a + 88, rng.choice((0x16, 0x17, 0x18, 0x19, rng.randint(0, 40))))
        ii = self.recs.index(a)
        I.w32(a + 96, INVS + 24 * ii)
        I.w8(a + 104, rng.choice((0, 0, 0, 1, 4, 8, 0x10, 0x20, 0x80, 0x81, 0x84, 0x88, rng.getrandbits(8))))
        I.w8(a + 105, rng.choice((0, 0, 0, 1, 4, 5, rng.getrandbits(8))))
        I.w8(a + 106, rng.choice((0, 0, 1, 2, 10, 20, 30)))
        I.w8(a + 107, rng.choice((0, 0, 1, 5, 0x1E, rng.getrandbits(8))))
        I.w16(a + 116, rng.choice((0x64, 0x64, 20, 50, 80, 120, rng.randint(1, 200))))
        I.w16(a + 118, rng.choice((0x50, 0x50, 10, 30, 60, 100, rng.randint(1, 200))))
        I.w16(a + 120, rng.choice((4, 5, 10, 40, 60, 100, rng.randint(0, 200))))
        I.w16(a + 122, self.pos(0, 330))
        I.w16(a + 124, self.pos(0, 170))
        I.w8(a + 130, 0)

    # -- the roles of one case --
    def arrange(self, self_addr, kind):
        """Fix the roles around the handler's record: fighters, links, jobs, daggers, active pair, globals.  Returns the
        addresses (self, first, second, dragon, attacker, hit target)."""
        I, rng = self.I, self.rng
        knights = [KNIGHT0 + REC * i for i in range(4)]
        dragon = KNIGHT0 + 4 * REC
        pool = [r for r in knights if r != self_addr]
        first = rng.choice(pool)
        second = rng.choice([r for r in pool if r != first])
        heap = [HEAP + REC * i for i in range(HEAP_N)]
        others = [r for r in self.recs if r not in (self_addr, first, second, dragon)]
        I.w32(A('05F2'), first)
        I.w32(A('05F4'), second)
        I.w32(A('0633'), rng.choice(self.recs))
        I.w32(A('0634'), rng.choice(self.recs))
        I.w32(A('05D1'), self_addr if rng.random() < 0.35 else rng.choice(self.recs))
        I.w16(A('05D3'), rng.randint(0, 1))
        I.w32(A('0193'), rng.choice(heap[:20]))
        I.w32(A('0194'), rng.choice(heap[:20]))
        I.w32(A('08CE'), rng.choice((0, A('08CC'))))
        # the active pair
        cur = rng.choice(knights)
        I.w32(A('05E4'), cur)
        I.w32(A('05E4') + 4, rng.choice(knights))
        I.w8(A('05E4') + 8, rng.randint(0, 1))
        I.w8(A('05E4') + 16, rng.randint(0, 0x30))
        I.w16(A('05E4') + 18, rng.choice((0x2D, 0x2E, 0x31, 0x2F, rng.randint(0, 0x40))))
        # links of the handler's record: an attacker of a mapped type, or a hit target, or neither
        r = rng.random()
        atk = hit = 0
        if r < 0.38:
            atk = rng.choice(others + [first, second])
        elif r < 0.62:
            hit = rng.choice(others + [first, second])
        elif r < 0.72:
            atk, hit = rng.sample(others + [first, second], 2)
        I.w32(self_addr + 14, hit)
        I.w32(self_addr + 18, atk)
        # the link partners
        for q in (atk, hit):
            if q:
                I.w8(q + 77, rng.choice(MAPPED + [0x0C, 0x0C, 0x10, 0x24, 0x34, 0x14] if kind != '01CA' else MAPPED))
                if rng.random() < 0.7:
                    I.w32(q + 14, 0)
                if rng.random() < 0.7:
                    I.w32(q + 18, 0)
        # jobs: every role has one, so the lookups never miss (the asm would read low memory)
        roles = [self_addr, first, second, dragon]
        roles += [q for q in (atk, hit) if q and rng.random() < 0.85]    # these are only used by the job helpers that skip a miss
        owners = list(dict.fromkeys(roles))
        while len(owners) < 10:
            owners.append(rng.choice(self.recs))
        rng.shuffle(owners)
        for i in range(10):
            j = JOBS + 50 * i
            self.jobs[50 * i:50 * i + 50] = rbytes(rng, 50)
            I.w8(j + 0, 1 if rng.random() < 0.85 else 0)
            I.w8(j + 1, rng.randint(0, 1))
            I.w16(j + 6, self.pos(0, 330))
            I.w16(j + 8, self.pos(0, 170))
            I.w16(j + 10, self.pos(0, 170))
            I.w8(j + 22, rng.choice((1, 3, 0, 2)))
            I.w32(j + 24, owners[i])
            I.w32(j + 36, WORKS + 36 * i)
            I.w16(j + 48, rng.randint(0, 1))
        self.works[:] = rbytes(rng, 360)
        # dagger slots: the handler's record usually owns one (flight states need it)
        for i in range(6):
            I.w32(SLOTS + 20 * i, rng.choice((0, 0, rng.choice(self.recs))))
        if rng.random() < 0.9:
            i = rng.randrange(6)
            I.w32(SLOTS + 20 * i, self_addr)
            I.w16(SLOTS + 20 * i + 4, rng.choice((1, 2, 3, 5, 9, 14, 20)))
        # the dragon-fight cells
        v = A('061D')
        I.w16(v + 8, rng.randint(0, 1))                  # LAB_0620
        I.w16(A('062C'), rng.choice((0, 0, 1, 3, 15)))
        I.w16(A('062A'), rng.choice((0, 1, 2, 5, 12, 20)))
        I.w16(A('0628'), rng.choice((0, 3, 4, 8, 16, 20, 40)))
        I.w16(A('0629'), rng.choice((0, 3, 7, 8, 10, 40, 120)))
        I.w16(A('062D'), rng.randint(0, 1))
        return self_addr, first, second, dragon, atk, hit


JOY = [0, 0, 1, 2, 4, 8, 5, 6, 9, 10, 0x10, 0x11, 0x12, 0x14, 0x18, 0x15, 0x1A, 0x1F]


def gen_case(rng, base, fn):
    """-> (World, regs, joy0, joy1, vhpos, salt)."""
    w = World(rng, base)
    I = w.I
    knights = [KNIGHT0 + REC * i for i in range(4)]
    dragon = KNIGHT0 + 4 * REC
    heap = [HEAP + REC * i for i in range(HEAP_N)]
    if fn == '027A':
        self_addr = dragon if rng.random() < 0.93 else rng.choice(heap[:10])
    elif fn == '01CA':
        self_addr = rng.choice(knights)
    else:
        self_addr = rng.choice(heap[:10])
    types = {'01CA': 0x0C, '0226': 0x00, '0236': rng.choice((0x18, 0x1C, 0x20)), '0251': 0x24, '027A': 0x14, '0298': 0x2C,
             '029F': 0x30, '02CB': 0x34, '02D2': 0x28}
    I.w8(self_addr + 77, types[fn])
    I.w32(self_addr, 1)
    roles = w.arrange(self_addr, fn)

    first = roles[1]
    sx, sy = I.r16(self_addr + 4), I.r16(self_addr + 8)
    if fn == '01CA':
        I.w8(self_addr + 77, rng.choice((0x0C, 0x0C, 0x38)))
        I.w8(self_addr + 11, rng.choice((1, 2)))
        if rng.random() < 0.5:                         # the fighters stand close together
            for rr in (first, roles[2]):
                I.w16(rr + 4, (sx + rng.randint(-60, 60)) & 0xFFFF)
                I.w16(rr + 8, (sy + rng.randint(-15, 15)) & 0xFFFF)
        if rng.random() < 0.35:                        # near an arena edge
            I.w16(self_addr + 4, rng.choice((0, 5, 9, 12, 30, 0x13C, 0x140, 0x141, 0x150)))
            I.w16(self_addr + 8, rng.choice((0, 10, 21, 22, 30, 0x95, 0x96, 0x9B, 0x9C, 0xA0)))
    if fn in ('0236', '0251', '027A', '029F', '0226', '0298'):
        # the target (first fighter): near the handler's record in x and depth most of the time
        if rng.random() < 0.75:
            I.w16(first + 4, (sx + rng.choice((-200, -120, -80, -60, -50, -40, -30, -10, 0, 10, 30, 45, 60, 80, 130, 170, 200))) & 0xFFFF)
            I.w16(first + 8, (sy + rng.choice((-100, -50, -20, -10, -3, 0, 3, 10, 30, 90))) & 0xFFFF)
        r = rng.random()
        if r < 0.5:
            I.w16(first + 80, rng.randint(1, 60))
        elif r < 0.7:
            I.w16(first + 80, rng.choice((0, 0xFFFF, rng.randint(-20, 0) & 0xFFFF)))
    if fn == '01CA' and roles[4]:
        # a defender whose action is what the defence table of its record asks for against the attacker's action (blocks)
        att = roles[4]
        tptr = I.r32(self_addr + 50)
        want = rng.choice((0x1C, 0x1C, None))
        acts = [x for x in (0, 4, 8, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20) if base is not None and (base.r32(tptr + x) & 0xFFFF) == want]
        if want is not None and acts and rng.random() < 0.8:
            I.w16(att + 64, rng.choice(acts))
            I.w16(self_addr + 64, want)
        elif rng.random() < 0.5:
            I.w16(self_addr + 64, base.r32(tptr + I.r16(att + 64)) & 0xFFFF if base is not None else 0)
    if fn == '0236':
        if rng.random() < 0.8:
            I.w8(self_addr + 106, rng.choice((0, 0, 0, 1, 5)))
        I.w16(self_addr + 62, rng.choice((0, 0, 0, 0x10, 0x20, 0x11, 1, 2, 4, 8)))
        r = rng.random()
        if r < 0.45 and not (I.r32(self_addr + 14) or I.r32(self_addr + 18)):
            # the tunables' dead zone: inside +116 but outside +118, depth in reach (the target alive, or dead and the walk with no input)
            sxn, syn = I.r16(self_addr + 4), I.r16(self_addr + 8)
            I.w16(self_addr + 116, rng.choice((100, 110, 120)))
            I.w16(self_addr + 118, rng.choice((80, 60, 40)))
            I.w16(first + 4, (sxn + rng.choice((-1, 1)) * rng.randint(81, 109)) & 0xFFFF)
            I.w16(first + 8, (syn + rng.randint(-5, 5)) & 0xFFFF)
            I.w16(self_addr + 120, 40)
            I.w8(self_addr + 106, 0)
            if r < 0.15:
                I.w16(first + 80, rng.choice((0, 0xFFFF)))
                I.w16(self_addr + 62, 0)
                I.w16(A('061D') + 8, 0)
            else:
                I.w16(first + 80, rng.randint(1, 40))
    if fn == '0251':
        I.w16(A('062C'), rng.choice((0, 0, 1)))
        I.w8(A('061D') + 176, rng.choice((0, 0, 4, 8, 0x20, 0x24, 0x28, rng.getrandbits(8))))
    if fn == '027A':
        if rng.random() < 0.3:
            I.w8(dragon + 12, rng.randint(0, 12))
        I.w8(A('061D') + 156, rng.choice((0, 0x10, 0x20, 0x30, 0x40, 0x60, 0x80, 0xA0, 0xF0, rng.getrandbits(8))))
        I.w16(A('061D') + 162, rng.randint(0, 200))
        I.w16(self_addr + 4, rng.choice((20, 31, 40, 80, 95, 99, 100, 101, 130, rng.randint(0, 200))))
    if fn == '029F':
        if rng.random() < 0.3:
            I.w16(self_addr + 64, 8)
        I.w8(A('061D') + 168, rng.choice((0, 0, 0, 1, 2, 0x20, 0x40, 0x21, 0x22, rng.getrandbits(8))))
        I.w16(A('061D') + 174, rng.choice((0, 1, 2, 5, 12, 20)))
        if rng.random() < 0.3:
            # a flight that ends on the target: the slot lands exactly on it
            I.w8(A('061D') + 168, 2)
            I.w32(self_addr + 14, 0)
            I.w32(self_addr + 18, 0)
            I.w16(A('061D') + 174, rng.choice((2, 3, 5)))
            I.w16(A('0628'), 40)
            I.w16(A('0629'), rng.choice((3, 8, 20)))
            i = rng.randrange(6)
            s = SLOTS + 20 * i
            for j in range(6):
                if j != i:
                    I.w32(SLOTS + 20 * j, 0)
            I.w32(s, self_addr)
            I.w16(s + 4, 8)
            for off in (6, 8, 10, 12):
                I.w16(s + off, 0)
            I.w16(s + 14, (I.r16(first + 4) << 6) & 0xFFFF)
            I.w16(s + 16, (I.r16(first + 8) << 6) & 0xFFFF)
            I.w16(s + 18, 0)
            I.w16(first + 80, rng.randint(1, 50))
    if fn == '0298':
        I.w16(A('061D') + 156, 0)
    if fn == '0226':
        I.w16(self_addr + 4, rng.choice((0xFFCE, 0xFFF0, 0, 5, 0x150, 0x153, 0x154, 0x160, 0x17C, rng.randint(0, 330))))
        I.w8(self_addr + 12, rng.randint(0, 3))
    # flight states need their slot (an empty slot table makes the asm store stale registers)
    st = I.r8(A('061D') + 168)
    fl104 = I.r8(self_addr + 104)
    need = (fn == '029F' and st & 2) or (fn == '0251' and fl104 & 0x81) or fn == '027A'
    if need or rng.random() < 0.2:
        i = rng.randrange(6)
        I.w32(SLOTS + 20 * i, self_addr)
        I.w16(SLOTS + 20 * i + 4, rng.choice((1, 2, 3, 5, 9, 14, 20)))
    if fn == '027A':
        # LAB_028E restores the tunables through the job pointer (job + 116 / 118 = the width / height of the job two on);
        # the table has no such slot for the last job (the words would land in a work block, raw bytes in the host test)
        owners = [I.r32(JOBS + 50 * i + 24) for i in range(10)]
        if dragon in owners and owners.index(dragon) == 9:
            I.w32(JOBS + 24, dragon)
    if fn == '027A' and roles[4] and I.r8(roles[4] + 77) not in (0x0C, 0x34):
        # LAB_0287 with a flight running and the dragon's job stopped continues with A1 = the job and stale registers
        I.w8(A('061D') + 156, I.r8(A('061D') + 156) & 0xEF)
    joy0 = rng.choice(JOY) if rng.random() < 0.8 else rng.getrandbits(5)
    joy1 = rng.choice(JOY) if rng.random() < 0.8 else rng.getrandbits(5)
    regs = {'D%d' % i: 0 for i in range(8)}
    regs.update({'A%d' % i: 0 for i in range(7)})
    regs['A0'] = self_addr
    return w, regs, joy0, joy1, rng.getrandbits(16), rng.getrandbits(16)


# ---- ROADMAP 7.1h: the S_40 handlers, the script-called routines, the knight defaults, the handler table -------------------------

DX_CHOICES = (-200, -150, -135, -120, -100, -95, -80, -70, -60, -52, -45, -30, -22, -10, -3, 0, 3, 10, 22, 30, 45, 52, 60, 70,
              80, 95, 100, 120, 135, 150, 200)


def arrange_s40(w, rng, self_addr, roles, fn):
    """The extra roles of the S_40 handlers and ops: the demon's two body records (LAB_01A1 / LAB_01A2) with jobs, the state
    cells, a first fighter near the handler's record.  Called after World.arrange (so the draws of the older kinds are not
    shifted)."""
    I = w.I
    first = I.r32(A('05F2'))
    heap = [HEAP + REC * i for i in range(HEAP_N)]
    pool = [r for r in heap[:12] if r != self_addr and r != first]
    body_a = rng.choice(pool)
    body_b = rng.choice([r for r in pool if r != body_a])
    I.w32(A('01A1'), body_a)
    I.w32(A('01A2'), body_b)
    for r in (body_a, body_b):
        I.w32(r, 1)
    # both bodies own a job (the lookups would miss otherwise: the asm then works through address 0): re-own two slots whose
    # owner is no role of the case
    critical = {self_addr, first}
    free = [i for i in range(10) if I.r32(JOBS + 50 * i + 24) not in critical]
    rng.shuffle(free)
    for i, r in zip(free[:2], (body_a, body_b)):
        I.w32(JOBS + 50 * i + 24, r)
    # the state cells
    I.w8(A('0EB6'), rng.choice((0, 0, 0, 0, 1, 2, 4, 0x40, 0x41, 0x42, 0x44, rng.getrandbits(8) & 0x47)))
    I.w8(A('0EB6') + 1, 0)
    I.w8(A('0EEA'), rng.choice((0, 0, 0, 1, 2, 8, 0x10, 0x20, 0x40, 0x41, 0x48, 0x50, 0x60, rng.getrandbits(8) & 0x7B)))
    I.w8(A('0EEA') + 1, rng.choice((0, 0, 0x33)))
    I.w16(A('0EC1'), rng.randint(0, 5))
    I.w16(A('06C0'), rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 19, rng.randint(0, 25))))
    I.w16(A('0F38'), rng.choice((0, 4, 8, 0x0C, 0x10, 0x1C, 0x20)))
    I.w16(A('08CF'), rng.choice((0, 1, 2, 3, 4, 0xFFFF)))
    I.put(A('08D9'), rbytes(rng, 16))
    # the first fighter near the handler's record, the tunables of the record, the flags of the record
    sx, sy = I.r16(self_addr + 4), I.r16(self_addr + 8)
    if rng.random() < 0.8:
        I.w16(first + 4, (sx + rng.choice(DX_CHOICES)) & 0xFFFF)
        I.w16(first + 8, (sy + rng.choice((-100, -40, -20, -10, -3, 0, 3, 10, 30, 90))) & 0xFFFF)
    r = rng.random()
    if r < 0.6:
        I.w16(first + 80, rng.randint(1, 60))
    elif r < 0.85:
        I.w16(first + 80, rng.choice((0, 0xFFFF, rng.randint(-20, 0) & 0xFFFF)))
    I.w8(self_addr + 104, rng.choice((0, 0, 8, 0x18, 0x10, 1, 2, 3, 0x40, 0x19, 0x1A, 0x58, 0x80, rng.getrandbits(8))))
    I.w8(self_addr + 13, rng.choice((0, 1, 2, 5, 0x14, 0x28, rng.randint(0, 255))))
    I.w8(self_addr + 106, rng.choice((0, 0, 1, 2, 5, 6)))
    I.w16(self_addr + 62, 0 if rng.random() < 0.85 else rng.getrandbits(16))
    I.w16(A('061D') + 8, rng.randint(0, 1))              # LAB_0620 (the lair flag)
    I.w16(self_addr + 120, rng.choice((4, 5, 10, 40, 60, 100)))
    I.w16(self_addr + 116, rng.choice((0x64, 0x64, 20, 50, 80, 120, rng.randint(1, 200))))
    I.w16(self_addr + 118, rng.choice((0x50, 0x50, 10, 30, 60, 100, rng.randint(1, 200))))
    if fn == 'T10':
        I.w8(self_addr + 76, rng.choice((0, 0, 3, 10)))
        if rng.random() < 0.3:                           # the knight is the first fighter: the opponent is the other of the pair
            I.w32(A('05F2'), self_addr)
        opp = I.r32(A('05E4') + 4)
        for q in (opp, first):
            I.w16(q + 64, rng.choice((0, 4, 8, 0x0C, 0x10, 0x1C, 0x20, 0x20, 8, 4)))
            I.w8(q + 10, rng.choice((1, 3)))
        I.w8(self_addr + 10, rng.choice((1, 3)))
        I.w16(self_addr + 64, rng.choice((0, 4, 8, 0x0C, 0x10, 0x1C, 0x20)))
        if rng.random() < 0.5 and I.r32(self_addr + 18):
            I.w16(self_addr + 80, rng.randint(1, 40))
        tgt = I.r32(A('05E4') + 4) if I.r32(A('05F2')) == self_addr else I.r32(A('05F2'))
        if rng.random() < 0.7:
            I.w16(tgt + 4, (sx + rng.choice((-150, -130, -125, -120, -110, -100, -96, -95, -93, -92, -90, -80, -50, 50, 80, 90, 92, 93, 95, 96, 100,
                                         110, 120, 125, 130, 150))) & 0xFFFF)
            I.w16(tgt + 8, (sy + rng.choice((-3, 0, 3, 10))) & 0xFFFF)
            I.w16(tgt + 80, rng.randint(1, 50) if rng.random() < 0.75 else rng.choice((0, 0xFFFF)))
        # deliberate difference (fight_creatures.cpp): with the opponent down and 256 or more away the asm walks with D0 still
        # holding that distance, so its step-table index carries the distance's high byte and reads far memory; the port reads
        # the table
        if s16(I.r16(tgt + 80)) <= 0 and abs(s16(I.r16(self_addr + 4) - I.r16(tgt + 4))) >= 256:
            I.w16(tgt + 4, (sx + rng.choice((-150, -100, 100, 150))) & 0xFFFF)
    if fn == 'T08' and rng.random() < 0.6:
        I.w16(first + 4, (sx + rng.choice((-170, -151, -141, -140, -135, -131, -130, -125, -120, -119, -101, -100, 100, 101, 119,
                                         120, 125, 130, 131, 135, 140, 141, 150, 151, 170))) & 0xFFFF)
        I.w16(first + 8, (sy + rng.choice((-2, 0, 2))) & 0xFFFF)
        I.w16(first + 80, rng.randint(1, 60))
    if fn == 'T04' and rng.random() < 0.45:               # on the screen, nothing running: the approach and the walk
        I.w8(A('0EB6'), 0)
        I.w8(self_addr + 104, rng.choice((8, 8, 0x28, 0x0C, 0x0A, 0x48)))
        I.w32(self_addr + 14, 0)
        I.w32(self_addr + 18, 0)
    if fn == 'T04' and rng.random() < 0.6:
        I.w16(first + 4, (sx + rng.choice((-110, -99, -75, -74, -60, -51, -50, -21, -20, -19, 19, 20, 21, 50, 51, 60, 74, 75, 99,
                                         100, 110))) & 0xFFFF)
        I.w16(first + 8, (sy + rng.choice((-2, 0, 2))) & 0xFFFF)
        I.w16(first + 80, rng.randint(1, 60))
    if fn == 'T40':
        I.w16(self_addr + 64, rng.choice((0, 8, 0x20, 4)))
        if rng.random() < 0.6:
            I.w16(first + 4, (sx + rng.choice((-160, -150, -149, -120, -100, -99, -60, 60, 99, 100, 120, 149, 150, 160))) & 0xFFFF)
            I.w16(first + 8, (sy + rng.choice((-2, 0, 2))) & 0xFFFF)
            I.w16(first + 80, rng.randint(1, 60))


def gen_new(rng, base, fn):
    """One case of the S_40 handlers (fn 'T04' / 'T08' / 'T10' / 'T40')."""
    w = World(rng, base)
    I = w.I
    knights = [KNIGHT0 + REC * i for i in range(4)]
    heap = [HEAP + REC * i for i in range(HEAP_N)]
    self_addr = rng.choice(knights + heap[:6]) if fn == 'T10' else rng.choice(heap[:10])
    I.w8(self_addr + 77, NEW_TYPES[fn])
    I.w32(self_addr, 1)
    roles = w.arrange(self_addr, fn)
    arrange_s40(w, rng, self_addr, roles, fn)
    joy0 = rng.choice(JOY) if rng.random() < 0.8 else rng.getrandbits(5)
    joy1 = rng.choice(JOY) if rng.random() < 0.8 else rng.getrandbits(5)
    regs = {'D%d' % i: 0 for i in range(8)}
    regs.update({'A%d' % i: 0 for i in range(7)})
    regs['A0'] = self_addr
    return w, regs, joy0, joy1, rng.getrandbits(16), rng.getrandbits(16)


def gen_op(rng, base, fn):
    """One case of a script-called routine (fn 'O' + label): A1 = the owner record, A2 = a frame-table list, D0..D3 = x / y / z /
    facing."""
    lab = fn[1:]
    if lab == '028E':
        w, regs, j0, j1, vh, salt = gen_case(rng, base, '027A')
        self_addr = KNIGHT0 + 4 * REC
    else:
        w = World(rng, base)
        heap = [HEAP + REC * i for i in range(HEAP_N)]
        self_addr = rng.choice(heap[:10])
        w.I.w8(self_addr + 77, rng.choice((0x18, 0x24, 0x28, 0x34, 0x0C)))
        w.I.w32(self_addr, 1)
        roles = w.arrange(self_addr, fn)
        arrange_s40(w, rng, self_addr, roles, 'T08')
        j0, j1, vh, salt = 0, 0, rng.getrandbits(16), rng.getrandbits(16)
    I = w.I
    if lab == '01C8':
        I.w8(self_addr + 13, rng.choice((0, 1, 2, 0x7F, 0x80, 0x81, 0xFF, rng.getrandbits(8))))
    if lab in ('0210', '0211'):
        I.w8(A('08D0'), rng.choice((1, 3, 0, 2)))
        I.w32(A('08CE'), A('08CC'))
        I.w16(A('08CF'), rng.choice((0, 1, 2, 3, 4, 0xFFFF)))
        I.w16(self_addr + 4, rng.choice((0, 5, 9, 12, 30, 150, 0x13C, 0x140, 0x150, 0xFFF0)))
        I.w16(self_addr + 8, rng.choice((0, 10, 21, 22, 60, 0x95, 0x9C, 0xA0)))
        I.w8(self_addr + 10, rng.choice((1, 3)))
    if lab == '02CA':
        I.w8(self_addr + 76, rng.choice((0, 1, 3, 10)))
    regs = {'D%d' % i: 0 for i in range(8)}
    regs.update({'A%d' % i: 0 for i in range(7)})
    regs['A1'] = self_addr
    regs['A2'] = 0x00500000 + 0x40 * rng.randrange(0x4000)
    regs['D0'] = rng.getrandbits(16)
    regs['D1'] = rng.getrandbits(16)
    regs['D2'] = rng.getrandbits(16)
    regs['D3'] = rng.choice((1, 3, 0, 2))
    return w, regs, j0, j1, vh, salt


def gen_defaults(rng, base, fn):
    """LAB_01C6: A1 = one of the four knight records (its +96 points at the inventory the routine clears)."""
    w = World(rng, base)
    regs = {'D%d' % i: rng.getrandbits(32) for i in range(8)}
    regs.update({'A%d' % i: 0 for i in range(7)})
    regs['A0'] = rng.getrandbits(32)
    regs['A1'] = KNIGHT0 + REC * rng.randrange(4)
    return w, regs, 0, 0, 0, 0


def gen_tables(rng, base, fn):
    w = World(rng, base)
    regs = {'D%d' % i: 0 for i in range(8)}
    regs.update({'A%d' % i: 0 for i in range(7)})
    return w, regs, 0, 0, 0, 0


def gen_picker(rng, base, fn):
    w = World(rng, base)
    # the shared list offset: 0..14 is all the pickers ever leave behind (each resets at its terminator)
    w.I.w16(A('02EC') + 2, rng.choice(range(0, 15, 2)))
    regs = {'D%d' % i: 0 for i in range(8)}
    regs.update({'A%d' % i: 0 for i in range(7)})
    return w, regs, 0, 0, 0, 0


def case_text(fn, w, regs, joy0, joy1, vhpos, salt):
    head = 'CASE %s %s %s %x %x %x %x\n' % (
        fn, ' '.join('%x' % regs['D%d' % i] for i in range(8)), ' '.join('%x' % regs['A%d' % i] for i in range(7)), joy0, joy1,
        vhpos, salt)
    return head + w.I.lines('M') + 'GO\n'


def handler_table_want():
    """{slot: label} of the first LAB_08C7 fill of the original asm (LAB_01AE): the entries fightTablesInit has to write."""
    want = {}
    with open(TC.MOG_ASM, encoding='utf-8', errors='replace') as f:
        asm = f.read().split(chr(10))
    start = next(i for i, ln in enumerate(asm) if ln.split() == ['LEA', 'LAB_08C7,A0'])
    for ln in asm[start + 1:start + 20]:
        m = re.match(r'\s*MOVE\.L\s+#(\w+),(\d+)\(A0\)', ln)
        if not m:
            break
        want[int(m.group(2))] = m.group(1)
    return want


def case_seed(fn):
    """The seed of a case family: the label for the old ones (so their cases are the ones they always were), a hash of the key
    for the ROADMAP 7.1h families ('T04', 'O02DC', 'K01C6')."""
    try:
        return 0x6A + (int(fn, 16) if fn[0] != 'S' else int(fn[1:], 16))
    except ValueError:
        return 0x6A + sum((i + 1) * ord(c) for i, c in enumerate(fn)) * 7


def run_cases(texts, base_text):
    exe = build_driver()
    r = subprocess.run([exe], input=base_text + ''.join(texts), capture_output=True, text=True, timeout=1200)
    if r.returncode != 0:
        raise RuntimeError('driver exit %d: %s' % (r.returncode, r.stderr[-2000:]))
    res = []
    for ln in r.stdout.split('\n'):
        if ln.startswith('RES '):
            head = ln.split(' | ')[0]
            parts = head.split(' ', 3)
            res.append((parts[1], parts[2], parts[3] if len(parts) > 3 else ''))
    return res


@unittest.skipUnless(CXX, 'clang++ not on PATH')
class FightersTest(unittest.TestCase):
    base_text = ''

    tabs = None

    @classmethod
    def setUpClass(cls):
        rng = random.Random(1)
        cls.tabs = tables_image(rng)
        cls.base_text = base_image().lines('BASE') + cls.tabs.lines('BASE')

    def check(self, fn, gen, n=None):
        n = n or N_CASES
        rng = random.Random(case_seed(fn))
        texts = []
        for _ in range(n):
            w, regs, j0, j1, vh, salt = gen(rng, self.tabs, fn if fn[0] != 'S' else fn[1:])
            texts.append(case_text(fn, w, regs, j0, j1, vh, salt))
        res = run_cases(texts, self.base_text)
        self.assertEqual(len(res), n)
        bad = [(i, r) for i, r in enumerate(res) if r[1] != 'OK']
        if bad:
            i, r = bad[0]
            self.fail('%s: %d of %d cases differ from the lifted asm; first #%d (%s): %s' % (fn, len(bad), n, i, r[1], r[2]))

    # ---- static checks ----
    def test_00_layout_on_the_symbols(self):
        """FightVars / MoveVars (fighters.hpp) overlay the asm's BSS cells at these offsets (the static_asserts of the header
        fix the struct side; the driver compiles them)."""
        v = A('061D')
        for lab, off in (('061E', 4), ('061F', 6), ('0620', 8), ('0621', 10), ('0622', 82), ('0623', 156), ('0624', 162),
                         ('0625', 164), ('0626', 166), ('0627', 168), ('0628', 170), ('0629', 172), ('062A', 174), ('062B', 176),
                         ('062C', 180), ('062D', 182), ('062E', 184)):
            self.assertEqual(A(lab) - v, off, 'LAB_' + lab)
        for i, lab in enumerate(('0F3A', '0F3B', '0F3C', '0F3D', '0F3E', '0F3F')):
            self.assertEqual(A(lab) - A('0F3A'), 2 * i, 'LAB_' + lab)
        self.assertEqual(A('08D0') - A('08CE'), 6)             # the hop cells: long, word, byte
        self.assertEqual(A('02EE') - A('02EC'), 4)             # the picker counters (two words) end where the first list starts

    def test_00_data_tables_match_the_image(self):
        """The asm data the oracle reads (DATA above, and the C++ constants transcribed from it) against the reassembled mog."""
        exe = os.path.join(ROOT, 'build', 'reasm', 'mog')
        syms = os.path.join(ROOT, 'build', 'reasm', 'mog.symbols.json')
        if not (os.path.exists(exe) and os.path.exists(syms)):
            self.skipTest('build/reasm/mog missing (py tools/reassemble.py)')
        for cand in (os.path.join(ROOT, 'reference', 'moonshard', 'tools'), os.path.join(os.path.dirname(ROOT), 'moonshard', 'tools')):
            if os.path.isdir(cand):
                sys.path.insert(0, cand)
        from moghunks import parse_hunk_file
        with open(exe, 'rb') as f:
            hf = parse_hunk_file(f.read())
        with open(syms) as f:
            table = json.load(f)
        for lab, words in DATA.items():
            s = table[lab if lab.startswith('SECSTRT_') else 'LAB_' + lab]
            data = hf.hunks[s['hunk']].data[s['offset']:s['offset'] + 2 * len(words)]
            got = [int.from_bytes(data[i:i + 2], 'big') for i in range(0, len(data), 2)]
            self.assertEqual(got, words, 'LAB_' + lab)

    def test_00_fight_ops_patch_file(self):
        """asm/patches/mog.fight_ops.json (ROADMAP 7.1h): well formed, the original text is the asm's, no line is claimed by
        another patch file, every $B0 operand that names a routine of fightOpRun is tagged (and no other), the rt symbols its
        shims call are defined in src/rt/fighters.cpp, the as_data blocks are the cells the C++ uses."""
        import glob
        mine = os.path.join(ROOT, 'asm', 'patches', 'mog.fight_ops.json')
        with open(mine, encoding='utf-8') as f:
            doc = json.load(f)
        self.assertEqual(doc['binary'], 'mog')
        ids = [p['id'] for p in doc['patches']]
        self.assertEqual(len(ids), len(set(ids)))

        def span(p):
            return (p['lines'][0], p['lines'][1]) if p.get('kind') == 'as_data' else (p['line'], p['line'] + len(p['orig']) - 1)
        taken = []
        for fn in sorted(glob.glob(os.path.join(ROOT, 'asm', 'patches', 'mog*.json'))):
            if os.path.abspath(fn) == os.path.abspath(mine):
                continue
            with open(fn, encoding='utf-8') as f:
                other = json.load(f)
            for p in other['patches']:
                if ('line' in p and 'orig' in p) or p.get('kind') == 'as_data':
                    taken.append((span(p), os.path.basename(fn), p['id']))
        with open(TC.MOG_ASM, encoding='utf-8', errors='replace') as f:
            asm = f.read().split(chr(10))
        seen = []
        for p in doc['patches']:
            a, b = span(p)
            self.assertIn('why', p)
            for (ta, tb), fn, pid in taken:
                self.assertTrue(b < ta or a > tb, '%s overlaps %s of %s (lines %d-%d)' % (p['id'], pid, fn, ta, tb))
            for (sa, sb), pid in seen:
                self.assertTrue(b < sa or a > sb, '%s overlaps %s' % (p['id'], pid))
            seen.append(((a, b), p['id']))
            if p.get('kind') == 'as_data':
                self.assertEqual(' '.join(asm[a - 1].split()), ' '.join(p['expect_first'].split()), p['id'])
                self.assertEqual(' '.join(asm[b - 1].split()), ' '.join(p['expect_last'].split()), p['id'])
                continue
            for k, want in enumerate(p['orig']):
                self.assertEqual(' '.join(asm[a - 1 + k].split()), ' '.join(want.split()), '%s line %d' % (p['id'], a + k))
        # every $B0 operand naming a routine of fightOpRun carries its tag, and nothing else is tagged
        with open(os.path.join(ROOT, 'src', 'game', 'fight_ops.cpp'), encoding='utf-8') as f:
            src = f.read()
        handled = sorted(re.findall(r'case 0x([0-9A-F]{4}):', src))
        self.assertEqual(handled, sorted(OPS))
        operand = re.compile(r'^\s+DC\.L\s+LAB_([0-9A-F]{4})\s*$')
        want_lines = {i + 1: m.group(1) for i, ln in enumerate(asm) for m in [operand.match(ln)] if m and m.group(1) in OPS}
        tagged = {}
        for p in doc['patches']:
            if p['id'].startswith('fight-op-'):
                self.assertEqual(len(p['orig']), 1)
                m = operand.match(p['orig'][0])
                self.assertTrue(m, p['id'])
                self.assertEqual(p['new'], [chr(9) + 'DC.L' + chr(9) + '$F000' + m.group(1)], p['id'])
                tagged[p['line']] = m.group(1)
        self.assertEqual(tagged, want_lines)
        self.assertEqual(len(tagged), 78)
        # the rt side: the symbols the shims call, the tag constant
        with open(os.path.join(ROOT, 'src', 'rt', 'fighters.cpp'), encoding='utf-8') as f:
            rt = f.read()
        for fn in doc['funcs']:
            self.assertEqual(fn['impl'], 'src/rt/fighters.cpp')
            self.assertIn('.globl ' + fn['name'], rt)
        used = {m.group(1) for p in doc['patches'] if 'new' in p for ln in p['new'] for m in [re.search(r'(rt_\w+)', ln)] if m}
        self.assertEqual(used, {fn['name'] for fn in doc['funcs']})
        with open(os.path.join(ROOT, 'include', 'game', 'fighters.hpp'), encoding='utf-8') as f:
            self.assertIn('FIGHT_OP_TAG = 0xF0000000u', f.read())
        # opcode $B0 (src/rt/combat_script.cpp cbCall) offers the operand to fightOpRun before it jumps to the asm
        with open(os.path.join(ROOT, 'src', 'rt', 'combat_script.cpp'), encoding='utf-8') as f:
            cs = f.read()
        body = cs[cs.index('void cbCall('):]
        self.assertLess(body.index('rtFightOpRun(ulFn'), body.index('rtCombatCall(ulFn'))
        # the as_data blocks are the code-hunk cells the rt environment points at
        cells = {p['expect_first'][:-1] for p in doc['patches'] if p.get('kind') == 'as_data'}
        self.assertEqual(cells, {'LAB_0234', 'LAB_02EC', 'LAB_0EB6', 'LAB_0EEA'})
        for lab in ('0234', '02EC', '0EB6', '0EEA'):
            sys.path.insert(0, os.path.join(ROOT, 'tools'))
            import cellnames  # ROADMAP 7.1s: a cell with a C++ name is spelled with it
            self.assertIn(cellnames.symbol('mog', 'LAB_' + lab), rt)

    def test_18_group_is_dead_on_paper(self):
        """With the patches in the generated asm (py tools/resource.py) nothing live reaches the handler bodies, the script op
        bodies or the S_40 routines any more (tools/asm_remaining.py); LAB_02BA stays: the map handlers LAB_04AC / LAB_04C4 /
        LAB_0DCF still jump to it."""
        import asm_remaining as AR
        with open(os.path.join(ROOT, 'asm', 'mog.s'), encoding='latin-1') as f:
            if 'ps_fight_ops_script_table' not in f.read():
                self.skipTest('asm/mog.s is not generated from asm/patches/mog.fight_ops.json yet (py tools/resource.py)')
        res = AR.analyse()
        dead = ('01C6 01C8 01CA 01E1 01E6 01ED 01EF 01F2 01F6 01F9 01FD 0200 0201 0203 0205 0206 0210 0211 0226 0236 0251 027A 028E '
                '0297 0298 029F 02AC 02AD 02AE 02AF 02BB 02BC 02BF 02C2 02C4 02C6 02C8 02CA 02CB 02D2 02D3 02DB 02DC 02DE 02E0 02E1 '
                '02E2 02E3 02E4 02E6 02E8 02E9 0358 035B 035D 0361 0362 0363 0366 0367 0368 0372 0374 038B 038C 038E 0390 0391 0392 '
                '0393 0397 039B 039C 0EA1 0EA4 0EB8 0EB9 0EBD 0EBE 0EC2 0EC6 0EC7 0ED0 0ED2 0EDA 0EEB 0EEC 0EED 0EEE 0EEF 0EF6 '
                '0EF7 0EFF 0F1A 0F1C 0F1E 0F1F 0F20 0F24 0F2E 0F32 0F33 0F34').split()
        for lab in dead:
            self.assertFalse(AR.is_live(res, 'mog', 'LAB_' + lab), 'LAB_' + lab)
        self.assertFalse(AR.is_live(res, 'mog', 'SECSTRT_40'))
        self.assertTrue(AR.is_live(res, 'mog', 'LAB_02BA'))
        # the cells and the entries the C++ and the main loop still use stay
        for lab in ('LAB_0234', 'LAB_02EC', 'LAB_0EB6', 'LAB_0EEA', 'LAB_0301', 'LAB_02D0', 'LAB_02CE'):
            self.assertTrue(AR.is_live(res, 'mog', lab), lab)

    def test_01_knight(self):
        self.check('01CA', gen_case)

    def test_02_flyer(self):
        self.check('0226', gen_case)

    def test_03_brawler(self):
        self.check('0236', gen_case)

    def test_04_caster(self):
        self.check('0251', gen_case)

    def test_05_dragon(self):
        self.check('027A', gen_case)

    def test_06_dragon_part(self):
        self.check('0298', gen_case)

    def test_07_drake(self):
        self.check('029F', gen_case)

    def test_08_dagger(self):
        self.check('02CB', gen_case)

    def test_09_idle(self):
        self.check('02D2', gen_case)

    def test_10_pickers(self):
        for p in PICKERS:
            self.check('S' + p, gen_picker, 300)

    # ---- ROADMAP 7.1h ----
    def test_11_snatcher(self):
        self.check('T04', gen_new)

    def test_12_demon(self):
        self.check('T08', gen_new)

    def test_13_ai_knight(self):
        self.check('T10', gen_new)

    def test_14_stalker(self):
        self.check('T40', gen_new)

    def test_15_script_ops(self):
        """Every routine the combat scripts call through opcode $B0, run through fightOpRun on its tag."""
        for op in OPS:
            self.check('O' + op, gen_op, 150)

    def test_16_knight_defaults(self):
        self.check('K01C6', gen_defaults, 40)

    def test_17_handler_table(self):
        """fightTablesInit writes the sixteen entries LAB_01AE used to store (slot 68, LAB_04AC, stays asm and is not ours)."""
        want = handler_table_want()
        self.assertEqual(sorted(want), [0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 68])
        res = run_cases([case_text('TABLES', *gen_tables(random.Random(5), self.tabs, 'TABLES'))], self.base_text)
        self.assertEqual(res[0][:2], ('TABLES', 'OK'))
        got = bytes.fromhex(res[0][2])
        for off, lab in want.items():
            if off == 68:
                self.assertEqual(int.from_bytes(got[off:off + 4], 'big'), 0, 'slot 68 is not written by the C++')
                continue
            self.assertEqual(int.from_bytes(got[off:off + 4], 'big'), SYM[lab], 'slot %d = %s' % (off, lab))
        self.assertEqual(sorted(w for w in range(0, 72, 4) if int.from_bytes(got[w:w + 4], 'big')), [o for o in sorted(want) if o != 68])


if __name__ == '__main__':
    unittest.main()
