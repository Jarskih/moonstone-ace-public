"""Tests for src/game/mainloop.cpp + src/game/mogjobs.cpp (ROADMAP 6.1): mog's top level and its job manager.

Reference: the LIFTED asm (tools/lift.py output, a literal 68k transliteration with exact CCR semantics), as in
tests/test_overworld.py.  Two host drivers (clang++) run the lifted original next to the C++ on a copy of the same guest
arena and compare what is observable:

  * mogjobs: LAB_0305 / LAB_0315 / LAB_0319 / LAB_031B / LAB_030D / LAB_031D / LAB_031F against ms::game::mogJobs* on
    random job tables (random owners drawn from a small pool so lookups hit, random garbage everywhere else): the WHOLE
    arena afterwards (the lifted code's stack excepted), the result register D0, and the ticks passed to the wait routine.
  * mainloop: SECSTRT_0 (boot + the main loop it falls into, LAB_0001 and the practice branch LAB_0002) and LAB_0064
    (quit, which jumps back into LAB_0001) against ms::game::mainBoot / mainRun.  The asm routines the top level calls
    are logged stubs on both sides (the oracle logs address + D0/A0, the C++ logs the MainStep); LAB_00B4 (menu) and
    LAB_01AE (scheduler reset) also write cells, the same ones on both sides, so the ORDER of the C++'s own writes
    against those calls is checked, and the whole arena is compared afterwards (ranges of the cells the C++ keeps in
    native byte order are compared by value).  The register contract of the shim (D0/A0 per step) is checked by parsing
    the step table of src/rt/mainloop.cpp against what the oracle loaded.

Also: the patch file asm/patches/mog.mainloop.json does not overlap any other mog patch, matches mog.asm, and (on a
scratch copy of the patch tables with the new rt_* names added to abs_symbols.json) tools/resource.py --verify holds.

Needs clang++ on PATH and build/reasm + build/inventory (py tools/reassemble.py, py tools/callgraph.py); skipped
otherwise.  The m68k shim itself is compiled (not run) by test_shim_compiles when m68k-amiga-elf-g++ is available.
"""
import copy
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
import flow_lib  # noqa: E402
MOG_ASM = os.path.join(os.path.dirname(ROOT), 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
SYMS_HPP = os.path.join(ROOT, 'include', 'ms', 'gen', 'mog_syms.hpp')
CXX = shutil.which('clang++')
WORK_DIR = os.path.join(ROOT, 'build', 'mainloop_test')
PATCH_FILE = os.path.join(ROOT, 'asm', 'patches', 'mog.mainloop.json')
HDR = os.path.join(ROOT, 'include', 'game', 'mainloop.hpp')
RT_SRC = os.path.join(ROOT, 'src', 'rt', 'mainloop.cpp')

# rt_* names the patch file introduces; they belong in abs_symbols.json "funcs" ({"name", "impl": "src/rt/mainloop.cpp"}).
RT_NAMES = ['rt_mog_main', 'rt_mog_quit', 'rt_mog_jobs_reset', 'rt_mog_job_toggle', 'rt_mog_frame_start', 'rt_mog_frame_wait']
# 7.1s: no game code calls these any more; their register-contract entries live in tests/mainloop_emu_support.cpp (unicorn tests only)
TEST_ENTRY_NAMES = ['rt_mog_again', 'rt_mog_job_find', 'rt_mog_job_kill', 'rt_mog_job_restart']
SUPPORT_SRC = os.path.join(ROOT, 'tests', 'mainloop_emu_support.cpp')


def load_syms():
    text = open(SYMS_HPP, encoding='utf-8').read()
    hunks = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'HUNK_([0-9A-F]{2}) = 0x([0-9A-F]+)u', text)}
    syms = {}
    for m in re.finditer(r'constexpr uint32_t ((?:LAB|SECSTRT)_[0-9A-Fa-f]+) = HUNK_([0-9A-F]{2}) \+ 0x([0-9A-F]+)u;', text):
        syms[m.group(1)] = hunks[m.group(2)] + int(m.group(3), 16)
    return syms


HAVE_INPUTS = os.path.exists(SYMS_HPP) and os.path.exists(MOG_ASM)
SYM = load_syms() if os.path.exists(SYMS_HPP) else {}   # facts (label addresses): also in a public checkout (ROADMAP 10.2)
if SYM:
    SYM['LAB_0426+2'] = SYM['LAB_0426'] + 2      # rt_mog_set_planes: the real entry behind the mis-decoded CMPA word (ROADMAP 7.1q)


def parse_steps():
    """{enum name without STEP_: (value, label)} from include/game/mainloop.hpp."""
    out = {}
    for m in re.finditer(r'^\s*STEP_(\w+)\s*=\s*(0x[0-9A-Fa-f]+)', open(HDR, encoding='utf-8').read(), re.M):
        lab = re.match(r'(LAB_[0-9A-F]{4}|SECSTRT_\d+)', m.group(1)).group(1)
        out[m.group(1)] = (int(m.group(2), 16), lab)
    return out


# rt entries a g_steps row may call instead of the patch stub at the label (src/rt/mainloop.cpp)
RT_ENTRY_LABEL = {'rt_mog_carve': 'LAB_0004', 'rt_fight_run': 'LAB_0036', 'rt_mog_display_init': 'SECSTRT_34',
                  'rt_mog_palette_hook_add': 'LAB_0E53', 'rt_mog_job_boot': 'LAB_0303'}


