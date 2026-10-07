"""Tests for the monster kit tools (ROADMAP 9.8a1): tools/monsterkit/{engine_ports.py, sounds.py, ai_catalog.json, kit.schema.json,
jsonschema_lite.py}.

  * engine_ports.py vs the host builds (clang++) of the C++ it ports, on random vectors: hitParse (src/engine/loaders.cpp), contactTest
    + contactOverlap + depthClose (src/game/creatures.cpp, incl. the mirrored-x quirk), celSizeEstimate; one deliberate mutation of
    contact_test (the quirk "fixed") is shown to fail; the original collide.hit (build/disks) parses, round-trips through
    parse_hit_sets / format_hit_set and equals the binary records of hit_record; the cel size formula holds for every original cel
    (build/disks + tools/artconv.py);
  * the frame size rule of the catalog ((2 * words + 2) * rows <= 4800, src/engine/blit.cpp planCel) against the host build;
  * the sound routines of the catalog (engine_ops ids) against fightOpRun / soundPick of the C++;
  * sounds.py: sound id -> bank map; every sound an original creature plays is usable with its own bank;
  * kit.schema.json accepts the example of docs/MONSTER_KIT.md and refuses mutations; the catalog's structure.
Tests that need the original game (build/disks, reference/, build/reasm) skip without it; the synthetic ones always run."""
import json
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'monsterkit'))
import engine_ports as P  # noqa: E402
import jsonschema_lite as J  # noqa: E402

def _load_json(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


CXX = shutil.which('clang++')
DISKS = os.path.join(ROOT, 'build', 'disks')
KITDIR = os.path.join(ROOT, 'tools', 'monsterkit')
CATALOG = _load_json(os.path.join(KITDIR, 'ai_catalog.json'))
SCHEMA = _load_json(os.path.join(KITDIR, 'kit.schema.json'))
HAVE_ORIG = (os.path.exists(os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog'))
             and os.path.exists(os.path.join(ROOT, 'build', 'reasm', 'mog.symbols.json')))
INC = '-I' + os.path.join(ROOT, 'include')
SRC = lambda *p: os.path.join(ROOT, 'src', *p)  # noqa: E731

PORTS_DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/loaders.hpp"
#include "engine/blit.hpp"
#include "game/creatures.hpp"
using namespace ms;
using namespace ms::game;

alignas(8) static uint8_t g_mem[0x80000];
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - g_mem); }
void *ms::jobHostPtr(uint32_t a) { return g_mem + a; }

static uint8_t text[1 << 22], out[1 << 22];

