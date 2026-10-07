"""Test for src/engine/scenes.cpp (ROADMAP 7.1i): the intro/ending scenes, the frame loop and the S_8 loaders of program.asm.

The ORIGINAL asm (the reassembled, unpatched program image, build/reasm/program) runs in unicorn; every routine the scenes
call that is not part of the ported code (display, palette, disk, job layer, music; list SPEC below) is hooked at its entry:
the hook records the call with the registers that routine reads, applies the same small stand-in effect as the C++ host below
(frame counter, the stop flag LAB_0120 after K job ticks, LAB_05E7 after the Nth loader step, deterministic return values) and
returns. Stores to the game cells and palette tables the engine writes are recorded from the memory-write hook. The C++ engine
(src/engine/scenes.cpp, host clang++) runs the same scenario against a mock host over a copy of the same memory image and
prints the same trace. The two traces must be identical, call for call and store for store: that pins the order, the arguments
and the timing contract (frame waits, music/palette sync points) of every scene.

ROADMAP 7.1q: the loader's progress screens (asm LAB_059E..LAB_05AC), the wipe-in LAB_05A5 and the small operations LAB_025F, LAB_0260,
LAB_0262, LAB_0263 are engine code now. Each is run from its own asm label in unicorn (the hook on that label is switched off, its
callees stay hooked) next to the C++ body, and the traces must match. The compiled (m68k) test runs the scene bodies and the loaders
with those operations expanded on both sides: the asm hooks of the operations the engine implements are off, so the asm executes them
and the compiled engine does the same through its host calls.

Also checked: the table of routine names against the SceneFn enum (so a new fn cannot slip in unspecified), the flash palette
words against the asm data, and the patch file (no overlap, only the expected sites).

Needs: unicorn, clang++ on PATH, build/reasm/program (py tools/reassemble.py). Skipped otherwise.
"""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import cellnames as CN  # noqa: E402  (ROADMAP 7.1s)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'diffharness'))
CXX = shutil.which('clang++')
EXE = os.path.join(ROOT, 'build', 'reasm', 'program')

try:
    import harness as H
    from unicorn import Uc, UcError, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ
    from unicorn import m68k_const as M
    HAVE_UC = True
except Exception:  # pragma: no cover - optional dependency
    HAVE_UC = False

NEEDS = HAVE_UC and CXX and os.path.exists(EXE)

# SceneFn name -> (asm label, registers the routine reads; 'name' = the string A0 points at, 'd1w' = low word of D1).
SPEC = {
    'FnJobsReset': ('LAB_01E4', ''), 'FnJobsSpawn': ('LAB_01E8', ''), 'FnJobsTick': ('LAB_01EC', ''),
    'FnFlip': ('LAB_0262', ''), 'FnSpriteBlit': ('LAB_0242', ''), 'FnBlackout': ('LAB_0258', ''),
    'FnPrepare': ('LAB_0263', ''), 'FnFade': ('LAB_025F', ''), 'FnWait': ('LAB_054F', 'd0'),
    'FnUnpack': ('LAB_0268+2', 'a0 a1'), 'FnOverlay': ('LAB_003C', ''), 'FnPalFade': ('LAB_0576', 'a0 d0'),
    'FnPalWrite': ('LAB_0565', 'a0'), 'FnPalCopy': ('LAB_025D', 'a0'), 'FnPalSet': ('LAB_0260', 'a0'),
    'FnPalLoad': ('LAB_025B', 'a0'), 'FnClear': ('LAB_054D', 'a0'), 'FnTarget': ('LAB_026C', 'd0'),
    'FnRamp': ('LAB_057A', 'd0 d1w d2w d3'), 'FnRampSet': ('LAB_0579', 'd0'), 'FnMusic': ('SECSTRT_1', ''),
    'FnText': ('LAB_028F', 'a0'), 'FnLoadingA': ('LAB_05AB', ''), 'FnLoadingB': ('LAB_05AC', ''),
    'FnLoad059E': ('LAB_059E', ''), 'FnLoad059F': ('LAB_059F', ''), 'FnLoad05A0': ('LAB_05A0', ''),
    'FnLoad05A1': ('LAB_05A1', ''), 'FnPicFile': ('LAB_0402', 'name a1'), 'FnFileOpen': ('LAB_0390', 'name'),
    'FnFileRead': ('LAB_03B2', 'a0 d0'), 'FnFileClose': ('LAB_03DA', ''), 'FnCelLoad': ('LAB_0496', 'name a1'),
    'FnCelSize': ('LAB_0491', 'name'), 'FnRnc': ('LAB_0190', 'a0'), 'FnPicMem': ('LAB_03FC', 'a0'),
    'FnCopy': ('LAB_0056', 'd0w a0 a1'),   # the inline copy loop of LAB_0054: hooked at its head, skipped as a whole
    'FnSwap': ('LAB_054C', ''), 'FnKeyReset': ('LAB_035E', ''), 'FnListClear': ('LAB_024B', ''),
    'FnDrawCel': ('LAB_04B4', 'd0 d1w d2w a0'), 'FnWipeStep': ('LAB_05B2', 'd1w'), 'FnWipeRows': ('LAB_05BA', ''),
    'FnWipeRefresh': ('LAB_05B7', ''), 'FnScreenCopy': ('LAB_0264', 'a0 a1'),
}
# the operations the engine implements itself (ms::sceneFade ... ms::sceneLoadingB): hooked in the mock-host tests, expanded in the
# compiled ones
ENGINE_OPS = ('FnFade', 'FnPalSet', 'FnFlip', 'FnPrepare', 'FnLoad059E', 'FnLoad059F', 'FnLoad05A0', 'FnLoad05A1', 'FnLoadingA',
              'FnLoadingB')
LOAD_FNS = ('FnLoad059E', 'FnLoad059F', 'FnLoad05A0', 'FnLoad05A1')

# cells and data blocks of the C++ host (id = LAB number; SECSTRT_n = 0x8000 + n), name in the asm
IDS = ('0005 0026 003E 0033 0034 0035 0045 00C2 00C4 00C6 00C7 00C8 00C9 00CA 00CB 00CC 00CD 00CE 00CF 00D0 00D1 00EF 00F0 '
       '011D 011E 011F 0120 0121 0122 0123 0124 0379 056C 05D2 05E7 01C9 01CA 00B0 01CB 01CC 01CD 01CE 01CF 01D0 01D1 01D2 '
       '026D 0276 0506 0042 0043 0028 011A 0261 04DF 05B0 05B8 05BC 05BD 05D6 05D7 05E6 0279 027A 027C 00FD 00FF 0102 0105 010A '
       '0107').split()
SCRIPTS = ('00D2 00D3 00D4 00D5 00D6 00D7 00D8 00D9 00DA 00DB 00DC 00DD 00DE 00DF 00E0 00E1 00E3 00E4 00E5 00E6 00E7 00E8 '
           '00E9 00EA 00EB 00EC 00ED 00EE').split()

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/scenes.hpp"
using namespace ms;

static uint8_t *g_mem, *g_pristine; static size_t g_size;
static uint32_t g_base;
static uint32_t symId[600], symAddr[600]; static int nSym;
static int g_k, g_abort, g_tickCount, g_loadCount, g_keyReads;

static uint32_t addrOf(uint16_t id) {
    for(int i = 0; i < nSym; ++i) if(symId[i] == id) return symAddr[i];
    printf("BADID %04X\n", id); exit(2);
}
static uint32_t rd(uint32_t a, int bits) {
    uint32_t v = 0; for(int i = 0; i < bits / 8; ++i) v = (v << 8) | g_mem[a - g_base + i]; return v;
}
static void wr(uint32_t a, int bits, uint32_t v) {
    for(int i = bits / 8 - 1; i >= 0; --i) { g_mem[a - g_base + i] = (uint8_t)v; v >>= 8; }
}
static const uint16_t kTicks = 0x0379, kStop = 0x0120, kAbort = 0x05E7;

