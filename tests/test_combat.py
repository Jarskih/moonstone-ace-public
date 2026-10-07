"""Host test for src/game/combat.cpp (ROADMAP 6.4).  The C++ is compiled with clang++ (no STL in the game code; the driver
may use it) together with rules.cpp (settleFight / settleDefeats) and compared, over many random states, with a literal
Python model of the mog.asm routines it replaces, transcribed from the asm text one block at a time:

  fight loop      LAB_0036 with LAB_000A, LAB_003E, LAB_0042, LAB_0048, LAB_004B, LAB_000E (via the rules model)
  creature count  LAB_0005 / LAB_0006 / LAB_0008
  entries         LAB_004F (+ LAB_0053..LAB_0057), LAB_0058, LAB_0065, LAB_005B (+ LAB_005F), LAB_0083..LAB_00B2
  arenas          LAB_0164, LAB_0165, LAB_0167, LAB_0192, LAB_0195, LAB_01A3
  screen loop     LAB_04CF / LAB_04D0 / LAB_04D2, LAB_04D4..LAB_04E0, LAB_04E1

Both sides are driven the same way: the asm primitives behind the callbacks (draw, sound, job table, ...) are not
modelled; every call is logged with its arguments, and a small script of "effects" (a cell or record field set after the
n-th call of an op) and return values (queues per op) stands in for what the primitive would have changed, applied
identically to the C++ driver and to the model.  A case passes when the call logs, every cell and every record
(big-endian images, all unrelated bytes random so stray writes show) agree.

Fixed address map (identical on both sides): knights 0x10000 + 132 i (i = 4 is the dragon), creature pool 0x20000 +
132 i, lairs 0x30000 + 20 i, inventories 0x40000 + 24 i, lair loot 0x41000 + 24 i, ActiveKnights 0x50000.

Needs clang++ on PATH and nothing else (the asm is quoted in the comments).
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: tests that read the listing / asm reference skip without it)
import os
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from test_game_rules import (g8, g16, g32, p8, p16, p32, s8, s16, h, m001c, m000e, rnd_knight, rnd_inv)  # noqa: E402

ROOT = os.path.dirname(HERE)
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
CXX = shutil.which('clang++')

KB, PB, LB, IB, OB = 0x10000, 0x20000, 0x30000, 0x40000, 0x41000
ACT, DEFEAT, MODE = 0x50000, 0x51000, 0x51010
DRAGON = KB + 4 * 132
ADDR = dict(a05f5=0x6F5, a05f6=0x6F6, a05f7=0x6F7, a0610=0x710, a05f8=0x6F8, a07db=0x7DB, a07dc=0x7DC, a05e1=0x6E1,
            a05e0=0x6E0, a07fc=0x7FC, a0166=0x166, a07f5=0x7F5, a07fe=0x7FE, a07fb=0x7FB, a0603=0x7603, a0880=0x880, a0604=0x7604, a060f=0x760F, a0882=0x882)
# the table the dragon arena patches in place lives in the "image": hurt tables and the damage table (4 longs apart
# is enough for the pokes: they are only logged)

# name, width in bytes, C expression of the cell in the driver
CELLS = [
    ('w0620', 2, 'C.w0620'), ('soundstep', 2, 'C.soundstep'),
    ('warn0', 4, 'C.warn[0]'), ('warn1', 4, 'C.warn[1]'), ('warn2', 4, 'C.warn[2]'),
    ('warn3', 4, 'C.warn[3]'), ('warn4', 4, 'C.warn[4]'), ('warn5', 4, 'C.warn[5]'),
    ('col0', 2, 'C.col[0]'), ('col1', 2, 'C.col[1]'), ('col2', 2, 'C.col[2]'),
    ('frameflag', 2, 'C.frameflag'), ('framestart', 4, 'C.framestart'), ('tick', 4, 'C.tick'),
    ('arenakind', 4, 'C.arenakind'), ('creaturebase', 4, 'C.creaturebase'), ('w06fc', 2, 'C.w06fc'),
    ('key', 2, 'C.key'), ('turnspent', 2, 'C.turnspent'), ('turnbudget', 2, 'C.turnbudget'),
    ('badluck', 2, 'C.badluck'), ('swapped', 2, 'C.swapped'), ('avoidby', 4, 'C.avoidby'),
    ('avoidwho', 4, 'C.avoidwho'), ('lastslot', 2, 'C.lastslot'), ('travel', 2, 'C.travel'),
    ('w0667', 2, 'C.w0667'), ('scene76d', 4, 'C.scene76d'), ('lairparam', 4, 'C.lairparam'),
    ('arenaparam', 4, 'C.arenaparam'), ('lair', 4, 'C.lair'), ('c5ec', 2, 'C.c5ec'), ('c5ed', 2, 'C.c5ed'),
    ('c5ee', 2, 'C.c5ee'), ('spawnfn', 4, 'C.spawnfn'), ('spawnfn2', 4, 'C.spawnfn2'),
    ('framedelay', 2, 'C.framedelay'), ('w0623', 2, 'C.w0623'), ('bat1', 4, 'C.bat1'), ('bat2', 4, 'C.bat2'),
    ('fighter', 4, 'C.fighter'), ('current', 4, 'C.current'), ('target', 4, 'C.target'), ('skipjob', 4, 'C.skipjob'),
    ('textflag', 2, 'C.textflag'), ('spritesrc', 4, 'C.spritesrc'), ('sprites', 4, 'C.sprites'),
    ('changed', 2, 'C.changed'), ('scene', 4, 'C.scene'), ('prevscene', 4, 'C.prevscene'), ('done', 2, 'C.done'),
    ('curx', 2, 'C.curx'), ('cury', 2, 'C.cury'), ('curlock', 2, 'C.curlock'), ('handover', 2, 'C.handover'),
    ('buttons', 4, 'C.buttons'), ('statknight', 4, 'C.statknight'), ('linebase', 2, 'C.linebase'),
    ('xshift', 2, 'C.xshift'), ('knighta', 4, 'C.knighta'), ('knightb', 4, 'C.knightb'),
    ('pal0', 2, 'C.pal[0]'), ('pal1', 2, 'C.pal[1]'), ('pal2', 2, 'C.pal[2]'), ('pal3', 2, 'C.pal[3]'),
]
CW = {n: w for n, w, _ in CELLS}
RECS = {'act': 22, 'defeat': 2, 'mode': 2}
for _i in range(5):
    RECS['k%d' % _i] = 132
    RECS['i%d' % _i] = 24
for _i in range(4):
    RECS['p%d' % _i] = 132
for _i in range(2):
    RECS['o%d' % _i] = 24
    RECS['l%d' % _i] = 20

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/combat.hpp"
#include "game/rules.hpp"
using namespace ms::game;

static const uint32_t KB = 0x10000, PB = 0x20000, LB = 0x30000, IB = 0x40000, OB = 0x41000;

static const int KW[] = {4, 6, 8, 62, 64, 66, 68, 74, 78, 80, 84, 116, 118, 120, 122, 124, 126, 128};
static const int KL[] = {0, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 58, 88, 92, 96, 100, 108};
static void sw2(uint8_t *p) { uint8_t t = p[0]; p[0] = p[1]; p[1] = t; }
static void sw4(uint8_t *p) { uint8_t t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }
static void swapK(uint8_t *p) { for (int o : KW) sw2(p + o); for (int o : KL) sw4(p + o); }
static void swapAct(uint8_t *p) { sw4(p + 0); sw4(p + 4); sw4(p + 10); sw2(p + 14); sw2(p + 18); sw2(p + 20); }
static void swapLair(uint8_t *p) { sw4(p + 0); sw2(p + 4); sw2(p + 6); sw2(p + 8); sw2(p + 10); sw2(p + 12); sw2(p + 14); sw4(p + 16); }

static Knight aK[5], aP[4];
static Inventory aI[5], aO[2];
static Lair aL[2];
static ActiveKnights act;
static ByteSlot defeat, mode;
static const uint16_t WIDE[3] = {9, 3, 0xFFFF};

static struct Cells {
	uint16_t w0620, soundstep, frameflag, w06fc, turnspent, turnbudget, badluck, swapped, lastslot, travel, w0667,
		c5ec, c5ed, c5ee, framedelay, w0623, col[3];
	volatile uint16_t key;
	uint32_t warn[6], framestart, tick, arenakind, creaturebase, avoidby, avoidwho, scene76d, lairparam, arenaparam, lair,
		spawnfn, spawnfn2, bat1, bat2, fighter, current, target, skipjob;
	uint16_t textflag, changed, done, curx, cury, curlock, handover, linebase, xshift, pal[4];
	uint32_t spritesrc, sprites, scene, prevscene, buttons, statknight, knighta, knightb;
} C;

struct Cell { const char *name; int width; void *p; };
static Cell g_cells[%NCELLS%];
static const int g_ncells = %NCELLS%;
static void buildCells() {
%CELLTABLE%
}
static uint64_t getCell(const Cell &c) {
	if (c.width == 1) return *(volatile uint8_t *)c.p;
	if (c.width == 2) return *(volatile uint16_t *)c.p;
	return *(volatile uint32_t *)c.p;
}
static void setCell(const Cell &c, uint64_t v) {
	if (c.width == 1) *(volatile uint8_t *)c.p = (uint8_t)v;
	else if (c.width == 2) *(volatile uint16_t *)c.p = (uint16_t)v;
	else *(volatile uint32_t *)c.p = (uint32_t)v;
}
static Cell *findCell(const char *n) { for (int i = 0; i < g_ncells; ++i) if (!strcmp(n, g_cells[i].name)) return &g_cells[i]; return 0; }

// ---- records by id
struct RecDef { char id[8]; int size; uint8_t *p; int kind; };   // kind 0 = bytes, 1 = Knight, 2 = act, 3 = lair
static RecDef g_recs[32];
static int g_nrecs;
static void addRec(const char *id, int size, void *p, int kind) {
	RecDef &r = g_recs[g_nrecs++];
	snprintf(r.id, sizeof r.id, "%s", id); r.size = size; r.p = (uint8_t *)p; r.kind = kind;
}
static void addRecN(char c, int i, int size, void *p, int kind) {
	char id[8]; snprintf(id, sizeof id, "%c%d", c, i); addRec(id, size, p, kind);
}
static void buildRecs() {
	g_nrecs = 0;
	addRec("act", 22, &act, 2);
	addRec("defeat", 2, &defeat, 0);
	addRec("mode", 2, &mode, 0);
	for (int i = 0; i < 5; ++i) addRecN('k', i, 132, &aK[i], 1);
	for (int i = 0; i < 4; ++i) addRecN('p', i, 132, &aP[i], 1);
	for (int i = 0; i < 5; ++i) addRecN('i', i, 24, &aI[i], 0);
	for (int i = 0; i < 2; ++i) addRecN('o', i, 24, &aO[i], 0);
	for (int i = 0; i < 2; ++i) addRecN('l', i, 20, &aL[i], 3);
}
static RecDef *findRec(const char *id) { for (int i = 0; i < g_nrecs; ++i) if (!strcmp(id, g_recs[i].id)) return &g_recs[i]; return 0; }
static void swapRec(int kind, uint8_t *p) {
	if (kind == 1) swapK(p); else if (kind == 2) swapAct(p); else if (kind == 3) swapLair(p);
}
static int unhex(const char *s, uint8_t *out, int max) {
	int n = 0;
	while (s[0] && s[1] && n < max) { unsigned v; sscanf(s, "%2x", &v); out[n++] = (uint8_t)v; s += 2; }
	return n;
}
static void loadRec(RecDef &r, const char *hex) { uint8_t b[256]; unhex(hex, b, r.size); memcpy(r.p, b, r.size); swapRec(r.kind, r.p); }
static void dumpRec(RecDef &r) {
	uint8_t b[256]; memcpy(b, r.p, r.size); swapRec(r.kind, b);
	printf("R %s ", r.id);
	for (int i = 0; i < r.size; ++i) printf("%02x", b[i]);
	printf("\n");
}
static void patchRec(const char *id, int off, int width, uint64_t v) {
	RecDef *r = findRec(id);
	uint8_t b[256]; memcpy(b, r->p, r->size); swapRec(r->kind, b);
	for (int i = 0; i < width; ++i) b[off + i] = (uint8_t)(v >> (8 * (width - 1 - i)));
	swapRec(r->kind, b); memcpy(r->p, b, r->size);
}

// ---- address resolution
static void fault(uint32_t a) { printf("FAULT %08x\n", a); exit(2); }
static Knight *kn(uint32_t a) {
	if (a >= KB && a < KB + 5 * 132 && (a - KB) % 132 == 0) return &aK[(a - KB) / 132];
	if (a >= PB && a < PB + 4 * 132 && (a - PB) % 132 == 0) return &aP[(a - PB) / 132];
	fault(a); return 0;
}
static Inventory *inv(uint32_t a) {
	if (a >= IB && a < IB + 5 * 24 && (a - IB) % 24 == 0) return &aI[(a - IB) / 24];
	if (a >= OB && a < OB + 2 * 24 && (a - OB) % 24 == 0) return &aO[(a - OB) / 24];
	fault(a); return 0;
}
static Lair *lair(uint32_t a) {
	if (a >= LB && a < LB + 2 * 20 && (a - LB) % 20 == 0) return &aL[(a - LB) / 20];
	fault(a); return 0;
}

// ---- the op log, the effects and the return queues
struct Fx { char op[24]; int occ; char target[24]; uint64_t val; };
static Fx g_fx[128];
static int g_nfx;
struct Occ { char name[24]; int n; };
static Occ g_occ[96];
static int g_nocc;
struct Ret { char name[24]; uint64_t v[96]; int n, pos; };
static Ret g_ret[16];
static int g_nret;
static int g_inc = 1;
static long g_lines;

static void applyTarget(const char *t, uint64_t v) {
	const char *d = strchr(t, '.');
	if (!d) { Cell *c = findCell(t); if (!c) { printf("NOCELL %s\n", t); exit(2); } setCell(*c, v); return; }
	char id[16]; memcpy(id, t, d - t); id[d - t] = 0;
	int off = atoi(d + 1); const char *d2 = strchr(d + 1, '.');
	patchRec(id, off, atoi(d2 + 1), v);
}
static void logop(const char *name, int n, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0) {
	if (n == 0) printf("L %s\n", name);
	else if (n == 1) printf("L %s %llx\n", name, (unsigned long long)a);
	else if (n == 2) printf("L %s %llx %llx\n", name, (unsigned long long)a, (unsigned long long)b);
	else printf("L %s %llx %llx %llx\n", name, (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
	if (++g_lines > 200000) { printf("RUNAWAY\n"); exit(2); }
}
static void applyFx(const char *name) {
	int i = 0;
	for (; i < g_nocc; ++i) if (!strcmp(g_occ[i].name, name)) break;
	if (i == g_nocc) { snprintf(g_occ[i].name, 24, "%s", name); g_occ[i].n = 0; ++g_nocc; }
	int occ = g_occ[i].n++;
	for (int j = 0; j < g_nfx; ++j) if (g_fx[j].occ == occ && !strcmp(g_fx[j].op, name)) applyTarget(g_fx[j].target, g_fx[j].val);
}
static uint64_t popRet(const char *name, uint64_t dflt) {
	for (int i = 0; i < g_nret; ++i)
		if (!strcmp(g_ret[i].name, name)) return g_ret[i].pos < g_ret[i].n ? g_ret[i].v[g_ret[i].pos++] : dflt;
	return dflt;
}
#define OP0(NAME) static void op_##NAME() { logop(#NAME, 0); applyFx(#NAME); }
#define OP1(NAME) static void op_##NAME(uint32_t a) { logop(#NAME, 1, a); applyFx(#NAME); }
OP0(clearScriptSlots) OP0(flip) OP0(initFightScreen) OP0(frameStart) OP0(jobPass) OP0(tick) OP0(contactPass)
OP0(drawPass) OP0(extraPass) OP0(frameWait) OP0(soundTick) OP0(afterFight) OP0(fadeIn) OP0(clearJobs) OP0(resetScreen)
OP0(returnToMap) OP0(clearObjects) OP0(op0155) OP0(fadeOut) OP0(showAvoidText) OP0(waitFire) OP0(op03EB) OP0(mapStep)
OP0(op015F) OP0(op0116) OP0(op0121) OP0(placeAttacker)
OP1(togglePause) OP1(killJob) OP1(screen) OP1(copyName) OP1(commonSetup) OP1(op0100) OP1(paletteMode)
static void op_creatureArena(int32_t a) { logop("creatureArena", 1, (uint32_t)a); applyFx("creatureArena"); }
static void op_keyReset() { logop("keyReset", 0); C.key = 1; applyFx("keyReset"); }
static void op_callSpawn(uint32_t a) { logop("callSpawn", 1, a); C.c5ee = (uint16_t)(C.c5ee + g_inc); applyFx("callSpawn"); }
static uint16_t op_translateKey(uint16_t k) { logop("translateKey", 1, k); applyFx("translateKey"); return (uint16_t)popRet("translateKey", 0); }
static uint32_t op_spawnWarning(uint16_t t, uint16_t c) { logop("spawnWarning", 2, t, c); applyFx("spawnWarning"); return (uint32_t)popRet("spawnWarning", 0x7000 + t); }
static void op_spawn(uint32_t r, uint32_t s) { logop("spawn", 2, r, s); applyFx("spawn"); }
static uint32_t op_allocCreature() {
	logop("allocCreature", 0);
	uint32_t a = (uint32_t)popRet("allocCreature", PB);
	kn(a)->ulActive = 1;
	applyFx("allocCreature");
	return a;
}
static void op_poke32(uint32_t a, uint32_t v) { logop("poke32", 2, a, v); applyFx("poke32"); }

static FightEnv g_fe;
static FightOps g_fo;
static ScreenEnv g_se;
static ScreenOps g_so;

// screen ops (own functions, same log names)
#define SOP0(NAME) static void sop_##NAME() { logop(#NAME, 0); applyFx(#NAME); }
SOP0(op0575) SOP0(op0588) SOP0(op057B) SOP0(op044E) SOP0(op04EA) SOP0(blitBackground) SOP0(setTarget) SOP0(lootSetup)
SOP0(op03A7) SOP0(op051B) SOP0(op0524) SOP0(op0522) SOP0(op04FE) SOP0(op051D) SOP0(blitScreen)
SOP0(waitBlitter) SOP0(op0D8A) SOP0(op03EE) SOP0(flip) SOP0(drawPass) SOP0(fadeOut)
static void sop_drawStats() { logop("drawStats", 3, C.statknight, C.linebase, C.xshift); applyFx("drawStats"); }
static uint32_t sop_hitTest(uint32_t x, uint32_t y) { logop("hitTest", 2, x, y); applyFx("hitTest"); return (uint32_t)popRet("hitTest", 0x777); }
static void sop_click(uint32_t r) { logop("click", 1, r); applyFx("click"); }
static void sop_op051F(uint32_t k) { logop("op051F", 1, k); applyFx("op051F"); }

static void setup() {
	buildCells(); buildRecs();
	g_fe.a.ulActionScripts = 0x6F5; g_fe.a.ulHurtScripts = 0x6F6; g_fe.a.ulDamageTable = 0x6F7; g_fe.a.ulKnightWalkTab = 0x710;
	g_fe.a.ulDefenseTable = 0x6F8; g_fe.a.ulIdleScript = 0x7DB; g_fe.a.ulAltScript = 0x7DC; g_fe.a.ulJobParamKnight = 0x6E1;
	g_fe.a.ulJobParamFight = 0x6E0; g_fe.a.ulSpawnScript = 0x7FC; g_fe.a.ulNoSpawn = 0x166; g_fe.a.ulKnight0 = KB;
	g_fe.a.ulKnight1 = KB + 132; g_fe.a.ulDragonRecord = KB + 4 * 132; g_fe.a.ulDragonHurtA = 0x7F5;
	g_fe.a.ulDragonHurtB = 0x7FE; g_fe.a.ulDragonHurtC = 0x7FB; g_fe.a.ulDragonDamage = 0x7603; g_fe.a.ulBatScript = 0x880;
	g_fe.a.ulDragonHurtTab = 0x7604; g_fe.a.ulDragonWalkTab = 0x760F; g_fe.a.ulDragonIdle = 0x882;
	g_fe.m.pfnKnight = kn; g_fe.m.pfnInventory = inv; g_fe.m.pfnLair = lair;
	g_fe.aKnights = aK; g_fe.aInv = aI;
	FightCells &c = g_fe.c;
	c.pAct = &act; c.pDefeat = &defeat; c.pMode = &mode; c.pFighter = &C.fighter; c.pCurrent = &C.current; c.pTarget = &C.target;
	c.pSkipJob = &C.skipjob; c.pActionLatch = &C.w0620; c.pSoundStep = &C.soundstep; c.pWarn = C.warn; c.pWarnColours = C.col;
	c.pFrameFlag = &C.frameflag; c.pFrameStart = &C.framestart; c.pTick = &C.tick; c.pArenaKind = &C.arenakind;
	c.pCreatureBase = &C.creaturebase; c.pExtraPassFlag = &C.w06fc; c.pKey = &C.key; c.pTurnSpent = &C.turnspent;
	c.pTurnBudget = &C.turnbudget; c.pBadLuck = &C.badluck; c.pSwapped = &C.swapped; c.pAvoidBy = &C.avoidby;
	c.pAvoidWho = &C.avoidwho; c.pLastSlot = &C.lastslot; c.pTravel = &C.travel; c.pDragonActive = &C.w0667;
	c.pEncounterKind = &C.scene76d; c.pLairParam = &C.lairparam; c.pArenaParam = &C.arenaparam; c.pLair = &C.lair;
	c.pFightTotal = &C.c5ec; c.pMaxAlive = &C.c5ed; c.pAliveNow = &C.c5ee; c.pSpawnFn = &C.spawnfn; c.pSpawnFn2 = &C.spawnfn2;
	c.pFrameDelay = &C.framedelay; c.pDragonFlags = &C.w0623; c.pHandleBat1 = &C.bat1; c.pHandleBat2 = &C.bat2;
	FightOps &o = g_fo;
	o.keyReset = op_keyReset; o.clearScriptSlots = op_clearScriptSlots; o.flip = op_flip; o.togglePause = op_togglePause;
	o.initFightScreen = op_initFightScreen; o.frameStart = op_frameStart; o.jobPass = op_jobPass; o.tick = op_tick;
	o.contactPass = op_contactPass; o.drawPass = op_drawPass; o.extraPass = op_extraPass; o.translateKey = op_translateKey;
	o.frameWait = op_frameWait; o.spawnWarning = op_spawnWarning; o.killJob = op_killJob; o.soundTick = op_soundTick;
	o.afterFight = op_afterFight; o.fadeIn = op_fadeIn; o.clearJobs = op_clearJobs; o.resetScreen = op_resetScreen;
	o.screen = op_screen; o.returnToMap = op_returnToMap; o.clearObjects = op_clearObjects; o.op0155 = op_op0155;
	o.fadeOut = op_fadeOut; o.copyName = op_copyName; o.showAvoidText = op_showAvoidText; o.waitFire = op_waitFire;
	o.op03EB = op_op03EB; o.mapStep = op_mapStep; o.callSpawn = op_callSpawn; o.commonSetup = op_commonSetup;
	o.op0100 = op_op0100; o.op015F = op_op015F; o.op0116 = op_op0116; o.op0121 = op_op0121; o.placeAttacker = op_placeAttacker;
	o.spawn = op_spawn; o.allocCreature = op_allocCreature; o.paletteMode = op_paletteMode; o.poke32 = op_poke32;
	o.creatureArena = op_creatureArena;
	ScreenCells &s = g_se.c;
	s.pTextFlag = &C.textflag; s.pSpriteSrc = &C.spritesrc; s.pSprites = &C.sprites; s.pChanged = &C.changed; s.pScene = &C.scene;
	s.pPrevScene = &C.prevscene; s.pDone = &C.done; s.pCursorX = &C.curx; s.pCursorY = &C.cury; s.pCursorLock = &C.curlock;
	s.pHandOver = &C.handover; s.pDragon = &aK[4]; s.pAct = &act; s.pButtons = &C.buttons;
	s.pStatKnight = &C.statknight; s.pLineBase = &C.linebase; s.pXShift = &C.xshift; s.pKnightA = &C.knighta;
	s.pKnightB = &C.knightb; s.pPalette = C.pal; s.pWideScenes = WIDE;
	g_se.a.ulUseButtons = 0x699; g_se.a.ulTakeButtons = 0x69A; g_se.a.ulMarketRecord = 0x690; g_se.pfnKnight = kn;
	ScreenOps &q = g_so;
	q.fadeOut = sop_fadeOut; q.op0575 = sop_op0575; q.op0588 = sop_op0588; q.op057B = sop_op057B; q.hitTest = sop_hitTest;
	q.click = sop_click; q.flip = sop_flip; q.drawPass = sop_drawPass; q.op044E = sop_op044E; q.op04EA = sop_op04EA;
	q.blitBackground = sop_blitBackground; q.setTarget = sop_setTarget; q.lootSetup = sop_lootSetup; q.op03A7 = sop_op03A7;
	q.drawStats = sop_drawStats; q.op051B = sop_op051B; q.op0524 = sop_op0524; q.op0522 = sop_op0522; q.op04FE = sop_op04FE;
	q.op051F = sop_op051F; q.op051D = sop_op051D; q.blitScreen = sop_blitScreen; q.waitBlitter = sop_waitBlitter;
	q.op0D8A = sop_op0D8A; q.op03EE = sop_op03EE;
}

int main() {
	static char line[1 << 16];
	setup();
	while (fgets(line, sizeof line, stdin)) {
		char *tok[256]; int nt = 0;
		for (char *t = strtok(line, " \r\n"); t && nt < 256; t = strtok(0, " \r\n")) tok[nt++] = t;
		if (!nt) continue;
		memset((void *)&C, 0, sizeof C);
		memset(aK, 0, sizeof aK); memset(aP, 0, sizeof aP); memset(aI, 0, sizeof aI); memset(aO, 0, sizeof aO);
		memset(aL, 0, sizeof aL); memset(&act, 0, sizeof act); memset(&defeat, 0, sizeof defeat); memset(&mode, 0, sizeof mode);
		g_nfx = 0; g_nocc = 0; g_nret = 0; g_inc = 1; g_lines = 0;
		const char *fn = ""; uint64_t args[4] = {0, 0, 0, 0}; int na = 0;
		for (int i = 0; i < nt; ++i) {
			char *t = tok[i];
			if (!strncmp(t, "fn:", 3)) fn = t + 3;
			else if (!strncmp(t, "arg:", 4)) args[na++] = strtoull(t + 4, 0, 16);
			else if (!strncmp(t, "inc:", 4)) g_inc = atoi(t + 4);
			else if (!strncmp(t, "c:", 2)) {
				char *q = strchr(t, '='); *q = 0; Cell *c = findCell(t + 2);
				if (!c) { printf("NOCELL %s\n", t); return 2; } setCell(*c, strtoull(q + 1, 0, 16));
			} else if (!strncmp(t, "r:", 2)) {
				char *q = strchr(t, '='); *q = 0; RecDef *r = findRec(t + 2);
				if (!r) { printf("NOREC %s\n", t); return 2; } loadRec(*r, q + 1);
			} else if (!strncmp(t, "fx:", 3)) {          // fx:op#occ:target=hex
				char *h1 = strchr(t, '#'), *c1 = strchr(h1, ':'), *eq = strchr(c1, '=');
				Fx &f = g_fx[g_nfx++]; *h1 = 0; *c1 = 0; *eq = 0;
				snprintf(f.op, 24, "%s", t + 3); f.occ = atoi(h1 + 1); snprintf(f.target, 24, "%s", c1 + 1); f.val = strtoull(eq + 1, 0, 16);
			} else if (!strncmp(t, "ret:", 4)) {          // ret:op=hex,hex
				char *q = strchr(t, '='); *q = 0;
				Ret &r = g_ret[g_nret++]; snprintf(r.name, 24, "%s", t + 4); r.n = 0; r.pos = 0;
				for (char *p = q + 1; *p;) { char *e; r.v[r.n++] = strtoull(p, &e, 16); p = (*e == ',') ? e + 1 : e; }
			}
		}
		if (!strcmp(fn, "fightRun")) fightRun(g_fe, g_fo);
		else if (!strcmp(fn, "creatureDied")) fightCreatureDied(g_fe, g_fo);
		else if (!strcmp(fn, "fightEnd")) fightEnd(g_fe);
		else if (!strcmp(fn, "meet")) fightMeet(g_fe, g_fo, (uint32_t)args[0], (uint32_t)args[1]);
		else if (!strcmp(fn, "avoid")) { bool b = fightAvoid(g_fe, g_fo); printf(b ? "L RET 1\n" : "L RET 0\n"); }
		else if (!strcmp(fn, "creature")) fightCreature(g_fe, g_fo, (uint32_t)args[0]);
		else if (!strcmp(fn, "lairTidy")) lairTidy(g_fe);
		else if (!strcmp(fn, "dragon")) fightDragon(g_fe, g_fo);
		else if (!strcmp(fn, "tables")) arenaKnightTables(g_fe, *kn((uint32_t)args[0]));
		else if (!strcmp(fn, "arenaMeet")) arenaMeet(g_fe, g_fo);
		else if (!strcmp(fn, "arenaPractice")) arenaPractice(g_fe, g_fo);
		else if (!strcmp(fn, "arenaDragon")) arenaDragon(g_fe, g_fo);
		else if (!strcmp(fn, "arenaCreature")) arenaCreature(g_fe, g_fo);
		else if (!strcmp(fn, "screenRun")) screenRun(g_se, g_so, (uint32_t)args[0]);
		else if (!strcmp(fn, "screenRedraw")) screenRedraw(g_se, g_so);
		else if (!strcmp(fn, "screenPalette")) screenPalette(g_se);
		else { printf("NOFN %s\n", fn); return 2; }
		for (int i = 0; i < g_ncells; ++i) printf("C %s %llx\n", g_cells[i].name, (unsigned long long)getCell(g_cells[i]));
		for (int i = 0; i < g_nrecs; ++i) dumpRec(g_recs[i]);
		printf("E\n");
	}
	return 0;
}
'''


def celltable():
    out = []
    for i, (n, w, e) in enumerate(CELLS):
        out.append('\tg_cells[%d] = {"%s", %d, (void *)&%s};' % (i, n, w, e))
    return '\n'.join(out)


_BUILD = {}


def run_driver(lines):
    with tempfile.TemporaryDirectory() as tmp:
        p, exe = os.path.join(tmp, 'drv.cpp'), os.path.join(tmp, 'drv.exe')
        with open(p, 'w') as f:
            f.write(DRIVER.replace('%CELLTABLE%', celltable()).replace('%NCELLS%', str(len(CELLS))))
        srcs = [p] + [os.path.join(ROOT, 'src', 'game', x) for x in ('combat.cpp', 'rules/stats.cpp', 'rules/healing.cpp', 'rules/settle.cpp', 'rules/clock.cpp', 'rules/turns.cpp', 'rules/waves.cpp')] + [GAMEDATA_SOURCE]
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS',
               '-Wno-unused-function', '-I', os.path.join(ROOT, 'include')] + srcs + ['-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stderr)
        out = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True)
        if out.returncode:
            raise RuntimeError('driver failed: %s %s' % (out.returncode, out.stdout[-300:]))
        res, cur = [], None
        for ln in out.stdout.splitlines():
            if ln == 'E':
                res.append(cur)
                cur = None
                continue
            if cur is None:
                cur = {'log': [], 'cells': {}, 'recs': {}}
            if ln.startswith('L '):
                cur['log'].append(ln[2:])
            elif ln.startswith('C '):
                _, n, v = ln.split()
                cur['cells'][n] = int(v, 16)
            elif ln.startswith('R '):
                _, n, v = ln.split()
                cur['recs'][n] = v
        return res


# ---------------------------------------------------------------------------------------------------------
# The model world: the same cells / records / effect scripts as the driver

class World:
    def __init__(s, cells, recs, fx, ret, inc=1):
        s.c = dict((n, 0) for n in CW)
        s.c.update(cells)
        s.r = dict((n, bytearray(RECS[n])) for n in RECS)
        for n, v in recs.items():
            s.r[n] = bytearray(v)
        s.fx, s.inc = fx, inc
        s.ret = dict((k, list(v)) for k, v in ret.items())
        s.log, s.occ = [], {}

    # -- memory by 68k address
    def loc(s, a):
        for base, size, pre, n in ((KB, 132, 'k', 5), (PB, 132, 'p', 4), (IB, 24, 'i', 5), (OB, 24, 'o', 2), (LB, 20, 'l', 2)):
            if base <= a < base + size * n:
                return pre + str((a - base) // size), (a - base) % size
        if ACT <= a < ACT + 22:
            return 'act', a - ACT
        raise AssertionError('address %x' % a)

    def rd(s, a, w):
        rid, off = s.loc(a)
        return {1: g8, 2: g16, 4: g32}[w](s.r[rid], off)

    def wr(s, a, w, v):
        rid, off = s.loc(a)
        {1: p8, 2: p16, 4: p32}[w](s.r[rid], off, v)

    def setc(s, n, v):
        s.c[n] = v & ((1 << (8 * CW[n])) - 1)

    def apply(s, target, v):
        if '.' not in target:
            s.setc(target, v)
            return
        rid, off, w = target.split('.')
        {1: p8, 2: p16, 4: p32}[int(w)](s.r[rid], int(off), v)

    def op(s, name, *args):
        s.log.append(name + ''.join(' %x' % (a & 0xFFFFFFFF) for a in args))
        if name == 'keyReset':
            s.c['key'] = 1
        if name == 'callSpawn':
            s.setc('c5ee', s.c['c5ee'] + s.inc)
        if name == 'allocCreature':
            a = s.pop('allocCreature', PB)
            s.wr(a, 4, 1)
            s.fxrun(name)
            return a
        s.fxrun(name)

    def fxrun(s, name):
        occ = s.occ.get(name, 0)
        s.occ[name] = occ + 1
        for (fn, fo, tg, v) in s.fx:
            if fn == name and fo == occ:
                s.apply(tg, v)

    def pop(s, name, dflt):
        q = s.ret.get(name)
        return q.pop(0) if q else dflt

    def opr(s, name, dflt, *args):
        """an op with a return value from its queue"""
        s.log.append(name + ''.join(' %x' % (a & 0xFFFFFFFF) for a in args))
        s.fxrun(name)
        return s.pop(name, dflt)


# ---------------------------------------------------------------------------------------------------------
# The asm, block by block

def m0042(w, a1):
    """LAB_0042: A1 = knight record; writes SECSTRT_1, LAB_05A3, LAB_05A4 (the tests run in the order 0, 1, 3, 2)."""
    k = w.rd(a1 + 54, 4)
    if k == 0:
        v = (0x0C, 0x09, 0x06)
    elif k == 1:
        v = (0x0FA0, 0x0E70, 0x0C50)
    elif k == 3:
        v = (0x0D00, 0x0900, 0x0500)
    elif k == 2:
        v = (0x0AE8, 0x06B5, 0x0473)
    else:
        v = (0x0408, 0x0305, 0x0003)
    w.c['col0'], w.c['col1'], w.c['col2'] = v


def m003E(w):
    c = w.c
    if c['warn0'] == 0:                                 # TST.L LAB_05A5 ; BNE.W LAB_003F
        a1 = w.rd(ACT + 0, 4)
        d0 = s16(w.rd(a1 + 80, 2))                      # MOVE.W 80(A1),D0
        if not d0 > 10:                                 # CMP.W #$a,D0 ; BGT.W LAB_003F
            m0042(w, a1)
            c['warn0'] = w.opr('spawnWarning', 0x7006, 6, c['col0'])
            c['warn1'] = w.opr('spawnWarning', 0x7007, 7, c['col1'])
            c['warn2'] = w.opr('spawnWarning', 0x7008, 8, c['col2'])
    if c['arenakind'] in (0x0C, 0x10):                  # LAB_003F: CMPI.L #$c,LAB_0411 ; BEQ ; CMPI.L #$10 ; BNE.W LAB_0041
        if c['warn3'] == 0:                             # LAB_0040
            a1 = w.rd(ACT + 4, 4)
            d0 = s16(w.rd(a1 + 80, 2))
            if not d0 > 10:
                m0042(w, a1)
                c['warn3'] = w.opr('spawnWarning', 0x7009, 9, c['col0'])
                c['warn4'] = w.opr('spawnWarning', 0x700A, 10, c['col1'])
                c['warn5'] = w.opr('spawnWarning', 0x700B, 11, c['col2'])


def m0048(w):
    c = w.c
    if c['warn0'] != 0:                                 # TST.L LAB_05A5 ; BEQ LAB_0049
        for n in ('warn0', 'warn1', 'warn2'):
            w.op('killJob', c[n])                       # MOVEA.L LAB_05A5,A0 ; CLR.L (A0)
    if c['warn3'] != 0:                                 # LAB_0049
        for n in ('warn3', 'warn4', 'warn5'):
            w.op('killJob', c[n])


def m004B(w):
    a2 = ACT
    if w.rd(a2 + 8, 1) != 0:                            # TST.B 8(A2) ; BEQ LAB_004C
        a0 = w.rd(a2 + 0, 4)
        if s16(w.rd(a0 + 80, 2)) < 0:                   # MOVE.W 80(A0),D0 ; BPL.S LAB_004C
            w.wr(a2 + 16, 1, 0x32)
            w.wr(a2 + 8, 1, 0)
    d0 = w.opr('translateKey', 0, w.c['key'])           # MOVE.W SECSTRT_21,D0 ; JSR LAB_0D8D
    if (d0 & 0xFFFF) == 0x20:                           # CMP.W #$20,D0 ; BNE.S LAB_004E
        w.op('keyReset')
        while w.c['key'] == 0:                          # LAB_004D: TST.W SECSTRT_21 ; BEQ.S LAB_004D
            raise AssertionError('key spin')
    w.op('keyReset')                                    # LAB_004E


def m000A(w):
    c = w.c
    d0 = c['creaturebase']                              # MOVE.L LAB_05C3,D0
    for _ in range(20):                                 # MOVE.L #$13,D1 ; DBF
        if d0 != c['skipjob']:                          # CMP.L LAB_05F4,D0 ; BEQ.S LAB_000C
            w.op('togglePause', d0)
        d0 = (d0 + 0x84) & 0xFFFFFFFF
    w.op('togglePause', DRAGON)


def mode_ub(w):
    return w.r['mode'][0]


def m0036(w):
    c = w.c
    c['w0620'] = 0
    c['soundstep'] = 0
    c['warn0'] = 0
    c['warn3'] = 0
    w.op('keyReset')
    w.wr(ACT + 8, 1, 1)                                 # MOVE.B #1,8(A2)
    c['fighter'] = w.rd(ACT + 0, 4)
    w.op('clearScriptSlots')
    c['frameflag'] = 1
    c['framestart'] = c['tick']
    w.op('flip')
    m000A(w)
    if mode_ub(w) == 4:
        w.op('initFightScreen')
    while True:                                         # LAB_0037
        for n in ('frameStart', 'jobPass', 'tick', 'flip', 'contactPass', 'drawPass'):
            w.op(n)
        m003E(w)
        if c['w06fc'] != 0:
            w.op('extraPass')
        m004B(w)
        w.op('frameWait')
        if w.rd(ACT + 8, 1) != 0:                       # TST.B 8(A2) ; BNE.W LAB_0037
            continue
        t = (w.rd(ACT + 16, 1) - 1) & 0xFF              # SUBI.B #1,16(A2) ; BNE.W LAB_0037
        w.wr(ACT + 16, 1, t)
        if t != 0:
            continue
        break
    w.op('soundTick')
    # LAB_000E
    w.r['defeat'][:] = b'\x00\x00'                      # MOVE.W #0,LAB_05DC
    a0 = w.rd(ACT + 0, 4)
    a1 = w.rd(ACT + 4, 4)
    ka, kb = w.r[w.loc(a0)[0]], w.r[w.loc(a1)[0]]
    bits = m000e(ka, kb)
    w.r['defeat'][0] = bits
    if mode_ub(w) == 4:
        w.op('afterFight')
    m0048(w)
    c['turnspent'] = c['turnbudget']
    c['badluck'] = 0
    w.op('fadeIn')
    w.op('clearJobs')
    if c['scene76d'] == 2:                              # CMPI.L #2,LAB_076D
        a0 = c['lair']
        w.wr(a0 + 6, 2, c['c5ec'])


def m0006(w):
    if w.rd(ACT + 8, 1) != 0:                           # TST.B 8(A2) ; BEQ.S LAB_0007
        w.wr(ACT + 16, 1, 0x23)
        w.wr(ACT + 8, 1, 0)


def m0005(w):
    c = w.c
    a0 = c['fighter']
    if s16(w.rd(a0 + 80, 2)) <= 0:                      # MOVE.W 80(A0),D0 ; BLE.S LAB_0006
        m0006(w)
        return
    w.setc('c5ee', c['c5ee'] - 1)
    new = s16(c['c5ec']) - 1                            # SUBI.W #1,LAB_05EC ; BGT.S LAB_0008 (true signed result)
    w.setc('c5ec', new)
    if not new > 0:
        if c['c5ee'] == 0:                              # TST.W LAB_05EE ; BNE.S LAB_0008
            m0006(w)
            return
    while True:                                         # LAB_0008
        if c['c5ed'] == c['c5ee']:                      # CMP.W LAB_05EE,D0 ; BEQ.S LAB_0009
            return
        a0 = c['spawnfn']
        if s16(c['c5ec']) <= 0:                         # TST.W LAB_05EC ; BLE.S LAB_0009
            return
        w.op('callSpawn', a0)                           # JSR (A0)


def kind(w, a):
    return w.rd(a + 54, 4)


def m_ks(w):
    return [w.r['k%d' % i] for i in range(5)], [w.r['i%d' % i] for i in range(5)]


def m001C(w, a0, a1):
    ks, invs = m_ks(w)
    m001c(ks, invs, (a0 - KB) // 132, (a1 - KB) // 132)


def m0058(w):
    c = w.c
    a1 = w.rd(ACT + 4, 4)
    d0 = 0
    if kind(w, a1) == 4:                                # CMPI.L #4,54(A1) ; BEQ.W LAB_005A
        return 0
    c['avoidwho'] = a1                                  # MOVE.L A1,LAB_05D2
    a0 = w.rd(a1 + 96, 4)
    if w.rd(a0 + 18, 1) == 0:                           # TST.B 18(A0) ; BEQ.W LAB_005A
        return 0
    w.op('copyName', w.rd(a1 + 108, 4))
    w.op('resetScreen')
    w.op('showAvoidText')
    w.op('waitFire')
    w.op('op03EB')
    c['lastslot'] = 0xFFFF
    a2 = w.rd(ACT + 0, 4)                               # MOVEA.L 0(A0),A2 ; MOVE.L 4(A0),0(A0)
    w.wr(ACT + 0, 4, w.rd(ACT + 4, 4))
    w.op('screen', 9)
    w.wr(ACT + 0, 4, a2)                                # MOVE.L A2,0(A0)
    d0 = 0
    if c['lastslot'] == 0x12:                           # CMPI.W #$12,LAB_053B ; BNE.S LAB_005A
        d0 = 1
        if c['badluck'] != 0:                           # TST.W LAB_05D3 ; BEQ.S LAB_005A
            d0 = 0
            c['avoidby'] = c['avoidwho']                # MOVE.L LAB_05D2,LAB_05D1
    return d0


def m0065(w):
    for n in ('clearJobs', 'clearObjects', 'op0155', 'fadeOut', 'keyReset'):
        w.op(n)
    w.c['skipjob'] = 0                                  # CLR.L LAB_05F4
    w.c['w0667'] = 0


def m004F(w, a0, a1):
    c = w.c
    w.op('resetScreen')                                 # MOVEM.L A0-A1,-(A7) ; JSR LAB_0DC8 ; MOVEM.L (A7)+,A0-A1
    w.wr(ACT + 0, 4, a0)
    w.wr(ACT + 4, 4, a1)
    skip = w.rd(a1 + 82, 1) != 0 or w.rd(a1 + 73, 1) == 0   # TST.B 82(A1) ; BNE.W LAB_0053 ; TST.B 73(A1) ; BEQ.W LAB_0053
    if not skip:
        if m0058(w) != 0:                               # JSR LAB_0058 ; TST.W D0 ; BNE.W LAB_0054
            w.op('returnToMap')
            return
        a0, a1 = w.rd(ACT + 0, 4), w.rd(ACT + 4, 4)
        if kind(w, a0) == 4 and kind(w, a1) == 4:       # BEQ.W LAB_0054
            w.op('returnToMap')
            return
        d0 = 2
        if kind(w, a0) != 4:
            w.wr(a0 + 77, 1, 0x0C)
            w.wr(a0 + 11, 1, d0)
            d0 = 1
        if kind(w, a1) != 4:                            # LAB_0051
            w.wr(a1 + 77, 1, 0x0C)
            w.wr(a1 + 11, 1, d0)
        m0065(w)                                        # LAB_0052
        m0164(w)
        m0036(w)
        c['swapped'] = 0
        a0, a1 = w.rd(ACT + 0, 4), w.rd(ACT + 4, 4)
        if w.r['defeat'][0] == 3:                       # CMPI.B #3,LAB_05DC ; BEQ.W LAB_0055
            a0 = w.rd(ACT + 0, 4)                       # LAB_0055
            if kind(w, a0) != 4:
                w.op('screen', 9)
            w.op('returnToMap')
            return
        if w.r['defeat'][0] & 1:                        # BTST #0,LAB_05DC ; BEQ.S LAB_0053
            c['swapped'] = 1
            w.wr(ACT + 4, 4, a0)
            w.wr(ACT + 0, 4, a1)
            a0, a1 = w.rd(ACT + 0, 4), w.rd(ACT + 4, 4)
    # LAB_0053
    if kind(w, a0) == 4:                                # CMPI.L #4,54(A0) ; BEQ.W LAB_0057
        w.wr(a0 + 78, 2, w.rd(a0 + 78, 2) + 1)          # ADDI.W #1,78(A0)
        m001C(w, a0, a1)                                # BSR.W LAB_001C
        w.op('returnToMap')
        return
    w.op('screen', 1)
    if c['swapped'] != 0:                               # TST.W LAB_05AD ; BEQ.S LAB_0054
        d0 = w.rd(ACT + 0, 4)
        w.wr(ACT + 0, 4, w.rd(ACT + 4, 4))
        w.wr(ACT + 4, 4, d0)
    w.op('returnToMap')


def m005F(w):
    c = w.c
    if c['travel'] != 0:                                # TST.W LAB_065E ; BNE.W LAB_0063
        return
    d0 = 0
    a0 = c['lair']
    if w.rd(a0 + 8, 2) != 0:                            # TST.W 8(A0) ; BEQ.S LAB_0060
        d0 = 1
    a1 = w.rd(a0 + 0, 4)
    for i in range(24):                                 # MOVE.L #$17,D7 ; TST.B (A1)+
        if w.rd(a1 + i, 1) != 0:
            d0 = 1
    if d0 == 0:
        w.wr(a0 + 10, 4, 0xFFFFFFFF)                    # MOVE.L #$ffffffff,10(A0)


def m005B(w, a1):
    c = w.c
    c['lair'] = a1
    w.op('resetScreen')
    if c['travel'] == 0:                                # TST.W LAB_065E ; BNE.W LAB_005D
        m0065(w)
        m01A3(w)
        m0036(w)
        c['current'] = w.rd(ACT + 0, 4)                 # MOVE.L 0(A0),LAB_0633
        if w.r['defeat'][0] & 1:                        # BTST #0,LAB_05DC ; BEQ.S LAB_005C
            w.op('screen', 9)
            w.op('returnToMap')
            return
        a1 = c['current']                               # LAB_005C
        w.wr(a1 + 78, 2, w.rd(a1 + 78, 2) + 1)
    w.op('screen', 2)                                   # LAB_005D
    m005F(w)
    w.op('returnToMap')
    if c['travel'] != 0:
        w.op('mapStep')


def m0083(w):
    c = w.c
    c['fighter'] = c['current']                         # MOVE.L LAB_0633,LAB_05F2
    a0 = c['fighter']
    lost = False
    if kind(w, a0) == 4:                                # CMPI.L #4,54(A0) ; BNE.S LAB_0084
        w.wr(a0 + 73, 1, w.rd(a0 + 73, 1) - 1)          # SUBI.B #1,73(A0)
        lost = True
    elif w.rd(a0 + 82, 1) != 0 or w.rd(a0 + 73, 1) == 0:     # LAB_0084
        lost = True
    else:
        m0192(w)
        m0036(w)
        lost = (w.r['defeat'][0] & 1) != 0              # BTST #0,LAB_05DC ; BEQ.S LAB_0086
    if lost:                                            # LAB_0085
        m001C(w, DRAGON, c['fighter'])
        w.r['defeat'][0] |= 1                           # ORI.B #1,LAB_05DC
    else:                                               # LAB_0086
        w.wr(DRAGON + 73, 1, 0xFF)
        a0 = c['fighter']
        w.wr(a0 + 78, 2, w.rd(a0 + 78, 2) + 2)
        w.op('screen', 10)
    c['turnspent'] = c['turnbudget']                    # LAB_00B2
    w.op('returnToMap')                                 # LAB_00B3


def m0167(w, a1):
    w.wr(a1 + 34, 4, ADDR['a05f5'])
    w.wr(a1 + 30, 4, ADDR['a05f6'])
    w.wr(a1 + 42, 4, ADDR['a05f7'])
    w.wr(a1 + 46, 4, ADDR['a0610'])
    w.wr(a1 + 50, 4, ADDR['a05f8'])
    w.wr(a1 + 22, 4, ADDR['a07db'])
    w.wr(a1 + 26, 4, ADDR['a07dc'])
    w.wr(a1 + 38, 4, ADDR['a05e1'])
    w.wr(a1 + 116, 2, 0x64)
    w.wr(a1 + 120, 2, 4)
    w.wr(a1 + 118, 2, 0x50)


def m0164(w):
    c = w.c
    w.op('commonSetup', 2)                              # MOVEQ #2,D0 ; BSR.W LAB_016F
    w.op('op0116')
    a1 = w.rd(ACT + 4, 4)
    c['target'] = a1
    w.wr(a1 + 4, 2, 0x1E)
    w.wr(a1 + 6, 2, 0)
    w.wr(a1 + 8, 2, 0x4B)
    w.wr(a1 + 10, 1, 1)
    m0167(w, a1)
    w.wr(a1 + 38, 4, ADDR['a05e0'])
    w.op('spawn', a1, ADDR['a07fc'])                    # MOVEA.L #LAB_07FC,A0 ; JSR LAB_01A9
    c['framedelay'] = 6
    c['c5ed'] = 1
    c['c5ec'] = 1
    c['c5ee'] = 0
    c['spawnfn'] = ADDR['a0166']
    w.op('paletteMode', 12)


def m0165(w):
    c = w.c
    w.op('op0100', 2)
    w.op('op015F')
    w.op('clearObjects')
    w.op('clearJobs')
    w.op('op0116')
    c['current'] = KB
    w.op('placeAttacker')
    a1 = KB + 132
    c['target'] = a1
    w.wr(a1 + 4, 2, 0x1E)
    w.wr(a1 + 6, 2, 0)
    w.wr(a1 + 10, 1, 1)
    m0167(w, a1)
    w.wr(a1 + 38, 4, ADDR['a05e0'])
    w.wr(a1 + 11, 1, 1)
    w.op('spawn', a1, ADDR['a07fc'])
    c['framedelay'] = 6
    c['c5ed'] = 1
    c['c5ec'] = 1
    c['c5ee'] = 0
    c['spawnfn'] = ADDR['a0166']
    c['arenaparam'] = 0x0C
    w.op('paletteMode', 12)


def m0195(w, a1):
    # LAB_0195 (mog.asm 4028), every store in order
    w.wr(a1 + 42, 4, ADDR['a0603'])
    w.wr(a1 + 38, 4, ADDR['a05e0'])
    w.wr(a1 + 30, 4, ADDR['a0604'])
    w.wr(a1 + 46, 4, ADDR['a060f'])
    w.wr(a1 + 22, 4, ADDR['a0882'])
    w.wr(a1 + 26, 4, ADDR['a0882'])
    w.wr(a1 + 116, 2, 0x3C)
    w.wr(a1 + 118, 2, 0x14)
    w.wr(a1 + 120, 2, 5)
    w.wr(a1 + 80, 2, 0x78)
    w.wr(a1 + 84, 2, 0x78)
    w.wr(a1 + 77, 1, 0x14)
    w.wr(a1 + 10, 1, 1)
    w.wr(a1 + 11, 1, 4)


def m0192(w):
    c = w.c
    w.op('commonSetup', 3)
    w.op('op0121')
    a1 = c['current']
    a0 = w.rd(a1 + 30, 4)
    w.op('poke32', a0 + 4, ADDR['a07f5'])
    w.op('poke32', a0 + 8, ADDR['a07fe'])
    w.op('poke32', a0 + 32, ADDR['a07fe'])
    w.op('poke32', a0 + 20, ADDR['a07fb'])
    a0 = ADDR['a0603']
    w.op('poke32', a0 + 8, 0x1E)
    w.op('poke32', a0 + 32, 0x1E)
    w.op('poke32', a0 + 20, 0x0A)
    w.op('poke32', a0 + 4, 0x0A)
    a1 = DRAGON
    w.wr(a1 + 4, 2, 0x50)
    w.wr(a1 + 6, 2, 0xFFD8)
    w.wr(a1 + 8, 2, 0x64)
    w.wr(a1 + 10, 1, 1)
    m0195(w, a1)
    c['w0623'] = 0
    w.op('spawn', a1, w.rd(a1 + 22, 4))                 # LAB_01A8: MOVEA.L 22(A1),A0
    for cell, y in (('bat1', 0x50), ('bat2', 0x78)):
        a1 = w.op('allocCreature')                      # JSR LAB_0171
        c[cell] = a1
        w.wr(a1 + 4, 2, 5)
        w.wr(a1 + 6, 2, 0)
        w.wr(a1 + 8, 2, y)
        w.wr(a1 + 80, 2, 0x32)
        w.wr(a1 + 84, 2, 0x32)
        m0195(w, a1)
        w.wr(a1 + 77, 1, 0x2C)
        w.wr(a1 + 22, 4, ADDR['a0880'])
        w.wr(a1 + 26, 4, ADDR['a0880'])
        w.wr(a1 + 120, 2, 0x0A)
        w.op('spawn', a1, w.rd(a1 + 22, 4))
    c['c5ed'] = 1
    c['c5ec'] = 1
    c['c5ee'] = 0
    c['spawnfn'] = ADDR['a0166']
    c['spawnfn2'] = ADDR['a0166']
    c['framedelay'] = 6
    w.op('paletteMode', 0x14)


def m01A3(w):
    c = w.c
    a0 = c['lair']
    c['scene76d'] = 2
    c['lairparam'] = w.rd(a0 + 16, 4)
    c['arenaparam'] = w.rd(a0 + 14, 2)                  # MOVEQ #0,D0 ; MOVE.W 14(A0),D0
    d0 = s16(w.rd(a0 + 4, 2))                           # MOVE.W 4(A0),D0 ; EXT.L D0
    w.op('creatureArena', d0)


# ---- screen

def m04E1(w):
    c = w.c
    a1 = c['knighta']
    out = []
    d0 = 1
    while True:                                         # LAB_04E2
        k = w.rd(a1 + 54, 4)
        if k == 0:
            out += [0x3F, 0x28]
        elif k == 1:
            out += [0x0FB0, 0x0B60]
        elif k == 3:
            out += [0x0F00, 0x0800]
        elif k == 2:
            out += [0x04C3, 0x0160]
        else:
            out += [0x27, 0x03]
        s = c['scene']                                  # LAB_04E7
        if s in (1, 8, 11):
            a1 = c['knightb']                           # LAB_04E8
            if d0 > 0:                                  # DBF D0,LAB_04E2
                d0 -= 1
                continue
        break
    for i, v in enumerate(out):
        c['pal%d' % i] = v


def m04D4(w):
    c = w.c
    while True:                                         # LAB_04D4
        c['textflag'] = 1
        for n in ('op044E', 'op04EA', 'blitBackground', 'setTarget', 'lootSetup', 'op03A7'):
            w.op(n)
        c['buttons'] = 0x699
        c['statknight'] = c['knighta']
        c['xshift'] = 0
        c['linebase'] = 0
        d0 = c['scene']
        for d1 in (9, 3):                               # LAB_04D5: MOVE.W (A2)+,D1 ; BMI ; CMP.W D1,D0 ; BNE
            if (d0 & 0xFFFF) == d1:
                c['xshift'] = 0x4A
                break
        w.op('drawStats', c['statknight'], c['linebase'], c['xshift'])                               # LAB_04D6
        c['buttons'] = 0x69A
        c['xshift'] = 0x96
        if d0 == 2:                                     # CMPI.L #2,LAB_068F ; BNE.S LAB_04D7
            w.op('op051B')
            break
        if d0 == 1:                                     # LAB_04D7
            a0 = c['knightb']
            lives = s8(w.rd(a0 + 73, 1))
            if not lives > 0:                           # TST.B 73(A0) ; BGT.S LAB_04D8
                c['changed'] = 0
            if c['changed'] != 0:                       # TST.W LAB_0689 ; BEQ.S LAB_04D9
                c['scene'] = 9
                continue
            c['statknight'] = c['knightb']              # LAB_04D9
            c['linebase'] = 1
            w.op('drawStats', c['statknight'], c['linebase'], c['xshift'])
            break
        if d0 == 11 or (d0 == 8 and c['changed'] == 0):  # LAB_04DA
            c['statknight'] = c['knightb']              # LAB_04DB
            c['linebase'] = 1
            w.op('drawStats', c['statknight'], c['linebase'], c['xshift'])
            w.op('op0524')
            break
        if d0 == 8:                                     # LAB_04DC
            c['changed'] = 0
            c['scene'] = c['prevscene']
            continue
        if d0 == 5:                                     # LAB_04DD
            w.op('op0522')
        if d0 == 6:                                     # LAB_04DE
            c['statknight'] = 0x690
            w.op('op04FE')
            w.op('op051F', c['statknight'])
        if d0 == 10:                                    # LAB_04DF
            w.op('op051D')
        break
    m04E1(w)                                            # LAB_04E0
    w.op('flip')
    w.op('blitScreen')
    w.op('waitBlitter')
    w.op('op0D8A')
    w.op('op03EE')


def m04CF(w, d0):
    c = w.c
    c['textflag'] = 1
    c['sprites'] = c['spritesrc']
    c['changed'] = 0
    c['scene'] = d0
    c['done'] = 0
    w.op('fadeOut')
    w.op('op0575')
    w.op('op0588')
    m04D4(w)
    while True:                                         # LAB_04D0
        d0 = w.opr('hitTest', 0x777, c['curx'], c['cury'])
        if d0 != 0 and c['curlock'] == 0:               # TST.L D0 ; BEQ.S LAB_04D1 ; TST.W LAB_0981 ; BNE.S LAB_04D1
            w.op('click', d0)
            if c['done'] != 0:                          # TST.W LAB_0984 ; BNE.S LAB_04D2
                break
        w.op('flip')                                    # LAB_04D1
        w.op('drawPass')
    w.op('fadeOut')                                     # LAB_04D2
    w.op('op057B')
    if c['handover'] != 0:                              # TST.W LAB_053C
        w.wr(DRAGON + 100, 4, w.rd(ACT + 4, 4))
        c['handover'] = 0
    c['textflag'] = 0


# ---------------------------------------------------------------------------------------------------------
# case generation

def hx(v):
    return '%x' % v


def case_line(fn, cells, recs, fx=(), ret=None, args=(), inc=1):
    t = ['fn:' + fn]
    t += ['arg:' + hx(a) for a in args]
    if inc != 1:
        t.append('inc:%d' % inc)
    t += ['c:%s=%s' % (n, hx(v)) for n, v in cells.items()]
    t += ['r:%s=%s' % (n, h(v)) for n, v in recs.items()]
    t += ['fx:%s#%d:%s=%s' % (n, o, tg, hx(v)) for (n, o, tg, v) in fx]
    for n, q in (ret or {}).items():
        if q:
            t.append('ret:%s=%s' % (n, ','.join(hx(v) for v in q)))
    return ' '.join(t)


def rnd_rec_knight(r, i, kinds=(0, 1, 2, 3, 4)):
    k = rnd_knight(r)
    p32(k, 96, IB + 24 * i)                             # the knight's Inventory
    p32(k, 54, r.choice(list(kinds)))
    p32(k, 108, r.getrandbits(32))
    p16(k, 80, r.choice([0xFFFF, 0, 1, 5, 10, 11, 20, 100, 0x8000, 0x7FFF, r.getrandbits(16)]))
    k[73] = r.choice([0, 1, 2, 3, 5, 0x80, 0xFF])
    k[82] = r.choice([0, 0, 0, 1, 3])
    k[77] = r.choice([0x0C, 0x10, 0x14, 0, 0x2C])
    return k


def rnd_world(r, creatures=True):
    recs = {}
    for i in range(5):
        recs['k%d' % i] = rnd_rec_knight(r, i)
        recs['i%d' % i] = rnd_inv(r, dense=r.random() < .5)
    for i in range(4):
        k = rnd_knight(r)
        p32(k, 96, 0)
        p32(k, 54, r.randrange(0, 6))
        p32(k, 0, r.choice([0, 1, 7]))
        recs['p%d' % i] = k
    for i in range(2):
        recs['o%d' % i] = rnd_inv(r, dense=r.random() < .3)
        la = bytearray(r.getrandbits(8) for _ in range(20))
        p32(la, 0, OB + 24 * i)
        recs['l%d' % i] = la
    act = bytearray(r.getrandbits(8) for _ in range(22))
    recs['act'] = act
    recs['defeat'] = bytearray([r.getrandbits(8), r.getrandbits(8)])
    recs['mode'] = bytearray([r.choice([4, 4, 0, 0x0C, 0x14, 0xFF]), r.getrandbits(8)])
    return recs


def rnd_cells(r):
    cells = {}
    for n, w, _ in CELLS:
        v = r.getrandbits(8 * w)
        if r.random() < .3:
            v = r.choice([0, 1, 2, 0xFFFF & ((1 << (8 * w)) - 1)])
        cells[n] = v
    cells['key'] = r.choice([0, 1, 0x45])
    cells['arenakind'] = r.choice([0x0C, 0x10, 0x14, 0, 8, r.getrandbits(32)])
    cells['creaturebase'] = PB if r.random() < .8 else r.choice([0x20000 + 132, 0])
    cells['skipjob'] = r.choice([0, PB, PB + 132, PB + 3 * 132, DRAGON, r.getrandbits(32)])
    cells['tick'] = r.getrandbits(32)
    return cells


def set_act(recs, cur, opp, active=1, timer=None, r=None):
    a = recs['act']
    p32(a, 0, KB + 132 * cur)
    p32(a, 4, KB + 132 * opp)
    a[8] = active
    if timer is not None:
        a[16] = timer


def compare(tc, case, exp_w):
    got = case
    assert got['log'] == exp_w.log, 'log differs:\n  C++  %s\n  model%s' % (got['log'][:60], exp_w.log[:60])
    for n, v in exp_w.c.items():
        assert got['cells'][n] == v, 'cell %s: C++ %x model %x' % (n, got['cells'][n], v)
    for n, v in exp_w.r.items():
        assert got['recs'][n] == h(v), 'record %s:\n  C++  %s\n  model%s' % (n, got['recs'][n], h(v))


def run_cases(tc, cases, model):
    """cases = [(line, world-args)]; model(world) runs the asm model.  Compares everything."""
    lines = [c[0] for c in cases]
    res = run_driver(lines)
    assert len(res) == len(cases), (len(res), len(cases))
    for (ln, wargs), got in zip(cases, res):
        w = World(*wargs)
        model(w)
        try:
            compare(tc, got, w)
        except AssertionError as e:
            raise AssertionError('%s\n%s' % (ln[:600], e))


def mk(fn, r, cells, recs, fx, ret, args=(), inc=1):
    return case_line(fn, cells, recs, fx, ret, args, inc), (cells, recs, fx, ret, inc)


def fight_fx(r, killer=True):
    """effects that end a fight loop: the active flag drops at a random frame, the first fighter may die."""
    fx = []
    if killer:
        fx.append(('jobPass', r.randrange(0, 4), 'act.8.1', 0))
    if r.random() < .3:
        fx.append(('contactPass', r.randrange(0, 3), 'k%d.80.2' % r.randrange(0, 2), r.choice([0, 0xFFFF, 5, 0x8000])))
    if r.random() < .15:
        fx.append(('frameWait', r.randrange(0, 3), 'warn0', r.choice([0, 0x1234])))
    return fx


def fight_ret(r):
    ret = {}
    ret['translateKey'] = [r.choice([0, 0, 0x20, 0x41]) for _ in range(12)]
    ret['spawnWarning'] = [0x7100 + i for i in range(6)] if r.random() < .5 else []
    return ret


@unittest.skipUnless(CXX, 'needs clang++')
class CombatTests(unittest.TestCase):
    def test_fight_run(self):
        r = random.Random(1)
        cases = []
        for n in range(150):
            recs = rnd_world(r)
            cur, opp = r.sample(range(4), 2)
            set_act(recs, cur, opp, r.choice([0, 1]), r.choice([1, 1, 2, 3, 3]) if n % 40 else 0)
            cells = rnd_cells(r)
            cells['scene76d'] = r.choice([2, 2, 0])
            cells['lair'] = LB + 20 * r.randrange(0, 2)
            cells['w06fc'] = r.choice([0, 1])
            cells['fighter'] = 0
            fx = fight_fx(r, killer=True)
            cases.append(mk('fightRun', r, cells, recs, fx, fight_ret(r)))
        run_cases(self, cases, m0036)

    def test_creature_died(self):
        r = random.Random(2)
        cases = []
        for n in range(400):
            recs = rnd_world(r)
            set_act(recs, 0, 1, r.choice([0, 1]), r.choice([0, 1, 5]))
            cells = rnd_cells(r)
            cells['fighter'] = KB + 132 * r.randrange(0, 4)
            hp = r.choice([0, 1, 5, 0xFFFF, 0x8000, 0x7FFF, 10])
            p16(recs['k%d' % ((cells['fighter'] - KB) // 132)], 80, hp)
            cells['c5ec'] = r.choice([0, 1, 2, 3, 5, 0xFFFF, 0x8000, 0x8001])
            cells['c5ed'] = r.randrange(0, 4)
            cells['c5ee'] = r.randrange(0, 5)
            if s16(hp) > 0 and s16(cells['c5ec']) - 1 > 0:
                # the spawn loop runs until alive == max: keep it short (the original spins when it never gets there)
                cells['c5ee'] = (cells['c5ed'] - r.randrange(0, 3) + 1) & 0xFFFF
            cells['spawnfn'] = r.getrandbits(32)
            cases.append(mk('creatureDied', r, cells, recs, [], {}))
        run_cases(self, cases, m0005)

    def test_fight_end(self):
        r = random.Random(3)
        cases = []
        for n in range(40):
            recs = rnd_world(r)
            set_act(recs, 0, 1, r.choice([0, 1]), r.getrandbits(8))
            cases.append(mk('fightEnd', r, {}, recs, [], {}))
        run_cases(self, cases, m0006)

    def test_avoid(self):
        r = random.Random(4)
        cases = []
        for n in range(300):
            recs = rnd_world(r)
            cur, opp = r.sample(range(4), 2)
            set_act(recs, cur, opp)
            if r.random() < .6:
                recs['i%d' % opp][18] = r.choice([0, 1, 2])
            cells = rnd_cells(r)
            fx = [('screen', 0, 'lastslot', r.choice([0x12, 0x12, 0x10, 0, 0xFFFF])),
                  ('screen', 0, 'badluck', r.choice([0, 1]))]
            cases.append(mk('avoid', r, cells, recs, fx, {}))

        def model(w):
            w.log.append('RET %d' % m0058(w))
        run_cases(self, cases, model)

    def test_meet(self):
        r = random.Random(5)
        cases = []
        for n in range(500):
            recs = rnd_world(r)
            cur, opp = r.sample(range(4), 2)
            set_act(recs, cur, opp, 0, r.choice([1, 1, 2, 3]))
            if r.random() < .5:
                recs['i%d' % opp][18] = r.choice([0, 1])
            if r.random() < .3:
                recs['k%d' % opp][82] = 1
            if r.random() < .1:
                recs['k%d' % opp][73] = 0
            cells = rnd_cells(r)
            cells['fighter'] = 0
            cells['lair'] = LB + 20 * r.randrange(0, 2)
            fx = fight_fx(r, killer=True) + [
                ('screen', 0, 'lastslot', r.choice([0x12, 0x12, 0x10, 0])), ('screen', 0, 'badluck', r.choice([0, 1]))]
            cases.append(mk('meet', r, cells, recs, fx, fight_ret(r), args=(KB + 132 * cur, KB + 132 * opp)))
        self._run_with_args(cases, lambda w, a: m004F(w, a[0], a[1]))

    def _run_with_args(self, cases, model):
        lines = [c[0] for c in cases]
        res = run_driver(lines)
        assert len(res) == len(cases)
        for (ln, wargs), got in zip(cases, res):
            args = [int(t[4:], 16) for t in ln.split() if t.startswith('arg:')]
            w = World(*wargs)
            model(w, args)
            try:
                compare(self, got, w)
            except AssertionError as e:
                raise AssertionError('%s\n%s' % (ln[:700], e))

    def test_creature(self):
        r = random.Random(6)
        cases = []
        for n in range(400):
            recs = rnd_world(r)
            cur = r.randrange(0, 4)
            set_act(recs, cur, (cur + 1) % 4, 0, r.choice([1, 2, 3]))
            lair_i = r.randrange(0, 2)
            cells = rnd_cells(r)
            cells['travel'] = r.choice([0, 0, 1])
            cells['current'] = KB + 132 * r.randrange(0, 4)
            if r.random() < .5:
                recs['o%d' % lair_i][:] = bytes(24)
                p16(recs['l%d' % lair_i], 8, 0)
            fx = fight_fx(r, killer=True)
            cases.append(mk('creature', r, cells, recs, fx, fight_ret(r), args=(LB + 20 * lair_i,)))
        self._run_with_args(cases, lambda w, a: m005B(w, a[0]))

    def test_lair_tidy(self):
        r = random.Random(7)
        cases = []
        for n in range(200):
            recs = rnd_world(r)
            lair_i = r.randrange(0, 2)
            cells = rnd_cells(r)
            cells['lair'] = LB + 20 * lair_i
            cells['travel'] = r.choice([0, 0, 1])
            if r.random() < .6:
                recs['o%d' % lair_i][:] = bytes(24)
                if r.random() < .6:
                    recs['o%d' % lair_i][r.randrange(24)] = r.randrange(1, 255)
                p16(recs['l%d' % lair_i], 8, r.choice([0, 0, 1]))
            cases.append(mk('lairTidy', r, cells, recs, [], {}))
        run_cases(self, cases, m005F)

    def test_dragon(self):
        r = random.Random(8)
        cases = []
        for n in range(500):
            recs = rnd_world(r)
            cur = r.randrange(0, 4)
            set_act(recs, cur, (cur + 1) % 4, 0, r.choice([1, 2, 3]))
            cells = rnd_cells(r)
            cells['current'] = KB + 132 * cur
            cells['lair'] = LB + 20 * r.randrange(0, 2)
            recs['k%d' % cur][30:34] = bytes([0, 0, 0x6, 0xF6])
            fx = fight_fx(r, killer=True)
            ret = fight_ret(r)
            ret['allocCreature'] = [PB + 132 * r.randrange(0, 4), PB + 132 * r.randrange(0, 4)]
            cases.append(mk('dragon', r, cells, recs, fx, ret))
        run_cases(self, cases, m0083)

    def test_arenas(self):
        r = random.Random(9)
        for fn, model in (('arenaMeet', m0164), ('arenaPractice', m0165), ('arenaDragon', m0192), ('arenaCreature', m01A3)):
            cases = []
            for n in range(120):
                recs = rnd_world(r)
                cur = r.randrange(0, 4)
                set_act(recs, cur, (cur + 1) % 4, 0, 1)
                cells = rnd_cells(r)
                cells['current'] = KB + 132 * cur
                cells['lair'] = LB + 20 * r.randrange(0, 2)
                ret = {'allocCreature': [PB + 132 * r.randrange(0, 4), PB + 132 * r.randrange(0, 4)]}
                cases.append(mk(fn, r, cells, recs, [], ret))
            run_cases(self, cases, model)

    def test_tables(self):
        r = random.Random(10)
        cases = []
        for n in range(30):
            recs = rnd_world(r)
            i = r.randrange(0, 5)
            cases.append(mk('tables', r, {}, recs, [], {}, args=(KB + 132 * i,)))
        self._run_with_args(cases, lambda w, a: m0167(w, a[0]))

    def _screen_case(self, r, fn, scene=None):
        recs = rnd_world(r)
        ka, kb = r.sample(range(4), 2)
        set_act(recs, ka, kb)
        cells = rnd_cells(r)
        cells['knighta'] = KB + 132 * ka
        cells['knightb'] = KB + 132 * kb if r.random() < .8 else DRAGON
        cells['curlock'] = r.choice([0, 0, 1])
        cells['done'] = 0
        cells['handover'] = r.choice([0, 1])
        cells['prevscene'] = r.choice([1, 2, 3, 5, 6, 8, 9, 10, 11])
        cells['changed'] = r.choice([0, 1, 2])
        s = scene if scene is not None else r.choice([1, 2, 3, 5, 6, 8, 9, 10, 11, 4, 7, 0x10001, 0x10009, 0x10003])
        cells['scene'] = s
        ret = {'hitTest': [r.choice([0, 0x777, 0x778]) for _ in range(r.randrange(0, 6))]}
        k = r.randrange(0, 3)
        fx = [('click', k, 'done', 1), ('flip', r.randrange(0, 3), 'curlock', 0)]
        if r.random() < .5:
            fx.append(('lootSetup', 0, 'changed', r.choice([0, 1])))
        return mk(fn, r, cells, recs, fx, ret, args=(s,))

    def test_screen_run(self):
        r = random.Random(11)
        cases = [self._screen_case(r, 'screenRun') for _ in range(400)]
        self._run_with_args(cases, lambda w, a: m04CF(w, a[0]))

    def test_screen_redraw(self):
        r = random.Random(12)
        cases = [self._screen_case(r, 'screenRedraw') for _ in range(400)]
        run_cases(self, cases, m04D4)

    def test_screen_palette(self):
        r = random.Random(13)
        cases = [self._screen_case(r, 'screenPalette') for _ in range(200)]
        run_cases(self, cases, m04E1)


    @origskip.need_asm_ref
    def test_patch_table(self):
        """asm/patches/mog.combat.json: original text matches mog.asm and nothing overlaps the other mog tables."""
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import resource as R
        asm = os.path.join(ROOT, "reference", "moonshard", "moonstone-main", "amiga_asm", "mog.asm")
        if not os.path.isfile(asm):
            self.skipTest("moonshard tree missing")
        lines = open(asm, encoding="latin-1").read().split(chr(10))
        patches = R.load_patches("mog")
        R.check_patches("mog", patches, lines)
        # (the fight-*-cpp patches were dead and went in the 7.1 cleanup; the fight entries are called from C++ directly)

if __name__ == '__main__':
    unittest.main()
