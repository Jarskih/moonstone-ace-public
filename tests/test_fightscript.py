"""Tests for tools/fightscript.py (ROADMAP 9.8a1): the fight bytecode disassembler and text assembler (docs/MONSTER_KIT.md 1.2, 3).

No original data in git, so the tests are layered:
  * synthetic (always, clang++ for the oracle part): every opcode of src/game/combat_script.cpp (the interpreter) is run on the
    host with one instruction + `$FF $FF`, and the final script position must be what fightscript.decode() says (size, branch
    target taken or not); random instruction sequences survive encode -> decode -> encode; the text assembler refuses bad input;
    one deliberate mutation of the size table is shown to fail the oracle test.
  * original (skipped without reference/ and build/reasm/): every creature script reachable from the 8 offered AIs (+ the demon)
    and EVERY decodable script of the script hunk round-trips disassemble -> assemble -> the original bytes and relocations; the
    catalog (tools/monsterkit/ai_catalog.json) agrees with the original (init records, roles, die sequences, knight attack
    actions, frame sets, sounds), and the checklist the catalog implies holds for the eight original creatures.
Needs clang++ for the oracle test; Python 3 + PyYAML otherwise.
"""
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'monsterkit'))
import fightscript as F  # noqa: E402

def _load_json(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


CXX = shutil.which('clang++')
HAVE_ORIG = (os.path.exists(os.path.join(F.ASM_DIR, 'mog')) and os.path.exists(os.path.join(F.ASM_DIR, 'mog.asm'))
             and os.path.exists(os.path.join(F.REASM_DIR, 'mog.symbols.json')))
CATALOG = _load_json(os.path.join(ROOT, 'tools', 'monsterkit', 'ai_catalog.json'))

BASE = 0x1000          # the script address in the host arena

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/combat_script.hpp"
using namespace ms::game;

alignas(8) static uint8_t g_mem[0x4000];
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - g_mem); }
void *ms::jobHostPtr(uint32_t a) { return g_mem + a; }

static uint32_t curJob, hurtList, atkList, rectList, curKnight, frameSets[6], rnd, bgBuf, scrBuf, demo;
static uint16_t jobIndex, rectCount, boxMinX, boxMaxX, boxMinY, boxMaxY, boxArmed, textFlag;
static void prepCel(uint32_t, uint16_t) {}
static void setTarget(uint32_t) {}
static void waitBlit() {}
static void drawCel(uint32_t t, uint16_t f, uint16_t x, uint16_t y) { printf("E draw %x %u %u %u\n", t, f, x, y); }
static void sound(uint8_t id) { printf("E snd %u\n", id); }
static void callRoutine(uint32_t fn, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint8_t) { printf("E call %x\n", fn); }
static void spawn(uint32_t s, uint32_t, uint16_t, uint16_t, uint16_t, uint8_t, uint32_t) { printf("E spawn %x\n", s); }

static void unhex(const char *s, uint8_t *dst, size_t n) {
	for(size_t i = 0; i < n; ++i) { unsigned v; sscanf(s + 2 * i, "%2x", &v); dst[i] = (uint8_t)v; }
}