static uint32_t hCall(void *, SceneFn fn, const SceneRegs &r) {
    printf("C %d %x %x %x %x %x %x %s\n", (int)fn, r.d0, r.d1, r.d2, r.d3, r.a0, r.a1, r.name ? r.name : "-");
    uint32_t ret = 0;
    uint32_t t = rd(addrOf(kTicks), 32);
    switch(fn) {
        case FnJobsTick:
            ++g_tickCount; wr(addrOf(kTicks), 32, t + 2);
            if(g_k && g_tickCount % g_k == 0) wr(addrOf(kStop), 16, 1);
            break;
        case FnFlip: case FnWait: case FnSwap: wr(addrOf(kTicks), 32, t + 3); break;
        case FnWipeStep: {   // the wipe moves its progress by the step (clamped), as rt::wipeStep
            uint32_t a = addrOf(0x05B8); int step = (int)rd(a + 2, 16); int pr = (int16_t)rd(a, 16);
            if(r.d1 & 4) { pr += step; if(pr > 0x3E8) pr = 0x3E8; }
            else if(r.d1 & 8) { pr -= step; if(pr < 0) pr = 0; }
            wr(a, 16, (uint32_t)pr); break;
        }
        case FnLoad059E: case FnLoad059F: case FnLoad05A0: case FnLoad05A1:
            ++g_loadCount; if(g_abort && g_loadCount == g_abort) wr(addrOf(kAbort), 16, 1);
            break;
        case FnCelSize: { uint32_t s = 0; for(const char *p = r.name; *p; ++p) s += (uint8_t)*p; ret = 0x1000 + 2 * s; break; }
        case FnRamp: ret = 0x5000 + r.d0 + (r.d1 & 0xffff); break;
        default: break;
    }
    return ret;
}
static uint32_t hGet(void *, uint16_t id, uint8_t bits) {
    if(id == 0x7000) return 0x9c40;
    if(id == 0x7001) return 0x10e6;
    if(id == 0x8010 && g_abort) {          // the key flag: the g_abort-th read finds a key pressed (both sides count the reads)
        if(++g_keyReads == g_abort) wr(addrOf(id), 16, 1);
    }
    return rd(addrOf(id), bits);
}
static void hSet(void *, uint16_t id, uint8_t bits, uint32_t v) {
    printf("W %d %x %x\n", bits, addrOf(id), bits == 16 ? (v & 0xffff) : v); wr(addrOf(id), bits, v);
}
static uint32_t hAddr(void *, uint16_t id) { return addrOf(id); }
static uint32_t hPeek(void *, uint32_t a, uint8_t bits) { return rd(a, bits); }
static void hPoke(void *, uint32_t a, uint8_t bits, uint32_t v) {
    printf("W %d %x %x\n", bits, a, bits == 16 ? (v & 0xffff) : v); wr(a, bits, v);
}
static bool hSpawn(void *, AnimKind k, uint16_t id) { printf("S %c %04X\n", k == AnimKindA ? 'A' : 'B', id); return true; }