# MainSteps that src/rt/mainloop.cpp opStep runs in C++ (7.1l) instead of through a g_steps row
# 7.1o: rt entries the C++ now calls instead of the label stubs (tools/stub_cutover.py): rt name -> the label whose oracle routine it is
RT_LABELS = {'rt_mog_dagger_clear': 'LAB_02F2', 'rt_mog_jobs_reset': 'LAB_0305', 'rt_job_create': 'LAB_0310', 'rt_mog_job_toggle': 'LAB_0319', 'rt_mog_frame_start': 'LAB_031D', 'rt_creature_dispatch': 'LAB_0322', 'rt_combat_tick': 'LAB_0328', 'rt_palette_set_target_mog': 'LAB_0E55', 'rt_palette_ramp_add_mog': 'LAB_0E5A', 'rt_mog_palette_scene': 'LAB_03F3', 'rt_display_palette_write': 'LAB_0D8A', 'rt_mog_key_xlat': 'LAB_0D8D', 'rt_mog_palette_clear': 'LAB_03EB', 'rt_mog_pal_copy_live': 'LAB_03EE', 'rt_mog_wait_fire': 'LAB_00EC', 'rt_mog_cursor_on': 'LAB_0575', 'rt_mog_cursor_off': 'LAB_057B', 'rt_mog_key_wait': 'LAB_0B46', 'rt_mog_key_reset': 'LAB_0B82', 'rt_places_gift_gold': 'LAB_046C', 'rt_places_gift_item': 'LAB_0471', 'rt_scr_lines': 'LAB_049E', 'rt_scr_dice_idle': 'LAB_04AC', 'rt_scr_ramp5': 'LAB_04C5', 'rt_knight_recalc_hp': 'LAB_0013', 'rt_knight_recalc_endurance': 'LAB_0019', 'rt_mog_text_string': 'LAB_0431', 'rt_mog_add_record': 'LAB_0448', 'rt_screen_run': 'LAB_04CF'}
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rt_labels_q import RT_LABELS_Q  # noqa: E402  (ROADMAP 7.1q: the entries the cut-over stubs became)
RT_LABELS.update(RT_LABELS_Q)

RT_ENTRY_LABEL.update(RT_LABELS)

CPP_STEPS = {'LAB_0003', 'LAB_0011', 'SECSTRT_36', 'LAB_0DC8',
             # 7.1o: the loader / arena / cursor stubs are called as C++ (rtCl*, rtAr*, rtCui*, rtArenaPractice), no asm row
             'LAB_00F8', 'LAB_012C', 'LAB_0128', 'LAB_0572', 'LAB_0115', 'LAB_013A', 'LAB_0152', 'LAB_0156', 'LAB_01AE', 'LAB_01BE',
             'LAB_0134', 'LAB_013C', 'LAB_0165', 'LAB_0137_06E6',
             # 7.1q: the title step, the knights screen, the disk prompt (a bare RTS) and the text player (a bare RTS) are C++
             'LAB_00B4', 'LAB_00D3', 'LAB_0100_D2', 'LAB_0BB3'}


def parse_cpp_steps():
    """The `case STEP_X:` labels of opStep's switch in src/rt/mainloop.cpp."""
    text = open(RT_SRC, encoding='utf-8').read()
    body = text[text.index('void opStep('):]
    body = body[:body.index('for(const StepRow')]
    return set(re.findall(r'case STEP_(\w+):', body))


def parse_step_table():
    """Rows of g_steps in src/rt/mainloop.cpp: {enum name: (fn label, d0, a0 label or None)}."""
    text = open(RT_SRC, encoding='utf-8').read()
    body = text[text.index('g_steps[] = {'):]
    body = body[:body.index('};')]
    rows = {}
    for m in re.finditer(r'\{STEP_(\w+),\s*(?:\(const void \*\))?(?:RT_FN\()?(mog_|rt_)(\w+?)\)?,\s*(\d+),\s*(nullptr|mog_\w+)\}', body):
        a0 = None if m.group(5) == 'nullptr' else m.group(5)[len('mog_'):]
        fn = m.group(3) if m.group(2) == 'mog_' else RT_ENTRY_LABEL[m.group(2) + m.group(3)]   # an rt entry stands for the label it replaces
        rows[m.group(1)] = (fn, int(m.group(4)), a0)
    return rows


# ---- lifted oracle ------------------------------------------------------------------------------------------------
LIFT_WORK = ['SECSTRT_0', 'LAB_0001', 'LAB_0064', 'LAB_031F']          # not in src/lifted/mog (or calls out): lifted here
LIFT_REPO = ['LAB_0305', 'LAB_0315', 'LAB_0319', 'LAB_031B', 'LAB_030D', 'LAB_031D', 'LAB_03A7', 'LAB_03C7', 'LAB_0161']


def lifted_path(lab):
    base = 'lab_' + lab[4:].lower() if lab.startswith('LAB_') else 'lab_' + lab.lower()
    for d in (os.path.join(WORK_DIR, 'gen'), os.path.join(ROOT, 'src', 'lifted', 'mog')):
        p = os.path.join(d, base + '.cpp')
        if os.path.exists(p):
            return p
    raise FileNotFoundError(lab)


def ensure_lifted():
    gen = os.path.join(WORK_DIR, 'gen')
    os.makedirs(gen, exist_ok=True)
    stamp = os.path.join(gen, 'stamp')
    newest = max(os.path.getmtime(p) for p in (MOG_ASM, os.path.join(ROOT, 'tools', 'lift.py'),
                                              os.path.join(ROOT, 'tools', 'lift_ops.py'), SYMS_HPP, __file__))
    if os.path.exists(stamp) and os.path.getmtime(stamp) >= newest:
        return
    for f in os.listdir(gen):
        os.remove(os.path.join(gen, f))
    raw = os.path.join(WORK_DIR, 'raw')
    shutil.rmtree(raw, ignore_errors=True)
    os.makedirs(raw)
    for lab in LIFT_WORK:
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'lift.py'), 'mog', lab, '--out-dir', raw,
                            '--allow-calls'], capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0:
            raise RuntimeError('lift %s failed: %s%s' % (lab, r.stdout, r.stderr))
    for f in os.listdir(raw):
        if not f.endswith('.cpp'):
            continue
        s = open(os.path.join(raw, f), encoding='utf-8').read()
        s = s.replace('lift::callAsm(', 'tcallAsm(')
        # direct calls to other lifted routines become logged stubs as well (the driver decides per address)
        s = re.sub(r'\blab_((?:LAB_[0-9A-F]{4})|(?:SECSTRT_\d+))\(R, M\);', r'tcallAsm(R, M, ms::sym_mog::\1);', s)
        s = re.sub(r'(void tcallAsm\(R, M, ms::sym_mog::\w+\);)', r'\1', s)
        with open(os.path.join(gen, f), 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(s)
    open(stamp, 'w').write('ok')


COMMON = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ms/regs.hpp"
#include "ms/gen/lift_ops.hpp"
#include "ms/gen/mog_syms.hpp"
#include "engine/jobs.hpp"
using namespace ms;
using namespace ms::sym_mog;

static const uint32_t ARENA = 0x180000, STACK_LO = 0x178000, STACK_TOP = 0x17F000;
static uint8_t g_o[ARENA];                   // oracle arena: big-endian everywhere
static uint8_t g_s[ARENA];                   // subject arena: the cells the C++ touches are native
static int g_fault;
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - g_s); }
void *ms::jobHostPtr(uint32_t a) { return g_s + a; }

