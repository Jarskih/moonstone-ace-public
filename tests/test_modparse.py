"""Host test for the mod data file reader (ROADMAP 9.4a): src/engine/inifile.cpp (tokenizer, value parsers),
src/game/data/modparse.cpp (table-driven applier, error text) and src/game/data/modcheck.cpp (generic and hook checks).

A host driver (clang++, no STL) over a test schema (a two-row `creature` table with every field type, a `waves` singleton)
prints what the code did; this file holds the expected output of every syntax case and every error message of
docs/ARCHITECTURE.md 3.4 (`mods/<file>:<line>: [<kind> <name>] <key> = <value>: <reason> (file ignored)`).  One deliberate
mutation of modparse.cpp (a range comparison) must make the cases fail (test_mutation_is_caught).
"""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')
SRC = ['src/engine/inifile.cpp', 'src/game/data/modparse.cpp', 'src/game/data/modcheck.cpp']

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "engine/inifile.hpp"
#include "game/data/modcheck.hpp"
using namespace ms;
using namespace ms::game;

struct Crit {
	int16_t swHp;
	uint8_t ubReach;
	int8_t sbBias;
	uint8_t ubMode;
	uint8_t ubFlag;
	uint8_t ubDmg[4];
	uint8_t ubDmgN;
	uint8_t ubPal[2];
	char szTag[8];
	int32_t slBig;
};
struct Waves { uint8_t ubScaling; };

static const EnumVal kMode[] = {{"idle", 0}, {"walk", 1}, {"fly", 7}, {0, 0}};
static const EnumVal kPal[] = {{"red", 1}, {"green", 2}, {"blue", 3}, {0, 0}};
static const char *const kRangeAlias[] = {"range", "rch", 0};
static const FieldDesc kCritF[] = {
	{"hp", 0, FT_INT, 2, offsetof(Crit, swHp), 1, 999, 0, 0, 0, MOD_NO_COUNT},
	{"reach", kRangeAlias, FT_INT, 1, offsetof(Crit, ubReach), 0, 255, 0, 0, 0, MOD_NO_COUNT},
	{"bias", 0, FT_INT, 1, offsetof(Crit, sbBias), -128, 127, 0, 0, 0, MOD_NO_COUNT},
	{"mode", 0, FT_ENUM, 1, offsetof(Crit, ubMode), 0, 0, kMode, 0, 0, MOD_NO_COUNT},
	{"flag", 0, FT_BOOL, 1, offsetof(Crit, ubFlag), 0, 1, 0, 0, 0, MOD_NO_COUNT},
	{"damage", 0, FT_LIST, 1, offsetof(Crit, ubDmg), 1, 8, 0, 1, 4, offsetof(Crit, ubDmgN)},
	{"palette", 0, FT_LIST, 1, offsetof(Crit, ubPal), 0, 0, kPal, 2, 2, MOD_NO_COUNT},
	{"tag", 0, FT_STRING, 8, offsetof(Crit, szTag), 0, 7, 0, 0, 0, MOD_NO_COUNT},
	{"big", 0, FT_INT, 4, offsetof(Crit, slBig), 0, 2000000000, 0, 0, 0, MOD_NO_COUNT},
};
static const FieldDesc kWavesF[] = {
	{"scaling", 0, FT_ENUM, 1, offsetof(Waves, ubScaling), 0, 0, kMode, 0, 0, MOD_NO_COUNT},
};
static const SectionDesc kSecs[] = {
	{"creature", kCritF, 9, false, sizeof(Crit), 0, 2, 4},
	{"waves", kWavesF, 1, true, sizeof(Waves), 0, 1, 1},
	{"fixed", kWavesF, 1, false, sizeof(Waves), 0, 2, 2},    // a table whose rows are fixed (built-in = pool)
};

static Crit g_crit[4];
static char g_names[4][MOD_NAME_LEN];
static ModRowInfo g_cinfo[4];
static Waves g_waves;
static ModRowInfo g_winfo[1];
static Waves g_fixed[2];
static char g_fnames[2][MOD_NAME_LEN];
static ModRowInfo g_finfo[2];
static ModTable g_tab[3];