int main() {
    static char line[2000000];
    SceneHost h = {nullptr, hCall, hGet, hSet, hAddr, hPeek, hPoke, hSpawn};
    while(fgets(line, sizeof line, stdin)) {
        char cmd[16];
        if(sscanf(line, "%15s", cmd) != 1) continue;
        if(!strcmp(cmd, "BASE")) { unsigned b; sscanf(line + 5, "%x", &b); g_base = b; }
        else if(!strcmp(cmd, "SYM")) { unsigned i, a; sscanf(line + 4, "%x %x", &i, &a); symId[nSym] = i; symAddr[nSym++] = a; }
        else if(!strcmp(cmd, "MEM")) {
            size_t n = strlen(line + 4); n = (n - 1) / 2; g_size = n; g_pristine = (uint8_t *)malloc(n); g_mem = (uint8_t *)malloc(n);
            for(size_t i = 0; i < n; ++i) { unsigned v; sscanf(line + 4 + 2 * i, "%2x", &v); g_pristine[i] = (uint8_t)v; }
        }
        else if(!strcmp(cmd, "INIT")) { memcpy(g_mem, g_pristine, g_size); g_tickCount = g_loadCount = g_keyReads = 0; }
        else if(!strcmp(cmd, "SET")) { unsigned b, a, v; sscanf(line + 4, "%u %x %x", &b, &a, &v); wr(a, b, v); }
        else if(!strcmp(cmd, "PARAM")) { sscanf(line + 6, "%d %d", &g_k, &g_abort); }
        else if(!strcmp(cmd, "RUN")) {
            char kind[32]; unsigned arg = 0; sscanf(line + 4, "%31s %x", kind, &arg);
            if(!strcmp(kind, "scene")) { if(!sceneRunLabel(h, (uint16_t)arg)) printf("NOSCENE\n"); }
            else if(!strcmp(kind, "frameloop")) sceneFrameLoop(h);
            else if(!strcmp(kind, "frameloopn")) sceneFrameLoopN(h, (uint16_t)arg);
            else if(!strcmp(kind, "stage")) sceneStageSetup(h);
            else if(!strcmp(kind, "ramp")) sceneRampSetup(h);
            else if(!strcmp(kind, "flash")) sceneFlash(h);
            else if(!strcmp(kind, "flashseries")) sceneFlashSeries(h);
            else if(!strcmp(kind, "wait")) sceneFrameWait(h);
            else if(!strcmp(kind, "stamp")) sceneFrameStamp(h);
            else if(!strcmp(kind, "caption")) sceneCaption(h, arg);
            else if(!strcmp(kind, "message")) sceneLoadMessage(h);
            else if(!strcmp(kind, "fade")) sceneFade(h);
            else if(!strcmp(kind, "palset")) scenePalSet(h, arg);
            else if(!strcmp(kind, "flip")) sceneFlip(h);
            else if(!strcmp(kind, "prepare")) scenePrepare(h);
            else if(!strcmp(kind, "loadstep")) sceneLoadStep(h, (uint8_t)arg);
            else if(!strcmp(kind, "progress")) sceneProgressWipe(h);
            else if(!strcmp(kind, "loadinga")) sceneLoadingA(h);
            else if(!strcmp(kind, "loadingb")) sceneLoadingB(h);
            printf("END\n"); fflush(stdout);
        }
    }
    return 0;
}
'''


def fn_names():
    """The SceneFn enumerators of include/engine/scenes.hpp in order (without FnFnCount)."""
    with open(os.path.join(ROOT, 'include', 'engine', 'scenes.hpp'), encoding='latin-1') as f:
        text = f.read()
    body = text[text.index('enum SceneFn'):text.index('FnFnCount')]
    return re.findall(r'\b(Fn\w+),', body)


class Rig:
    """The asm side (unicorn) and the C++ side (driver process), fed the same scenario."""

    STACK, STACK_SIZE, SENTINEL = H.STACK_BASE, H.STACK_SIZE, H.SENTINEL

    def __init__(self):
        self.h = H.Harness('program')
        self.image = self.h.image
        self.base = H.IMAGE_BASE
        self.fns = fn_names()
        self.addr = {k: self.h.address(k) for k in self.h.symbols}
        self.id_addr = {}
        for i in IDS:
            self.id_addr[int(i, 16)] = self.addr['LAB_' + i]
        for i in SCRIPTS:
            self.id_addr[int(i, 16)] = self.addr['LAB_' + i]
        for n in (9, 16, 30, 33):
            self.id_addr[0x8000 + n] = self.addr['SECSTRT_%d' % n]
        self.disabled = set()      # SceneFn names whose asm hook is off (the asm then runs that routine's own body)
        self.key_reads = 0
        self.script_by_addr = {self.addr['LAB_' + i]: i for i in SCRIPTS}
        # asm hook table: address -> (fn name, regs)
        self.hooks = {}
        for name, (lab, regs) in SPEC.items():
            if '+' in lab:
                a = self.addr[lab.split('+')[0]] + int(lab.split('+')[1])
            else:
                a = self.addr[lab]
            self.hooks[a] = (name, regs)
        self.hooks[self.addr['LAB_0015']] = ('SPAWN_A', '')
        self.hooks[self.addr['LAB_0016']] = ('SPAWN_B', '')
        # stores the engine makes (both sides record them): (address, size)
        self.watch = [(self.addr['LAB_' + c], n) for c, n in
                      (('0122', 4), ('0026', 2), ('011D', 4), ('011E', 2), ('011F', 2), ('0120', 2), ('00D0', 4), ('00D1', 2),
                       ('003E', 2), ('0123', 4), ('00C6', 4), ('00EF', 2), ('00F0', 2), ('0033', 4), ('0034', 4), ('0035', 4),
                       ('0121', 4), ('01C9', 2), ('01CA', 2), ('0261', 4), ('04DF', 2), ('05B0', 2), ('05B8', 2), ('05BC', 2),
                       ('05BD', 2), ('05D7', 4), ('05E6', 2), ('05E7', 2), ('0279', 4), ('027A', 4), ('027C', 4))]
        self.watch.append((self.addr['LAB_05B8'] + 2, 2))
        self.watch.append((self.addr['SECSTRT_9'], 2))
        self.watch_ranges = [(self.addr['LAB_01CB'], self.addr['LAB_01CB'] + 8 * 64), (self.addr['LAB_0276'], self.addr['LAB_0276'] + 24),
                              (self.addr['LAB_0506'], self.addr['LAB_0506'] + 64),
                              (self.addr['LAB_05D6'], self.addr['LAB_05D6'] + 12)]
        self.proc = None
        self.build_driver()
        self.setup_unicorn()

    # ---- C++ side -------------------------------------------------------------------------------------------------------
    def build_driver(self):
        self.tmp = tempfile.mkdtemp(prefix='scenes_')
        src = os.path.join(self.tmp, 'd.cpp')
        with open(src, 'w') as f:
            f.write(DRIVER)
        self.exe = os.path.join(self.tmp, 'd.exe')
        subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-D_CRT_SECURE_NO_WARNINGS', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), src,
                        os.path.join(ROOT, 'src', 'engine', 'scenes.cpp'), '-o', self.exe], check=True)
        self.proc = subprocess.Popen([self.exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        w = self.proc.stdin.write
        w('BASE %x\n' % self.base)
        for i, a in self.id_addr.items():
            w('SYM %x %x\n' % (i, a))
        w('MEM %s\n' % self.image.hex())

    def close(self):
        if self.proc:
            self.proc.stdin.close()
            self.proc.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_cpp(self, kind, arg, sets, k, abort):
        w = self.proc.stdin.write
        w('INIT\n')
        for bits, a, v in sets:
            w('SET %d %x %x\n' % (bits, a, v))
        w('PARAM %d %d\n' % (k, abort))
        w('RUN %s %x\n' % (kind, arg))
        self.proc.stdin.flush()
        out = []
        while True:
            ln = self.proc.stdout.readline()
            if not ln:
                raise RuntimeError('driver died')
            ln = ln.strip()
            if ln == 'END':
                break
            out.append(ln)
        res = []
        for ln in out:
            f = ln.split()
            if f[0] == 'C':
                name = self.fns[int(f[1])]
                d0, d1, d2, d3, a0, a1 = (int(x, 16) for x in f[2:8])
                res.append(self.fmt_call(name, d0, d1, d2, d3, a0, a1, f[8] if f[8] != '-' else None))
            elif f[0] == 'W':
                res.append('W%s %x %x' % (f[1], int(f[2], 16), int(f[3], 16)))
            elif f[0] == 'S':
                res.append('S %s %s' % (f[1], f[2]))
            else:
                res.append(ln)
        return res

    @staticmethod
    def fmt_call(name, d0, d1, d2, d3, a0, a1, s):
        regs = SPEC[name][1].split()
        parts = [name]
        for r in regs:
            if r == 'name':
                parts.append('s=%s' % s)
            elif r in ('d0w', 'd1w', 'd2w'):
                parts.append('%s=%x' % (r[:2], {'d0w': d0, 'd1w': d1, 'd2w': d2}[r] & 0xFFFF))
            else:
                parts.append('%s=%x' % (r, {'d0': d0, 'd1': d1, 'd2': d2, 'd3': d3, 'a0': a0, 'a1': a1}[r]))
        return ' '.join(parts)

    # ---- asm side -------------------------------------------------------------------------------------------------------
    def setup_unicorn(self):
        uc = self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(M.UC_CPU_M68K_M68000)
        uc.mem_map(self.base, H._round(len(self.image)))
        uc.mem_map(self.STACK, self.STACK_SIZE)
        uc.mem_map(self.SENTINEL, 0x1000)
        for a, (name, regs) in self.hooks.items():
            uc.hook_add(UC_HOOK_CODE, self._on_hook, begin=a, end=a)
        uc.hook_add(UC_HOOK_MEM_WRITE, self._on_write)
        key = self.addr['SECSTRT_16']
        uc.hook_add(UC_HOOK_MEM_READ, self._on_read, begin=key, end=key + 1)
        self.events = []
        self.k = self.abort = self.tick_count = self.load_count = 0

    def rd32(self, a):
        return struct.unpack('>I', bytes(self.uc.mem_read(a, 4)))[0]

    def wr32(self, a, v):
        self.uc.mem_write(a, struct.pack('>I', v & 0xFFFFFFFF))

    def cstr(self, a):
        out = bytearray()
        while True:
            c = self.uc.mem_read(a, 1)[0]
            if c == 0:
                return out.decode('latin-1')
            out.append(c)
            a += 1

    def _on_read(self, uc, access, addr, size, value, _):
        # the key flag SECSTRT_16: with `abort` set, the abort-th read finds it non-zero (the C++ driver counts its reads the same way)
        if self.abort:
            self.key_reads += 1
            if self.key_reads == self.abort:
                uc.mem_write(self.addr['SECSTRT_16'], b'\x00\x01')

    def _on_write(self, uc, access, addr, size, value, _):
        for a, n in self.watch:
            if addr == a and size == n:
                self.events.append('W%d %x %x' % (size * 8, addr, value & ((1 << (8 * size)) - 1)))
                return
        for lo, hi in self.watch_ranges:
            if lo <= addr < hi:
                self.events.append('W%d %x %x' % (size * 8, addr, value & ((1 << (8 * size)) - 1)))
                return

    def _on_hook(self, uc, addr, size, _):
        name, regs = self.hooks[addr]
        if name in self.disabled:
            return
        g = lambda r: uc.reg_read(r)
        D = [g(M.UC_M68K_REG_D0 + i) for i in range(4)]
        A = [g(M.UC_M68K_REG_A0 + i) for i in range(2)]
        if name == 'FnCopy':       # not a call: the loop MOVE.B (A0)+,(A1)+ ; DBF D0 is skipped (2 + 4 bytes)
            self.events.append(self.fmt_call(name, D[0], D[1], D[2], D[3], A[0], A[1], None))
            uc.reg_write(M.UC_M68K_REG_PC, addr + 6)
            return
        sp = g(M.UC_M68K_REG_A7)
        ret = self.rd32(sp)
        uc.reg_write(M.UC_M68K_REG_A7, sp + 4)
        uc.reg_write(M.UC_M68K_REG_PC, ret)
        if name in ('SPAWN_A', 'SPAWN_B'):
            self.events.append('S %s %s' % (name[-1], self.script_by_addr[A[0]]))
            return
        s = self.cstr(A[0]) if 'name' in regs.split() else None
        self.events.append(self.fmt_call(name, D[0], D[1], D[2], D[3], A[0], A[1], s))
        ticks = self.addr['LAB_0379']
        t = self.rd32(ticks)
        ret_d0 = None
        if name == 'FnJobsTick':
            self.tick_count += 1
            self.wr32(ticks, t + 2)
            if self.k and self.tick_count % self.k == 0:
                self.uc.mem_write(self.addr['LAB_0120'], b'\x00\x01')
        elif name in ('FnFlip', 'FnWait', 'FnSwap'):
            self.wr32(ticks, t + 3)
        elif name == 'FnWipeStep':
            progress, step = struct.unpack('>hh', bytes(uc.mem_read(self.addr['LAB_05B8'], 4)))
            if D[1] & 4:
                progress = min(progress + step, 0x3E8)
            elif D[1] & 8:
                progress = max(progress - step, 0)
            uc.mem_write(self.addr['LAB_05B8'], struct.pack('>h', progress))
        elif name in LOAD_FNS:
            self.load_count += 1
            if self.abort and self.load_count == self.abort:
                self.uc.mem_write(self.addr['LAB_05E7'], b'\x00\x01')
        elif name == 'FnCelSize':
            ret_d0 = 0x1000 + 2 * sum(s.encode('latin-1'))
        elif name == 'FnRamp':
            ret_d0 = 0x5000 + D[0] + (D[1] & 0xFFFF)
        if ret_d0 is not None:
            uc.reg_write(M.UC_M68K_REG_D0, ret_d0)

    def run_asm(self, label, sets, k, abort, a0=None):
        uc = self.uc
        uc.mem_write(self.base, self.image)
        uc.mem_write(self.STACK, bytes(self.STACK_SIZE))
        uc.mem_write(self.SENTINEL, bytes.fromhex('4E71' * 8))
        for bits, a, v in sets:
            uc.mem_write(a, v.to_bytes(bits // 8, 'big'))
        uc.ctl_flush_tb()
        self.events = []
        self.k, self.abort, self.tick_count, self.load_count, self.key_reads = k, abort, 0, 0, 0
        sp = self.STACK + self.STACK_SIZE - 0x100
        uc.reg_write(M.UC_M68K_REG_SR, 0x2700)
        for i in range(8):
            uc.reg_write(M.UC_M68K_REG_D0 + i, 0x11110000 + i)
            uc.reg_write(M.UC_M68K_REG_A0 + i, 0x22220000 + i)
        if a0 is not None:
            uc.reg_write(M.UC_M68K_REG_A0, a0)
        uc.reg_write(M.UC_M68K_REG_A7, sp - 4)
        uc.mem_write(sp - 4, struct.pack('>I', self.SENTINEL))
        try:
            uc.emu_start(self.addr[label], self.SENTINEL, count=400000)
        except UcError as e:
            raise RuntimeError('asm run %s failed: %s at pc=%x' % (label, e, uc.reg_read(M.UC_M68K_REG_PC)))
        if uc.reg_read(M.UC_M68K_REG_PC) != self.SENTINEL:
            raise RuntimeError('asm run %s did not return (pc=%x)' % (label, uc.reg_read(M.UC_M68K_REG_PC)))
        return list(self.events)

    # ---- scenario -------------------------------------------------------------------------------------------------------
    def base_sets(self, flags=0, extra=()):
        sets = []
        for i in ('00C2', '00C4', '00C6', '00C7', '00C8', '00C9', '00CA', '00CB', '00CC', '00CD', '00CE', '00CF', '0045', '0124',
                  '0121', '05D2', '056C'):
            sets.append((32, self.addr['LAB_' + i], 0x00A00000 + (int(i, 16) << 8)))
        sets.append((32, self.addr['SECSTRT_30'], 0x00B00000))
        sets.append((32, self.addr['LAB_0379'], 1000))
        sets.append((32, self.addr['LAB_00D0'], 2))
        sets.append((16, self.addr['LAB_0005'], flags))
        sets.append((32, self.addr['LAB_0026'], 0))
        sets.append((16, self.addr['SECSTRT_9'], 0x0123))
        sets.append((16, self.addr['LAB_01C9'], 0x0456))
        sets.append((16, self.addr['LAB_01CA'], 0x0789))
        for lab, bits, v in (('LAB_0279', 32, 0x00A10000), ('LAB_027A', 32, 0x00A20000), ('LAB_027C', 32, 0), ('LAB_0261', 32, 1),
                             ('LAB_05B0', 16, 0), ('LAB_05B8', 32, 0x03E80005), ('LAB_05E6', 16, 0), ('LAB_05E7', 16, 0),
                             ('SECSTRT_16', 16, 0), ('LAB_04DF', 16, 0)):
            sets.append((bits, self.addr[lab], v))
        sets += list(extra)
        return sets

    def compare(self, tc, asm_label, kind, arg, sets, k=3, abort=0, what='', a0=None, off=()):
        """off: SceneFn names whose asm hook is off for this run (the routine under test starts at a hooked label)."""
        self.disabled = set(off)
        try:
            a = self.run_asm(asm_label, sets, k, abort, a0)
        finally:
            self.disabled = set()
        c = self.run_cpp(kind, arg, sets, k, abort)
        if a != c:
            i = next((n for n, (x, y) in enumerate(zip(a, c)) if x != y), min(len(a), len(c)))
            tc.fail('%s: traces differ at event %d (asm %d events, C++ %d)\n asm: %s\n C++: %s' % (
                what, i, len(a), len(c), a[max(0, i - 2):i + 3], c[max(0, i - 2):i + 3]))
        return a


@unittest.skipUnless(NEEDS, 'needs unicorn, clang++ and build/reasm/program')
class ScenesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rig = Rig()

    @classmethod
    def tearDownClass(cls):
        cls.rig.close()

    def test_spec_covers_every_fn(self):
        self.assertEqual(sorted(self.rig.fns), sorted(SPEC))

    def test_scene_bodies(self):
        r = self.rig
        for label in ('001A', '001B', '001C', '002C', '002D', '002E', '002F', '0030', '0031', '0036', '0037', '0039', '003B'):
            for k in (2, 3, 5):
                tr = r.compare(self, 'LAB_' + label, 'scene', int(label, 16), r.base_sets(), k=k, what='scene %s k=%d' % (label, k))
                self.assertTrue(any(e.startswith('FnWait') for e in tr) or label in ('0030', '0031'), label)

    def test_toggle_persists_between_scenes(self):
        # LAB_0026 (the left/right toggle of LAB_001F) is not reset: a scene that starts with it set swaps A and B
        r = self.rig
        for tog in (0, 1):
            sets = r.base_sets(extra=[(16, r.addr['LAB_0026'], tog)])
            r.compare(self, 'LAB_0037', 'scene', 0x37, sets, what='toggle %d' % tog)
            r.compare(self, 'LAB_001A', 'scene', 0x1A, sets, what='toggle %d (001A)' % tog)

    def test_intro_loader_every_abort_point(self):
        r = self.rig
        for flags in (0, 0x10, 0x20, 0x08, 0x40, 0x70):
            for abort in range(0, 14):
                tr = r.compare(self, 'LAB_0185', 'scene', 0x185, r.base_sets(flags), abort=abort, what='0185 flags=%x abort=%d' % (flags, abort))
                if abort == 0 or abort > 12:  # 12 loader steps in all (the flag is tested after the step that set it)
                    self.assertTrue(any('FnRnc' in e for e in tr))
                else:
                    self.assertFalse(any('FnRnc' in e for e in tr))

    def test_ending_loader_all_flags(self):
        r = self.rig
        for flags in (0, 1, 2, 4, 3, 5, 6, 7, 0x10, 0x20, 0x08, 0x40, 0x12, 0x61, 0x84):
            r.compare(self, 'LAB_018E', 'scene', 0x18E, r.base_sets(flags), what='018E flags=%x' % flags)

    def test_caption_and_message_loader(self):
        r = self.rig
        tab = r.addr['LAB_011A']
        sets = r.base_sets(extra=[(32, tab + 16, 0x00A10000), (32, tab + 20, 0x00A20000)])
        for text in ('LAB_00AA', 'LAB_00A2', 'LAB_0002'):
            tr = r.compare(self, 'LAB_0054', 'caption', r.addr[text], sets, a0=r.addr[text], what='0054 ' + text)
            self.assertTrue(any(e.startswith('FnCopy') for e in tr))
            self.assertEqual(sum(1 for e in tr if e.startswith('W16')), 5)
        tr = r.compare(self, 'LAB_0051', 'message', 0, sets, what='0051')
        self.assertEqual([e.split()[0] for e in tr], ['FnCelLoad', 'FnFileOpen', 'FnFileRead', 'FnFileClose'])

    def test_picture_setup(self):
        r = self.rig
        r.compare(self, 'LAB_0174', 'scene', 0x174, r.base_sets(), what='0174')

    # ---- ROADMAP 7.1q: the operations and progress screens that were asm ----
    def test_fade_and_palette_set(self):
        r = self.rig
        tr = r.compare(self, 'LAB_025F', 'fade', 0, r.base_sets(), what='025F', off=('FnFade',))
        self.assertEqual([e.split()[0] for e in tr], ['FnPalFade', 'FnWait'])
        self.assertEqual(tr[1], 'FnWait d0=24')
        for steps in (0, 1, 2, 5):
            sets = r.base_sets(extra=[(32, r.addr['LAB_0261'], steps)])
            tr = r.compare(self, 'LAB_0260', 'palset', r.addr['LAB_01CB'], sets, a0=r.addr['LAB_01CB'], what='0260 steps=%d' % steps,
                           off=('FnPalSet',))
            self.assertEqual(tr[1], 'FnWait d0=%x' % (steps << 4))

    def test_flip_and_prepare(self):
        r = self.rig
        for work in (0x00A30000, 0x00A31234):
            sets = r.base_sets(extra=[(32, r.addr['LAB_056C'], work)])
            tr = r.compare(self, 'LAB_0262', 'flip', 0, sets, what='0262', off=('FnFlip',))
            self.assertEqual([e.split()[0] for e in tr], ['FnSwap', 'W32', 'W32', 'W32', 'FnTarget'])
        tr = r.compare(self, 'LAB_0263', 'prepare', 0, r.base_sets(), what='0263', off=('FnPrepare',))
        self.assertEqual([e.split()[0] for e in tr], ['FnScreenCopy', 'FnScreenCopy'])

    def test_loader_steps(self):
        r = self.rig
        for step, label in enumerate(('LAB_059E', 'LAB_059F', 'LAB_05A0', 'LAB_05A1')):
            for page in (0, 3, 5, 6):
                for key in (0, 1):
                    sets = r.base_sets(extra=[(16, r.addr['LAB_05B0'], page), (16, r.addr['SECSTRT_16'], key),
                                              (32, r.addr['LAB_011A'] + 16, 0x00A40000)])
                    tr = r.compare(self, label, 'loadstep', step, sets, what='%s page=%d key=%d' % (label, page, key),
                                   off=('FnLoad059E', 'FnLoad059F', 'FnLoad05A0', 'FnLoad05A1'))
                    self.assertTrue(any(e.startswith('FnSpriteBlit') for e in tr), label)
                    abort = [e for e in tr if e == 'W16 %x 1' % r.addr['LAB_05E7']]
                    self.assertEqual(len(abort), key, (label, page, key))
                    if step >= 2:      # LAB_05A0 / LAB_05A1: a text page unless six were shown
                        self.assertEqual(any(e.startswith('FnText') for e in tr), page < 6, (label, page))

    def test_progress_wipe(self):
        r = self.rig
        for progress, step in ((0, 1), (0x20, 2), (0x380, 6), (0x3E0, 5), (0x3E8, 1)):
            for k in (2, 3):
                sets = r.base_sets(extra=[(16, r.addr['LAB_05B8'], progress), (16, r.addr['LAB_05B8'] + 2, step)])
                tr = r.compare(self, 'LAB_05A5', 'progress', 0, sets, k=k, what='05A5 progress=%x' % progress)
                self.assertEqual(sum(1 for e in tr if e.startswith('FnWipeStep')), sum(1 for e in tr if e.startswith('FnFlip')))
                self.assertEqual([e for e in tr if e.startswith('FnScreenCopy')].__len__(), 2)
        # the music starts whenever a row's step is 4 (the wipe is the intro's picture reveal)
        sets = r.base_sets(extra=[(16, r.addr['LAB_05B8'], 0), (16, r.addr['LAB_05B8'] + 2, 1)])
        tr = r.run_asm('LAB_05A5', sets, 3, 0)
        self.assertGreaterEqual(sum(1 for e in tr if e.startswith('FnMusic')), 1)

    def test_loading_screen_halves(self):
        r = self.rig
        tr = r.compare(self, 'LAB_05AB', 'loadinga', 0, r.base_sets(), what='05AB', off=('FnLoadingA',))
        self.assertEqual([e.split()[0] for e in tr if e.startswith('Fn')], ['FnListClear', 'FnFlip', 'FnClear', 'FnWipeRows', 'FnWipeRefresh', 'FnFlip'])
        for progress, step, stop in ((0x3E8, 9, 0), (0x300, 9, 0), (0x00D0, 9, 0), (0x00C7, 9, 0), (0x3E8, 9, 1), (0x0100, 4, 0)):
            sets = r.base_sets(extra=[(16, r.addr['LAB_05B8'], progress), (16, r.addr['LAB_05B8'] + 2, step),
                                      (16, r.addr['LAB_05E6'], stop)])
            tr = r.compare(self, 'LAB_05AC', 'loadingb', 0, sets, what='05AC p=%x s=%d stop=%d' % (progress, step, stop),
                           off=('FnLoadingB',))
            self.assertEqual(sum(1 for e in tr if e.startswith('FnScreenCopy')), 2)

    def test_frame_loops(self):
        r = self.rig
        for k in (1, 2, 4):
            for speed, gap in ((2, 0), (6, 4), (8, 0), (0, 0)):
                sets = r.base_sets(extra=[(32, r.addr['LAB_00D0'], speed), (32, r.addr['LAB_0123'], gap),
                                          (16, r.addr['LAB_011F'], 1)])
                r.compare(self, 'LAB_0007', 'frameloop', 0, sets, k=k, what='0007 k=%d speed=%d gap=%d' % (k, speed, gap))
        for first in (0, 4, 8, 15):
            for credits in (0, 1):
                sets = r.base_sets(extra=[(16, r.addr['LAB_0028'], first), (16, r.addr['LAB_011F'], 1),
                                          (16, r.addr['LAB_00D1'], credits)])
                r.compare(self, 'LAB_000B', 'frameloopn', first, sets, what='000B first=%d' % first)

    def test_stage_modes(self):
        r = self.rig
        for mode in (2, 3, 4, 5, 0, 7):
            sets = r.base_sets(extra=[(16, r.addr['LAB_011E'], mode), (32, r.addr['LAB_011D'], 0x00C01000),
                                      (16, r.addr['LAB_011F'], 0)])
            tr = r.compare(self, 'LAB_000F', 'stage', 0, sets, what='000F mode=%d' % mode)
            self.assertIn('W16 %x 1' % r.addr['LAB_011F'], tr)
            sets = r.base_sets(extra=[(16, r.addr['LAB_011E'], mode), (16, r.addr['LAB_011F'], 0)])
            r.compare(self, 'LAB_0007', 'frameloop', 0, sets, k=2, what='0007 staged mode=%d' % mode)

    def test_frame_primitives(self):
        r = self.rig
        for speed, gap, now, stamp in ((2, 0, 1000, 990), (6, 4, 1000, 1000), (8, 0, 1000, 995), (1, 1, 5, 4), (0, 0, 7, 0)):
            sets = r.base_sets(extra=[(32, r.addr['LAB_00D0'], speed), (32, r.addr['LAB_0123'], gap),
                                      (32, r.addr['LAB_0379'], now), (32, r.addr['LAB_0122'], stamp)])
            r.compare(self, 'LAB_0018', 'wait', 0, sets, what='0018 %s' % ((speed, gap, now, stamp),))
        r.compare(self, 'LAB_0017', 'stamp', 0, r.base_sets(), what='0017')

    def test_animation_script_callbacks(self):
        r = self.rig
        for lab, kind in (('LAB_0032', 'ramp'), ('LAB_003F', 'flash'), ('LAB_0040', 'flashseries')):
            tr = r.compare(self, lab, kind, 0, r.base_sets(), what=lab)
            self.assertTrue(tr)
        tr = r.run_asm('LAB_0032', r.base_sets(), 3, 0)
        self.assertEqual(sum(1 for e in tr if e.startswith('W32')), 3)

    def test_flash_palettes_are_the_asm_data(self):
        import ctypes  # noqa: F401  (kept out of the engine; the arrays are compared through a tiny driver)
        r = self.rig
        for lab, ident in (('LAB_0042', 0x42), ('LAB_0043', 0x43)):
            a = r.addr[lab] - r.base
            want = list(struct.unpack('>32H', r.image[a:a + 64]))
            src = os.path.join(r.tmp, 'pal.cpp')
            open(src, 'w').write('#include <stdio.h>\n#include "engine/scenes.hpp"\nint main(){ for(int i=0;i<32;++i) '
                                 'printf("%%d ", ms::sceneFlashPalette(%d)[i]); return 0; }\n' % ident)
            exe = os.path.join(r.tmp, 'pal.exe')
            subprocess.run([CXX, '-std=c++17', '-I', os.path.join(ROOT, 'include'), src,
                            os.path.join(ROOT, 'src', 'engine', 'scenes.cpp'), '-o', exe], check=True)
            got = [int(x) for x in subprocess.run([exe], capture_output=True, text=True).stdout.split()]
            self.assertEqual(got, want, lab)

    def test_names_in_the_loaders_are_the_asm_strings(self):
        r = self.rig
        with open(os.path.join(ROOT, 'src', 'engine', 'scenes.cpp'), encoding='latin-1') as f:
            text = f.read()
        names = set(re.findall(r'"([a-z0-9]+\.(?:piv|cel|stile|cmp)|mindscape)"', text))
        data = r.image
        for n in sorted(names):
            self.assertIn(n.encode() + b'\0', data, n)


# ---- the compiled build (m68k-amiga-elf-g++ -m68020) in unicorn ---------------------------------------------------------------
# src/engine/scenes.cpp + src/rt/engine_scenes.cpp are compiled for the Amiga and linked flat; the asm symbols they name (prg_*)
# are pinned to the original image's addresses, the C++ neighbours (job tick/spawn, overlay, spawn, rt_job_reset) are 68k stubs
# at known addresses. The SAME hooks as above fire when the compiled code calls an asm routine (so the rt_scn_call register
# marshalling is tested too) and the trace must equal the original asm's.
BLOB_BASE, BLOB_SIZE = 0x00C00000, 0x00040000
FAKE_RAM, FAKE_RAM_SIZE = 0x00A00000, 0x00180000     # where the fake cell values (LAB_00C2.. = $A0xx00) point

STUB_CPP = r'''
extern "C" {
void rtJobsSpawnQueued(void) {}
void rtJobsTick(void) {}
void rtAnimOverlay(void) {}
unsigned long rtAnimSpawn(unsigned long, unsigned long, unsigned long) { return 0; }
// 7.1q: the screen primitives and the wipe the host calls (src/rt/prg_ops.cpp, prg_copy.cpp, wipe.cpp, engine_jobs.cpp)
void rtJobsClearLists(void) {}
void rtPrgBlackout(void) {}
void rtPrgPalLoad(unsigned long) {}
void rtPrgCopyLongs(unsigned long, unsigned long) {}
void rtPrgTarget(unsigned long) {}
void rtPrgClear(unsigned long) {}
void rtPrgCopyScreen(unsigned long, unsigned long) {}
void rtPrgWipeStepC(unsigned long) {}
void rtPrgWipeRowsC(void) {}
void rtPrgWipeRefreshC(void) {}
}
asm(R"(
	.text
	.globl rt_job_reset
rt_job_reset:
	rts
	.data
	.balign 4
	.globl rt_enh_scr
rt_enh_scr:
	.long 40000
	.globl rt_enh_raw_prg_msg
rt_enh_raw_prg_msg:
	.long 4326
)");
'''

_BLOB = None
RT_LABEL = {'rt_palette_ramp_add_prg': 'LAB_057A', 'rt_palette_slot_free': 'LAB_0579', 'rt_prg_pal_copy_live': 'LAB_025D',
            'rt_prg_pic_file': 'LAB_0402', 'rt_prg_file_open': 'LAB_0390', 'rt_prg_file_read': 'LAB_03B2', 'rt_prg_file_close': 'LAB_03DA',
            'rt_prg_cel_load': 'LAB_0496', 'rt_prg_cel_size': 'LAB_0491', 'rt_prg_pic_mem': 'LAB_03FC',
            'rt_display_wait_frames': 'LAB_054F', 'rt_palette_set_target_prg': 'LAB_0576', 'rt_display_palette_write': 'LAB_0565',
            'rt_music_start': 'SECSTRT_1', 'rt_prg_text_list': 'LAB_028F', 'rt_rnc_decode': 'LAB_0190',
            'rt_prg_restore_pass': 'LAB_0242', 'rt_prg_display_swap': 'LAB_054C', 'rt_prg_key_reset': 'LAB_035E',
            'rt_prg_draw_cel': 'LAB_04B4'}
# the C++ functions of the 7.1q glue that the blob calls with C arguments (on the stack): stub name -> (SceneFn, argument registers)
STACK_STUBS = {'rtPrgTarget': ('FnTarget', ('d0',)), 'rtPrgClear': ('FnClear', ('a0',)), 'rtPrgPalLoad': ('FnPalLoad', ('a0',)),
               'rtPrgCopyLongs': ('FnUnpack', ('a0', 'a1')), 'rtPrgCopyScreen': ('FnScreenCopy', ('a0', 'a1')),
               'rtPrgWipeStepC': ('FnWipeStep', ('d1',))}
PLAIN_STUBS = {'rtJobsSpawnQueued': 'FnJobsSpawn', 'rtJobsTick': 'FnJobsTick', 'rtAnimOverlay': 'FnOverlay',
               'rt_job_reset': 'FnJobsReset', 'rtJobsClearLists': 'FnListClear', 'rtPrgBlackout': 'FnBlackout',
               'rtPrgWipeRowsC': 'FnWipeRows', 'rtPrgWipeRefreshC': 'FnWipeRefresh'}


def build_blob(addr_of):
    """Compile + link the engine and its rt host flat. addr_of(symbol) -> image address of a prg_ symbol."""
    global _BLOB
    if _BLOB:
        return _BLOB
    import test_creatures_emu as E
    tmp = tempfile.mkdtemp(prefix='scenes_blob_')
    env = dict(os.environ, PATH=os.path.dirname(E.GXX) + os.pathsep + os.environ.get('PATH', ''))
    flags = ['-m68020', '-msoft-float', '-fomit-frame-pointer', '-nostdlib', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
             '-std=c++17', '-O2', '-DNDEBUG', '-DAMIGA', '-DMS_LINK_GAME_ASM=1', '-ffunction-sections',
             '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(E.ACE, 'mini_std'), '-I', E.ACE,
             '-I', E.GCC_SUPPORT]
    stub = os.path.join(tmp, 'stub.cpp')
    with open(stub, 'w') as f:
        f.write(STUB_CPP)
    objs = []
    for i, src in enumerate((os.path.join(ROOT, 'src', 'engine', 'scenes.cpp'), os.path.join(ROOT, 'src', 'rt', 'engine_scenes.cpp'), stub)):
        o = os.path.join(tmp, '%d.o' % i)
        r = subprocess.run([E.GXX] + flags + ['-c', src, '-o', o], capture_output=True, text=True, env=env)
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
    cmd = [E.LD, '-Ttext=0x%X' % BLOB_BASE, '-e', 'rtSceneRun']
    for u in sorted(CN.legacy_set(und - defs)):
        if u in RT_LABEL:       # 7.1o: the rt entry the C++ calls instead of the patch stub at this label
            cmd.append('--defsym=%s=%d' % (u, addr_of(RT_LABEL[u])))
            continue
        if not u.startswith('prg_'):
            raise RuntimeError('unresolved symbol ' + u)
        cmd.append('--defsym=%s=%d' % (u, addr_of(u[4:])))
    cmd += CN.defsym_aliases(cmd, und - defs)   # ROADMAP 7.1s: the C++ names of the cells resolve like their labels
    elf = os.path.join(tmp, 'blob.elf')
    r = subprocess.run(cmd + ['-o', elf] + objs, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError('link failed: ' + r.stderr[-3000:])
    binf = os.path.join(tmp, 'blob.bin')
    subprocess.run([E.OBJCOPY, '-O', 'binary', elf, binf], check=True, env=env)
    syms = {}
    for ln in subprocess.run([E.NM, elf], capture_output=True, text=True, env=env).stdout.split('\n'):
        f = ln.split()
        if len(f) == 3:
            syms[f[2]] = int(f[0], 16)
    with open(binf, 'rb') as f:
        blob = f.read()
    assert len(blob) < BLOB_SIZE - 0x2000, len(blob)
    _BLOB = (blob, syms)
    return _BLOB


def have_m68k_toolchain():
    sys.path.insert(0, HERE)
    try:
        import test_creatures_emu as E
        return bool(E.HAVE_TOOLS)
    except Exception:
        return False


@unittest.skipUnless(NEEDS and have_m68k_toolchain(), 'needs unicorn, clang++, the m68k toolchain and build/reasm/program')
class CompiledScenesTest(unittest.TestCase):
    """The m68k build of the engine + rt host against the original asm."""

    @classmethod
    def setUpClass(cls):
        cls.rig = Rig()
        r = cls.rig
        blob, syms = build_blob(lambda n: r.addr[n])
        cls.blob, cls.syms = blob, syms
        uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(M.UC_CPU_M68K_M68020)          # the blob is 68020 code; the original runs on it too
        uc.mem_map(r.base, H._round(len(r.image)))
        uc.mem_map(r.STACK, r.STACK_SIZE)
        uc.mem_map(r.SENTINEL, 0x1000)
        uc.mem_map(BLOB_BASE, BLOB_SIZE)
        uc.mem_map(FAKE_RAM, FAKE_RAM_SIZE)
        cls.uc2 = uc
        # the same hooks, plus the C++ stubs the blob calls instead of the asm routines
        cls.stub_hooks = {syms[s]: fn for s, fn in PLAIN_STUBS.items()}
        cls.stub_hooks.update({syms[s]: v[0] for s, v in STACK_STUBS.items()})
        cls.stub_hooks[syms['rtAnimSpawn']] = 'SPAWN'
        # 7.1q: the operations the engine implements run as code on both sides (the asm hooks of ENGINE_OPS are off)
        r.disabled = set(ENGINE_OPS)
        key = r.addr['SECSTRT_16']
        uc.hook_add(UC_HOOK_MEM_READ, cls.on_read, begin=key, end=key + 1)
        for a in r.hooks:
            uc.hook_add(UC_HOOK_CODE, cls.on_hook, begin=a, end=a)
        for a in cls.stub_hooks:
            uc.hook_add(UC_HOOK_CODE, cls.on_stub, begin=a, end=a)
        uc.hook_add(UC_HOOK_MEM_WRITE, cls.on_write)
        # the cells that moved from the asm image into the C++ (LAB_0026, LAB_0033..LAB_0035)
        tog = [v for k, v in syms.items() if 's_uwToggle' in k]
        ramp = [v for k, v in syms.items() if 's_aulRamp' in k]
        assert len(tog) == 1 and len(ramp) == 1, (tog, ramp)
        cls.xlat = {tog[0]: r.addr['LAB_0026']}
        for i in range(3):
            cls.xlat[ramp[0] + 4 * i] = r.addr['LAB_%04X' % (0x33 + i)]
        cls.flash = {}
        for lab in ('LAB_0042', 'LAB_0043'):
            a = r.addr[lab] - r.base
            cls.flash[bytes(r.image[a:a + 64])] = r.addr[lab]

    @classmethod
    def tearDownClass(cls):
        cls.rig.close()

    # -- hooks (the Rig's logic, run against the blob's unicorn) --
    @classmethod
    def with_uc(cls, fn, uc, *a):
        r = cls.rig
        saved = r.uc
        r.uc = uc
        try:
            return fn(*a)
        finally:
            r.uc = saved

    @classmethod
    def on_hook(cls, uc, addr, size, ud):
        r = cls.rig
        name = r.hooks[addr][0]
        if name == 'FnPalWrite':
            a0 = uc.reg_read(M.UC_M68K_REG_A0)
            if BLOB_BASE <= a0 < BLOB_BASE + BLOB_SIZE:       # the flash palettes are C++ data in the blob: name them by content
                a0n = cls.flash.get(bytes(uc.mem_read(a0, 64)))
                if a0n:
                    uc.reg_write(M.UC_M68K_REG_A0, a0n)
        cls.with_uc(r._on_hook, uc, uc, addr, size, ud)

    @classmethod
    def on_read(cls, uc, access, addr, size, value, ud):
        cls.with_uc(cls.rig._on_read, uc, uc, access, addr, size, value, ud)

    @classmethod
    def on_stub(cls, uc, addr, size, ud):
        r = cls.rig
        name = cls.stub_hooks[addr]
        sp = uc.reg_read(M.UC_M68K_REG_A7)
        ret = struct.unpack('>I', bytes(uc.mem_read(sp, 4)))[0]
        stack_args = {s[0]: s[1] for s in STACK_STUBS.values()}
        if name in stack_args:
            regs = {'d0': 0, 'd1': 0, 'a0': 0, 'a1': 0}
            for i, rn in enumerate(stack_args[name]):
                regs[rn] = struct.unpack('>I', bytes(uc.mem_read(sp + 4 + 4 * i, 4)))[0]
            r.events.append(r.fmt_call(name, regs['d0'], regs['d1'], 0, 0, regs['a0'], regs['a1'], None))
            cls.with_uc(cls.effects, uc, name, regs)
            uc.reg_write(M.UC_M68K_REG_A7, sp + 4)
            uc.reg_write(M.UC_M68K_REG_PC, ret)
            return
        if name == 'SPAWN':
            kind = struct.unpack('>I', bytes(uc.mem_read(sp + 4, 4)))[0]
            script = struct.unpack('>I', bytes(uc.mem_read(sp + 8, 4)))[0]
            r.events.append('S %s %s' % ('AB'[kind], r.script_by_addr[script]))
            uc.reg_write(M.UC_M68K_REG_D0, 0)                  # a slot was taken
        else:
            r.events.append(name)
            cls.with_uc(cls.effects, uc, name)
        uc.reg_write(M.UC_M68K_REG_A7, sp + 4)
        uc.reg_write(M.UC_M68K_REG_PC, ret)

    @classmethod
    def effects(cls, name, regs=None):
        r = cls.rig
        ticks = r.addr['LAB_0379']
        t = r.rd32(ticks)
        if name == 'FnWipeStep':     # the wipe moves its progress by the step (clamped), as rt::wipeStep and the asm hook do
            progress, step = struct.unpack('>hh', bytes(r.uc.mem_read(r.addr['LAB_05B8'], 4)))
            if regs['d1'] & 4:
                progress = min(progress + step, 0x3E8)
            elif regs['d1'] & 8:
                progress = max(progress - step, 0)
            r.uc.mem_write(r.addr['LAB_05B8'], struct.pack('>h', progress))
        if name == 'FnJobsTick':
            r.tick_count += 1
            r.wr32(ticks, t + 2)
            if r.k and r.tick_count % r.k == 0:
                r.uc.mem_write(r.addr['LAB_0120'], b'\x00\x01')

    @classmethod
    def on_write(cls, uc, access, addr, size, value, ud):
        r = cls.rig
        if addr in cls.xlat and size in (2, 4):
            r.events.append('W%d %x %x' % (size * 8, cls.xlat[addr], value & ((1 << (8 * size)) - 1)))
            return
        r._on_write(uc, access, addr, size, value, ud)

    # -- one run of a blob entry (C calling convention: arguments on the stack) --
    @classmethod
    def run_blob(cls, entry, args, sets, k=3, abort=0):
        r = cls.rig
        uc = cls.uc2
        uc.mem_write(r.base, r.image)
        uc.mem_write(r.STACK, bytes(r.STACK_SIZE))
        uc.mem_write(r.SENTINEL, bytes.fromhex('4E71' * 8))
        uc.mem_write(FAKE_RAM, bytes(FAKE_RAM_SIZE))
        uc.mem_write(BLOB_BASE, bytes(BLOB_SIZE))
        uc.mem_write(BLOB_BASE, cls.blob)
        for bits, a, v in sets:
            if a == r.addr['LAB_0026']:
                a = [x for x, y in cls.xlat.items() if y == a][0]
            uc.mem_write(a, v.to_bytes(bits // 8, 'big'))
        uc.ctl_flush_tb()
        r.events = []
        r.k, r.abort, r.tick_count, r.load_count, r.key_reads = k, abort, 0, 0, 0
        sp = r.STACK + r.STACK_SIZE - 0x100 - 4 * len(args) - 4
        uc.reg_write(M.UC_M68K_REG_SR, 0x2700)
        init = {}
        for i in range(8):
            init['D%d' % i] = 0x11110000 + i
            uc.reg_write(M.UC_M68K_REG_D0 + i, init['D%d' % i])
            if i < 7:
                init['A%d' % i] = 0x22220000 + i
                uc.reg_write(M.UC_M68K_REG_A0 + i, init['A%d' % i])
        for i, a in enumerate(args):
            uc.mem_write(sp + 4 + 4 * i, struct.pack('>I', a))
        uc.mem_write(sp, struct.pack('>I', r.SENTINEL))
        uc.reg_write(M.UC_M68K_REG_A7, sp)
        saved = r.uc
        r.uc = uc
        try:
            uc.emu_start(entry, r.SENTINEL, count=3000000)
        except UcError as e:
            raise RuntimeError('blob run failed: %s at pc=%x' % (e, uc.reg_read(M.UC_M68K_REG_PC)))
        finally:
            r.uc = saved
        if uc.reg_read(M.UC_M68K_REG_PC) != r.SENTINEL:
            raise RuntimeError('blob run did not return (pc=%x)' % uc.reg_read(M.UC_M68K_REG_PC))
        after = {('D%d' % i): uc.reg_read(M.UC_M68K_REG_D0 + i) for i in range(8)}
        after.update({('A%d' % i): uc.reg_read(M.UC_M68K_REG_A0 + i) for i in range(7)})
        after['SP'] = uc.reg_read(M.UC_M68K_REG_A7)
        return list(r.events), init, after, uc

    @staticmethod
    def filtered(ev):
        return [e for e in ev if not e.startswith('FnCopy')]

    def check_callee_saved(self, init, after, what, also=()):
        for n in ['D%d' % i for i in range(2, 8)] + ['A%d' % i for i in range(2, 7)] + list(also):
            self.assertEqual(after[n], init[n], '%s clobbered %s' % (what, n))

    # -- tests --
    def test_scene_entry_matches_the_asm(self):
        r = self.rig
        labels = ('001A', '001B', '001C', '002C', '002D', '002E', '002F', '0036', '0037', '0039', '003B', '0174')
        for label in labels:
            for k in (2, 5):
                sets = r.base_sets()
                a = r.run_asm('LAB_' + label, sets, k, 0)
                c, init, after, _ = self.run_blob(self.syms['rtSceneRun'], [int(label, 16)], sets, k=k)
                self.assertEqual(self.filtered(a), self.filtered(c), 'scene %s k=%d' % (label, k))
                self.assertEqual(after['D0'] & 0xFF, 1)
                self.check_callee_saved(init, after, 'rtSceneRun(%s)' % label)
        notours = self.run_blob(self.syms['rtSceneRun'], [0x25F], r.base_sets())
        self.assertEqual((notours[2]['D0'] & 0xFF, notours[0]), (0, []))   # not ours: the caller runs it as asm

    def test_loaders_match_the_asm(self):
        r = self.rig
        for label in ('0185', '018E'):
            for flags in (0, 1, 2, 4, 0x10, 0x20, 0x08, 0x40):
                for abort in ((0, 1, 3, 7, 12) if label == '0185' else (0,)):
                    sets = r.base_sets(flags)
                    a = r.run_asm('LAB_' + label, sets, 3, abort)
                    c, init, after, _ = self.run_blob(self.syms['rtSceneRun'], [int(label, 16)], sets, abort=abort)
                    self.assertEqual(self.filtered(a), self.filtered(c), '%s flags=%x abort=%d' % (label, flags, abort))
                    self.check_callee_saved(init, after, 'rtSceneRun(%s)' % label)

    def test_toggle_cell_lives_in_the_blob(self):
        r = self.rig
        for tog in (0, 1):
            sets = r.base_sets(extra=[(16, r.addr['LAB_0026'], tog)])
            a = r.run_asm('LAB_0037', sets, 3, 0)
            c, init, after, _ = self.run_blob(self.syms['rtSceneRun'], [0x37], sets)
            self.assertEqual(self.filtered(a), self.filtered(c), 'toggle %d' % tog)

    def test_script_callbacks(self):
        # ROADMAP 7.1r: the script operands hold the C++ routines (rtScn*, asm/patches/program.data_cells.json link_names) instead of the
        # asm labels LAB_0032 / LAB_003F / LAB_0040 / LAB_05AE / LAB_05AF; rtSceneScriptCall recognises them and does what the asm did.
        r = self.rig
        for lab, fn in (('LAB_0032', 'rtScnRampSetup'), ('LAB_003F', 'rtScnFlash'), ('LAB_0040', 'rtScnFlashSeries')):
            sets = r.base_sets()
            a = r.run_asm(lab, sets, 3, 0)
            c, init, after, _ = self.run_blob(self.syms['rtSceneScriptCall'], [self.syms[fn]], sets)
            self.assertEqual(a, c, lab)
            self.assertEqual(after['D0'] & 0xFF, 1)
            c2, init2, after2, _ = self.run_blob(self.syms[fn], [], sets)                  # the routine itself (called by address)
            self.assertEqual(a, c2, fn)
            self.check_callee_saved(init2, after2, fn)
        c, _, after, _ = self.run_blob(self.syms['rtSceneScriptCall'], [r.addr['LAB_0041']], r.base_sets())
        self.assertEqual((after['D0'] & 0xFF, c), (0, []))     # any other address is not intercepted

    def test_page_callbacks(self):
        """LAB_05AE (MOVE.W #1,LAB_05E6) and LAB_05AF (SUBI.W #1,LAB_05B8+2), the one-line script callbacks of the intro text pages."""
        r = self.rig
        for lab, fn, cell, start, want in (('LAB_05AE', 'rtScnPageFlag', r.addr['LAB_05E6'], 0, 1),
                                           ('LAB_05AF', 'rtScnPageCount', r.addr['LAB_05B8'] + 2, 5, 4)):
            sets = r.base_sets(extra=[(16, cell, start)])
            r.run_asm(lab, sets, 3, 0)
            self.assertEqual(struct.unpack('>H', bytes(r.uc.mem_read(cell, 2)))[0], want, lab)
            for entry, args in ((self.syms[fn], []), (self.syms['rtSceneScriptCall'], [self.syms[fn]])):
                c, init, after, uc = self.run_blob(entry, args, sets)
                self.assertEqual(struct.unpack('>H', bytes(uc.mem_read(cell, 2)))[0], want, fn)
                self.check_callee_saved(init, after, fn)

    def test_caption_and_message(self):
        r = self.rig
        tab = r.addr['LAB_011A']
        sets = r.base_sets(extra=[(32, tab + 16, 0x00A10000), (32, tab + 20, 0x00A20000)])
        for text in ('LAB_00AA', 'LAB_00A2', 'LAB_0002'):
            a = r.run_asm('LAB_0054', sets, 3, 0, a0=r.addr[text])
            c, init, after, _ = self.run_blob(self.syms['rtSceneCaption'], [r.addr[text]], sets)
            self.assertEqual(self.filtered(a), self.filtered(c), text)
            self.check_callee_saved(init, after, 'rtSceneCaption')
        a = r.run_asm('LAB_0051', sets, 3, 0)
        c, init, after, _ = self.run_blob(self.syms['rt_scn_message'], [], sets)
        self.assertEqual(a, c)
        self.check_callee_saved(init, after, 'rt_scn_message')

    def test_anim_init_matches_the_asm(self):
        r = self.rig
        c, init, after, uc2 = self.run_blob(self.syms['rt_prg_anim_init'], [], [])
        blob_mem = bytes(uc2.mem_read(r.base, len(r.image)))
        res = r.h.run('SECSTRT_10', {'d': [0] * 8, 'a': [0] * 7 + [H.STACK_TOP]}, [])
        orig = bytearray(r.image)
        for a, data in res.writes:
            if r.base <= a < r.base + len(orig):
                orig[a - r.base:a - r.base + len(data)] = data
        lo, hi = r.addr['LAB_0288'], r.addr['LAB_0288'] + 23 * 4     # the opcode handler table of the dead asm interpreter
        diff = [i for i in range(len(orig)) if orig[i] != blob_mem[i] and not (lo <= r.base + i < hi)]
        self.assertEqual(diff, [], 'bytes differ at %s' % [hex(r.base + i) for i in diff[:8]])
        self.assertTrue(any(lo <= a < hi for a, _ in res.writes))     # the original does fill it (and nothing reads it any more)
        self.check_callee_saved(init, after, 'rt_prg_anim_init')

    def test_job_handler_stop(self):
        r = self.rig
        res = r.h.run('LAB_0014', {'d': [0] * 8, 'a': [0] * 7 + [H.STACK_TOP]}, [])        # asm: LAB_0120 = 1, A0 = 0
        self.assertEqual(res.regs['a'][0], 0)
        cell = r.addr['LAB_0120']
        self.assertTrue(any(a <= cell < a + len(d) and d[cell - a:cell - a + 2] == b'\x00\x01' for a, d in res.writes))
        out = FAKE_RAM + 0x100
        c, init, after, uc2 = self.run_blob(self.syms['rt_job_handler_stop'], [FAKE_RAM + 0x200, out], [])
        self.assertEqual(bytes(uc2.mem_read(cell, 2)), b'\x00\x01')
        self.assertEqual(struct.unpack('>I', bytes(uc2.mem_read(out, 4)))[0], 0)     # JobHandlerResult.pScript = 0: free the slot
        self.check_callee_saved(init, after, 'rt_job_handler_stop')


SORT_DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/jobs.hpp"
using namespace ms;
uint32_t ms::jobHostAddr(const void *) { return 0; }
void *ms::jobHostPtr(uint32_t) { return nullptr; }
static Job pool[40], temp; static JobState states[40], tempState; static uint16_t full, flag;
int main() {
    unsigned x, z;
    for(int i = 0; i < 40; ++i) { if(scanf("%u %u", &x, &z) != 2) return 1; memset(&pool[i], 0, sizeof(Job)); pool[i].x = (uint16_t)x; pool[i].z = (uint16_t)z; }
    JobEnv env = {pool, states, &temp, &tempState, &full, &flag, nullptr, nullptr, nullptr};
    jobsSort(env);
    for(int i = 0; i < 40; ++i) printf("%u ", pool[i].x);
    printf("| %u\n", temp.x);
    return 0;
}
'''