struct OMem : Mem {
    uint8_t r8(uint32_t a) override { if(a >= ARENA) { g_fault = 1; return 0; } return g_o[a]; }
    void w8(uint32_t a, uint8_t v) override { if(a >= ARENA) { g_fault = 1; return; } g_o[a] = v; }
};
static OMem g_mem;
void tcallAsm(Regs &, Mem &, unsigned);

static uint32_t g_rng = 12345;
static uint32_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint32_t rnd(uint32_t n) { return rnd() % n; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)(v >> 16)); wr16(p + 2, (uint16_t)v); }
static void sw2(uint8_t *p) { uint8_t t = p[0]; p[0] = p[1]; p[1] = t; }
static void sw4(uint8_t *p) { uint8_t t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }

struct Range { uint32_t lo, n; };
// first difference of the two arenas outside the ignored ranges, or -1
static long firstDiff(const Range *ign, int nign) {
    for(uint32_t a = 0; a < ARENA; ++a) {
        if(g_o[a] == g_s[a]) continue;
        bool skip = false;
        for(int i = 0; i < nign; ++i) if(a >= ign[i].lo && a < ign[i].lo + ign[i].n) { skip = true; break; }
        if(!skip) return (long)a;
    }
    return -1;
}
'''

JOBS_DRIVER = COMMON + r'''
#include "game/mogjobs.hpp"
using namespace ms::game;
extern "C" {
void lab_LAB_0305(Regs &, Mem &); void lab_LAB_0315(Regs &, Mem &); void lab_LAB_0319(Regs &, Mem &);
void lab_LAB_031B(Regs &, Mem &); void lab_LAB_030D(Regs &, Mem &); void lab_LAB_031D(Regs &, Mem &);
void lab_LAB_031F(Regs &, Mem &); void lab_LAB_0161(Regs &, Mem &);
}
static uint32_t g_waitO[8], g_waitS[8]; static int g_nwO, g_nwS;
void tcallAsm(Regs &R, Mem &M, unsigned addr) {
    if(addr == LAB_0D74) { if(g_nwO < 8) g_waitO[g_nwO++] = R.d[0]; return; }
    fprintf(stderr, "tcallAsm: unexpected call %08x\n", addr);
    g_fault = 2;
}

static const uint32_t HEAP = 0x140000, REC = 132;
static const int J_W[] = {6, 8, 10, 12, 14, 16, 18, 48};
static const int J_L[] = {2, 24, 28, 36, 40, 44};
static void convertAll(uint8_t *g) {          // involution: BE <-> native, for the cells the C++ reads natively
    for(int i = 0; i < 10; ++i) {
        uint8_t *j = g + LAB_0649 + 50 * i;
        for(int o : J_W) sw2(j + o);
        for(int o : J_L) sw4(j + o);
    }
    const uint32_t L[] = {LAB_0A4D, LAB_0A4E, LAB_0A4F, LAB_0A50, LAB_0B9D, LAB_0321};
    for(uint32_t a : L) sw4(g + a);
    sw2(g + LAB_05BA);
}

static void cbClearHitLinks() {               // the asm routine on the shared memory: native cells -> BE -> asm -> native
    convertAll(g_s);
    struct SMem : Mem {
        uint8_t r8(uint32_t a) override { if(a >= ARENA) { g_fault = 1; return 0; } return g_s[a]; }
        void w8(uint32_t a, uint8_t v) override { if(a >= ARENA) { g_fault = 1; return; } g_s[a] = v; }
    } sm;
    Regs R; memset(&R, 0, sizeof R); R.a[7] = STACK_TOP - 0x100;
    lab_LAB_0161(R, sm);
    convertAll(g_s);
}
static void cbWait(uint32_t t) { if(g_nwS < 8) g_waitS[g_nwS++] = t; }

static MogJobEnv subjEnv() {
    MogJobEnv e;
    e.pJobs = (CombatJob *)(g_s + LAB_0649);
    e.pWork = g_s + LAB_064B; e.pAttackLists = g_s + LAB_064F; e.pHurtLists = g_s + LAB_0650; e.pDrawBuffer = g_s + LAB_064D;
    e.pHitA = (uint32_t *)(g_s + LAB_0A4D); e.pHitB = (uint32_t *)(g_s + LAB_0A4E);
    e.pHitASaved = (uint32_t *)(g_s + LAB_0A4F); e.pHitBSaved = (uint32_t *)(g_s + LAB_0A50);
    e.pTick = (uint32_t *)(g_s + LAB_0B9D); e.pFrameStart = (uint32_t *)(g_s + LAB_0321);
    e.pFrameBudget = (uint16_t *)(g_s + LAB_05BA);
    e.clearHitLinks = cbClearHitLinks; e.wait = cbWait;
    return e;
}