int main() {
	static char line[8192];
	CombatEnv env = {};
	env.pJobs = (CombatJob *)(g_mem + 0x100);
	env.pScratchJob = (CombatJob *)(g_mem + 0x300);
	env.pScratchWork = (CombatWork *)(g_mem + 0x340);
	env.pCurJob = &curJob; env.pJobIndex = &jobIndex; env.pHurtList = &hurtList; env.pAttackList = &atkList;
	env.pRectList = &rectList; env.pRectCount = &rectCount;
	env.pBoxMinX = &boxMinX; env.pBoxMaxX = &boxMaxX; env.pBoxMinY = &boxMinY; env.pBoxMaxY = &boxMaxY;
	env.pBoxArmed = &boxArmed; env.pTextFlag = &textFlag; env.pDemoFlag = &demo; env.pCurrentKnight = &curKnight;
	env.pFrameSets = frameSets + 1; env.pRandom = &rnd; env.pBackground = &bgBuf; env.pScreen = &scrBuf;
	env.prepCel = prepCel; env.setTarget = setTarget; env.waitBlitter = waitBlit; env.drawCel = drawCel;
	env.sound = sound; env.callRoutine = callRoutine; env.spawn = spawn;
	// one case per line: <hp> <field104> <len> <hex script>
	while(fgets(line, sizeof line, stdin)) {
		int hp, f104; unsigned len;
		if(sscanf(line, "%d %d %u", &hp, &f104, &len) != 3) continue;
		const char *hex = strrchr(line, ' ') + 1;
		memset(g_mem, 0, sizeof g_mem);
		unhex(hex, g_mem + 0x1000, len);
		// cel table at 0x800: header (10) + 4 frames of 10 bytes {offset, w, h, flag byte, plane byte}; frame set list at 0x700
		g_mem[0x800 + 1] = 4;
		for(int f = 0; f < 4; ++f) { uint8_t *e = g_mem + 0x80A + 10 * f; e[5] = 16; e[7] = 8; e[8] = 1; e[9] = 1; }
		for(int i = 0; i < 5; ++i) { g_mem[0x700 + 4 * i + 2] = 0x08; }
		frameSets[0] = 0; for(int i = 0; i < 5; ++i) frameSets[1 + i] = 0x700;
		curJob = hurtList = atkList = rectList = curKnight = rnd = bgBuf = scrBuf = demo = 0;
		atkList = 0x1400; hurtList = 0x1800; rectList = 0x1C00;
		CombatJob *j = (CombatJob *)(g_mem + 0x100);
		j->ubActive = 1; j->ubRunning = 1; j->ulScript = 0x1000; j->ulWork = 0x400; j->ulOwner = 0x600; j->ulFrames = 0x700;
		j->ubFlags = 1; j->ulAttackList = 0x1400; j->ulHurtList = 0x1800;
		Knight *k = (Knight *)(g_mem + 0x600);
		k->swHp = (int16_t)hp;
		g_mem[0x600 + 104] = (uint8_t)f104;
		combatRunJob(env, j);
		printf("R %u %u %u %u\n", j->ulScript, j->ubRunning, j->ubActive, g_mem[0x600 + 104]);
	}
	return 0;
}
'''


def build_driver(tmp):
    src = os.path.join(tmp, 'drv.cpp')
    with open(src, 'w') as f:
        f.write(DRIVER)
    exe = os.path.join(tmp, 'drv.exe' if os.name == 'nt' else 'drv')
    r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-D_CRT_SECURE_NO_WARNINGS', '-I' + os.path.join(ROOT, 'include'), src,
                        os.path.join(ROOT, 'src', 'game', 'combat_script.cpp'), '-o', exe], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


def assemble_at(text, names=None):
    """Assemble `text` (instructions only) at BASE in a memory image with a few labels; returns (bytes, relocs, img)."""
    img = F.MemImage(bytes(0x1100), names=names or {})
    sec = F.assemble('.org h0+$%X\n' % BASE + text, img)[0]
    mem = bytearray(0x1100)
    mem[BASE:BASE + len(sec.data)] = sec.data
    img = F.MemImage(bytes(mem), relocs={BASE + o: h for o, h in sec.relocs.items()}, names=names or {})
    return sec.data, sec.relocs, img


# (text, kind): kind 'plain' = falls through; 'jump' = always goes to T; 'call' = armed, taken at the tick end;
# 'cond' = taken when the condition holds (hp / field values chosen per outcome)
CASES = (
    [('draw %d %d dx=%d dy=%d%s' % (s, f, dx, dy, fl), 'plain') for s, f, dx, dy, fl in
     ((0, 0, 0, 0, ''), (1, 2, -5, 7, ' hurt'), (2, 3, 300, -128, ' hurt attack bg'), (4, 1, -32768, 127, ' nobox nogore text bit2 bit3'))]
    + [('draw.raw $18 1 dx=3 dy=4 hurt', 'plain'), ('draw.raw $44 1 dx=3 dy=4', 'plain')]
    + [('face right', 'plain'), ('face left', 'plain'), ('face flip', 'plain'), ('face $07', 'plain')]
    + [('jump T', 'jump'), ('call T', 'call'), ('call T mode=$05', 'call')]
    + [('loop 3', 'plain'), ('loop random', 'plain'), ('loop2 5', 'plain')]
    + [('motion param=$40 count=7 flags=$00 vy=2 vylim=16 vx=0 vxlim=0', 'plain'), ('motion param=1 count=2 flags=$13 vy=3 vylim=4 vx=5 vxlim=6', 'plain')]
    + [('ifnogore T b1=$07', 'plain'), ('ifdead T', 'cond-dead'), ('ifdead T b1=$01', 'cond-dead'), ('ifsame T', 'plain'), ('spawn T b1=$0C', 'plain'),
       ('engine T', 'plain'), ('engine T b1=$03', 'plain'), ('second T', 'plain'), ('second off', 'plain'), ('second T flag=$00', 'plain')]
    + [('move x=6 y=0 z=0 flags=$01', 'plain'), ('move x=1 y=2 z=3 flags=$29', 'plain'), ('move x=-5 y=7 z=9 flags=$40', 'plain')]
    + [('sound $2F', 'plain'), ('kill', 'plain'), ('reset', 'plain'), ('frameset knights', 'plain'), ('frameset creature', 'plain'), ('frameset 3', 'plain')]
    + [('poke.b +104 1', 'plain'), ('poke.w +104 258', 'plain'), ('poke.l +104 $00010203', 'plain'), ('poke flags=$04 +104 T', 'plain')]
    + [('ifzero.b +104 T', 'cond-field0'), ('ifnonzero.b +104 T', 'cond-field1'), ('ifzero.w +104 T', 'cond-field0'), ('ifnonzero.l +104 T', 'cond-field1')]
    + [('end', 'tick'), ('end pad=$07', 'tick'), ('end loop2', 'tick'), ('done', 'done')]
)


def expected(text, kind, outcome):
    """(final ulScript, running) after one combatRunJob of `text` + terminators, from fightscript alone."""
    data, relocs, img = assemble_at(text + '\n', {'T': BASE + 0x40})
    ins = F.decode(img, 0, BASE)
    size = ins.size
    if kind == 'done':
        return BASE, 0
    if kind == 'tick':
        return BASE + size, 1
    if kind == 'jump':
        return BASE + 0x40, 0
    if kind == 'call':
        return BASE + 0x40, 0
    if kind.startswith('cond'):
        taken = outcome
        return (BASE + 0x40 if taken else BASE + size), 0
    return BASE + size, 0


@unittest.skipUnless(CXX, 'clang++ not on PATH')
class OpcodeOracle(unittest.TestCase):
    """The host build of combat_script.cpp is the oracle: instruction sizes and branch behaviour of fightscript.decode()."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='fightscript_')
        cls.exe = build_driver(cls.tmp)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_cases(self, cases):
        """cases: [(hp, f104, script bytes)] -> [(final script, running, active, byte104)]"""
        lines = ''.join('%d %d %d %s\n' % (hp, f, len(b), b.hex()) for hp, f, b in cases)
        r = subprocess.run([self.exe], input=lines, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        out = [l.split()[1:] for l in r.stdout.splitlines() if l.startswith('R ')]
        self.assertEqual(len(out), len(cases))
        return [tuple(int(x) for x in o) for o in out]

    def test_every_instruction_form(self):
        cases, meta = [], []
        for text, kind in CASES:
            outcomes = [None]
            if kind == 'cond-dead':
                outcomes = [True, False]          # hp <= 0 takes the jump
            elif kind in ('cond-field0', 'cond-field1'):
                outcomes = [True, False]
            for oc in outcomes:
                data, relocs, img = assemble_at(text + '\n', {'T': BASE + 0x40})
                body = data + b'\xff\xff'
                body += bytes(0x40 - len(body)) + b'\xff\xff'      # the branch target T = +0x40 holds `done`
                hp = 1 if kind == 'cond-dead' and not oc else (-1 if kind == 'cond-dead' else 1)
                f104 = 0
                if kind == 'cond-field0':
                    f104 = 0 if oc else 1          # ifzero: taken when the field is zero
                if kind == 'cond-field1':
                    f104 = 1 if oc else 0          # ifnonzero: taken when non-zero
                cases.append((hp, f104, body))
                meta.append((text, kind, oc))
        res = self.run_cases(cases)
        for (text, kind, oc), (script, running, active, b104) in zip(meta, res):
            want_script, want_running = expected(text, kind, oc)
            self.assertEqual(script, want_script, '%s [%s %s]: final script' % (text, kind, oc))
            if kind in ('tick', 'done'):
                self.assertEqual(running, want_running, '%s: running' % text)

    def test_poke_stores_the_low_byte(self):
        """$A8 byte form: MOVE.B D1 -> the LOW byte of the long replaces the whole byte (no OR of a bit)."""
        for text, field, want in (('poke.b +104 1', 0xFF, 0x01), ('poke.b +104 $0102', 0xFF, 0x02), ('poke.b +104 0', 0x55, 0x00)):
            data, _, _ = assemble_at(text + '\n')
            res = self.run_cases([(1, field, data + b'\xff\xff')])
            self.assertEqual(res[0][3], want, text)

    def test_mutated_decoder_fails(self):
        """Prove the oracle bites: a decoder that gets the size of `loop` wrong (2 -> 4) disagrees with the C++."""
        orig = F.decode

        def bad(img, h, o):
            ins = orig(img, h, o)
            if ins.text.startswith('loop '):
                ins.size += 2
            return ins
        F.decode = bad
        try:
            with self.assertRaises(AssertionError):
                self.test_every_instruction_form()
        finally:
            F.decode = orig


class SyntheticRoundTrip(unittest.TestCase):
    def rnd_insn(self, r):
        k = r.randrange(26)
        if k == 0:
            return 'draw %d %d dx=%d dy=%d%s' % (r.randrange(32), r.randrange(256), r.randrange(-3000, 3000), r.randrange(-128, 128),
                                                 ''.join(' ' + n for n, _ in F.FLAG_NAMES if r.random() < 0.3))
        if k == 1:
            return r.choice(['face right', 'face left', 'face flip', 'face $%02X' % r.randrange(256)])
        if k == 2:
            return 'jump L%d' % r.randrange(4)
        if k == 3:
            return 'call L%d%s' % (r.randrange(4), r.choice(['', ' mode=$%02X' % r.choice([0, 1, 2, 4, 0xFF])]))
        if k == 4:
            return r.choice(['loop random', 'loop %d' % r.randrange(1, 256)])
        if k == 5:
            return 'loop2 %d' % r.randrange(256)
        if k == 6:
            return 'motion ' + ' '.join('%s=%d' % (n, r.randrange(256)) for n in ('param', 'count', 'flags', 'vy', 'vylim', 'vx', 'vxlim'))
        if k == 7:
            return '%s L%d%s' % (r.choice(['ifnogore', 'ifdead', 'spawn', 'ifsame', 'engine']), r.randrange(4),
                                 r.choice(['', ' b1=$%02X' % r.randrange(1, 256)]))
        if k == 8:
            return 'move x=%d y=%d z=%d flags=$%02X' % (r.randrange(0, 65536), r.randrange(0, 65536), r.randrange(0, 65536), r.randrange(0x40))
        if k == 9:
            return 'move x=%d y=%d z=%d flags=$%02X' % (r.randrange(-3000, 3000), r.randrange(-3000, 3000), r.randrange(-3000, 3000), 0x40 | r.randrange(0x40))
        if k == 10:
            return 'sound $%02X' % r.randrange(256)
        if k == 11:
            return r.choice(['kill', 'reset', 'kill b1=$%02X' % r.randrange(1, 256), 'reset b1=$%02X' % r.randrange(1, 256)])
        if k == 12:
            return 'frameset %s' % r.choice(['knights', 'creature', 'map', 'effects', str(r.randrange(5, 256))])
        if k == 13:
            return 'poke.%s %+d %d' % (r.choice('bwl'), r.randrange(-300, 300), r.randrange(0, 70000))
        if k == 14:
            return 'poke flags=$%02X %+d L%d' % (r.choice([4, 8, 0x14]), r.randrange(0, 130), r.randrange(4))
        if k == 15:
            return 'second %s' % r.choice(['off', 'L1', 'L2 flag=$00', 'L3 flag=$%02X' % r.randrange(2, 256)])
        if k == 16:
            return r.choice(['ifzero', 'ifnonzero']) + r.choice(['.b', '.w', '.l']) + ' %+d L%d' % (r.randrange(-100, 200), r.randrange(4))
        if k == 17:
            return '%s flags=$%02X %+d L%d' % (r.choice(['ifzero', 'ifnonzero']), r.choice([3, 4, 5, 8]), r.randrange(0, 130), r.randrange(4))
        if k == 18:
            return r.choice(['end', 'end loop2', 'end pad=$%02X' % r.randrange(1, 254)])
        if k == 19:
            return 'done'
        if k == 20:
            return r.choice(['jumpret', 'jumploop2'])
        if k == 21:
            return 'draw.raw $%02X %d dx=%d dy=%d' % (r.choice([0x01, 0x02, 0x22, 0x44, 0x7F]), r.randrange(256), r.randrange(-100, 100), r.randrange(-100, 100))
        return 'poke.l %+d $%08X' % (r.randrange(0, 130), r.randrange(0x10000, 0xFFFFFFFF))

    def test_random_programs(self):
        r = random.Random(20261007)
        names = {'L%d' % i: 0x20 * i for i in range(4)}
        for n in range(300):
            lines = []
            for _ in range(r.randrange(1, 30)):
                lines.append(self.rnd_insn(r))
            text = '.org h0+$800\n' + '\n'.join('\t' + l for l in lines) + '\n'
            img = F.MemImage(bytes(0x1000), names=names)
            sec = F.assemble(text, img)[0]
            # the image holds the assembled bytes and relocations: decode them again
            data = bytearray(0x1000)
            data[0x800:0x800 + len(sec.data)] = sec.data
            relocs = {0x800 + o: h for o, h in sec.relocs.items()}
            img2 = F.MemImage(bytes(data), relocs=relocs, names=names)
            o = 0x800
            decoded = []
            while o < 0x800 + len(sec.data):
                ins = F.decode(img2, 0, o)
                decoded.append(ins.text)
                o += ins.size
            self.assertEqual(o, 0x800 + len(sec.data), 'size mismatch in program %d' % n)
            # re-assemble the decoded text: identical bytes and relocations
            sec2 = F.assemble('.org h0+$800\n' + '\n'.join(decoded) + '\n', img2)[0]
            self.assertEqual(sec2.data, sec.data, 'program %d' % n)
            self.assertEqual(sec2.relocs, sec.relocs)

    def test_tick_and_terminator_bytes(self):
        data, _, _ = assemble_at('end\nend pad=$07\nend loop2\ndone\n')
        self.assertEqual(data, bytes([0xFF, 0x00, 0xFF, 0x07, 0xFF, 0xFE, 0xFF, 0xFF]))

    def test_documented_draft_syntax(self):
        """The T2 example of docs/MONSTER_KIT.md section 3 assembles (names resolved by the caller's symbol table)."""
        src = '''
        ifdead  fall          ; $B4
        draw    0 12 dx=-41 dy=-90 hurt   ; $00 slot 0 frame 12
        end                               ; $FF
        draw    0 13 dx=-41 dy=-90 hurt attack
        sound   $2F                       ; $A4
        end
        move    x=6                       ; $A0
        draw    0 14 dx=-41 dy=-90 hurt
        end
        done                              ; $FF $FF
        '''
        data, relocs, _ = assemble_at(src, {'fall': 0x1080})
        self.assertEqual(data[:6], bytes([0xB4, 0x00, 0, 0, 0x10, 0x80]))
        self.assertEqual(relocs, {2: 0})
        self.assertEqual(data[6:12], bytes([0x00, 12, 0xA6, 0x01, 0xFF, 0xD7]))
        self.assertEqual(data[-2:], b'\xff\xff')

    def test_assembler_rejects(self):
        img = F.MemImage(bytes(0x100), names={'a': 0})
        bad = ['frobnicate 1', 'draw 0 300', 'draw 0 1 dy=200', 'draw 0 1 wobble', 'jump nowhere', 'loop 0x100', 'end pad=$FF',
               'move x=1 q=2', 'poke.b +99999 1', 'call a mode=$03', 'sound $100']
        for line in bad:
            with self.assertRaises(F.ScriptError, msg=line):
                F.assemble('.org h0+$10\n\t%s\n' % line, img)
        with self.assertRaises(F.ScriptError):
            F.assemble('.org h0+$10\nx:\nx:\n', img)

    def test_unknown_opcode_fails_loudly(self):
        for op in (0x90, 0x9C, 0xD4, 0xE0, 0xF8):
            img = F.MemImage(bytes([op]) + bytes(8))
            with self.assertRaises(F.ScriptError, msg=hex(op)):
                F.decode(img, 0, 0)

    def test_unrelocated_pointer_fails_loudly(self):
        img = F.MemImage(bytes([0x84, 0x03, 0, 0, 0x12, 0x34]))
        with self.assertRaises(F.ScriptError):
            F.decode(img, 0, 0)


class CatalogShape(unittest.TestCase):
    """The catalog file is data in git: its structure must hold without the original."""

    def test_roles_and_tunables(self):
        names = set()
        for ai, a in CATALOG['ais'].items():
            self.assertIn('summary', a, ai)
            self.assertIn('knight_reaction', a, ai)
            seen = set()
            for r in a['roles']:
                self.assertNotIn(r['name'], seen, '%s: duplicate role %s' % (ai, r['name']))
                seen.add(r['name'])
                self.assertIn(r['kind'], ('field', 'label', 'table'))
                self.assertIn(r['attack'], ('none', 'required', 'optional'))
                if r['kind'] == 'label':
                    self.assertRegex(r['label'], r'^LAB_[0-9A-F]{4}$')
                if r['kind'] == 'table':
                    self.assertIn(r['field'], ('hurt', 'action', 'walk', 'damage'))
                self.assertRegex(r['cite'], r'^src/[a-z_/]+\.cpp:\d+')
            for t in a['tunables']:
                self.assertIn(t['name'], ('reach', 'keep_away', 'depth'))
        for c in CATALOG['creatures']:
            self.assertIn(c['ai'], CATALOG['ais'])
        offered = [n for n, a in CATALOG['ais'].items() if not a['advanced']]
        self.assertEqual(sorted(offered), sorted(['flyer', 'snatcher', 'brawler', 'brawler_b', 'spearman', 'caster', 'drake', 'stalker']))

    def test_cites_point_inside_the_sources(self):
        import re

        def walk(o):
            if isinstance(o, dict):
                for k, v in o.items():
                    if k == 'cite' and isinstance(v, str):
                        yield v
                    else:
                        yield from walk(v)
            elif isinstance(o, list):
                for v in o:
                    yield from walk(v)
        n = 0
        for cite in walk(CATALOG):
            for part in cite.split(','):
                part = part.strip()
                m = re.match(r'^((?:src|include|tools)/[\w/.]+?\.\w+)(?::(\d+)(?:-(\d+))?)?$', part)
                if not m:
                    continue            # prose ("src/game/fighters.cpp:1819-1825" style only; others are free text)
                path = os.path.join(ROOT, m.group(1))
                self.assertTrue(os.path.exists(path), cite)
                if m.group(2):
                    with open(path, encoding='utf-8', errors='replace') as f:
                        total = sum(1 for _ in f)
                    hi = int(m.group(3) or m.group(2))
                    self.assertLessEqual(hi, total, cite)
                    n += 1
        self.assertGreater(n, 100)


# ------------------------------------------------------------------------------------------------------------- original data

def all_roots(img):
    return sorted(a for a in img.by_addr if a[0] == 4)


@unittest.skipUnless(HAVE_ORIG, 'needs reference/ (original mog) and build/reasm/mog.symbols.json')
class OriginalScripts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.img = F.Image()
        cls.cat = F.Catalog()
        cls.tm = F.TableMem(cls.img)

    def creature(self, name):
        return F.Creature(self.img, self.cat, name, self.tm)

    def test_roundtrip_every_creature(self):
        for c in CATALOG['creatures']:
            cr = self.creature(c['name'])
            for scope in ('all', 'own'):
                prog = cr.program(scope=scope)
                ok, text, problems = F.roundtrip_program(self.img, prog)
                self.assertTrue(ok, '%s/%s: %s' % (c['name'], scope, problems))
                self.assertGreater(len(prog.insns), 20)

    def test_roundtrip_every_decodable_script(self):
        """Every label of the script hunk that disassembles (the rest is data / code): the union round-trips."""
        img = self.img
        prog = F.Program(img)
        good = 0
        for a in all_roots(img):
            p = F.Program(img)
            try:
                p.add_root(a)
            except F.ScriptError:
                continue
            prog.insns.update(p.insns)
            prog.roots.append(a)
            good += 1
        self.assertGreater(good, 250)
        ok, text, problems = F.roundtrip_program(img, prog)
        self.assertTrue(ok, problems)
        # and the exact opcode inventory of the original script hunk (docs/MONSTER_KIT.md 1.2 is complete)
        kinds = {i.text.split()[0].split('.')[0] for i in prog.insns.values()}
        self.assertTrue({'draw', 'end', 'done', 'loop', 'engine', 'sound', 'ifdead', 'kill', 'reset', 'call', 'loop2', 'second', 'move',
                         'jump', 'ifnogore', 'frameset', 'motion', 'face', 'spawn', 'ifzero', 'poke', 'jumpret'} <= kinds, kinds)

    def test_pokes_in_the_original_only_retarget_the_idle_script(self):
        """[verify] event pokes: the original never pokes +104 / +105; only the dragon and the demon poke +22 with a script pointer."""
        img = self.img
        prog = F.Program(img)
        for a in all_roots(img):
            p = F.Program(img)
            try:
                p.add_root(a)
            except F.ScriptError:
                continue
            prog.insns.update(p.insns)
        pokes = [i.text for i in prog.insns.values() if i.text.startswith('poke')]
        self.assertTrue(pokes)
        for t in pokes:
            self.assertTrue(t.startswith('poke flags=$04 +22 '), t)
        for c in CATALOG['creatures']:
            if c['ai'] in CATALOG['ais'] and not CATALOG['ais'][c['ai']]['advanced']:
                self.assertFalse([i for i in self.creature(c['name']).program(scope='all').insns.values() if i.text.startswith('poke')], c['name'])

    def test_init_records_match_catalog(self):
        for c in CATALOG['creatures']:
            cr = self.creature(c['name'])
            rec = {F.REC[off]: v[1] for off, v in cr.fields.items() if off in F.REC and F.REC[off] != 'celslots'}
            self.assertEqual(rec, c['record'], c['name'])

    def test_frame_sets(self):
        tm = F.TableMem(self.img, first='LAB_0303', last='LAB_0304')
        cells = tm.table(self.img.syms['LAB_0647'], 5)
        want = [self.img.syms[l] for l in ('LAB_05E1', 'LAB_05E0', 'LAB_05E2', 'LAB_0648')] + [0]
        self.assertEqual(cells, want)

    def test_every_hurt_script_can_die_and_every_die_is_accounted(self):
        """[verify] death: each hurt role has `ifdead`; its target calls engine LAB_0005 once and ends with `kill` before `done`."""
        img = self.img
        died = img.syms['LAB_0005']
        for c in CATALOG['creatures']:
            ai = CATALOG['ais'][c['ai']]
            if ai['advanced']:
                continue
            cr = self.creature(c['name'])
            prog = cr.program(scope='own')
            hurt_addrs = []
            for role, addr in cr.role_roots():
                if role.startswith('hurt') or role in ('hurt',) or role in [r['name'] for r in ai['roles'] if r['name'].startswith(('hurt', 'finished_off'))]:
                    hurt_addrs.append((role, addr))
            self.assertTrue(hurt_addrs, c['name'])
            idle = img.syms[cr.field(22)]
            for role, addr in hurt_addrs:
                if addr == idle:
                    continue            # a table entry that plays the idle script (Be action $10): no hit reaction at all
                seq = self.linear(prog, addr)
                dead = [i for i in seq if i.text.startswith('ifdead')]
                self.assertTrue(dead or any(i.text == 'kill' for i in seq), '%s/%s has no way to die' % (c['name'], role))
                for d in dead:
                    tgt = d.targets[0]
                    dseq = self.linear(prog, tgt)
                    calls = [i for i in dseq if i.text == 'engine ' + img.label(died)]
                    self.assertEqual(len(calls), 1, '%s/%s: die sequence %s' % (c['name'], role, img.label(tgt)))
                    texts = [i.text for i in dseq]
                    self.assertIn('kill', texts)
                    self.assertLess(texts.index('engine ' + img.label(died)), texts.index('kill'))
                    self.assertEqual(texts[-1], 'done')

    @staticmethod
    def linear(prog, addr):
        out, a = [], addr
        while a in prog.insns:
            i = prog.insns[a]
            out.append(i)
            if i.flow == 'stop':
                break
            a = (a[0], a[1] + i.size)
        # a die sequence may continue through an `ifdead` / `ifnogore` fallback target: follow the targets of ifnogore once
        return out

    def test_knight_attack_actions(self):
        """The hurt-table indices a creature must fill are the knight actions whose script carries attack draws."""
        img = self.img
        t = self.tm.table(img.syms['LAB_05F5'], 9)
        attack = []
        for i, v in enumerate(t):
            p = F.Program(img)
            p.add_root(v)
            seq = self.linear(p, v)
            if any(x.text.startswith('draw') and ' attack' in x.text for x in seq):
                attack.append(i)
        self.assertEqual(attack, CATALOG['knight']['attack_action_indices'])

    def test_checklist_holds_for_the_eight_original_creatures(self):
        """The checklist the catalog implies (R1) passes on every original creature: required fields / table entries non-empty,
        label roles exist, attack roles carry attack draws, idle / walk draws carry the hurt flag, hurt roles can die."""
        img = self.img
        for c in CATALOG['creatures']:
            ai = CATALOG['ais'][c['ai']]
            if ai['advanced']:
                continue
            cr = self.creature(c['name'])
            prog = cr.program(scope='own')
            for r in ai['roles']:
                where = '%s/%s' % (c['name'], r['name'])
                scripts = []
                if r['kind'] == 'field':
                    lab = cr.field(r['offset'])
                    self.assertTrue(lab, where)
                    scripts = [img.syms[lab]]
                elif r['kind'] == 'label':
                    scripts = [img.syms[r['label']]]
                else:
                    tab = cr.table(r['field'])
                    self.assertIsNotNone(tab, where)
                    cells = tab[1]
                    idx = list(r.get('required_indices', []))
                    for i in idx:
                        self.assertTrue(isinstance(cells[i], tuple) and img.hunk(cells[i][0]).kind != 'BSS', '%s[%d] empty' % (where, i))
                        scripts.append(cells[i])
                for s in scripts:
                    seq = self.linear(prog, s)
                    self.assertTrue(seq, where)
                    draws = [i.text for i in seq if i.text.startswith('draw')]
                    if r['attack'] == 'required':
                        self.assertTrue(any(' attack' in d for d in draws), where + ': no attack draw')
                    if r['kind'] in ('field',) and r['name'] == 'idle' or r['name'].startswith('walk'):
                        self.assertTrue(any(' hurt' in d for d in draws), where + ': no hurt draw')

    def test_cli(self):
        """The command line: creatures, dis (text assembles again with `asm`), roundtrip."""
        exe = [sys.executable, os.path.join(ROOT, 'tools', 'fightscript.py')]
        r = subprocess.run(exe + ['roundtrip', '--creature', 'troll'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('ok', r.stdout)
        r = subprocess.run(exe + ['dis', '--creature', 'troll'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        import cellnames
        self.assertIn(cellnames.names().get(('mog', 'LAB_08A5'), 'LAB_08A5') + ':', r.stdout)      # the troll's idle script
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, 'troll.txt')
            with open(src, 'w', encoding='utf-8') as f:
                f.write(r.stdout)
            r2 = subprocess.run(exe + ['asm', src], capture_output=True, text=True)
            self.assertEqual(r2.returncode, 0, r2.stderr)
            self.assertIn('bytes', r2.stdout)
        r = subprocess.run(exe + ['dis', '--label', 'LAB_08AA'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(cellnames.names().get(('mog', 'LAB_08AA'), 'LAB_08AA') + ':', r.stdout)

    def test_script_sounds_and_engine_ops_match_catalog(self):
        for c in CATALOG['creatures']:
            cr = self.creature(c['name'])
            prog = cr.program(scope='own')
            snd = sorted({int(i.text.split('$')[1], 16) for i in prog.insns.values() if i.text.startswith('sound')})
            eng = sorted(self.img.label(a) for a in prog.calls)
            self.assertEqual(snd, c['script_sounds'], c['name'])
            self.assertEqual(eng, c['engine_ops'], c['name'])


if __name__ == '__main__':
    unittest.main()