@unittest.skipUnless(NEEDS, 'needs unicorn, clang++ and build/reasm/program')
class JobSortTest(unittest.TestCase):
    """asm LAB_020E (the depth sort at the start of every job tick) in unicorn against ms::jobsSort, on random pools."""

    def test_sort_matches_asm(self):
        import random
        h = H.Harness('program')
        pool, temp = h.address('LAB_0282'), h.address('LAB_0283')
        with tempfile.TemporaryDirectory() as d:
            src, exe = os.path.join(d, 'd.cpp'), os.path.join(d, 'd.exe')
            with open(src, 'w') as f:
                f.write(SORT_DRIVER)
            subprocess.run([CXX, '-std=c++17', '-D_CRT_SECURE_NO_WARNINGS', '-I', os.path.join(ROOT, 'include'), src,
                            os.path.join(ROOT, 'src', 'engine', 'jobs.cpp'), '-o', exe], check=True)
            rng = random.Random(7)
            for case in range(60):
                zs = [rng.choice([rng.randrange(6), rng.randrange(65536), 0xFFFF, 0]) for _ in range(40)]
                if case == 0:
                    zs = list(range(40, 0, -1))        # worst case: fully reversed
                elif case == 1:
                    zs = [5] * 40                      # all equal: nothing moves
                mem = []
                for i, z in enumerate(zs):
                    rec = bytearray(42)
                    rec[6:8] = struct.pack('>H', i)    # x = the job's marker
                    rec[10:12] = struct.pack('>H', z)
                    rec[36:40] = struct.pack('>I', 0x1000 + i)  # state pointer travels with the job
                    mem.append(rec)
                regs = {'d': [0] * 8, 'a': [0] * 7 + [H.STACK_TOP]}
                res = h.run('LAB_020E', regs, [(pool, b''.join(mem))])
                out = bytearray(b''.join(mem))
                for a, data in res.writes:
                    if pool <= a < pool + 1680:
                        out[a - pool:a - pool + len(data)] = data
                asm_order = [struct.unpack('>H', out[42 * i + 6:42 * i + 8])[0] for i in range(40)]
                asm_ptrs = [struct.unpack('>I', out[42 * i + 36:42 * i + 40])[0] for i in range(40)]
                self.assertEqual(asm_ptrs, [0x1000 + m for m in asm_order])  # the original swaps whole records
                temp_x = 0
                for a, data in res.writes:
                    if a <= temp + 6 < a + len(data):
                        temp_x = struct.unpack('>H', data[temp + 6 - a:temp + 8 - a])[0]
                inp = ' '.join('%d %d' % (i, z) for i, z in enumerate(zs))
                r = subprocess.run([exe], input=inp, capture_output=True, text=True)
                order, _, tmp = r.stdout.partition('|')
                self.assertEqual([int(v) for v in order.split()], asm_order, 'case %d' % case)
                self.assertEqual(int(tmp), temp_x, 'case %d (scratch job)' % case)


