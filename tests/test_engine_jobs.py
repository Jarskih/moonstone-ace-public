"""Host test for src/engine/jobs.cpp (ROADMAP 4.3): the pool logic of program.asm LAB_01DA/01E4/01E8/01EC, compiled
with clang++ and run against fake asm callbacks. Layout (sizeof/offsetof) is checked by static_asserts in jobs.hpp,
so the driver only exercises behaviour; fields are host-endian, nothing here depends on byte order."""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "engine/jobs.hpp"
using namespace ms;

// 64-bit host: game addresses are handles into this table.
static const void *g_h[64]; static unsigned g_n = 1;
uint32_t ms::jobHostAddr(const void *p) {
    for(unsigned i = 1; i < g_n; ++i) if(g_h[i] == p) return i;
    g_h[g_n] = p; return g_n++;
}
void *ms::jobHostPtr(uint32_t a) { return (void *)g_h[a]; }

static Job pool[40], temp; static JobState states[40], tempState;
static uint16_t full, flag273; static uint32_t handlers[22];
static int handlerCalls; static const Job *ran[8]; static const JobState *ranSt[8]; static unsigned nran;
static uint32_t handlerReply[4]; // per handler address
static JobHandlerResult reply;

static void callHandler(uint32_t addr, Job *j, JobHandlerResult *out) { ++handlerCalls; (void)addr; (void)j; *out = reply; }
static void runScript(Job *j, JobState *s) { ran[nran] = j; ranSt[nran] = s; ++nran; }

static int fails = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while(0)

int main() {
    JobEnv env = {pool, states, &temp, &tempState, &full, &flag273, (const uint8_t *)handlers, callHandler, runScript};
    memset(pool, 0xAA, sizeof pool); memset(states, 0xBB, sizeof states);
    jobsReset(env);
    for(int i = 0; i < 40; ++i) { CHECK(jobPtr<JobState>(pool[i].pState) == &states[i]); CHECK(pool[i].active == 0 && pool[i].paused == 0); }
    CHECK(states[0].twin == 0 && states[1].twin == 0xBB);  // only block 0 is cleared (original quirk)

    // alloc: first free slot, state block cleared, fields set, slots fill in order, 41st fails with flag 2.
    JobInit in = {0x1000, 0x2000, 0x3000, 10, 20, 30, 5, 4};
    pool[0].sx = 0x1234;
    Job *a = jobAlloc(env, in);
    CHECK(a == &pool[0] && a->active == 1 && a->hasScript == 1 && a->pScript == 0x1000 && a->id == 0x2000 &&
          a->pFrames == 0x3000 && a->x == 10 && a->y == 20 && a->z == 30 && a->flags == 5 && a->handler == 4);
    CHECK(a->sx == 0x1234);  // untouched field kept
    for(int i = 1; i < 40; ++i) { in.id = i; CHECK(jobAlloc(env, in) == &pool[i]); }
    full = 0; CHECK(jobAlloc(env, in) == nullptr && full == 2);
    pool[7].active = 0; CHECK(jobAlloc(env, in) == &pool[7]);

    // spawn-queued: only active && !hasScript slots call the handler; table index is a byte offset.
    for(int i = 0; i < 40; ++i) pool[i].hasScript = 1;
    pool[3].hasScript = 0; pool[5].hasScript = 0; pool[6].hasScript = 0; pool[8].active = 0; pool[8].hasScript = 0;
    pool[3].handler = 0; pool[5].handler = 0; pool[6].handler = 0;
    handlerCalls = 0;
    reply = {JOB_HANDLER_KEEP, 1, 2, 3, 4, 0}; jobsSpawnQueued(env);
    CHECK(handlerCalls == 3 && pool[3].hasScript == 0);
    reply = {0, 0, 0, 0, 0, 0}; pool[5].hasScript = 1; pool[6].hasScript = 1; jobsSpawnQueued(env);
    CHECK(pool[3].active == 0 && pool[3].hasScript == 0 && pool[5].active == 1);
    pool[3].active = 1; pool[3].hasScript = 0; reply = {0x5000, 11, 22, 33, 9, 0}; jobsSpawnQueued(env);
    CHECK(pool[3].pScript == 0x5000 && pool[3].x == 11 && pool[3].y == 22 && pool[3].z == 33 && pool[3].flags == 9 && pool[3].hasScript == 1);

    // tick: the depth sort first, paused/inactive skipped, flag273 cleared, twin job = scratch copy then real.
    for(int i = 0; i < 40; ++i) { pool[i].active = 0; pool[i].paused = 0; pool[i].z = 30; states[i].twin = 0; }
    pool[2].active = 1; pool[4].active = 1; pool[4].paused = 1; pool[9].active = 1;
    states[9].twin = 1; states[9].pTwinScript = 0x7777; pool[9].y = 55; pool[9].pScript = 0x1111;
    flag273 = 9; nran = 0;
    jobsTick(env);
    CHECK(flag273 == 0 && nran == 3);
    CHECK(ran[0] == &pool[2] && ranSt[0] == &states[2]);
    CHECK(ran[1] == &temp && ranSt[1] == &states[9] && temp.y == 0 && temp.pScript == 0x7777 && temp.pState == jobAddr(&tempState));
    CHECK(ran[2] == &pool[9] && pool[9].y == 55 && pool[9].pScript == 0x1111);

    // depth sort (asm LAB_020E): bubble passes while any slot has a successor with a smaller z; whole 42-byte swaps.
    for(int i = 0; i < 40; ++i) { pool[i].z = (uint16_t)(1000 - 7 * i + ((i & 3) == 1 ? 40 : 0)); pool[i].x = (uint16_t)i; }
    pool[5].z = pool[6].z;  // equal z keeps its order (BCC: a successor with z >= does not swap)
    Job before[40]; memcpy(before, pool, sizeof before);
    jobsSort(env);
    for(int i = 0; i + 1 < 40; ++i) CHECK(pool[i].z <= pool[i + 1].z);
    bool stable = true; for(int i = 0; i + 1 < 40; ++i) if(pool[i].z == pool[i + 1].z && pool[i].x > pool[i + 1].x) stable = false;
    CHECK(stable);
    unsigned seen = 0; for(int i = 0; i < 40; ++i) seen |= 0; (void)seen;
    for(int i = 0; i < 40; ++i) { int found = 0; for(int j = 0; j < 40; ++j) if(memcmp(&pool[i], &before[j], sizeof(Job)) == 0) ++found; CHECK(found == 1); }
    // LAB_024B: 0x820 bytes of $FF, nothing beyond
    static uint8_t lists[0x830]; memset(lists, 0x55, sizeof lists);
    jobsClearDrawLists(lists);
    for(int i = 0; i < 0x820; ++i) CHECK(lists[i] == 0xFF);
    for(int i = 0x820; i < 0x830; ++i) CHECK(lists[i] == 0x55);
    printf(fails ? "FAILED\n" : "OK\n");
    return fails;
}
'''


@unittest.skipUnless(CXX, 'clang++ not on PATH')
class JobsHostTest(unittest.TestCase):
    def test_pool_logic(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, 'drv.cpp')
            exe = os.path.join(d, 'drv.exe')
            with open(src, 'w') as f:
                f.write(DRIVER)
            r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-I', os.path.join(ROOT, 'include'),
                                src, os.path.join(ROOT, 'src', 'engine', 'jobs.cpp'), '-o', exe],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn('OK', r.stdout)


if __name__ == '__main__':
    unittest.main()