static uint32_t ja(const CombatJob *p) { return p ? jobAddr(p) : 0; }
static uint32_t g_pool[6];
static void randomArena() {
    for(uint32_t a = 0; a < ARENA; ++a) g_o[a] = 0;
    for(uint32_t a = LAB_0649; a < LAB_0650 + 800; ++a) g_o[a] = (uint8_t)rnd();
    for(uint32_t a = HEAP; a < HEAP + 22 * REC; ++a) g_o[a] = (uint8_t)rnd();
    for(uint32_t a = LAB_0613; a < LAB_0613 + 5 * REC; ++a) g_o[a] = (uint8_t)rnd();
    for(uint32_t a = LAB_0A4D; a < LAB_0A4D + 16; ++a) g_o[a] = (uint8_t)rnd();
    for(uint32_t a = LAB_0B9D; a < LAB_0B9D + 4; ++a) g_o[a] = (uint8_t)rnd();
    for(uint32_t a = LAB_0321; a < LAB_0321 + 4; ++a) g_o[a] = (uint8_t)rnd();
    wr16(g_o + LAB_05BA, (uint16_t)(rnd(4) == 0 ? rnd() : rnd(8)));
    wr32(g_o + LAB_05C3, HEAP);
    for(int i = 0; i < 6; ++i) g_pool[i] = HEAP + REC * rnd(20);
    for(int i = 0; i < 10; ++i) {                 // owners from the pool, valid work pointers
        uint8_t *j = g_o + LAB_0649 + 50 * i;
        wr32(j + 24, rnd(5) == 0 ? 0 : g_pool[rnd(6)]);
        wr32(j + 36, LAB_064B + 36 * rnd(10));
    }
    if(rnd(3) == 0) { uint32_t t = rnd(); wr32(g_o + LAB_0B9D, t); wr32(g_o + LAB_0321, t - rnd(10)); }
    if(rnd(5) == 0) { wr32(g_o + LAB_0B9D, rnd(4)); }                 // counter wrapped past the start
    memcpy(g_s, g_o, ARENA);
    convertAll(g_s);
}

int main(int argc, char **argv) {
    const char *op = argc > 1 ? argv[1] : "";
    int n = argc > 2 ? atoi(argv[2]) : 200;
    for(int c = 0; c < n; ++c) {
        randomArena();
        const MogJobEnv e = subjEnv();
        Regs R; memset(&R, 0, sizeof R); R.a[7] = STACK_TOP - 0x100;
        for(int i = 0; i < 8; ++i) R.d[i] = rnd();
        g_nwO = g_nwS = 0;
        uint32_t owner = rnd(4) == 0 ? rnd() & 0x1FFFFE : g_pool[rnd(6)];
        uint32_t script = rnd();
        uint32_t resO = 0, resS = 0;
        bool hasRes = true;
        if(!strcmp(op, "reset")) {
            lab_LAB_0305(R, g_mem); mogJobsReset(e); hasRes = false;
        } else if(!strcmp(op, "find")) {
            R.d[0] = owner; lab_LAB_0315(R, g_mem); resO = R.d[0];
            resS = ja(mogJobFind(e, owner));
        } else if(!strcmp(op, "toggle")) {
            R.d[0] = owner; lab_LAB_0319(R, g_mem); resO = R.d[0];
            resS = ja(mogJobTogglePause(e, owner));
        } else if(!strcmp(op, "kill")) {
            R.d[0] = owner; lab_LAB_031B(R, g_mem); resO = R.d[0];
            resS = 0; mogJobKill(e, owner);                       // the asm returns D0 = 0 either way
        } else if(!strcmp(op, "restart")) {
            R.d[0] = rnd(); R.a[0] = script; R.a[1] = owner; lab_LAB_030D(R, g_mem); resO = R.d[0];
            resS = ja(mogJobRestart(e, owner, script));
        } else if(!strcmp(op, "framestart")) {
            lab_LAB_031D(R, g_mem); mogFrameStart(e); hasRes = false;
        } else if(!strcmp(op, "framewait")) {
            lab_LAB_031F(R, g_mem); mogFrameWait(e); hasRes = false;
            if(g_nwO != 1 || g_nwS != 1 || g_waitO[0] != g_waitS[0]) {
                printf("FAIL %s case %d: waits oracle %d x %u, subject %d x %u\n", op, c, g_nwO, g_waitO[0], g_nwS, g_waitS[0]);
                return 1;
            }
        } else { printf("unknown op\n"); return 2; }
        convertAll(g_s);
        if(g_fault) { printf("FAIL %s case %d: fault %d\n", op, c, g_fault); return 1; }
        if(hasRes && resO != resS) { printf("FAIL %s case %d: D0 oracle %08x subject %08x\n", op, c, resO, resS); return 1; }
        const Range ign[] = {{STACK_LO, STACK_TOP - STACK_LO + 64}};
        long d = firstDiff(ign, 1);
        if(d >= 0) { printf("FAIL %s case %d: arena differs at %06lx (oracle %02x subject %02x)\n", op, c, d, g_o[d], g_s[d]); return 1; }
    }
    printf("OK %s %d\n", op, n);
    return 0;
}
'''

LOOP_DRIVER = COMMON + r'''
#include "game/mainloop.hpp"
using namespace ms::game;
extern "C" void lab_SECSTRT_0(Regs &, Mem &);
extern "C" void lab_LAB_0001(Regs &, Mem &);
extern "C" void lab_LAB_0064(Regs &, Mem &);

struct Step { uint32_t addr; uint16_t id; };
static const Step kSteps[] = {
@STEPS@
};
static const uint16_t ID_RNG = 0x04A5, ID_MAP = 0x0DAB, ID_LOOP = 0x0001;
struct Ev { uint16_t id; uint32_t d0, a0, dig; };
static Ev g_logO[256], g_logS[256];
static int g_nO, g_nS;
struct Script { uint16_t cursor, players; };
static Script g_script[16];
static int g_nscript, g_iscript;