class SequenceCoverageTest(unittest.TestCase):
    def test_every_intro_call_has_a_cpp_scene_or_a_known_asm_helper(self):
        with open(os.path.join(ROOT, 'src', 'engine', 'intro.cpp'), encoding='latin-1') as f:
            ids = set(re.findall(r'\{IntroOpCall, 0x([0-9A-Fa-f]{4})', f.read()))
        with open(os.path.join(ROOT, 'src', 'engine', 'scenes.cpp'), encoding='latin-1') as f:
            text = f.read()
        ours = set(re.findall(r'SCENE\(([0-9A-F]{4}),', text)) | set(re.findall(r'case 0x([0-9A-F]{4}):', text))
        with open(os.path.join(ROOT, 'src', 'rt', 'engine_intro.cpp'), encoding='latin-1') as f:
            asm_helpers = {a or b for a, b in re.findall(r'E\(([0-9A-F]{4})\)|\{0x([0-9A-F]{4}),', f.read().split('s_routines[]')[1].split(';')[0])}
        self.assertEqual(asm_helpers, {'005B'})        # 7.1q: the progress wipe LAB_05A5 is a scene of the engine
        self.assertEqual({i.upper() for i in ids} - ours - asm_helpers, set())
        self.assertTrue({'001A', '001B', '001C', '0174', '0185', '018E', '0036', '0037', '0039', '003B'} <= ours)


