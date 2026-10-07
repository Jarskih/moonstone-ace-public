"""Host test for tools/gen_moddata.py (ROADMAP 9.4b): tools/mod_schema/*.yaml -> build/gen/mod_defaults.cpp, mod_schema.cpp,
defaults/*.ini and docs/MOD_KEYS.md.

  * the real schema: the generated C++ compiles with clang++ together with the real parser; parsing the generated
    defaults/rules.ini into a zeroed GameData gives kDefaults byte for byte (docs/ARCHITECTURE.md 3.7 level 2); an override
    file changes exactly the bytes of the keys it names; a mutated reference file is detected;
  * docs/MOD_KEYS.md in git is what the schema generates and carries no default values;
  * a synthetic schema with every key type (int, bool, enum, string, list with count cell, aliases) goes through the same
    round trip;
  * schema mistakes are refused with a message naming the key.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_moddata  # noqa: E402

CXX = shutil.which('clang++')
PARSER = ['src/engine/inifile.cpp', 'src/game/data/modparse.cpp', 'src/game/data/modcheck.cpp']

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "HEADER"
#include "game/data/modcheck.hpp"
#include "game/data/modschema.hpp"
using namespace ms;
using namespace ms::game;

static GameData g_data;
static ModRowInfo g_info[64];
static char g_names[64][MOD_NAME_LEN];
static ModTable g_tab[16];

static const char *base(const char *p) {
	const char *b = p;
	for(; *p; ++p) if(*p == '/' || *p == '\\') b = p + 1;
	return b;
}

int main(int argc, char **argv) {
	memset(&g_data, 0, sizeof g_data);
	int bad = 0;
	for(int a = 1; a < argc; ++a) {
		const ModFileDesc *fd = 0;
		for(uint8_t i = 0; i < kModFileCount; ++i) if(!strcmp(kModFiles[i].pName, base(argv[a]))) fd = &kModFiles[i];
		if(!fd) { printf("NOFILE %s\n", argv[a]); return 1; }
		FILE *f = fopen(argv[a], "rb");
		static char buf[65536];
		uint32_t n = (uint32_t)fread(buf, 1, sizeof buf, f);
		fclose(f);
		uint8_t row = 0;
		for(uint8_t i = 0; i < fd->ubCount; ++i) {
			const SectionDesc &s = kModSections[fd->ubFirst + i];
			g_tab[i].pDesc = &s;
			g_tab[i].pRows = (uint8_t *)&g_data + s.uwDataOff;
			g_tab[i].aNames = s.bSingleton ? 0 : &g_names[row];
			g_tab[i].aInfo = &g_info[row];
			g_tab[i].ubBuiltin = s.ubBuiltin;
			g_tab[i].ubMax = s.ubMax;
			g_tab[i].ubUsed = s.ubBuiltin;
			for(uint8_t r = 0; s.ppNames && r < s.ubBuiltin; ++r) strcpy(g_names[row + r], s.ppNames[r]);
			row += s.ubMax;
		}
		ModReport rep;
		modReportBegin(rep, fd->pName);
		modParseText(buf, n, g_tab, fd->ubCount, rep);
		modCheck(g_tab, fd->ubCount, rep, 0, 0);
		for(uint8_t i = 0; i < rep.ubStored; ++i) printf("ERR %s\n", rep.aMsg[i]);
		bad += rep.uwTotal;
	}
	const uint8_t *x = (const uint8_t *)&g_data, *y = (const uint8_t *)&kDefaults;
	int diffs = 0;
	for(size_t i = 0; i < sizeof g_data; ++i)
		if(x[i] != y[i]) { printf("DIFF %u %u %u\n", (unsigned)i, x[i], y[i]); ++diffs; }
	printf("%s errors=%d sizeof=%u\n", diffs ? "DIFFERENT" : "EQUAL", bad, (unsigned)sizeof g_data);
	return 0;
}
'''

SYN_HEADER = '''#pragma once
#include <stdint.h>
namespace ms { namespace game {
struct Syn {
	uint16_t uwA;
	int8_t sbB;
	uint8_t ubMode;
	uint8_t ubOn;
	uint8_t aList[4];
	uint8_t ubListN;
	uint8_t aPal[3];
	char szName[12];
	int32_t slBig;
};
struct GameData { Syn syn; };
extern const GameData kDefaults;
} }
'''

SYN_SCHEMA = '''
file: syn.ini
title: Synthetic
struct: Syn
target: syn
layout: [uwA, sbB, ubMode, ubOn, aList, ubListN, aPal, szName, slBig]
sections:
  one:
    doc: First section.
    singleton: true
    keys:
      a: {member: uwA, type: int, size: 2, min: 5, max: 60000, default: 777, doc: An int., aliases: [old_a, older_a]}
      b: {member: sbB, type: int, size: 1, min: -100, max: 100, default: -42, doc: A signed int.}
      mode: {member: ubMode, type: enum, values: {idle: 0, walk: 1, fly: 7}, default: fly, doc: An enum.}
      enabled: {member: ubOn, type: bool, default: true, doc: A bool.}
  two:
    singleton: true
    keys:
      list: {member: aList, type: list, size: 1, min: 1, max: 8, count_min: 1, count_max: 4, count_member: ubListN, default: [4, 6, 8], doc: A list.}
      pal: {member: aPal, type: list, elem: enum, size: 1, values: {red: 1, green: 2, blue: 3}, count_min: 3, count_max: 3, default: [blue, red, green], doc: Names.}
      name: {member: szName, type: string, size: 12, max: 10, default: "a b # c", doc: A string.}
      big: {member: slBig, type: int, size: 4, min: -5, max: 2000000000, default: 1999999999, doc: Big.}
'''


def compile_driver(tmp, gen_dir, header, extra_inc=()):
    drv = os.path.join(tmp, 'drv.cpp')
    with open(drv, 'w') as f:
        f.write('#include <stddef.h>\n' + DRIVER.replace('HEADER', header))
    exe = os.path.join(tmp, 'drv.exe')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
           '-I', os.path.join(ROOT, 'include')]
    for i in extra_inc:
        cmd += ['-I', i]
    cmd += [drv, os.path.join(gen_dir, 'mod_defaults.cpp'), os.path.join(gen_dir, 'mod_schema.cpp')]
    cmd += [os.path.join(ROOT, p) for p in PARSER] + ['-o', exe]
    subprocess.run(cmd, check=True)
    return exe


def run(exe, *files):
    out = subprocess.run([exe] + list(files), capture_output=True, text=True, check=True).stdout.splitlines()
    return out


class SchemaFiles(unittest.TestCase):
    def test_outputs_exist_and_are_stable(self):
        with tempfile.TemporaryDirectory() as tmp:
            files, outs = gen_moddata.generate(gen_moddata.SCHEMA_DIR, tmp, 'game/api/data.hpp')
            self.assertEqual(sorted(os.path.relpath(p, tmp).replace('\\', '/') for p in outs),
                             sorted(['defaults/%s.ini' % n for n in ('arenas', 'creatures', 'encounters', 'items', 'lairs', 'places', 'rules', 'shops')] +
                                    ['mod_defaults.cpp', 'mod_names.cpp', 'mod_schema.cpp']))
            r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gen_moddata.py'), '--out-dir', tmp], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn('11 output(s) written', r.stdout)
            r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gen_moddata.py'), '--out-dir', tmp], capture_output=True, text=True)
            self.assertIn('0 output(s) written', r.stdout)  # unchanged outputs are left alone (make does not rebuild)

    def test_rules_schema_content(self):
        files = gen_moddata.load_schema(gen_moddata.SCHEMA_DIR)
        keys = {(s['kind'], k['key']): k for fl in files for s in fl['sections'] for k in s['keys']}
        self.assertEqual(keys[('waves', 'scaling')]['values'], {'original': 0, 'none': 1, 'limited': 2})
        self.assertEqual(keys[('waves', 'scaling')]['default'], 0)  # classic play by default
        self.assertEqual([keys[('coop', k)]['default'] for k in ('alive_bonus', 'total_factor', 'writeback_divisor')], [1, 2, 2])
        self.assertEqual((keys[('limits', 'gold_cap')]['default'], keys[('limits', 'dagger_cap')]['default']), (150, 10))

    def test_key_doc_is_current_and_has_no_defaults(self):
        files = gen_moddata.load_schema(gen_moddata.SCHEMA_DIR)
        with open(os.path.join(ROOT, 'docs', 'MOD_KEYS.md'), encoding='utf-8', newline='') as f:
            doc = f.read()
        self.assertEqual(doc, gen_moddata.gen_doc(files), 'docs/MOD_KEYS.md is stale: py tools/gen_moddata.py --doc docs/MOD_KEYS.md')
        self.assertNotIn('\r', doc)
        for fl in files:
            for s in fl['sections']:
                for k in s['keys']:
                    self.assertIn(f'`{k["key"]}`', doc)
        self.assertNotIn('150', doc)  # a default value must not be in the doc

    def test_data_header_matches_schema_layout(self):
        """include/game/api/data.hpp declares RulesDef's members in the schema's layout order (the generated static_asserts compile)."""
        with open(os.path.join(ROOT, 'include', 'game', 'api', 'data.hpp')) as f:
            text = f.read()
        body = text[text.index('struct RulesDef'):text.index('};', text.index('struct RulesDef'))]
        declared = re.findall(r'^\s*u?int\d+_t\s+(\w+);', body, re.M)
        files = gen_moddata.load_schema(gen_moddata.SCHEMA_DIR)
        rules = [b for fl in files for b in fl['blocks'] if b['struct'] == 'RulesDef'][0]
        self.assertEqual(declared, rules['layout'])
        # the item tables: each row struct declares its members in the schema's order, and GameData holds the arrays
        for st in ('WeaponDef', 'ArmourDef', 'ItemDef'):
            blk = [b for fl in files for b in fl['blocks'] if b['struct'] == st][0]
            body = text[text.index('struct ' + st):text.index('};', text.index('struct ' + st))]
            self.assertEqual(re.findall(r'^\s*u?int\d+_t\s+(\w+);', body, re.M), blk['layout'], st)
            self.assertIn(blk['target'] + '[', text)


class SchemaErrors(unittest.TestCase):
    def check(self, text, needle):
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, 'x.yaml'), 'w') as f:
                f.write(text)
            with self.assertRaises(gen_moddata.SchemaError) as cm:
                gen_moddata.load_schema(tmp)
            self.assertIn(needle, str(cm.exception))

    HEAD = 'file: x.ini\nstruct: S\ntarget: s\nlayout: [m]\nsections:\n  sec:\n    singleton: true\n    keys:\n'

    def test_mistakes(self):
        k = '      k: {member: m, type: int, size: 1, min: 0, max: 9, default: 3, doc: d}\n'
        self.check(self.HEAD + '      k: {member: m, type: int, size: 1, min: 0, max: 9, default: 10, doc: d}\n', '[sec] k: default 10 outside 0..9')
        self.check(self.HEAD + '      k: {member: m, type: int, size: 1, min: 0, max: 300, default: 3, doc: d}\n', 'max 300 outside 0..255')
        self.check(self.HEAD + '      k: {member: m, type: int, size: 1, min: 9, max: 0, default: 3, doc: d}\n', 'min > max')
        self.check(self.HEAD + '      k: {member: m, type: int, size: 3, min: 0, max: 9, default: 3, doc: d}\n', 'size must be 1, 2 or 4')
        self.check(self.HEAD + '      k: {member: m, type: int, size: 1, min: 0, max: 9, default: 3}\n', 'missing `doc`')
        self.check(self.HEAD + '      on: {member: m, type: int, size: 1, min: 0, max: 9, default: 3, doc: d}' + chr(10), 'bad key name')
        self.check(self.HEAD + '      k: {member: m, type: real, size: 1, default: 3, doc: d}\n', 'type must be one of')
        self.check(self.HEAD + '      k: {member: m, type: enum, values: {a: 0}, default: b, doc: d}\n', "default 'b' is not one of ['a']")
        self.check(self.HEAD + '      k: {member: m, type: bool, default: 1, doc: d}\n', 'bool default must be true or false')
        self.check(self.HEAD + k + '      j: {member: m, type: int, size: 1, min: 0, max: 9, default: 3, doc: d}\n', 'member m already used by [sec] k')
        self.check(self.HEAD + k + '      K: {member: n, type: int, size: 1, min: 0, max: 9, default: 3, doc: d}\n', 'not unique')
        self.check(self.HEAD + '      base: {member: m, type: int, size: 1, min: 0, max: 9, default: 3, doc: d}\n', 'reserved')
        self.check(self.HEAD + k.replace('doc: d}', 'doc: d, aliases: [k]}'), 'not unique')
        self.check(self.HEAD.replace('layout: [m]', 'layout: [m, x]') + k, '`layout` must list every member exactly once')
        self.check(self.HEAD.replace('singleton: true', 'singleton: false') + k, 'a table section needs `names`')
        tab = 'file: x.ini\nsections:\n  sec:\n    struct: S\n    target: s\n    layout: [m]\n    names: [a, b]\n    max: 3\n    keys:\n'
        self.check(tab + '      k: {member: m, type: int, size: 1, min: 0, max: 9, default: {a: 1}, doc: d}\n', 'must give a value for exactly the rows')
        self.check(tab + '      k: {member: m, type: int, size: 1, min: 0, max: 9, default: {a: 1, b: 10}, doc: d}\n', 'default 10 outside 0..9')
        self.check(tab.replace('max: 3', 'max: 1') + '      k: {member: m, type: int, size: 1, min: 0, max: 9, default: 1, doc: d}\n', 'max 1 outside 2..255')
        self.check(tab.replace('names: [a, b]', 'names: [a, A]') + '      k: {member: m, type: int, size: 1, min: 0, max: 9, default: 1, doc: d}\n', 'unique built-in row names')
        self.check(self.HEAD.replace('file: x.ini\n', '') + k, "missing `file`")
        self.check(self.HEAD + '      k: {member: m, type: string, size: 4, max: 9, default: abc, doc: d}\n', 'max 9 outside 0..3')
        self.check(self.HEAD + '      k: {member: m, type: list, size: 1, min: 0, max: 9, count_max: 20, default: [1], doc: d}\n', 'count_max 20 outside 1..16')
        self.check(self.HEAD + '      k: {member: m, type: list, size: 1, min: 0, max: 9, count_max: 2, default: [1, 2, 3], doc: d}\n', 'default must be a list of 0..2')


@unittest.skipUnless(CXX, 'needs clang++')
class RoundTrip(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.gen = os.path.join(cls.tmp.name, 'gen')
        gen_moddata.generate  # noqa: B018
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gen_moddata.py'), '--out-dir', cls.gen], check=True, capture_output=True)
        cls.exe = compile_driver(cls.tmp.name, cls.gen, 'game/api/data.hpp')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def refs(self):
        d = os.path.join(self.gen, 'defaults')
        return [os.path.join(d, n) for n in sorted(os.listdir(d))]

    def test_default_ini_parses_to_kdefaults_byte_for_byte(self):
        out = run(self.exe, *self.refs())
        self.assertEqual(len(out), 1, out)
        self.assertRegex(out[0], r'^EQUAL errors=0 sizeof=\d+$')

    def test_empty_file_is_not_kdefaults(self):
        """The reference file really sets every byte: an empty file leaves the zeroed struct different from kDefaults."""
        p = os.path.join(self.tmp.name, 'rules.ini')
        open(p, 'w').close()
        out = run(self.exe, p)
        self.assertEqual(out[-1].split()[0], 'DIFFERENT')

    def test_override_changes_only_its_key(self):
        sub = os.path.join(self.tmp.name, 'ov')
        os.makedirs(sub, exist_ok=True)
        p = os.path.join(sub, 'rules.ini')
        with open(p, 'w') as f:
            f.write('[coop]\ntotal_factor = 3   # three\n[waves]\nscaling = NONE\n')
        out = run(self.exe, *self.refs(), p)
        self.assertEqual([l.split()[0] for l in out], ['DIFF', 'DIFF', 'DIFFERENT'], out)
        offs = sorted(int(l.split()[1]) for l in out[:2])
        self.assertEqual(offs[1] - offs[0], 2)  # ubWaveScaling (offset 3 in RulesDef), ubCoopTotalFactor (5)
        self.assertEqual(sorted((int(l.split()[2]), int(l.split()[3])) for l in out[:2]), [(1, 0), (3, 2)])

    def test_override_errors_are_reported_with_the_file_name(self):
        sub = os.path.join(self.tmp.name, 'ov2')
        os.makedirs(sub, exist_ok=True)
        p = os.path.join(sub, 'rules.ini')
        with open(p, 'w') as f:
            f.write('[limits]\ngold_cap = 40000\n[waves]\nscaling = fast\n')
        out = run(self.exe, *self.refs(), p)
        self.assertEqual(out[0], 'ERR mods/rules.ini:2: [limits] gold_cap = 40000: out of range 1..32767 (file ignored)')
        self.assertEqual(out[1], 'ERR mods/rules.ini:4: [waves] scaling = fast: unknown name (expected original, none, limited) (file ignored)')

    def test_mutated_reference_is_detected(self):
        """Deliberate mutation: gold_cap 150 -> 151 in the reference file; the byte-for-byte comparison must fail."""
        with open(os.path.join(self.gen, 'defaults', 'rules.ini')) as f:
            text = f.read()
        self.assertIn('gold_cap = 150', text)
        sub = os.path.join(self.tmp.name, 'mut')
        os.makedirs(sub, exist_ok=True)
        p = os.path.join(sub, 'rules.ini')
        with open(p, 'w') as f:
            f.write(text.replace('gold_cap = 150', 'gold_cap = 151'))
        out = run(self.exe, *[r for r in self.refs() if not r.endswith('rules.ini')], p)
        self.assertEqual(out[-1].split()[0], 'DIFFERENT')
        self.assertEqual(out[0].split()[2:], ['151', '150'])   # the live byte vs the default's, the first byte that differs

    def test_item_table_override_changes_only_its_row(self):
        """ROADMAP 9.5d: `[weapon broad] damage = 3` changes exactly that def's damage byte (the live copy has 3, the default 2)."""
        sub = os.path.join(self.tmp.name, 'items1')
        os.makedirs(sub, exist_ok=True)
        p = os.path.join(sub, 'items.ini')
        with open(p, 'w') as f:
            f.write('[weapon broad]\ndamage = 3\n')
        out = run(self.exe, *self.refs(), p)
        self.assertEqual([l.split()[0] for l in out], ['DIFF', 'DIFFERENT'], out)   # exactly one byte
        self.assertEqual(out[0].split()[2:], ['3', '2'])

    def test_item_table_new_rows_and_errors(self):
        sub = os.path.join(self.tmp.name, 'items2')
        os.makedirs(sub, exist_ok=True)

        def errs(n, text):
            p = os.path.join(sub, '%d' % n)
            os.makedirs(p, exist_ok=True)
            with open(os.path.join(p, 'items.ini'), 'w') as f:
                f.write(text)
            return [l for l in run(self.exe, os.path.join(p, 'items.ini')) if l.startswith('ERR')]
        # a new row copies its base, then overrides; the checker (modload.cpp) knows the codes, the parser does not
        self.assertEqual(errs(0, '[weapon pike]\nbase = claymore\ncode = 40\ndamage = 4\n[item ring2]\nbase = protection_ring\nslot = 10\n'), [])
        self.assertEqual(errs(1, '[weapon pike]\ncode = 40\n'),
                         ["ERR mods/items.ini:1: [weapon pike]: new row needs 'base = <existing row>' as its first key (file ignored)"])
        self.assertEqual(errs(2, '[weapon broad]\ndamage = 300\n'),
                         ['ERR mods/items.ini:2: [weapon broad] damage = 300: out of range 0..255 (file ignored)'])
        self.assertEqual(errs(3, '[weapon broad]\nbase = long\n'),
                         ['ERR mods/items.ini:2: [weapon broad] base = long: base is for new rows only (file ignored)'])
        self.assertEqual(errs(4, '[weapon sling]\nbase = long\nreach = 4\n'),
                         ['ERR mods/items.ini:3: [weapon sling] reach = 4: unknown key (file ignored)'])
        nine = ''.join('[weapon w%d]\nbase = long\ncode = %d\n' % (i, 50 + i) for i in range(5))
        self.assertEqual(errs(5, nine), ['ERR mods/items.ini:13: [weapon w4]: too many rows: the table holds at most 8 (1 refused) (file ignored)'])

    def test_synthetic_schema_every_type_round_trips(self):
        with tempfile.TemporaryDirectory() as tmp:
            sdir, gdir, hdir = (os.path.join(tmp, d) for d in ('schema', 'gen', 'inc'))
            for d in (sdir, hdir):
                os.makedirs(d)
            with open(os.path.join(sdir, 'syn.yaml'), 'w') as f:
                f.write(SYN_SCHEMA)
            with open(os.path.join(hdir, 'syn_data.hpp'), 'w') as f:
                f.write(SYN_HEADER)
            subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gen_moddata.py'), '--out-dir', gdir, '--schema-dir', sdir,
                            '--header', 'syn_data.hpp'], check=True, capture_output=True)
            exe = compile_driver(tmp, gdir, 'syn_data.hpp', [hdir])
            ini = os.path.join(gdir, 'defaults', 'syn.ini')
            with open(ini) as f:
                text = f.read()
            for line in ('a = 777', 'b = -42', 'mode = fly', 'enabled = on', 'list = 4, 6, 8', 'pal = blue, red, green',
                         'name = "a b # c"', 'big = 1999999999'):
                self.assertIn(line, text.splitlines())
            self.assertIn('old names: old_a, older_a', text)
            out = run(exe, ini)
            self.assertRegex(out[-1], r'^EQUAL errors=0')
            # an alias still works
            p = os.path.join(tmp, 'syn.ini')
            with open(p, 'w') as f:
                f.write('[one]\nolder_a = 9\n')
            out = run(exe, ini, p)
            self.assertEqual([l.split() for l in out[:-1]], [['DIFF', '1', '0', '3']])  # 9 vs 777 = 0x309: the high byte differs
            with open(os.path.join(gdir, 'mod_schema.cpp')) as f:
                self.assertIn('kAlias_one_a[] = {"old_a", "older_a", 0}', f.read())


if __name__ == '__main__':
    unittest.main()