static void setup() {
	memset(g_crit, 0, sizeof g_crit);
	memset(g_names, 0, sizeof g_names);
	Crit &t = g_crit[0];
	t.swHp = 60; t.ubReach = 90; t.sbBias = -3; t.ubMode = 1; t.ubFlag = 0;
	t.ubDmg[0] = 4; t.ubDmg[1] = 6; t.ubDmg[2] = 8; t.ubDmgN = 3; strcpy(t.szTag, "tr");
	Crit &o = g_crit[1];
	o.swHp = 30; o.ubReach = 40; o.ubFlag = 1; o.ubDmg[0] = 2; o.ubDmgN = 1;
	strcpy(g_names[0], "troll");
	strcpy(g_names[1], "orc");
	g_waves.ubScaling = 0;
	memset(g_tab, 0, sizeof g_tab);
	g_tab[0].pDesc = &kSecs[0]; g_tab[0].pRows = (uint8_t *)g_crit; g_tab[0].aNames = g_names; g_tab[0].aInfo = g_cinfo;
	g_tab[0].ubBuiltin = 2; g_tab[0].ubMax = 4; g_tab[0].ubUsed = 2;
	g_tab[1].pDesc = &kSecs[1]; g_tab[1].pRows = (uint8_t *)&g_waves; g_tab[1].aNames = 0; g_tab[1].aInfo = g_winfo;
	g_tab[1].ubBuiltin = 1; g_tab[1].ubMax = 1; g_tab[1].ubUsed = 1;
	memset(g_fixed, 0, sizeof g_fixed);
	strcpy(g_fnames[0], "p"); strcpy(g_fnames[1], "q");
	g_tab[2].pDesc = &kSecs[2]; g_tab[2].pRows = (uint8_t *)g_fixed; g_tab[2].aNames = g_fnames; g_tab[2].aInfo = g_finfo;
	g_tab[2].ubBuiltin = 2; g_tab[2].ubMax = 2; g_tab[2].ubUsed = 2;
}

// A cross-field hook: reach above 4 times hp.
static void hookReach(const ModTable *aT, uint8_t, ModReport &rep, void *) {
	const ModTable &t = aT[0];
	for(uint8_t i = 0; i < t.ubUsed; ++i) {
		const Crit &c = ((const Crit *)t.pRows)[i];
		if(c.ubReach > 4 * c.swHp)
			modReportAdd(rep, t.aInfo[i].uwLine ? t.aInfo[i].uwLine : 1, modLit("creature"), modLit(t.aNames[i]), modLit("reach"),
			             IniSlice{0, 0}, "reach is above 4 times hp");
	}
}

static void show(IniSlice s) { printf("[%.*s]", (int)s.n, s.p); }

static char *readAll(uint32_t &n) {
	size_t cap = 1 << 16, len = 0;
	char *b = (char *)malloc(cap);
	size_t r;
	while((r = fread(b + len, 1, cap - len, stdin)) > 0) {
		len += r;
		if(len == cap) b = (char *)realloc(b, cap *= 2);
	}
	n = (uint32_t)len;
	return b;
}