// both sides write the same cells in the same way: the (be ? oracle : subject) arena
static void put16(bool be, uint32_t a, uint16_t v) { if(be) wr16(g_o + a, v); else *(uint16_t *)(g_s + a) = v; }
static void put32(bool be, uint32_t a, uint32_t v) { if(be) wr32(g_o + a, v); else *(uint32_t *)(g_s + a) = v; }
static void put8(bool be, uint32_t a, uint8_t v) { if(be) g_o[a] = v; else g_s[a] = v; }
static void hook(bool be, uint16_t id) {
    if(id == 0x00B4) {                                   // the menu: the player's choice
        Script s = g_iscript < g_nscript ? g_script[g_iscript] : Script{0, 1};
        ++g_iscript;
        put16(be, LAB_06DC, s.cursor);
        if(s.players) put16(be, LAB_05C5, s.players);      // 0 = the menu leaves the player count alone
    } else if(id == 0x01AE) {                            // the scheduler reset overwrites what the C++ wrote before it
        put16(be, LAB_05E4 + 14, 0x1111);
        put32(be, LAB_05E4 + 0, 0x22223333);
        put32(be, LAB_05E4 + 4, 0x44445555);
        for(int k = 0; k < 2; ++k) {
            put8(be, LAB_0613 + 132 * k + 77, 0x90 + k);
            put8(be, LAB_0613 + 132 * k + 11, 0x98 + k);
            put32(be, LAB_0613 + 132 * k + 54, 0x700 + k);
        }
    }
}

// digest of every cell the C++ writes, as the callee sees it at the moment of the call (so the ORDER of the C++'s writes
// against the asm calls is part of the comparison, not only the final state)
static uint32_t digest(bool be) {
    uint32_t h = 2166136261u;
    auto r16 = [&](uint32_t a) -> uint32_t { return be ? rd16(g_o + a) : *(uint16_t *)(g_s + a); };
    auto r32 = [&](uint32_t a) -> uint32_t { return be ? rd32(g_o + a) : *(uint32_t *)(g_s + a); };
    auto r8 = [&](uint32_t a) -> uint32_t { return be ? g_o[a] : g_s[a]; };
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix(r32(LAB_05BC)); mix(r32(LAB_05BD)); mix(r32(LAB_05BE)); mix(r32(LAB_05BF)); mix(r32(LAB_08C4));
    mix(r16(0x7F684)); mix(r16(0x7F682)); mix(r16(LAB_05C5)); mix(r16(LAB_05DB)); mix(r16(LAB_06DC));
    mix(r32(LAB_05E4)); mix(r32(LAB_05E4 + 4)); mix(r16(LAB_05E4 + 14));
    for(int k = 0; k < 2; ++k) { const uint32_t K = LAB_0613 + 132 * k; mix(r8(K + 77)); mix(r8(K + 11)); mix(r32(K + 54)); }
    return h;
}

void tcallAsm(Regs &R, Mem &M, unsigned addr) {
    if(addr == LAB_0001) { lab_LAB_0001(R, M); return; }       // JMP / fall through into the loop: real code
    if(addr == LAB_020F) return;                               // a bare RTS since 7.1h: the C++ has no step for it (7.1l)
    uint16_t id = 0;
    if(addr == LAB_04A5) id = ID_RNG;
    else if(addr == LAB_0DAB) id = ID_MAP;
    else { for(const Step &s : kSteps) if(s.addr == addr) id = s.id; }
    if(!id) { fprintf(stderr, "tcallAsm: unknown routine %08x\n", addr); g_fault = 2; return; }
    if(g_nO < 256) g_logO[g_nO++] = Ev{id, R.d[0], R.a[0], digest(true)};
    hook(true, id);
}

static void sStep(void *, MainStep s) {
    if(g_nS < 256) g_logS[g_nS++] = Ev{(uint16_t)s, 0, 0, digest(false)};
    hook(false, (uint16_t)s);
}
static void sMap(void *) { if(g_nS < 256) g_logS[g_nS++] = Ev{ID_MAP, 0, 0, digest(false)}; }

static MainEnv subjEnv() {
    MainEnv e;
    e.pChipFree = (uint32_t *)(g_s + LAB_05BC); e.pChipSize = (uint32_t *)(g_s + LAB_05BD);
    e.pFastFree = (uint32_t *)(g_s + LAB_05BE); e.pFastSize = (uint32_t *)(g_s + LAB_05BF);
    e.pTextP1 = (uint16_t *)(g_s + 0x7F684); e.pTextP0 = (uint16_t *)(g_s + 0x7F682);
    e.pPlayers = (uint16_t *)(g_s + LAB_05C5); e.pPlayersSaved = (uint16_t *)(g_s + LAB_05DB);
    e.pMenuCursor = (uint16_t *)(g_s + LAB_06DC);
    e.pActive = (ActiveKnights *)(g_s + LAB_05E4); e.pKnights = (Knight *)(g_s + LAB_0613);
    e.pArena = (uint32_t *)(g_s + LAB_08C4);
    return e;
}

static void randomArena() {
    for(uint32_t a = 0; a < ARENA; ++a) g_o[a] = 0;
    for(uint32_t a = 0x7F000; a < 0x80000; ++a) g_o[a] = (uint8_t)rnd();
    for(uint32_t a = 0x100000; a < 0x140000; ++a) g_o[a] = (uint8_t)rnd();
    memcpy(g_s, g_o, ARENA);
    // the cells the C++ reads and writes natively hold the same logical values as the oracle's
    const uint32_t L[] = {LAB_05BC, LAB_05BD, LAB_05BE, LAB_05BF, LAB_08C4, LAB_05E4, LAB_05E4 + 4, LAB_0613 + 54, LAB_0613 + 132 + 54};
    for(uint32_t a : L) sw4(g_s + a);
    const uint32_t W[] = {0x7F684, 0x7F682, LAB_05C5, LAB_05DB, LAB_06DC, LAB_05E4 + 14};
    for(uint32_t a : W) sw2(g_s + a);
}

// value of a cell on each side
static bool eq16(uint32_t a) { return rd16(g_o + a) == *(uint16_t *)(g_s + a); }
static bool eq32(uint32_t a) { return rd32(g_o + a) == *(uint32_t *)(g_s + a); }