int main(int argc, char **argv) {
	const char *mode = argv[1];
	char line[1 << 16];
	if(!strcmp(mode, "hit")) {                       // hit <file> <size>: names on stdin -> "ret hex"
		FILE *f = fopen(argv[2], "rb"); size_t n = fread(text, 1, 1 << 20, f); fclose(f);
		int32_t size = atoi(argv[3]); (void)n;
		while(fgets(line, sizeof line, stdin)) {
			size_t l = strlen(line);
			while(l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
			memset(out, 0xCC, sizeof out);
			int32_t r = hitParse(text, size, (const uint8_t *)line, out);
			printf("%d ", r);
			for(int32_t i = 0; i < r; ++i) printf("%02x", out[i]);
			printf("\n");
		}
	} else if(!strcmp(mode, "contact")) {           // contact <arena>: vectors on stdin
		FILE *f = fopen(argv[2], "rb"); fread(g_mem, 1, sizeof g_mem, f); fclose(f);
		unsigned dt, df, dx, dy, at, hs, af, ax, ay;
		while(fgets(line, sizeof line, stdin)) {
			if(sscanf(line, "%u %u %u %u %u %u %u %u %u", &dt, &df, &dx, &dy, &at, &hs, &af, &ax, &ay) != 9) continue;
			ContactPoint pt = {0, 0};
			bool r = contactTest(g_mem + dt, (uint16_t)df, (uint16_t)dx, (uint16_t)dy, g_mem + at, g_mem + hs, (uint16_t)af, (uint16_t)ax, (uint16_t)ay, &pt);
			printf("%d %u %u\n", r ? 1 : 0, r ? pt.uwX : 0, r ? pt.uwY : 0);
		}
	} else if(!strcmp(mode, "overlap")) {
		int a, b, c, d;
		while(fgets(line, sizeof line, stdin)) {
			if(sscanf(line, "%d %d %d %d", &a, &b, &c, &d) != 4) continue;
			printf("%d\n", contactOverlap(a, b, c, d) ? 1 : 0);
		}
	} else if(!strcmp(mode, "depth")) {
		unsigned a, b;
		while(fgets(line, sizeof line, stdin)) {
			if(sscanf(line, "%u %u", &a, &b) != 2) continue;
			printf("%d\n", depthClose((uint16_t)a, (uint16_t)b) ? 1 : 0);
		}
	} else if(!strcmp(mode, "recordat")) {          // recordat <arena> then "set index"
		FILE *f = fopen(argv[2], "rb"); fread(g_mem, 1, sizeof g_mem, f); fclose(f);
		unsigned s, i;
		while(fgets(line, sizeof line, stdin)) {
			if(sscanf(line, "%u %u", &s, &i) != 2) continue;
			printf("%u\n", (unsigned)(hitRecordAt(g_mem + s, (uint16_t)i) - (g_mem + s)));
		}
	} else if(!strcmp(mode, "celsize")) {
		while(fgets(line, sizeof line, stdin)) {
			uint8_t h[10];
			for(int i = 0; i < 10; ++i) { unsigned v; sscanf(line + 2 * i, "%2x", &v); h[i] = (uint8_t)v; }
			printf("%u\n", celSizeEstimate(h));
		}
	} else if(!strcmp(mode, "plan")) {              // plan <clipH> : "w h x y" -> "visible words height"
		int w, h, x, y;
		const int clipH = atoi(argv[2]);
		while(fgets(line, sizeof line, stdin)) {
			if(sscanf(line, "%d %d %d %d", &w, &h, &x, &y) != 4) continue;
			CelFrame fr = {0x1000, (uint16_t)w, (uint16_t)h, 0, 15};
			CelView v = {};
			v.clipH = (int16_t)clipH; v.clipW = 40; v.origin = 0; v.stride = 40; v.temp = 0x20000; v.planes = 5;
			for(int i = 0; i < 6; ++i) v.dest[i] = 0x30000 + i * 8000;
			CelJob job;
			bool vis = planCel(fr, (int16_t)x, (int16_t)y, v, job);
			printf("%d %u %u\n", vis ? 1 : 0, vis ? job.words : 0, vis ? job.height : 0);
		}
	}
	return 0;
}
'''

OPS_DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "game/fighters.hpp"
using namespace ms::game;
alignas(8) static uint8_t g_mem[0x40000];
uint32_t ms::jobHostAddr(const void *p) { return (uint32_t)((const uint8_t *)p - g_mem); }
void *ms::jobHostPtr(uint32_t a) { return g_mem + a; }
static unsigned char ids[256]; static unsigned char fixedIds[4][256];
static void snd(uint16_t id) { ids[id & 255] = 1; }
static void fx(uint8_t ch, uint16_t id) { fixedIds[ch & 3][id & 255] = 1; }
int main() {
	static FighterEnv e; memset(&e, 0, sizeof e);
	static uint32_t seed; static SoundCycle cyc; static uint16_t bat; static FightVars vars;
	e.pSeed = &seed; e.pPickCycle = &cyc; e.pBatCounter = &bat; e.pVars = &vars; e.sound = snd; e.sfxFixed = fx;
	char line[64];
	while(fgets(line, sizeof line, stdin)) {
		unsigned l;
		if(sscanf(line, "%x", &l) != 1) continue;
		memset(ids, 0, sizeof ids); memset(fixedIds, 0, sizeof fixedIds); cyc.uwPlain = 0; cyc.uwList = 0; bat = 0;
		int handled = 1;
		for(uint32_t s = 1; s < 3000; ++s) {
			seed = s * 2654435761u;
			if(!fightOpRun(e, FIGHT_OP_TAG | l, 0x100, 0, 0, 0, 0, 0)) { handled = 0; break; }
		}
		printf("%04X %d ids:", l, handled);
		for(unsigned i = 0; i < 256; ++i) if(ids[i]) printf(" %u", i);
		printf(" fixed:");
		for(unsigned c = 0; c < 4; ++c) for(unsigned i = 0; i < 256; ++i) if(fixedIds[c][i]) printf(" %u/%u", c, i);
		printf("\n");
	}
	return 0;
}
'''


def compile_cpp(tmp, name, source, files):
    src = os.path.join(tmp, name + '.cpp')
    with open(src, 'w') as f:
        f.write(source)
    exe = os.path.join(tmp, name + ('.exe' if os.name == 'nt' else ''))
    r = subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-D_CRT_SECURE_NO_WARNINGS', INC, src] + files + ['-o', exe],
                       capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


def run(exe, args, stdin):
    r = subprocess.run([exe] + args, input=stdin, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return r.stdout.splitlines()


# ------------------------------------------------------------------------------------------------------------- hit text helpers

def random_hit_text(rnd, nsets=4):
    """A valid collide.hit-like text plus the names in it."""
    out, names = [], []
    for s in range(nsets):
        name = '%s%d.cel' % (rnd.choice(['Balok', 'troll', 'x', 'be']), s)
        names.append(name)
        frames = []
        for _ in range(rnd.randrange(1, 9)):
            if rnd.random() < 0.35:
                frames.append(None)
            else:
                n = rnd.randrange(1, 9)
                frames.append({'type': rnd.randrange(0, 5), 'points': [(rnd.randrange(0, 256), rnd.randrange(0, 256)) for _ in range(n)]})
        out.append(P.format_hit_set(name, frames))
    return ''.join(out), names


@unittest.skipUnless(CXX, 'clang++ not on PATH')
class PortsVsHost(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='monsterkit_')
        cls.exe = compile_cpp(cls.tmp, 'ports', PORTS_DRIVER, [SRC('engine', 'loaders.cpp'), SRC('engine', 'lzss.cpp'), SRC('engine', 'blit.cpp'),
                                                                SRC('game', 'creatures.cpp')])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def path(self, name):
        return os.path.join(self.tmp, name)

    # ---- hitParse ------------------------------------------------------------------------------------------------------------
    def compare_hit(self, text, names, size):
        p = self.path('hit.txt')
        with open(p, 'wb') as f:
            f.write(text)
        lines = run(self.exe, ['hit', p, str(size)], '\n'.join(names) + '\n')
        self.assertEqual(len(lines), len(names))
        found = 0
        for name, line in zip(names, lines):
            parts = line.split(' ')
            want = P.hit_parse(text, size, name)
            if want is None:
                self.assertEqual(parts[0], '-1', name)
            else:
                found += 1
                self.assertEqual(int(parts[0]), len(want), name)
                self.assertEqual(parts[1] if len(parts) > 1 else '', want.hex(), name)
        return found

    def test_hit_parse_random_texts(self):
        rnd = random.Random(7)
        found = 0
        for _ in range(25):
            text, names = random_hit_text(rnd)
            data = text.encode('latin-1')
            extra = ['nothere.cel', 'Balok', '00', '99', 'x' * 30, names[0][:-1], names[0] + 'x']
            found += self.compare_hit(data, names + extra, len(data))
            # truncated / shifted bounds and a corrupted byte
            self.compare_hit(data, names, len(data) // 2)
            self.compare_hit(data, names, 0)
            bad = bytearray(data)
            bad[rnd.randrange(len(bad))] = rnd.randrange(256)
            self.compare_hit(bytes(bad), names, len(bad))
        self.assertGreater(found, 80)

    def test_hit_parse_garbage_digits(self):
        """Non-digit bytes wrap like the asm (SUBI.W #$30, ADD.B on the low byte)."""
        rnd = random.Random(11)
        for _ in range(60):
            body = bytes(rnd.choice(b'0123456789ABz /\n\x00\xff') for _ in range(rnd.randrange(8, 120)))
            text = b'n.cel\n' + body + b'\n99\n'
            self.compare_hit(text, ['n.cel'], len(text))

    # ---- contactTest -----------------------------------------------------------------------------------------------------------
    def build_arena(self, rnd):
        """Random cel tables (defenders and attackers), hit sets and plane data in one arena; returns (arena, defs, atts)."""
        ar = P.Arena(bytes(0x80000))
        cursor = 0x1000
        planes_at = 0x20000
        defs, atts = [], []
        for _ in range(6):                                   # defender tables
            nf = rnd.randrange(1, 6)
            tab = cursor
            cursor += 10 + 10 * nf + 16
            ar.w16(tab, nf)
            ar.w32(tab + 2, planes_at)
            for f in range(nf):
                w, h = rnd.randrange(1, 70), rnd.randrange(1, 40)
                mask = rnd.choice([1, 3, 5, 7, 15, 31, 4, 16, 17, 63, 63, 2, 0, 64, 200])
                b8 = rnd.choice([1, 0, 0x11, 1])
                e = tab + 10 + 10 * f
                nplanes = P.popcount(mask) if mask <= 63 else 0
                size = P.frame_bytes(w, h, mask) if mask <= 63 else 0
                ar.w32(e, planes_at - 0x20000 + (planes_at - 0x20000))   # offset: filled below
                off = self_off = None
                off = planes_at - 0x20000
                ar.w32(e, off)
                ar.w16(e + 4, w)
                ar.w16(e + 6, h)
                ar.data[e + 8] = b8
                ar.data[e + 9] = mask
                ar.ensure(planes_at + size + 4)
                for i in range(size):
                    ar.data[planes_at + i] = rnd.choice([0, 0, 0xFF, rnd.randrange(256)])
                planes_at += size + 8
            defs.append((tab, nf))
        for _ in range(6):                                   # attacker tables + hit sets
            nf = rnd.randrange(1, 6)
            tab = cursor
            cursor += 10 + 10 * nf + 16
            ar.w16(tab, nf)
            for f in range(nf):
                e = tab + 10 + 10 * f
                ar.w16(e + 4, rnd.randrange(8, 100))
                ar.w16(e + 6, rnd.randrange(8, 60))
                ar.data[e + 8] = rnd.choice([1, 0, 0, 1, 0x10, 0x11])
                ar.data[e + 9] = 15
            frames = []
            for f in range(nf):
                if rnd.random() < 0.2:
                    frames.append(None)
                else:
                    frames.append({'type': rnd.randrange(0, 4), 'points': [(rnd.randrange(0, 90), rnd.randrange(0, 70)) for _ in range(rnd.randrange(1, 7))]})
            hs = cursor
            blob = b''.join(P.hit_record(fr) for fr in frames)
            ar.ensure(hs + len(blob) + 4)
            ar.data[hs:hs + len(blob)] = blob
            cursor += len(blob) + 8
            atts.append((tab, hs, nf))
        return ar, defs, atts

    def contact_vectors(self, rnd, defs, atts, n, ar=None):
        """Random vectors; with the arena, 70 % of them put one of the attacker's points inside the defender's frame (so the planes decide)."""
        vecs = []
        for _ in range(n):
            dt, dn = rnd.choice(defs)
            at, hs, an = rnd.choice(atts)
            df, af = rnd.randrange(dn), rnd.randrange(an)
            dx = rnd.choice([rnd.randrange(0, 80), rnd.randrange(0, 80), rnd.randrange(0, 80), rnd.randrange(65480, 65536)])
            dy = rnd.randrange(0, 60) if rnd.random() < 0.9 else rnd.randrange(65490, 65536)
            ax = (dx + rnd.randrange(-60, 70)) & 0xFFFF if rnd.random() < 0.85 else rnd.randrange(0, 65536)
            ay = (dy + rnd.randrange(-50, 40)) & 0xFFFF if rnd.random() < 0.85 else rnd.randrange(0, 90)
            if ar is not None and rnd.random() < 0.7:
                rec = P.hit_record_at(ar, hs, af)
                cnt = ar.r8(rec)
                if cnt:
                    k = rnd.randrange(cnt)
                    px, py = ar.r8(rec + 4 + 2 * k), ar.r8(rec + 5 + 2 * k)
                    cel = dt + 10 + 10 * df
                    w, h = ar.r16(cel + 4), ar.r16(cel + 6)
                    mirror = 0 if (ar.r8(at + 18 + af * 10) & 1) else ar.r16(at + 14 + af * 10)
                    tx, ty = rnd.randrange(0, max(1, w)), rnd.randrange(0, max(1, h))
                    # the window gate uses the mirrored x, the bit the raw one: aim the mirrored point inside, the raw point near it
                    ax = (dx + tx - ((mirror - px) if mirror else px)) & 0xFFFF
                    ay = (dy + ty - py) & 0xFFFF
            vecs.append((dt, df, dx, dy, at, hs, af, ax, ay))
        return vecs

    def run_contact(self, arena, vecs):
        p = self.path('arena.bin')
        with open(p, 'wb') as f:
            f.write(bytes(arena.data[:0x80000]) + bytes(max(0, 0x80000 - len(arena.data))))
        lines = run(self.exe, ['contact', p], '\n'.join(' '.join(str(x) for x in v) for v in vecs) + '\n')
        return [tuple(int(x) for x in l.split()) for l in lines]

    def test_contact_test_random(self):
        rnd = random.Random(1991)
        hits = mirrored_hits = total = 0
        for _ in range(5):
            ar, defs, atts = self.build_arena(rnd)
            vecs = self.contact_vectors(rnd, defs, atts, 4000, ar)
            got = self.run_contact(ar, vecs)
            for v, g in zip(vecs, got):
                want = P.contact_test(ar, *v)
                self.assertEqual((int(want[0]), want[1], want[2]), g, 'vector %s' % (v,))
                total += 1
                hits += want[0]
                if want[0] and not (ar.r8(v[4] + 18 + v[6] * 10) & 1):
                    mirrored_hits += 1
        self.assertGreater(hits, 400, 'the vectors must produce hits (%d of %d)' % (hits, total))
        self.assertGreater(mirrored_hits, 100, 'and mirrored hits (the quirk)')

    def test_contact_quirk_is_visible_and_mutation_fails(self):
        """The mirrored-x quirk (the bit tested and the reported point use the RAW point): a port that "fixes" it disagrees with the C++."""
        import inspect
        src = inspect.getsource(P.contact_test)
        self.assertIn('cx = _u16(px + att_x)', src)
        mutated = src.replace('cx = _u16(px + att_x)', 'cx = _u16(sx)')
        ns = dict(P.__dict__)
        exec(mutated, ns)
        rnd = random.Random(5)
        ar, defs, atts = self.build_arena(rnd)
        vecs = self.contact_vectors(rnd, defs, atts, 6000, ar)
        got = self.run_contact(ar, vecs)
        diff = 0
        for v, g in zip(vecs, got):
            w = ns['contact_test'](ar, *v)
            if (int(w[0]), w[1], w[2]) != g:
                diff += 1
        self.assertGreater(diff, 0, 'the mutation must be caught')
        # while the faithful port agrees on the same vectors
        for v, g in zip(vecs, got):
            w = P.contact_test(ar, *v)
            self.assertEqual((int(w[0]), w[1], w[2]), g)

    def test_overlap_and_depth(self):
        rnd = random.Random(3)
        vec = [(rnd.randrange(0, 600), rnd.randrange(0, 600), rnd.randrange(0, 600), rnd.randrange(0, 600)) for _ in range(3000)]
        vec += [(a, a + 5, a + 5, a + 9) for a in range(0, 50)] + [(10, 20, 20, 30), (10, 20, 21, 30), (10, 20, 5, 10), (10, 20, 5, 9)]
        got = run(self.exe, ['overlap'], '\n'.join('%d %d %d %d' % v for v in vec) + '\n')
        for v, g in zip(vec, got):
            self.assertEqual(int(P.contact_overlap(*v)), int(g), v)
        dv = [(rnd.randrange(0, 65536), rnd.randrange(0, 65536)) for _ in range(2000)] + [(100, 110), (100, 111), (0x8000, 0), (0, 0x8000), (5, 5)]
        got = run(self.exe, ['depth'], '\n'.join('%d %d' % v for v in dv) + '\n')
        for v, g in zip(dv, got):
            self.assertEqual(int(P.depth_close(*v)), int(g), v)

    def test_hit_record_at(self):
        rnd = random.Random(9)
        ar = P.Arena(bytes(0x80000))
        frames = [None if rnd.random() < 0.3 else {'type': 0, 'points': [(rnd.randrange(256), rnd.randrange(256)) for _ in range(rnd.randrange(1, 9))]} for _ in range(20)]
        blob = b''.join(P.hit_record(f) for f in frames)
        ar.data[0x100:0x100 + len(blob)] = blob
        p = self.path('arena2.bin')
        with open(p, 'wb') as f:
            f.write(bytes(ar.data))
        got = run(self.exe, ['recordat', p], '\n'.join('256 %d' % i for i in range(20)) + '\n')
        for i, g in enumerate(got):
            self.assertEqual(P.hit_record_at(ar, 0x100, i) - 0x100, int(g) - 0)

    # ---- cel sizes -------------------------------------------------------------------------------------------------------------
    def test_cel_size_estimate_random(self):
        rnd = random.Random(21)
        hdrs = [struct.pack('>HII', rnd.randrange(65536), rnd.randrange(1 << 20), rnd.choice([rnd.randrange(1 << 32), rnd.randrange(1 << 22)])) for _ in range(2000)]
        got = run(self.exe, ['celsize'], '\n'.join(h.hex() for h in hdrs) + '\n')
        for h, g in zip(hdrs, got):
            self.assertEqual(P.cel_size_estimate(h), int(g), h.hex())
        # one mutation: dropping the $168 slack must be caught
        self.assertNotEqual(P.cel_size_estimate(hdrs[0]) - 0x168, int(got[0]))

    def test_frame_size_rule(self):
        """catalog limits.frame_size: a frame of at most 336 px (21 words) with (2 * ceil(w/16) + 2) * h <= 4800 never plans a job that overruns a temp plane."""
        rule = CATALOG['limits']['frame_size']
        stride = rule['temp_plane_bytes']
        rnd = random.Random(33)
        vecs = []
        for _ in range(6000):
            w = rnd.choice([rnd.randrange(1, 337), rnd.randrange(1, 130)])
            words = (w + 15) // 16
            hmax = stride // (2 * words + 2)
            h = rnd.choice([rnd.randrange(1, hmax + 1), rnd.randrange(1, 300), hmax, hmax + 1])
            vecs.append((w, h, rnd.randrange(-150, 400), rnd.randrange(-120, 220)))
        got = run(self.exe, ['plan', '200'], '\n'.join('%d %d %d %d' % v for v in vecs) + '\n')
        over_ok = over_bad = 0
        for (w, h, x, y), g in zip(vecs, got):
            vis, words, height = (int(t) for t in g.split())
            if not vis:
                continue
            words_c = (w + 15) // 16
            fits = (2 * words_c + 2) * h <= stride
            used = (2 * words + 2) * height
            if fits:
                self.assertLessEqual(used, stride, (w, h, x, y))
                over_ok += 1
            elif used > stride:
                over_bad += 1
        self.assertGreater(over_ok, 1000)
        self.assertGreater(over_bad, 10, 'the rule is tight: frames beyond it do overrun')
        self.assertLessEqual((2 * 21 + 2) * rule['safe_max_height_at_full_width'], stride)
        self.assertGreater((2 * 21 + 2) * (rule['safe_max_height_at_full_width'] + 1), stride)


@unittest.skipUnless(CXX and os.path.exists(os.path.join(DISKS, 'B', 'collide.hit')), 'needs clang++ and the original collide.hit in build/disks')
class OriginalHitFile(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='monsterkit_hit_')
        cls.exe = compile_cpp(cls.tmp, 'ports', PORTS_DRIVER, [SRC('engine', 'loaders.cpp'), SRC('engine', 'lzss.cpp'), SRC('engine', 'blit.cpp'),
                                                                SRC('game', 'creatures.cpp')])
        with open(os.path.join(DISKS, 'B', 'collide.hit'), 'rb') as f:
            cls.text = f.read()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_sets_roundtrip_and_equal_the_cpp(self):
        sets = P.parse_hit_sets(self.text)
        self.assertGreaterEqual(len(sets), 14, 'the original holds 14 sets')
        p = os.path.join(self.tmp, 'collide.hit')
        with open(p, 'wb') as f:
            f.write(self.text)
        names = [n for n, _ in sets]
        lines = run(self.exe, ['hit', p, str(len(self.text))], '\n'.join(names) + '\n')
        rebuilt = ''
        for (name, frames), line in zip(sets, lines):
            parts = line.split(' ')
            want = b''.join(P.hit_record(fr) for fr in frames)
            self.assertEqual(parts[1] if len(parts) > 1 else '', want.hex(), name)
            self.assertEqual(P.hit_parse(self.text, len(self.text), name), want, name)
            rebuilt += P.format_hit_set(name, frames)
        self.assertEqual(rebuilt.encode('latin-1') + b'99' + bytes([10]), self.text.replace(b'\r\n', b'\n'))

    def test_gate_box_is_the_byte_of_the_maximum(self):
        for name, frames in P.parse_hit_sets(self.text):
            for fr in frames:
                if fr:
                    mx = max(x for x, _ in fr['points'])
                    my = max(y for _, y in fr['points'])
                    rec = P.hit_record(fr)
                    self.assertEqual((rec[2], rec[3]), (mx & 0xFF, my & 0xFF))


class PortsSynthetic(unittest.TestCase):
    def test_hit_set_format_roundtrip(self):
        rnd = random.Random(2)
        for _ in range(50):
            text, names = random_hit_text(rnd, 3)
            sets = P.parse_hit_sets(text.encode())
            self.assertEqual([n for n, _ in sets], names)
            self.assertEqual(''.join(P.format_hit_set(n, f) for n, f in sets), text)

    def test_gate_box_wraps_above_255(self):
        self.assertEqual(P.gate_box([(300, 10), (5, 999)]), (300 & 255, 999 & 255))
        self.assertEqual(P.hit_record({'type': 1, 'points': [(4, 5), (200, 7)]}), bytes([2, 1, 200, 7, 4, 5, 200, 7]))
        self.assertEqual(P.hit_record(None), b'\0')

    def test_cel_formulas(self):
        self.assertEqual(P.frame_bytes(1, 1, 1), 2)
        self.assertEqual(P.frame_bytes(16, 3, 5), 2 * 3 * 2)
        self.assertEqual(P.frame_bytes(17, 3, 31), 4 * 3 * 5)
        frames = [(82, 92, 31), (16, 8, 0)]
        self.assertEqual(P.cel_header_bits(frames), 8 * (6 * 2 * 92 * 0 + P.frame_bytes(82, 92, 31)))
        hdr = struct.pack('>HII', 2, 100, P.cel_header_bits(frames))
        self.assertEqual(P.cel_size_estimate(hdr), P.cel_arena_bytes(frames))

    def test_cel_formula_on_original_files(self):
        if not os.path.isdir(DISKS):
            self.skipTest('no build/disks')
        try:
            import artconv
        except (SystemExit, ImportError):
            self.skipTest('tools/artconv.py needs Pillow and numpy')
        n = 0
        for disk in sorted(os.listdir(DISKS)):
            d = os.path.join(DISKS, disk)
            if not os.path.isdir(d):
                continue
            for name in sorted(os.listdir(d)):
                if not name.lower().endswith(('.cel', '.ob')):
                    continue
                with open(os.path.join(d, name), 'rb') as f:
                    data = f.read()
                try:
                    c = artconv.parse_cel(data)
                except ValueError:
                    continue
                frames = [(f['w'], f['h'], f['b9']) for f in c['frames']]
                self.assertEqual(P.cel_header_bits(frames), c['bits'], name)
                self.assertEqual(P.cel_decoded_bytes(frames), len(c['blob']), name)
                self.assertEqual(P.cel_size_estimate(data[:10]), P.cel_arena_bytes(frames), name)
                n += 1
        self.assertGreater(n, 20)


@unittest.skipUnless(CXX, 'clang++ not on PATH')
class EngineOpSounds(unittest.TestCase):
    """catalog engine_ops: the sound ids of the script-called routines, run through fightOpRun on the host."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='monsterkit_ops_')
        cls.exe = compile_cpp(cls.tmp, 'ops', OPS_DRIVER, [SRC('game', 'fighters.cpp'), SRC('game', 'fight_ops.cpp'), SRC('game', 'fight_creatures.cpp'),
                                                           SRC('game', 'creatures.cpp'), SRC('engine', 'util.cpp'), SRC('game', 'rules/damage.cpp')] + [SRC('game', 'rules/' + n + '.cpp') for n in ['ai_fight', 'ai_fight_flyer', 'ai_fight_snatcher', 'ai_fight_demon', 'ai_fight_knight', 'ai_fight_dragon', 'ai_fight_brawler', 'ai_fight_caster', 'ai_fight_drake', 'ai_fight_stalker']] + [GAMEDATA_SOURCE])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_engine_op_sounds(self):
        ops = {k: v for k, v in CATALOG['engine_ops'].items() if 'sounds' in v or 'fixed_channel' in v}
        lines = run(self.exe, [], ''.join('%s\n' % k[4:] for k in ops))
        self.assertEqual(len(lines), len(ops))
        for (label, op), line in zip(ops.items(), lines):
            m = re.match(r'(\w+) (\d) ids:([\d ]*) fixed:([\d/ ]*)$', line)
            self.assertTrue(m, line)
            self.assertEqual(m.group(2), '1', '%s is not handled by fightOpRun' % label)
            ids = [int(x) for x in m.group(3).split()]
            self.assertEqual(ids, sorted(op.get('sounds', [])), label)
            fixed = {}
            for x in m.group(4).split():
                ch, i = x.split('/')
                fixed.setdefault(ch, []).append(int(i))
            self.assertEqual(fixed, {k: sorted(v) for k, v in op.get('fixed_channel', {}).items()}, label)

    def test_mutation_is_caught(self):
        op = dict(CATALOG['engine_ops']['LAB_02E0'])
        op['sounds'] = [91, 93]
        lines = run(self.exe, [], '02E0\n')
        self.assertNotEqual([int(x) for x in re.match(r'\w+ \d ids:([\d ]*) fixed', lines[0]).group(1).split()], op['sounds'])


@unittest.skipUnless(HAVE_ORIG, 'needs the original mog (reference/, build/reasm)')
class SoundBanks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import sounds
        cls.S = sounds

    def test_ranges_in_the_catalog_are_the_map(self):
        S = self.S
        want = []
        prev = None
        for i in range(S.SEQ_COUNT):
            b = '+'.join(S.banks_of(i)) or 'none'
            if prev and prev[2] == b and prev[1] == i - 1:
                prev[1] = i
            else:
                prev = [i, i, b]
                want.append(prev)
        self.assertEqual([[r['from'], r['to'], r['banks']] for r in CATALOG['sound_banks']['id_ranges']], want)
        # spot checks of the map (the knights' bank 1..19, the ratmen bank 91..108, silence 167)
        self.assertEqual(S.banks_of(10), ['C7'])
        self.assertEqual(S.banks_of(91), ['CB'])
        self.assertEqual(S.banks_of(167), ['wave'])
        self.assertEqual(S.banks_of(0x2F), ['C8'])

    def test_every_sound_an_original_creature_plays_is_usable_with_its_bank(self):
        S = self.S
        ops = CATALOG['engine_ops']
        for c in CATALOG['creatures']:
            ids = set(c['script_sounds'])
            for label in c['engine_ops']:
                ids |= set(ops.get(label, {}).get('sounds', []))
                for chan, lst in ops.get(label, {}).get('fixed_channel', {}).items():
                    ids |= set(lst)
            ai = CATALOG['ais'].get(c['ai'], {})
            for hw in ai.get('hard_wired_sounds', []):
                ids.add(hw['id'])
            for i in sorted(ids):
                self.assertTrue(S.usable_with(i, c['bank_file']), '%s plays sound %d (%s) which its bank %s cannot serve' % (c['name'], i, S.banks_of(i), c['bank_file']))
        # a ratmen-only sound is refused with another creature's bank, a creature-bank sound with none
        self.assertFalse(S.usable_with(91, 'Be.a'))
        self.assertTrue(S.usable_with(91, 'Ra.a'))
        self.assertFalse(S.usable_with(0x2F, None))
        self.assertTrue(S.usable_with(10, None))


class SchemaAndCatalog(unittest.TestCase):
    def example_kit(self):
        with open(os.path.join(ROOT, 'docs', 'MONSTER_KIT.md'), encoding='utf-8') as f:
            doc = f.read()
        i = doc.index('## 2. The kit format')
        m = re.search(r'```json\n(.*?)```', doc[i:], re.S)
        return json.loads(m.group(1))

    def test_example_kit_validates(self):
        self.assertEqual(J.validate(self.example_kit(), SCHEMA), [])

    def test_schema_refuses_mutations(self):
        kit = self.example_kit()

        def mutate(fn):
            k = json.loads(json.dumps(kit))
            fn(k)
            return J.validate(k, SCHEMA)
        cases = {
            'kit version': lambda k: k.__setitem__('kit', 2),
            'planes 4': lambda k: k.__setitem__('planes', 4),
            'missing ai': lambda k: k.pop('ai'),
            'name with capitals': lambda k: k.__setitem__('name', 'Frost'),
            'slot 5': lambda k: k['celsets'][0].__setitem__('slot', 5),
            'dy beyond s8': lambda k: k['animations']['stand']['steps'][0]['draws'][0].__setitem__('dy', 200),
            'attack point 300': lambda k: k['celsets'][0]['frames'][0]['attack']['points'].append([300, 1]),
            'unknown key': lambda k: k.__setitem__('wobble', 1),
            'hurt_by_action 8 entries': lambda k: k.__setitem__('hurt_by_action', k['hurt_by_action'][:8]),
            'six celsets': lambda k: k.__setitem__('celsets', [k['celsets'][0]] * 6),
            'bad colour': lambda k: k['palette']['own'].__setitem__('9', 'blue'),
            'own colour 6': lambda k: k['palette']['own'].__setitem__('6', '#000000'),
            'event op or': lambda k: k['animations']['stand']['steps'][0].__setitem__('events', [{'poke': {'off': 104, 'size': 'byte', 'op': 'or', 'value': 1}}]),
        }
        for label, fn in cases.items():
            self.assertTrue(mutate(fn), 'schema accepted: %s' % label)
        ok = json.loads(json.dumps(kit))
        ok['animations']['stand']['steps'][0]['events'] = [{'name': 'retarget', 'poke': {'off': 22, 'size': 'long', 'op': 'store', 'value': 'stand'}}]
        self.assertEqual(J.validate(ok, SCHEMA), [])

    def test_validator_basics(self):
        s = {'type': 'object', 'required': ['a'], 'properties': {'a': {'type': 'integer', 'minimum': 1}, 'b': {'oneOf': [{'type': 'string'}, {'type': 'null'}]}},
             'additionalProperties': False}
        self.assertEqual(J.validate({'a': 1, 'b': None}, s), [])
        self.assertTrue(J.validate({'a': 0}, s))
        self.assertTrue(J.validate({'a': True}, s))
        self.assertTrue(J.validate({'a': 1, 'c': 2}, s))
        self.assertTrue(J.validate({'a': 1, 'b': 3}, s))

    def test_catalog_checklist_facts(self):
        """The facts the verification of 9.8a1 settled, as data (the original-data tests prove them against the binary)."""
        ais = CATALOG['ais']
        self.assertEqual(ais['stalker']['damage'], {'mode': 'fixed', 'value': 7, 'note': ais['stalker']['damage']['note'], 'cite': ais['stalker']['damage']['cite']})
        for ai in ('flyer', 'spearman', 'drake', 'stalker'):
            self.assertEqual(ais[ai]['damage']['mode'], 'fixed', ai)
        for ai in ('snatcher', 'brawler', 'brawler_b', 'caster'):
            self.assertEqual(ais[ai]['damage']['mode'], 'table', ai)
        for ai in ('flyer', 'snatcher', 'brawler', 'brawler_b', 'spearman', 'caster', 'drake', 'stalker'):
            self.assertTrue(ais[ai]['die']['required'])
            self.assertEqual(ais[ai]['events'], [], ai)
            attack_roles = [r for r in ais[ai]['roles'] if r['attack'] == 'required']
            self.assertTrue(attack_roles, ai)
        self.assertEqual(CATALOG['limits']['hit_pairs'], 10)
        self.assertEqual(CATALOG['frame_sets']['2'].split()[0], 'LAB_05E0')


# ------------------------------------------------------------------------------------------------ 9.8a2: tools/monsterkit.py

import monsterkit as MK  # noqa: E402

try:
    import numpy as np  # noqa: E402
    from PIL import Image as PImage  # noqa: E402
    import artconv  # noqa: E402
except ImportError:  # pragma: no cover
    np = None
HAVE_DISKS = os.path.isdir(os.path.join(DISKS, 'B')) and os.path.isdir(os.path.join(DISKS, 'C'))
GRAY32 = [(i * 8, i * 8, i * 8) for i in range(32)]


def _write_json(path, doc):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(doc, f, indent=1)


def synthetic_stalker(kitdir):
    """A hand-made kit for the stalker AI (no original data): 2 frames, a 32x16 sheet, every role of the checklist."""
    os.makedirs(kitdir, exist_ok=True)
    sheet = np.zeros((16, 32), dtype=np.uint8)
    sheet[2:14, 2:14] = 9
    sheet[4:10, 20:28] = 10
    MK.write_indexed_png(os.path.join(kitdir, 'sheet0.png'), sheet, GRAY32)

    def fr(i, x, att=None):
        d = {'id': 'f%d' % i, 'src': {'sheet': 0, 'rect': [x, 0, 16, 16]}, 'anchor': [8, 16]}
        if att:
            d['attack'] = {'type': 0, 'points': att}
        return d

    def draw(f, **kw):
        return dict({'frame': f, 'slot': 0, 'dx': 0, 'dy': 0, 'hurt': True}, **kw)

    def anim(*draws):
        return {'steps': [{'ticks': 1, 'draws': list(draws)}]}

    kit = {
        'kit': 1, 'name': 'synth', 'ai': 'stalker', 'planes': 5,
        'palette': {'scene': 'troll', 'colors': ['#000000'] + ['#%02x%02x%02x' % (i * 8, i * 8, i * 8) for i in range(1, 32)], 'allow': list(range(32))},
        'sheets': [{'file': 'sheet0.png'}],
        'celsets': [{'slot': 0, 'file': 'auto', 'hits': True, 'hit_name': 'Synth1.cel', 'hit_frames': 2,
                     'frames': [fr(0, 0), fr(1, 16, [[12, 4]])]}],
        'animations': {
            'stand': anim(draw('f0')), 'ouch': anim(draw('f0')),
            'club': anim(draw('f0'), draw('f1', attack=True)), 'slam': anim(draw('f0'), draw('f1', attack=True)),
            'fall': {'steps': [{'ticks': 1, 'draws': [draw('f0')]}],
                     'asm': '.org h4+$0\nfall:\n\tengine LAB_0005\n\tdraw 0 0 dx=-8 dy=-16 hurt\n\tend\n\tkill\n\tdone\n'},
        },
        'roles': {'idle': 'stand', 'after_hit': 'stand', 'hurt': 'ouch', 'strike': 'club', 'heavy': 'slam',
                  'walk.side': ['stand', None, None, None, None, None, None, None], 'die': 'fall'},
        'hurt_by_action': [None] * 9, 'damage': [0] * 9, 'stats': {'hp': 10, 'reach': 100, 'keep_away': 50, 'depth': 5},
        'sounds': {'bank': 'To.a'}, 'arena': {}, 'tier': 'auto',
    }
    _write_json(os.path.join(kitdir, 'kit.json'), kit)
    return kit


@unittest.skipIf(np is None, 'needs numpy + Pillow')
class MonsterKitSynthetic(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='mk_')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.kitdir = os.path.join(self.tmp, 'synth')
        self.kit = synthetic_stalker(self.kitdir)

    def test_schema_and_check_pass(self):
        self.assertEqual(J.validate(self.kit, SCHEMA), [])
        rep = MK.check_kit(self.kitdir)
        self.assertEqual(rep.errors, [], rep.text())
        self.assertTrue(all(c['ok'] for c in rep.checklist))
        self.assertEqual(rep.tier, 't2')                    # own scripts, no source: a T2 kit

    def test_checklist_blocks_missing_roles(self):
        for role in ('heavy', 'hurt', 'die'):
            kit = json.loads(json.dumps(self.kit))
            kit['roles'][role] = None
            _write_json(os.path.join(self.kitdir, 'kit.json'), kit)
            rep = MK.check_kit(self.kitdir)
            self.assertFalse(rep.ok, role)
            self.assertTrue(any(role in e for e in rep.errors), (role, rep.errors))
            self.assertTrue(any((not c['ok']) and c['role'] == role for c in rep.checklist), role)

    def test_attack_role_needs_attack_points(self):
        kit = json.loads(json.dumps(self.kit))
        del kit['celsets'][0]['frames'][1]['attack']
        _write_json(os.path.join(self.kitdir, 'kit.json'), kit)
        rep = MK.check_kit(self.kitdir)
        self.assertTrue(any('no attack draw with attack points' in e for e in rep.errors), rep.errors)

    def test_limits(self):
        kit = json.loads(json.dumps(self.kit))
        sheet = np.zeros((16, 32), dtype=np.uint8)
        sheet[0, 0] = 40                                      # index 40 does not fit 5 planes
        MK.write_indexed_png(os.path.join(self.kitdir, 'sheet0.png'), sheet, [(0, 0, 0)] * 48)
        rep = MK.check_kit(self.kitdir)
        self.assertTrue(any('does not fit 5 planes' in e for e in rep.errors), rep.errors)
        sheet[0, 0] = 7                                       # the first fighter's colour
        MK.write_indexed_png(os.path.join(self.kitdir, 'sheet0.png'), sheet, [(0, 0, 0)] * 48)
        self.assertTrue(any('first fighter' in e for e in MK.check_kit(self.kitdir).errors))
        kit['celsets'][0]['frames'][0]['src']['rect'] = [0, 0, 400, 16]    # outside the sheet
        _write_json(os.path.join(self.kitdir, 'kit.json'), kit)
        self.assertFalse(MK.check_kit(self.kitdir).ok)

    def test_rgb_sheet_snaps_to_the_palette(self):
        rgb = np.zeros((16, 32, 4), dtype=np.uint8)
        rgb[2:14, 2:14] = (72, 72, 72, 255)                   # = palette index 9 (9 * 8)
        PImage.fromarray(rgb, 'RGBA').save(os.path.join(self.kitdir, 'sheet0.png'))
        idx = MK.read_sheet(os.path.join(self.kitdir, 'sheet0.png'), self.kit['palette']['colors'])
        self.assertEqual(int(idx[5, 5]), 9)
        self.assertEqual(int(idx[0, 0]), 0)

    def test_cel_encoder_round_trip(self):
        rng = np.random.RandomState(7)
        frames = [rng.randint(0, 32, size=(h, w)).astype(np.uint8) for w, h in ((16, 4), (33, 7), (5, 9), (70, 3))]
        frames.append(np.zeros((6, 20), dtype=np.uint8))
        data, blob, table = MK.encode_cel(frames, [0] * len(frames))
        cel = artconv.parse_cel(data)
        self.assertEqual(cel['blob'], blob)
        self.assertEqual(cel['bits'], 8 * len(blob))
        for f, idx, t in zip(cel['frames'], frames, table):
            self.assertEqual((f['offset'], f['w'], f['h'], f['b8'], f['b9']), t)
            self.assertEqual(f['b8'], 1)
            np.testing.assert_array_equal(artconv.planes_to_indices(cel['blob'], f['offset'], f['w'], f['h'], f['b9']), idx)

    def test_splice_hit_sets_keeps_the_rest(self):
        a = P.format_hit_set('A.cel', [None, {'type': 0, 'points': [(1, 2)]}])
        text = (a + P.format_hit_set('B.cel', [{'type': 3, 'points': [(9, 8), (7, 6)]}])).encode() + b'99\n'
        new = P.format_hit_set('b.cel', [None, {'type': 0, 'points': [(5, 5)]}])
        out = MK.splice_hit_sets(text, {'b.cel': new})
        self.assertTrue(out.startswith(a.encode()))
        self.assertTrue(out.endswith(b'99\n99\n'))
        self.assertEqual(MK.splice_hit_sets(text, {}), text)
        with self.assertRaises(MK.KitError):
            MK.splice_hit_sets(text, {'c.cel': new})

    def test_fit_by_anchor(self):
        of = {'w': 8, 'h': 8, 'anchor': [4, 8]}
        art = np.ones((6, 6), dtype=np.uint8)
        out, moved = MK._fit(art, [3, 6], of)
        self.assertTrue(moved)
        self.assertEqual(out.shape, (8, 8))
        self.assertEqual(int(out[2:8, 1:7].sum()), 36)        # the 6x6 block shifted by (+1, +2) so the anchors coincide
        self.assertEqual(int(out.sum()), 36)
        same, moved = MK._fit(np.ones((8, 8), dtype=np.uint8), [4, 8], of)
        self.assertFalse(moved)

    def test_tracked_path_is_refused(self):
        with self.assertRaises(MK.KitError):
            MK._refuse_tracked(os.path.join(ROOT, 'tools', 'would_be_tracked_kit'))
        MK._refuse_tracked(os.path.join(ROOT, 'build', 'kits', 'x'))      # build/ is git-ignored

    def test_t1_build_is_refused_cleanly(self):
        import contextlib
        import io
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(MK.main(['build', self.kitdir, '--tier', 'T1']), 2)
        with self.assertRaises(MK.KitError):
            MK.build_t0(self.kitdir, os.path.join(self.tmp, 'out'))           # not a game clone: no origin


@unittest.skipUnless(np is not None and HAVE_ORIG and HAVE_DISKS and os.path.exists(os.path.join(DISKS, 'B', 'collide.hit')),
                     'needs the original game (build/disks, reference/, build/reasm), numpy, Pillow')
class MonsterKitGame(unittest.TestCase):
    """clone every offered creature, check it, rebuild T0: byte-identical (passthrough) and decode-equal (--reencode)."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='mkg_')
        cls.kits = {}
        for c in MK.OFFERED:
            d = os.path.join(cls.tmp, c)
            cls.kits[c] = (d, MK.clone_game(c, d))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, True)

    def test_every_clone_passes_check_as_t0(self):
        for c, (d, kit) in self.kits.items():
            self.assertEqual(J.validate(kit, SCHEMA), [], c)
            rep = MK.check_kit(d)
            self.assertEqual(rep.errors, [], (c, rep.text()))
            self.assertEqual(rep.tier, 't0', c)
            self.assertEqual(kit['ai'], next(x for x in CATALOG['creatures'] if x['name'] == c)['ai'])
            self.assertTrue(kit['roles']['die'], c)
            self.assertTrue(all(ch['ok'] for ch in rep.checklist), (c, [x for x in rep.checklist if not x['ok']]))

    def test_t0_rebuild_is_byte_identical(self):
        for c, (d, kit) in self.kits.items():
            res = MK.build_t0(d, os.path.join(d, 'out'))
            self.assertTrue(res['files'], c)
            for f in res['files']:
                self.assertTrue(f['identical'], (c, f))
                with open(MK.find_game_file(f['name']), 'rb') as a, open(os.path.join(d, 'out', 'art', f['name']), 'rb') as b:
                    self.assertEqual(a.read(), b.read(), (c, f['name']))

    def test_t0_reencode_is_decode_equal(self):
        for c, (d, kit) in self.kits.items():
            res = MK.build_t0(d, os.path.join(d, 'oute'), reencode=True)
            for f in res['files']:
                if f['name'] == 'collide.hit':
                    self.assertTrue(f['identical'], c)
                    continue
                self.assertEqual(f['mode'], 'encoded')
                with open(MK.find_game_file(f['name']), 'rb') as a, open(os.path.join(d, 'oute', 'art', f['name']), 'rb') as b:
                    orig, ours = artconv.parse_cel(a.read()), artconv.parse_cel(b.read())
                self.assertEqual(orig['blob'], ours['blob'], (c, f['name']))
                self.assertEqual([(x['offset'], x['w'], x['h'], x['b8'], x['b9']) for x in orig['frames']],
                                 [(x['offset'], x['w'], x['h'], x['b8'], x['b9']) for x in ours['frames']], (c, f['name']))
                self.assertEqual(orig['bits'], ours['bits'])

    def test_clone_scripts_reassemble_to_the_original_bytes(self):
        import fightscript as F
        img = F.Image()
        for c, (d, kit) in self.kits.items():
            text = ''.join(a['asm'] for a in kit['animations'].values())
            secs = F.assemble(text, img)
            self.assertTrue(secs, c)
            for s in secs:
                orig = img.hunk(s.hunk).data[s.offset:s.offset + len(s.data)]
                self.assertEqual(s.data, orig, (c, s.hunk, s.offset))
                want = {o - s.offset: img.relocs[s.hunk][o] for o in img.relocs[s.hunk] if s.offset <= o < s.offset + len(s.data)}
                self.assertEqual(want, s.relocs, (c, s.hunk, s.offset))

    def test_recoloured_clone_builds_and_passes_check(self):
        d, kit = self.kits['troll']
        rd = os.path.join(self.tmp, 'frost_troll')
        MK.clone_kit(d, rd, 'frost_troll')
        k2 = MK.load_kit(rd)
        self.assertEqual(k2['name'], 'frost_troll')
        self.assertEqual(k2['cloned_from'], {'kind': 'kit', 'name': 'troll'})
        lut = np.arange(256, dtype=np.uint8)
        lut[9], lut[10], lut[11] = 11, 9, 10                   # rotate three of the creature's own colours
        for sh in k2['sheets']:
            p = os.path.join(rd, sh['file'])
            sheet = np.array(PImage.open(p), dtype=np.uint8)
            MK.write_indexed_png(p, lut[sheet], GRAY32)
        rep = MK.check_kit(rd)
        self.assertEqual(rep.errors, [], rep.text())
        self.assertEqual(rep.tier, 't0')
        res = MK.build_t0(rd, os.path.join(rd, 'out'))
        for f in res['files']:
            if f['name'].lower().endswith('.cel'):
                self.assertEqual(f['mode'], 'encoded')
                self.assertFalse(f['identical'])
        # decode what was built: every frame equals the original frame with the colour rotation applied
        with open(MK.find_game_file('TROLL1.CEL'), 'rb') as fh:
            orig = artconv.parse_cel(fh.read())
        with open(os.path.join(rd, 'out', 'art', 'TROLL1.CEL'), 'rb') as fh:
            new = artconv.parse_cel(fh.read())
        self.assertEqual(len(orig['frames']), len(new['frames']))
        changed = 0
        for fo, fn in zip(orig['frames'], new['frames']):
            io = artconv.planes_to_indices(orig['blob'], fo['offset'], fo['w'], fo['h'], fo['b9'])
            inn = artconv.planes_to_indices(new['blob'], fn['offset'], fn['w'], fn['h'], fn['b9'])
            np.testing.assert_array_equal(lut[io], inn)
            changed += int((io != inn).any())
        self.assertGreater(changed, 0)
        # attack points are untouched: collide.hit equals the original
        with open(MK.find_game_file('collide.hit'), 'rb') as a, open(os.path.join(rd, 'out', 'art', 'collide.hit'), 'rb') as b:
            self.assertEqual(a.read(), b.read())

    def test_edited_attack_points_land_in_collide_hit(self):
        d, kit = self.kits['troll']
        rd = os.path.join(self.tmp, 'troll_hits')
        MK.clone_kit(d, rd, 'troll_hits')
        k2 = MK.load_kit(rd)
        fr = next(f for f in k2['celsets'][0]['frames'] if 'attack' in f)
        p0 = fr['attack']['points'][0]
        fr['attack']['points'][0] = [p0[0], 1 + (p0[1] + 1) % 50]
        MK.save_kit(rd, k2)
        MK.build_t0(rd, os.path.join(rd, 'out'))
        with open(os.path.join(rd, 'out', 'art', 'collide.hit'), 'rb') as fh:
            sets = dict((n.lower(), f) for n, f in P.parse_hit_sets(fh.read()))
        idx = k2['celsets'][0]['frames'].index(fr)
        self.assertEqual([list(p) for p in sets['troll1.cel'][idx]['points']], fr['attack']['points'])

    def test_check_blocks_a_clone_with_a_missing_animation(self):
        d, kit = self.kits['troll']
        rd = os.path.join(self.tmp, 'troll_broken')
        MK.clone_kit(d, rd, 'troll_broken')
        k2 = MK.load_kit(rd)
        k2['roles']['heavy'] = None
        MK.save_kit(rd, k2)
        rep = MK.check_kit(rd)
        self.assertFalse(rep.ok)
        with self.assertRaises(MK.KitError):
            MK.build_t0(rd, os.path.join(rd, 'out'))

    def test_install_copies_into_hd_art(self):
        d, kit = self.kits['be']
        MK.build_t0(d, os.path.join(d, 'out'))
        hd = os.path.join(self.tmp, 'hd')
        names = MK.install_art(os.path.join(d, 'out'), hd)
        self.assertEqual(sorted(names), ['be1.c', 'be2.c', 'collide.hit'])
        self.assertTrue(os.path.isfile(os.path.join(hd, 'art', 'be1.c')))

    def test_cli_clone_check_build(self):
        import contextlib
        import io
        d = os.path.join(self.tmp, 'cli_ratmen')
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(MK.main(['clone', 'ratmen', d]), 0)
            self.assertEqual(MK.main(['check', d]), 0)
            self.assertEqual(MK.main(['build', d, '--tier', 'T0', '--out', os.path.join(d, 'o')]), 0)
            self.assertEqual(MK.main(['clone', 'ratmen', d]), 1)              # exists
            self.assertEqual(MK.main(['clone', 'nosuch', os.path.join(self.tmp, 'x')]), 1)


if __name__ == '__main__':
    unittest.main()