int main(int argc, char **argv) {
	const char *mode = argv[1];
	uint32_t n;
	char *text = readAll(n);
	if(!strcmp(mode, "tok")) {
		IniReader r;
		IniLine l;
		iniOpen(r, text, n);
		for(;;) {
			uint8_t t = iniNext(r, l);
			if(t == INI_END) break;
			if(t == INI_SECTION) { printf("S %u ", (unsigned)l.ulLine); show(l.kind); show(l.name); }
			else if(t == INI_PAIR) { printf("P %u ", (unsigned)l.ulLine); show(l.key); show(l.value); }
			else printf("E %u %s%s", (unsigned)l.ulLine, l.pError, l.ubHeader ? " (header)" : "");
			printf("\n");
		}
		return 0;
	}
	// the other test modes take one case per line
	if(!strcmp(mode, "int") || !strcmp(mode, "str") || !strcmp(mode, "list")) {
		char *p = text, *e = text + n;
		while(p < e) {
			char *q = p;
			while(q < e && *q != '\n') ++q;
			IniSlice s = {p, (uint16_t)(q - p)};
			if(!strcmp(mode, "int")) {
				int32_t v = 0;
				uint8_t r = iniParseInt(s, v);
				if(r == INI_NUM_OK) printf("OK %d\n", (int)v);
				else printf("%s\n", r == INI_NUM_BAD ? "BAD" : "RANGE");
			} else if(!strcmp(mode, "str")) {
				IniSlice in;
				if(iniParseString(s, in)) { printf("OK "); show(in); printf("\n"); }
				else printf("NO\n");
			} else {
				IniList l;
				IniSlice it;
				iniListOpen(l, s);
				while(iniListNext(l, it)) show(it);
				printf("\n");
			}
			p = q + 1;
		}
		return 0;
	}
	// parse / check / checkhook
	setup();
	ModReport rep;
	modReportBegin(rep, "creatures.ini");
	modParseText(text, n, g_tab, 3, rep);
	if(!strcmp(mode, "check") || !strcmp(mode, "checkhook")) {
		ModCheck hook = {hookReach, 0};
		modCheck(g_tab, 3, rep, &hook, !strcmp(mode, "checkhook") ? 1 : 0);
	}
	for(uint8_t i = 0; i < rep.ubStored; ++i) printf("ERR %s\n", rep.aMsg[i]);
	printf("TOTAL %u\n", (unsigned)rep.uwTotal);
	for(uint8_t i = 0; i < g_tab[0].ubUsed; ++i) {
		const Crit &c = g_crit[i];
		printf("ROW %s hp=%d reach=%u bias=%d mode=%u flag=%u dmg=%u,%u,%u,%u/%u pal=%u,%u tag=%s big=%d\n", g_names[i], c.swHp,
		       c.ubReach, c.sbBias, c.ubMode, c.ubFlag, c.ubDmg[0], c.ubDmg[1], c.ubDmg[2], c.ubDmg[3], c.ubDmgN, c.ubPal[0],
		       c.ubPal[1], c.szTag, (int)c.slBig);
	}
	printf("WAVES %u\n", (unsigned)g_waves.ubScaling);
	return 0;
}
'''

TROLL = 'ROW troll hp=60 reach=90 bias=-3 mode=1 flag=0 dmg=4,6,8,0/3 pal=0,0 tag=tr big=0'
ORC = 'ROW orc hp=30 reach=40 bias=0 mode=0 flag=1 dmg=2,0,0,0/1 pal=0,0 tag= big=0'
WAVES0 = 'WAVES 0'
ZERO = 'ROW %s hp=0 reach=0 bias=0 mode=0 flag=0 dmg=0,0,0,0/0 pal=0,0 tag= big=0'


def troll(**kw):
    """The troll row with fields replaced (kw: hp reach bias mode flag dmg pal tag big)."""
    f = dict(name='troll', hp=60, reach=90, bias=-3, mode=1, flag=0, dmg='4,6,8,0/3', pal='0,0', tag='tr', big=0)
    f.update(kw)
    return 'ROW %(name)s hp=%(hp)s reach=%(reach)s bias=%(bias)s mode=%(mode)s flag=%(flag)s dmg=%(dmg)s pal=%(pal)s ' \
           'tag=%(tag)s big=%(big)s' % f


def err(line, ctx, reason):
    return 'ERR mods/creatures.ini:%d: %s%s (file ignored)' % (line, ctx + ': ' if ctx else '', reason)


NUM = 'not a number (decimal, $hex or -number)'
BOOL = 'expected on or off (also yes/no, true/false, 1/0)'
CTX = '[creature troll] '

# (mode, input text, expected stdout lines)
TOKEN_CASES = [
    ('section, pair, trailing # comment', '[creature troll]\nhp = 60  # decimal\n', ['S 1 [creature][troll]', 'P 2 [hp][60]']),
    ('no name', '[waves]\nscaling=none\n', ['S 1 [waves][]', 'P 2 [scaling][none]']),
    ('CRLF', '[waves]\r\nscaling = none\r\n', ['S 1 [waves][]', 'P 2 [scaling][none]']),
    ('no final newline', 'a = 1', ['P 1 [a][1]']),
    ('comments and blank lines count', '# c\n; c\n\n   # indented\nhp = 5\n', ['P 5 [hp][5]']),
    ('; trailing comment', 'hp = 5 ; five\n', ['P 1 [hp][5]']),
    ('tabs', 'hp\t=\t5\t\n', ['P 1 [hp][5]']),
    ('quoted string keeps # ; and quotes', 'tag = "a # b; c" # t\n', ['P 1 [tag]["a # b; c"]']),
    ('list', 'damage = 4, 6,8\n', ['P 1 [damage][4, 6,8]']),
    ('hex and negative', 'a = $1E\nb = -12\n', ['P 1 [a][$1E]', 'P 2 [b][-12]']),
    ('UTF-8 BOM', '\xef\xbb\xbf[waves]\n', ['S 1 [waves][]']),
    ('section with comment, spaces', '  [ creature   troll ] # x\n', ['S 1 [creature][troll]']),
    ('exactly 120 bytes', 'k = ' + 'x' * 116 + '\n', ['P 1 [k][' + 'x' * 116 + ']']),
    ('121 bytes', 'k = ' + 'x' * 117 + '\nhp = 1\n', ['E 1 line too long (max 120 bytes)', 'P 2 [hp][1]']),
    ('121 byte comment is also an error', '#' * 121 + '\n', ['E 1 line too long (max 120 bytes)']),
    ('control character', 'hp = 5\x01\n', ['E 1 bad character (control code)']),
    ('lone CR', 'hp = 5\rfoo = 6\n', ['E 1 bad character (control code)']),
    ('missing ]', '[creature troll\nhp = 1\n', ['E 1 section header missing \']\' (header)', 'P 2 [hp][1]']),
    ('empty header', '[]\n', ['E 1 bad section header (expected [kind name]) (header)']),
    ('three words', '[a b c]\n', ['E 1 bad section header (expected [kind name]) (header)']),
    ('bad header chars', '[a-b]\n', ['E 1 bad section header (letters, digits and _ only) (header)']),
    ('text after header', '[a b] x\n', ['E 1 text after section header (header)']),
    ('no =', 'hp 5\n', ['E 1 expected \'key = value\'']),
    ('no key', '= 5\n', ['E 1 bad key name']),
    ('key with a space', 'h p = 1\n', ['E 1 expected \'key = value\'']),
    ('no value', 'hp =\n', ['E 1 missing value']),
    ('comment only value', 'hp = # x\n', ['E 1 missing value']),
    ('unterminated string', 'tag = "abc\n', ['E 1 unterminated string']),
    ('errors do not stop the reader', 'hp 5\n[w]\nhp = 1\n', ['E 1 expected \'key = value\'', 'S 2 [w][]', 'P 3 [hp][1]']),
]

INT_CASES = [
    ('12', 'OK 12'), ('-12', 'OK -12'), ('+7', 'OK 7'), ('$1E', 'OK 30'), ('$1e', 'OK 30'), ('-$1E', 'OK -30'), ('$7FFFFFFF', 'OK 2147483647'),
    ('2147483647', 'OK 2147483647'), ('-2147483648', 'OK -2147483648'), ('0', 'OK 0'), ('007', 'OK 7'),
    ('2147483648', 'RANGE'), ('-2147483649', 'RANGE'), ('$80000000', 'RANGE'), ('99999999999', 'RANGE'), ('$FFFFFFFFF', 'RANGE'),
    ('', 'BAD'), ('abc', 'BAD'), ('1.5', 'BAD'), ('$', 'BAD'), ('-', 'BAD'), ('0x10', 'BAD'), ('12a', 'BAD'), ('$G', 'BAD'), ('1 2', 'BAD'),
    ('--1', 'BAD'), ('$-1', 'BAD'),
]
STR_CASES = [('"abc"', 'OK [abc]'), ('""', 'OK []'), ('abc', 'NO'), ('"a"b"', 'NO'), ('"', 'NO'), ('"abc', 'NO'), ('abc"', 'NO'),
             ('"a b, c"', 'OK [a b, c]')]
LIST_CASES = [('a, b, c', '[a][b][c]'), ('a,b', '[a][b]'), ('a', '[a]'), ('"x, y", z', '["x, y"][z]'), ('a,', '[a][]'),
              ('a,,b', '[a][][b]'), ('  a ,\tb  ', '[a][b]'), (',', '[][]')]

LONG = '"' + 'a' * 40 + '"'
MANY = ''.join('hp = 0\n' for _ in range(20))

# (mode, name, input text, expected stdout lines)
PARSE_CASES = [
    ('parse', 'partial override, boundary values', '[creature troll]\nhp = 999\nreach = $5A  # same\n[creature orc]\nhp = 1\n[waves]\nscaling = fly\n',
     ['TOTAL 0', troll(hp=999), ORC.replace('hp=30', 'hp=1'), 'WAVES 7']),
    ('parse', 'empty file is a no-op', '', ['TOTAL 0', TROLL, ORC, WAVES0]),
    ('parse', 'new row from a prototype', '[creature cave_troll]\nbase = troll\nhp = 80\n',
     ['TOTAL 0', TROLL, ORC, troll(name='cave_troll', hp=80), WAVES0]),
    ('parse', 'prototype is an earlier new row', '[creature a]\nbase = orc\nbias = 5\n[creature b]\nbase = a\nhp = 7\n',
     ['TOTAL 0', TROLL, ORC, ORC.replace('orc', 'a').replace('bias=0', 'bias=5'), ORC.replace('orc', 'b').replace('bias=0', 'bias=5').replace('hp=30', 'hp=7'), WAVES0]),
    ('parse', 'names and keys are case-insensitive', '[Creature TROLL]\nHP = 5\n', ['TOTAL 0', troll(hp=5), ORC, WAVES0]),
    ('parse', 'key alias', '[creature troll]\nrange = 11\n', ['TOTAL 0', troll(reach=11), ORC, WAVES0]),
    ('parse', 'second alias', '[creature troll]\nrch = 12\n', ['TOTAL 0', troll(reach=12), ORC, WAVES0]),
    ('parse', 'enum, bool, list, string, negative, big',
     '[creature troll]\nmode = FLY\nflag = yes\ndamage = 1, 2\npalette = red, blue\ntag = "x y"\nbias = -128\nbig = 2000000000\n',
     ['TOTAL 0', troll(mode=7, flag=1, dmg='1,2,0,0/2', pal='1,3', tag='x y', bias=-128, big=2000000000), ORC, WAVES0]),
    ('parse', 'bool spellings', '[creature troll]\nflag = ON\n[creature orc]\nflag = 0\n', ['TOTAL 0', troll(flag=1), ORC.replace('flag=1', 'flag=0'), WAVES0]),
    ('parse', 'list longer than before is zero-filled', '[creature troll]\ndamage = 5\n', ['TOTAL 0', troll(dmg='5,0,0,0/1'), ORC, WAVES0]),
    ('parse', 'CRLF file', '[creature troll]\r\nhp = 7\r\n', ['TOTAL 0', troll(hp=7), ORC, WAVES0]),
    # --- the line-level messages
    ('parse', 'range (ARCHITECTURE 3.4 example)', '[creature troll]\nhp = 2000\n',
     [err(2, CTX + 'hp = 2000', 'out of range 1..999'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'range low', '[creature troll]\nhp = 0\n', [err(2, CTX + 'hp = 0', 'out of range 1..999'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'range signed', '[creature troll]\nbias = 200\nbias = -129\n',
     [err(2, CTX + 'bias = 200', 'out of range -128..127'), err(3, CTX + 'bias = -129', 'out of range -128..127'), 'TOTAL 2', TROLL, ORC, WAVES0]),
    ('parse', 'number too large for 32 bits', '[creature troll]\nhp = 99999999999\n',
     [err(2, CTX + 'hp = 99999999999', 'out of range 1..999'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'not a number', '[creature troll]\nhp = abc\n', [err(2, CTX + 'hp = abc', NUM), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'unknown enum name', '[creature troll]\nmode = run\n',
     [err(2, CTX + 'mode = run', 'unknown name (expected idle, walk, fly)'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'bad bool', '[creature troll]\nflag = maybe\n', [err(2, CTX + 'flag = maybe', BOOL), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'list too long', '[creature troll]\ndamage = 1, 2, 3, 4, 5\n',
     [err(2, CTX + 'damage = 1, 2, 3, 4, 5', 'too many entries (max 4)'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'list too short', '[creature troll]\npalette = red\n',
     [err(2, CTX + 'palette = red', 'too few entries (min 2)'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'list empty entry', '[creature troll]\ndamage = 1, , 3\n',
     [err(2, CTX + 'damage = 1, , 3', 'empty list entry'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'list entry range (row untouched)', '[creature troll]\ndamage = 1, 9\n',
     [err(2, CTX + 'damage = 1, 9', 'entry 2: out of range 1..8'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'list entry not a number', '[creature troll]\ndamage = x\n',
     [err(2, CTX + 'damage = x', 'entry 1: ' + NUM), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'list enum entry', '[creature troll]\npalette = red, pink\n',
     [err(2, CTX + 'palette = red, pink', 'entry 2: unknown name (expected red, green, blue)'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'string not quoted', '[creature troll]\ntag = abc\n', [err(2, CTX + 'tag = abc', 'expected a quoted string'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'string too long', '[creature troll]\ntag = "12345678"\n',
     [err(2, CTX + 'tag = "12345678"', 'too long (max 7 characters)'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'long value is shortened in the message', '[creature troll]\ntag = ' + LONG + '\n',
     [err(2, CTX + 'tag = ' + LONG[:32] + '...', 'too long (max 7 characters)'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'unknown key', '[creature troll]\ncolour = 3\n', [err(2, CTX + 'colour = 3', 'unknown key'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'unknown key in a singleton (no name in the message)', '[waves]\nbase = x\n',
     [err(2, '[waves] base = x', 'unknown key'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'unknown section kind skips its keys', '[monster x]\nhp = 5\nreach = 1\n[creature orc]\nhp = 9\n',
     [err(1, '[monster x]', 'unknown section kind'), 'TOTAL 1', TROLL, ORC.replace('hp=30', 'hp=9'), WAVES0]),
    ('parse', 'singleton with a name', '[waves x]\nscaling = fly\n',
     [err(1, '[waves x]', 'this section takes no name'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'table without a name', '[creature]\nhp = 5\n',
     [err(1, '[creature]', 'this section needs a name'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'name too long', '[creature abcdefghijklmnopqrstuvwx]\n',
     [err(1, '[creature abcdefghijklmnopqrstuvwx]', 'name too long (max 23 characters)'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'key outside a section (one message per block)', 'hp = 5\nreach = 6\n[creature orc]\nhp = 9\n',
     [err(1, 'hp = 5', 'key outside a section'), 'TOTAL 1', TROLL, ORC.replace('hp=30', 'hp=9'), WAVES0]),
    ('parse', 'base on an existing row', '[creature troll]\nbase = orc\n',
     [err(2, CTX + 'base = orc', 'base is for new rows only'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'base not first', '[creature x]\nhp = 5\nbase = troll\n',
     [err(3, '[creature x] base = troll', 'base must be the first key of the section'), 'TOTAL 1', TROLL, ORC,
      (ZERO % 'x').replace('hp=0', 'hp=5'), WAVES0]),
    ('parse', 'base twice', '[creature x]\nbase = troll\nbase = orc\n',
     [err(3, '[creature x] base = orc', 'base must be the first key of the section'), 'TOTAL 1', TROLL, ORC, troll(name='x'), WAVES0]),
    ('parse', 'unknown base', '[creature x]\nbase = ghost\n', [err(2, '[creature x] base = ghost', 'unknown base row'), 'TOTAL 1', TROLL, ORC, ZERO % 'x', WAVES0]),
    ('parse', 'base is the row itself', '[creature x]\nbase = x\n', [err(2, '[creature x] base = x', 'unknown base row'), 'TOTAL 1', TROLL, ORC, ZERO % 'x', WAVES0]),
    ('parse', 'tokenizer error has no context', 'hp 5\n', [err(1, '', "expected 'key = value'"), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('parse', 'broken header: its keys are skipped, not applied to the section before',
     '[creature orc]\nhp = 9\n[creature troll\nhp = 1\n[waves]\nscaling = fly\n',
     [err(3, '', "section header missing ']'"), 'TOTAL 1', TROLL, ORC.replace('hp=30', 'hp=9'), 'WAVES 7']),
    ('parse', 'every error is reported, in file order', '[creature troll]\nhp = 0\nflag = x\n[waves]\nscaling = no\n',
     [err(2, CTX + 'hp = 0', 'out of range 1..999'), err(3, CTX + 'flag = x', BOOL),
      err(5, '[waves] scaling = no', 'unknown name (expected idle, walk, fly)'), 'TOTAL 3', TROLL, ORC, WAVES0]),
    ('parse', 'only the first 16 messages are kept, all are counted', '[creature troll]\n' + MANY,
     [err(i + 2, CTX + 'hp = 0', 'out of range 1..999') for i in range(16)] + ['TOTAL 20', TROLL, ORC, WAVES0]),
    # --- modcheck (generic)
    ('check', 'clean file passes', '[creature troll]\nhp = 70\n', ['TOTAL 0', troll(hp=70), ORC, WAVES0]),
    ('check', 'new row without base', '[creature x]\nhp = 5\n',
     [err(1, '[creature x]', "new row needs 'base = <existing row>' as its first key"), 'TOTAL 1', TROLL, ORC,
      (ZERO % 'x').replace('hp=0', 'hp=5'), WAVES0]),
    ('check', 'new row with only a header and no base', '[creature y]\n',
     [err(1, '[creature y]', "new row needs 'base = <existing row>' as its first key"), 'TOTAL 1', TROLL, ORC,
      ZERO % 'y', WAVES0]),
    ('check', 'duplicate row', '[creature troll]\nhp = 1\n[creature orc]\n[creature troll]\nhp = 2\n',
     [err(4, '[creature troll]', 'section appears twice (first at line 1)'), 'TOTAL 1', troll(hp=2), ORC, WAVES0]),
    ('check', 'duplicate of a case-different name', '[creature troll]\n[creature TROLL]\n',
     [err(2, '[creature troll]', 'section appears twice (first at line 1)'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('check', 'duplicate singleton', '[waves]\nscaling = idle\n\n[waves]\nscaling = fly\n',
     [err(4, '[waves]', 'section appears twice (first at line 1)'), 'TOTAL 1', TROLL, ORC, 'WAVES 7']),
    ('check', 'pool limit (2 built-in + 3 new, 4 fit)', '[creature a]\nbase = orc\n[creature b]\nbase = orc\n[creature c]\nbase = orc\n',
     [err(5, '[creature c]', 'too many rows: the table holds at most 4 (1 refused)'), 'TOTAL 1', TROLL, ORC,
      ORC.replace('orc', 'a'), ORC.replace('orc', 'b'), WAVES0]),
    ('check', 'pool limit counts every refused row', '[creature a]\nbase = orc\n[creature b]\nbase = orc\n[creature c]\nbase = orc\n[creature d]\nbase = orc\n',
     [err(5, '[creature c]', 'too many rows: the table holds at most 4 (2 refused)'), 'TOTAL 1', TROLL, ORC,
      ORC.replace('orc', 'a'), ORC.replace('orc', 'b'), WAVES0]),
    ('check', 'a fixed table has no new rows', '[fixed r]\nscaling = idle\n',
     [err(1, '[fixed r]', 'no such row (this table has fixed rows, no new ones)'), 'TOTAL 1', TROLL, ORC, WAVES0]),
    ('check', 'a fixed table row is addressed by its name', '[fixed q]\nscaling = fly\n', ['TOTAL 0', TROLL, ORC, WAVES0]),
    ('check', 'parse and check messages are both reported', '[creature troll]\nhp = 0\n[creature x]\n',
     [err(2, CTX + 'hp = 0', 'out of range 1..999'), err(3, '[creature x]', "new row needs 'base = <existing row>' as its first key"),
      'TOTAL 2', TROLL, ORC, ZERO % 'x', WAVES0]),
    # --- topic hook
    ('checkhook', 'hook error (key without value)', '[creature troll]\nhp = 10\n',
     [err(1, CTX + 'reach', 'reach is above 4 times hp').replace('[creature troll] reach', '[creature troll] reach'), 'TOTAL 1',
      troll(hp=10), ORC, WAVES0]),
    ('checkhook', 'hook passes', '[creature troll]\nhp = 30\n', ['TOTAL 0', troll(hp=30), ORC, WAVES0]),
]


def build(tmp, patch=None):
    """Compile the driver (+ sources; patch = (file, old, new) applied to a copy)."""
    drv = os.path.join(tmp, 'drv.cpp')
    with open(drv, 'w') as f:
        f.write('#include <stddef.h>\n' + DRIVER)
    srcs = []
    for s in SRC:
        path = os.path.join(ROOT, s)
        if patch and patch[0] == s:
            with open(path, newline='') as f:
                text = f.read()
            assert patch[1] in text, 'mutation target not found'
            path = os.path.join(tmp, os.path.basename(s))
            with open(path, 'w', newline='') as f:
                f.write(text.replace(patch[1], patch[2], 1))
        srcs.append(path)
    exe = os.path.join(tmp, 'drv.exe')
    subprocess.run([CXX, '-std=c++17', '-O1', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), drv]
                   + srcs + ['-o', exe], check=True)
    return exe


def run(exe, mode, text):
    data = text.encode('latin-1')
    out = subprocess.run([exe, mode], input=data, capture_output=True, check=True).stdout
    return out.decode('latin-1').splitlines()


def failures(exe):
    """Every case of the tables against exe; returns the list of (case, got, want) that differ."""
    bad = []

    def chk(name, got, want):
        if got != want:
            bad.append((name, got, want))

    for name, text, want in TOKEN_CASES:
        chk('tok: ' + name, run(exe, 'tok', text), want)
    for mode, cases in (('int', INT_CASES), ('str', STR_CASES), ('list', LIST_CASES)):
        got = run(exe, mode, ''.join(c[0] + '\n' for c in cases))
        for (inp, want), g in zip(cases, got):
            chk(f'{mode}: {inp!r}', g, want)
        chk(mode + ' count', len(got), len(cases))
    for mode, name, text, want in PARSE_CASES:
        chk(f'{mode}: {name}', run(exe, mode, text), want)
    return bad


@unittest.skipUnless(CXX, 'needs clang++')
class ModParse(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = build(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_all_cases(self):
        bad = failures(self.exe)
        self.assertEqual(bad, [], '\n'.join(f'{n}\n  got  {g}\n  want {w}' for n, g, w in bad))

    def test_each_error_reason_of_the_spec_is_covered(self):
        """Every reason string in the sources appears in an expected message of the table above."""
        import re
        expected = '\n'.join(' '.join(map(str, c)) for c in TOKEN_CASES + PARSE_CASES)
        for s in SRC:
            with open(os.path.join(ROOT, s)) as f:
                text = f.read()
            for m in re.finditer(r'(?:modReportAdd\([^;]*?|fail\(o, |puts_\(why, |pWhy = )"([^"]+)"', text):
                reason = m.group(1)
                if reason in ('internal: bad field type', 'bad base'):
                    continue
                key = reason.rstrip(' (')
                self.assertIn(key, expected, f'{s}: reason {reason!r} has no test case')

    def test_mutation_is_caught(self):
        """One deliberate mutation (range check `>` -> `>=` at the upper bound) must make cases fail."""
        with tempfile.TemporaryDirectory() as tmp:
            exe = build(tmp, ('src/game/data/modparse.cpp', 'out < f.slMin || out > f.slMax', 'out < f.slMin || out >= f.slMax'))
            bad = failures(exe)
            self.assertTrue(bad, 'the mutated parser passed every case: the tests are not sensitive')
            names = [b[0] for b in bad]
            self.assertTrue(any('boundary' in n for n in names), names)


if __name__ == '__main__':
    unittest.main()