@unittest.skipUnless(os.path.exists(os.path.join(ROOT, 'asm', 'patches', 'program.scenes.json')), 'no patch file')
class PatchFileTest(unittest.TestCase):
    def test_patch_sites(self):
        with open(os.path.join(ROOT, 'asm', 'patches', 'program.scenes.json')) as f:
            d = json.load(f)
        self.assertEqual(d['binary'], 'program')
        ids = {p['id']: p for p in d['patches']}
        # 7.1o / 7.1q: the stubs of LAB_0017 / LAB_0018 / LAB_024B went with their last asm callers
        self.assertEqual(sorted(ids), ['scn-flash', 'scn-flash-series', 'scn-ramp-setup'])
        asm = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'program.asm')
        if os.path.exists(asm):
            with open(asm, encoding='latin-1') as f:
                lines = f.read().splitlines()
            for p in d['patches']:
                for i, orig in enumerate(p['orig']):
                    self.assertEqual(lines[p['line'] - 1 + i].split(), orig.split(), p['id'])
            # the label right before each patched body
            for lab, ln in (('LAB_0032', 562), ('LAB_003F', 829), ('LAB_0040', 837)):
                self.assertEqual(lines[ln - 1].strip(), lab + ':')

    def test_no_overlap_with_other_program_patch_files(self):
        import glob
        with open(os.path.join(ROOT, 'asm', 'patches', 'program.scenes.json')) as f:
            mine = json.load(f)['patches']
        span = lambda p: (p['line'], p['line'] + len(p['orig']) - 1)
        for f in glob.glob(os.path.join(ROOT, 'asm', 'patches', 'program*.json')):
            if f.endswith('program.scenes.json'):
                continue
            with open(f) as fh:
                others = json.load(fh)['patches']
            for q in others:
                if q.get('kind') == 'as_data':
                    qa, qb = q['lines']
                else:
                    qa, qb = span(q)
                for p in mine:
                    pa, pb = span(p)
                    self.assertTrue(pb < qa or qb < pa, '%s overlaps %s (%s)' % (p['id'], q['id'], os.path.basename(f)))


if __name__ == '__main__':
    unittest.main()