static int check(const char *what, int c) {
    if(g_fault) { printf("FAIL %s case %d: fault %d\n", what, c, g_fault); return 1; }
    int nO = g_nO, i0 = 0;
    if(!strcmp(what, "boot")) {                           // LAB_04A5 first (the audio patch's JSR keeps it out of the C++)
        if(g_logO[0].id != ID_RNG) { printf("FAIL %s case %d: first call is not LAB_04A5\n", what, c); return 1; }
        i0 = 1;
    }
    if(nO - i0 != g_nS) { printf("FAIL %s case %d: %d calls in the oracle, %d in the C++\n", what, c, nO - i0, g_nS); return 1; }
    for(int i = 0; i < g_nS; ++i) {
        if(g_logO[i0 + i].id != g_logS[i].id) {
            printf("FAIL %s case %d: call %d is %04x in the oracle, %04x in the C++\n", what, c, i, g_logO[i0 + i].id, g_logS[i].id);
            return 1;
        }
        if(g_logO[i0 + i].dig != g_logS[i].dig) {
            printf("FAIL %s case %d: the cells differ at call %d (%04x)\n", what, c, i, g_logS[i].id);
            return 1;
        }
    }
    // cells kept natively on the subject side: compare by value, then ignore their bytes in the raw diff
    if(!eq32(LAB_05BC) || !eq32(LAB_05BD) || !eq32(LAB_05BE) || !eq32(LAB_05BF) || !eq32(LAB_08C4)) { printf("FAIL %s case %d: loader cells / arena\n", what, c); return 1; }
    if(!eq16(0x7F684) || !eq16(0x7F682) || !eq16(LAB_05C5) || !eq16(LAB_05DB) || !eq16(LAB_06DC)) { printf("FAIL %s case %d: word cells\n", what, c); return 1; }
    if(!eq32(LAB_05E4 + 0) || !eq32(LAB_05E4 + 4) || !eq16(LAB_05E4 + 14)) { printf("FAIL %s case %d: LAB_05E4\n", what, c); return 1; }
    for(int k = 0; k < 2; ++k) {
        const uint32_t K = LAB_0613 + 132 * k;
        if(g_o[K + 77] != g_s[K + 77] || g_o[K + 11] != g_s[K + 11] || !eq32(K + 54)) { printf("FAIL %s case %d: knight %d\n", what, c, k); return 1; }
    }
    Range ign[32]; int n = 0;
    const uint32_t cells[][2] = {{LAB_05BC, 16}, {0x7F682, 4}, {LAB_05C5, 2}, {LAB_05DB, 2}, {LAB_06DC, 2}, {LAB_08C4, 4},
        {LAB_05E4, 8}, {LAB_05E4 + 14, 2}, {LAB_0613 + 11, 1}, {LAB_0613 + 54, 4}, {LAB_0613 + 77, 1},
        {LAB_0613 + 132 + 11, 1}, {LAB_0613 + 132 + 54, 4}, {LAB_0613 + 132 + 77, 1}, {STACK_LO, STACK_TOP - STACK_LO + 64}};
    for(auto &r : cells) ign[n++] = Range{r[0], r[1]};
    long d = firstDiff(ign, n);
    if(d >= 0) { printf("FAIL %s case %d: arena differs at %06lx (oracle %02x subject %02x)\n", what, c, d, g_o[d], g_s[d]); return 1; }
    return 0;
}

static void randomScript() {
    g_nscript = (int)rnd(5);
    for(int i = 0; i < g_nscript; ++i) g_script[i] = Script{(uint16_t)(rnd(3) == 0 ? rnd(4) : 2), (uint16_t)rnd(5)};
    if(g_nscript && rnd(2)) g_script[g_nscript - 1].cursor = 3;
    g_iscript = 0;
}

int main(int argc, char **argv) {
    const char *op = argc > 1 ? argv[1] : "";
    int n = argc > 2 ? atoi(argv[2]) : 100;
    if(!strcmp(op, "log")) {                              // one boot run, print the oracle's calls with their registers
        n = 1;
    }
    for(int c = 0; c < n; ++c) {
        randomArena();
        randomScript();
        g_nO = g_nS = 0; g_iscript = 0;
        const MainEnv e = subjEnv();
        static const MainOps ops = {nullptr, sStep, sMap};
        Regs R; memset(&R, 0, sizeof R); R.a[7] = STACK_TOP - 0x100;
        if(!strcmp(op, "boot") || !strcmp(op, "log")) {
            if(!strcmp(op, "log")) { g_nscript = 3; g_script[0] = Script{2, 3}; g_script[1] = Script{0, 1}; g_script[2] = Script{2, 2}; g_script[1].cursor = 3; }
            for(int i = 0; i < 8; ++i) R.d[i] = rnd();
            for(int i = 0; i < 7; ++i) R.a[i] = rnd();
            const BootRegs b = {R.a[1], R.d[1], R.a[0], R.d[0]};
            lab_SECSTRT_0(R, g_mem);
            g_iscript = 0;
            mainBoot(e, ops, b);
            mainRun(MAINSCENE_TITLE, e, ops);
            if(!strcmp(op, "log")) {
                for(int i = 0; i < g_nO; ++i) printf("ORACLE %04x %08x %08x\n", g_logO[i].id, g_logO[i].d0, g_logO[i].a0);
            }
        } else if(!strcmp(op, "quit")) {
            lab_LAB_0064(R, g_mem);
            g_iscript = 0;
            mainRun(MAINSCENE_QUIT, e, ops);
        } else if(!strcmp(op, "loop")) {                  // LAB_0001 entered directly (a stray JMP LAB_0001)
            lab_LAB_0001(R, g_mem);
            g_iscript = 0;
            mainRun(MAINSCENE_TITLE, e, ops);
        } else { printf("unknown op\n"); return 2; }
        if(check(!strcmp(op, "log") ? "boot" : op, c)) return 1;
    }
    printf("OK %s %d\n", op, n);
    return 0;
}
'''


_EXE = {}


def build(name, driver, sources, subs=None):
    if name in _EXE and os.path.exists(_EXE[name]):
        return _EXE[name]
    ensure_lifted()
    tmp = tempfile.mkdtemp(prefix='mainloop_')
    src = driver
    for k, v in (subs or {}).items():
        src = src.replace('@%s@' % k, v)
    assert not re.search(r'@\w+@', src)
    drv = os.path.join(tmp, 'driver.cpp')
    open(drv, 'w').write(src)
    open(os.path.join(tmp, 'tcall.hpp'), 'w').write('#pragma once\n#include "ms/regs.hpp"\nvoid tcallAsm(ms::Regs &, ms::Mem &, unsigned);\n')
    exe = os.path.join(tmp, 'driver.exe' if os.name == 'nt' else 'driver')
    cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '-fno-exceptions',
           '-fno-rtti', '-w', '-include', os.path.join(tmp, 'tcall.hpp'), '-I', os.path.join(ROOT, 'include'), '-I', tmp,
           drv] + sources + ['-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('driver compile failed:\n%s\n%s' % (' '.join(cmd), r.stderr[-4000:]))
    _EXE[name] = exe
    return exe


def jobs_exe():
    ensure_lifted()
    labs = ['LAB_031F'] + LIFT_REPO
    # LAB_0161 / LAB_03A7 / LAB_03C7 come from src/lifted unchanged; the ones that call out are rewritten above only
    # when they live in WORK_DIR/gen.  The repo files call each other directly, so they are used as they are.
    files = [lifted_path(l) for l in labs]
    # lab_031f in gen calls tcallAsm; the repo ones never leave the lifted set
    return build('jobs', JOBS_DRIVER, [os.path.join(ROOT, 'src', 'game', 'mogjobs.cpp')] + files)


def loop_exe():
    ensure_lifted()
    steps = parse_steps()
    rows = []
    for name, (val, lab) in sorted(steps.items()):
        rows.append('    {ms::sym_mog::%s, 0x%04X},' % (lab, val))
    files = [lifted_path(l) for l in ('SECSTRT_0', 'LAB_0001', 'LAB_0064')]
    return build('loop', LOOP_DRIVER, flow_lib.flow_sources() + files,   # mainloop.cpp + the flow scenes it runs (ROADMAP 9.2a)
                 {'STEPS': '\n'.join(rows)})


def run(exe, op, n):
    r = subprocess.run([exe, op, str(n)], capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr)


@unittest.skipUnless(CXX and HAVE_INPUTS, 'needs clang++ and the generated include/ms/gen/mog_syms.hpp')
class MogJobsTest(unittest.TestCase):
    def check(self, op, n=300):
        rc, out = run(jobs_exe(), op, n)
        self.assertEqual(rc, 0, out)
        self.assertIn('OK %s' % op, out)

    def test_reset(self):
        self.check('reset')

    def test_find(self):
        self.check('find', 1000)

    def test_toggle_pause(self):
        self.check('toggle', 1000)

    def test_kill(self):
        self.check('kill', 1000)

    def test_restart(self):
        self.check('restart', 1000)

    def test_frame_start(self):
        self.check('framestart')

    def test_frame_wait(self):
        self.check('framewait', 1000)


@unittest.skipUnless(CXX and HAVE_INPUTS, 'needs clang++ and the generated include/ms/gen/mog_syms.hpp')
class MainLoopTest(unittest.TestCase):
    def check(self, op, n=300):
        rc, out = run(loop_exe(), op, n)
        self.assertEqual(rc, 0, out)
        self.assertIn('OK %s' % op, out)

    def test_boot_and_loop(self):
        self.check('boot')

    def test_quit_scene(self):
        self.check('quit')

    def test_loop_entry(self):
        self.check('loop')

    def test_shim_step_table_matches_the_oracle(self):
        """Every row of g_steps (src/rt/mainloop.cpp) calls the routine of its MainStep with the registers the original
        loaded: D0 / A0 as the lifted code had them at each call that takes an argument."""
        steps, rows = parse_steps(), parse_step_table()
        self.assertEqual(parse_cpp_steps(), CPP_STEPS)
        self.assertEqual(sorted(set(steps) - CPP_STEPS), sorted(rows), 'the shim table must have one row per MainStep that is not C++')
        for name, (fn, d0, a0) in rows.items():
            self.assertEqual(fn, steps[name][1], name)
            self.assertIn(fn, SYM)
            if a0:
                self.assertIn(a0, SYM)
        rc, out = run(loop_exe(), 'log', 1)
        self.assertEqual(rc, 0, out)
        by_val = {v[0]: k for k, v in steps.items()}
        seen = set()
        for ln in out.split('\n'):
            if not ln.startswith('ORACLE'):
                continue
            _, sid, d0, a0 = ln.split()
            sid, d0, a0 = int(sid, 16), int(d0, 16), int(a0, 16)
            if sid not in by_val:
                continue
            name = by_val[sid]
            seen.add(name)
            if name in CPP_STEPS:
                continue
            _, want_d0, want_a0 = rows[name]
            if name.endswith('_D2'):
                self.assertEqual(d0, want_d0, name)
            if want_a0:
                self.assertEqual(a0, SYM[want_a0], name)
        # the 'log' run walks boot + practice + campaign, so every step is seen, except the quit scene's
        quit_only = {'LAB_0137_06E6', 'LAB_00EC', 'LAB_0DC8'}
        self.assertEqual(set(steps) - seen - quit_only, set())


@unittest.skipUnless(HAVE_INPUTS, 'needs include/ms/gen/mog_syms.hpp')
class PatchFileTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import importlib.util
        spec = importlib.util.spec_from_file_location('ms_resource', os.path.join(ROOT, 'tools', 'resource.py'))
        cls.res = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.res)
        cls.mine = json.load(open(PATCH_FILE))['patches']
        cls.others = []
        for f in sorted(glob.glob(os.path.join(ROOT, 'asm', 'patches', 'mog*.json'))):
            if os.path.abspath(f) != os.path.abspath(PATCH_FILE):
                cls.others += json.load(open(f))['patches']

    def test_no_overlap_with_any_other_mog_patch(self):
        res = self.res
        taken = [res.patch_lines(p) + (p['id'],) for p in self.others]
        for p in self.mine:
            a, b = res.patch_lines(p)
            for lo, hi, pid in taken:
                self.assertFalse(a <= hi and lo <= b, '%s (%d-%d) overlaps %s (%d-%d)' % (p['id'], a, b, pid, lo, hi))
        ids = [p['id'] for p in self.mine] + [p['id'] for p in self.others]
        self.assertEqual(len(ids), len(set(ids)), 'patch ids must be unique across the mog tables')

    def test_mine_do_not_overlap_each_other(self):
        res = self.res
        spans = sorted(res.patch_lines(p) for p in self.mine)
        for (a, b), (c, d) in zip(spans, spans[1:]):
            self.assertLess(b, c)

    def test_original_text_matches_mog_asm(self):
        res = self.res
        raw = open(MOG_ASM, encoding='latin-1').read().split('\n')
        res.check_patches('mog', sorted(self.mine, key=lambda p: p['line']), raw)

    def test_replacements_are_jumps_to_the_new_entries(self):
        for p in self.mine:
            self.assertEqual(len(p['new']), 1, p['id'])
            m = re.match(r'\tJMP\t(rt_\w+)$', p['new'][0])
            self.assertTrue(m, p['id'])
            self.assertIn(m.group(1), RT_NAMES)
        # 7.1 cleanup: rt_mog_again / quit / job_find / kill / restart lost their (dead) patches; their entries stay for the emulator tests
        live = [n for n in RT_NAMES if n not in ('rt_mog_quit',
                                           # 7.1o: stub_cutover retired the patches (the C++ calls the entries directly; 7.1q: rt_mog_frame_wait)
                                           'rt_mog_jobs_reset', 'rt_mog_job_toggle', 'rt_mog_frame_start', 'rt_mog_frame_wait')]
        self.assertEqual(sorted(re.match(r'\tJMP\t(rt_\w+)$', p['new'][0]).group(1) for p in self.mine), sorted(live))

    def test_every_rt_name_is_defined_in_the_shim(self):
        text = open(RT_SRC, encoding='utf-8').read()
        for n in RT_NAMES:
            self.assertRegex(text, r'\.globl %s\n%s:' % (n, n))
        support = open(SUPPORT_SRC, encoding='utf-8').read()
        for n in TEST_ENTRY_NAMES:
            self.assertRegex(support, r'\.globl %s\n%s:' % (n, n))
            self.assertNotIn('.globl %s\n' % n, text)

    @unittest.skipUnless(os.path.exists(os.path.join(ROOT, 'build', 'reasm', 'mog.lst')), 'run tools/reassemble.py first')
    def test_resource_verify_on_a_scratch_copy_of_the_tables(self):
        """tools/resource.py --verify for mog with the new patch file and the rt_* names added to a COPY of abs_symbols.json
        (the real table is not edited by this task): load-equivalent modulo the patch ranges."""
        res = self.res
        tmp = tempfile.mkdtemp(prefix='mainloop_patches_')
        for f in glob.glob(os.path.join(ROOT, 'asm', 'patches', '*.json')):
            shutil.copy(f, tmp)
        p = os.path.join(tmp, 'abs_symbols.json')
        d = json.load(open(p))
        have = {f['name'] if isinstance(f, dict) else f for f in d['funcs']}
        for n in RT_NAMES:
            if n not in have:
                d['funcs'].append({'name': n, 'impl': 'src/rt/mainloop.cpp'})
        json.dump(d, open(p, 'w'), indent=1)
        old = res.PATCH_DIR
        res.PATCH_DIR = tmp
        try:
            import contextlib
            import io
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                ok = res.verify_one('mog')
            self.assertTrue(ok, buf.getvalue()[-2000:])
            self.assertIn('mainloop-boot', buf.getvalue())
        finally:
            res.PATCH_DIR = old
            shutil.rmtree(tmp, ignore_errors=True)


def tool(name):
    p = shutil.which(name)
    if p:
        return p
    cand = os.path.join(ROOT, 'tools', 'toolchain', 'opt', 'bin', name + ('.exe' if os.name == 'nt' else ''))
    if os.path.exists(cand):
        return cand
    cand = os.path.join(os.path.dirname(ROOT), 'tools', 'amiga-debug', 'extension', 'bin', 'win32', 'opt', 'bin',
                        name + ('.exe' if os.name == 'nt' else ''))
    return cand if os.path.exists(cand) else None


GXX = tool('m68k-amiga-elf-g++')
ACE = os.path.join(ROOT, 'ace', 'include') if os.path.isdir(os.path.join(ROOT, 'ace', 'include')) else os.path.join(os.path.dirname(ROOT), 'ace', 'include')   # ./ace: the public repository's submodule
GCC_SUPPORT = os.path.join(ROOT, 'tools', 'bartman_gcc_support', 'include')


@unittest.skipUnless(GXX and os.path.isdir(ACE) and os.path.isdir(GCC_SUPPORT), 'needs m68k-amiga-elf-g++ (AGENTS.md toolchain)')
class ShimCompileTest(unittest.TestCase):
    def test_modules_compile_for_the_target(self):
        env = dict(os.environ, PATH=os.path.dirname(GXX) + os.pathsep + os.environ.get('PATH', ''))
        flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti',
                 '-fno-threadsafe-statics', '-std=c++17', '-O2', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1', '-Wall',
                 '-Wextra', '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'),
                 '-I', os.path.join(ACE, 'mini_std'), '-I', ACE, '-I', GCC_SUPPORT]
        tmp = tempfile.mkdtemp(prefix='mainloop_m68k_')
        for src in ('src/game/mogjobs.cpp', 'src/game/mainloop.cpp', 'src/rt/mainloop.cpp', 'src/game/overworld.cpp', 'src/game/rules/ai_map.cpp',
                    'src/rt/overworld.cpp', 'src/game/placevisit.cpp'):
            o = os.path.join(tmp, os.path.basename(src) + '.o')
            r = subprocess.run([GXX] + flags + ['-c', os.path.join(ROOT, src), '-o', o], capture_output=True, text=True, env=env)
            self.assertEqual(r.returncode, 0, src + '\n' + r.stderr[-3000:])
            self.assertEqual(r.stderr.strip(), '', src)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
