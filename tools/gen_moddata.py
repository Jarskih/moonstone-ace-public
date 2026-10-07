#!/usr/bin/env python3
"""gen_moddata.py -- the mod data schema -> compiled defaults, parser tables, reference .ini files, key docs (ROADMAP 9.4b).

Input : tools/mod_schema/*.yaml        one file per data file (docs/ARCHITECTURE.md 3.2; format at the top of rules.yaml)
Output: <out>/mod_defaults.cpp         `const GameData kDefaults = {...}` (positional initialiser in the schema's `layout:` order,
                                       with static_asserts that include/game/api/data.hpp declares the members in that order)
        <out>/mod_schema.cpp           the SectionDesc / FieldDesc tables of the generic parser (src/game/data/modparse.cpp)
        <out>/defaults/<file>.ini      complete commented reference of every key at its default; parsing it with the real
                                       parser gives kDefaults byte for byte (tests/test_gen_moddata.py)
        --doc docs/MOD_KEYS.md         keys, types, ranges, docs (no default values: the defaults of original-derived keys stay
                                       in build/, ROADMAP 9.1)
The default <out> is build/gen (git-ignored); CMake runs this at build time (same pattern as gen_data.py).  An output whose
content did not change is not rewritten.

    py tools/gen_moddata.py [--out-dir build/gen] [--schema-dir tools/mod_schema] [--header game/api/data.hpp] [--doc FILE | --check-doc FILE]
"""
import argparse
import glob
import os
import re
import sys
import textwrap

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, 'build', 'gen')
SCHEMA_DIR = os.path.join(ROOT, 'tools', 'mod_schema')
IDENT = re.compile(r'^[A-Za-z0-9_]+$')
MAX_LIST = 16
TYPES = ('int', 'bool', 'enum', 'string', 'list')


class SchemaError(Exception):
    pass


def die(where, msg):
    raise SchemaError(f'{where}: {msg}')


def int_range(size, signed):
    bits = 8 * size
    return (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)


# ---- loading and validation ----------------------------------------------------------------------------------------------
def check_int(where, v, lo, hi, what):
    if isinstance(v, bool) or not isinstance(v, int):
        die(where, f'{what} must be an integer, got {v!r}')
    if not lo <= v <= hi:
        die(where, f'{what} {v} outside {lo}..{hi}')


def load_key(fname, sec, key, k, members, enums=None):
    where = f'{fname}: [{sec}] {key}'
    if not isinstance(key, str) or not IDENT.match(key):  # YAML 1.1 turns bare on/off/yes/no into booleans: quote them
        die(where, 'bad key name')
    for need in ('member', 'type', 'default', 'doc'):
        if need not in k:
            die(where, f'missing `{need}`')
    t = k['type']
    if t not in TYPES:
        die(where, f'type must be one of {TYPES}')
    if k['member'] in members:
        die(where, f'member {k["member"]} already used by {members[k["member"]]}')
    members[k['member']] = f'[{sec}] {key}'
    size = k.get('size', 1 if t in ('bool', 'enum') else None)
    if size is None:
        die(where, 'missing `size`')
    d = {'key': key, 'member': k['member'], 'type': t, 'size': size, 'doc': ' '.join(str(k['doc']).split()),
         'aliases': list(k.get('aliases') or []), 'source': k.get('source', ''), 'where': where}
    for a in d['aliases']:
        if not IDENT.match(a):
            die(where, f'bad alias {a!r}')
    if t == 'string':
        if not 2 <= size <= 255:
            die(where, 'string size (buffer bytes) must be 2..255')
        mx = k.get('max', size - 1)
        check_int(where, mx, 0, size - 1, 'max')
        v = k['default']
        if not isinstance(v, str) or len(v) > mx or '"' in v or any(ord(c) < 32 or ord(c) > 126 for c in v):
            die(where, f'default {v!r} must be printable ASCII without quotes, at most {mx} characters')
        d.update(min=0, max=mx, default=v)
        return d
    if size not in (1, 2, 4):
        die(where, 'size must be 1, 2 or 4')
    if t == 'bool':
        if not isinstance(k['default'], bool):
            die(where, 'bool default must be true or false')
        d.update(min=0, max=1, default=1 if k['default'] else 0)
        return d
    if t == 'enum' or (t == 'list' and k.get('elem') == 'enum'):
        vals = k.get('values')
        if isinstance(vals, str):                      # a shared enum of the file's `enums:` (many names, one table)
            if not enums or vals not in enums:
                die(where, f'`values: {vals}` names no entry of `enums:`')
            d['enum_ref'] = vals
            vals = enums[vals]['values']
        if not isinstance(vals, dict) or not vals:
            die(where, '`values` must be a mapping name -> number')
        lo, hi = int_range(size, any(v < 0 for v in vals.values() if isinstance(v, int)))
        for n, v in vals.items():
            if not IDENT.match(str(n)):
                die(where, f'bad value name {n!r}')
            check_int(where, v, lo, hi, f'value of {n}')
        d['values'] = {str(n): v for n, v in vals.items()}
        d.update(min=0, max=0)
    else:
        for need in ('min', 'max'):
            if need not in k:
                die(where, f'missing `{need}`')
        lo, hi = int_range(size, k['min'] < 0)
        check_int(where, k['min'], lo, hi, 'min')
        check_int(where, k['max'], lo, hi, 'max')
        if k['min'] > k['max']:
            die(where, 'min > max')
        d.update(min=k['min'], max=k['max'])
    if t == 'int':
        check_int(where, k['default'], d['min'], d['max'], 'default')
        d['default'] = k['default']
    elif t == 'enum':
        if k['default'] not in d['values']:
            die(where, f'default {k["default"]!r} is not one of {list(d["values"])}')
        d['default'] = d['values'][k['default']]
        d['default_name'] = k['default']
    else:  # list
        d['elem'] = k.get('elem', 'int')
        if d['elem'] not in ('int', 'enum'):
            die(where, 'elem must be int or enum')
        cmin, cmax = k.get('count_min', 0), k.get('count_max')
        if cmax is None:
            die(where, 'missing `count_max`')
        check_int(where, cmax, 1, MAX_LIST, 'count_max')
        check_int(where, cmin, 0, cmax, 'count_min')
        d['count_min'], d['count_max'] = cmin, cmax
        d['count_member'] = k.get('count_member')
        if d['count_member'] is not None and d['count_member'] in members:
            die(where, f'count_member {d["count_member"]} already used')
        if d['count_member'] is not None:
            members[d['count_member']] = where + ' (count)'
        dv = k['default']
        if not isinstance(dv, list) or not cmin <= len(dv) <= cmax:
            die(where, f'default must be a list of {cmin}..{cmax} entries')
        out = []
        for e in dv:
            if d['elem'] == 'enum':
                if e not in d['values']:
                    die(where, f'default entry {e!r} is not one of {list(d["values"])}')
                out.append(d['values'][e])
            else:
                check_int(where, e, d['min'], d['max'], 'default entry')
                out.append(e)
        d['default'] = out
        d['default_names'] = list(dv)
    return d


def load_table_key(fname, sec, key, k, members, names, enums=None):
    """A key of a table section: `default` is a mapping row name -> value (or one value for all rows).  Returns the key
    description of the first row with `rows` = the per-row descriptions (each carries its own `default`)."""
    where = f'{fname}: [{sec}] {key}'
    dm = k.get('default')
    if not isinstance(dm, dict):
        dm = {n: dm for n in names}
    if set(dm) != set(names):
        die(where, f'`default` must give a value for exactly the rows {names}')
    rows = []
    for n in names:
        rows.append(load_key(fname, sec, key, dict(k, default=dm[n]), members if not rows else {}, enums))
    d = dict(rows[0])
    d['rows'] = rows
    return d


def load_enums(fname, ys):
    """The file's shared enums: `names:` is a list of `name` or a mapping name -> C symbol (the address table of
    build/gen/mod_names.cpp); numbers are 1.. in list order, `specials:` are fixed names (keep = 0 ...).  Names, never values."""
    out = {}
    for en, e in ys.items():
        where = f'{fname}: enum {en}'
        if not IDENT.match(en):
            die(where, 'bad enum name')
        names = e.get('names')
        if isinstance(names, list):
            names = {n: None for n in names}
        if not isinstance(names, dict) or not names:
            die(where, 'missing `names`')
        values = {str(n): v for n, v in (e.get('specials') or {}).items()}
        symbols = []
        for i, (n, sym) in enumerate(names.items(), 1):
            if not IDENT.match(str(n)) or str(n) in values:
                die(where, f'bad or duplicate name {n!r}')
            if sym is not None and not IDENT.match(str(sym)):
                die(where, f'bad symbol {sym!r}')
            values[str(n)] = i
            symbols.append((str(n), sym))
        out[en] = {'values': values, 'symbols': symbols if any(s for _, s in symbols) else None,
                   'doc': ' '.join(str(e.get('doc', '')).split())}
    return out


def load_schema(schema_dir):
    files = []
    seen_kinds, seen_files = {}, set()
    for path in sorted(glob.glob(os.path.join(schema_dir, '*.yaml'))):
        fname = os.path.basename(path)
        with open(path, encoding='utf-8') as f:
            y = yaml.safe_load(f)
        for need in ('file', 'sections'):
            if need not in y:
                die(fname, f'missing `{need}`')
        if y['file'] in seen_files:
            die(fname, f'file {y["file"]} defined twice')
        seen_files.add(y['file'])
        # A block = the sections that edit one struct / GameData member.  rules.yaml names it once for the whole file; a table
        # section (items.yaml) names its own (`struct`, `target`, `layout`) because a file can hold several tables.
        enums = load_enums(fname, y.get('enums') or {})
        blocks, sections = {}, []
        for sec, s in y['sections'].items():
            if not isinstance(sec, str) or not IDENT.match(sec):
                die(fname, f'bad section kind {sec!r}')
            if sec in seen_kinds:
                die(fname, f'section [{sec}] already defined in {seen_kinds[sec]}')
            seen_kinds[sec] = fname
            singleton = bool(s.get('singleton', False))
            names = None
            if singleton:
                rows = mx = 1
            else:
                names = s.get('names')
                if not isinstance(names, list) or not names or not all(isinstance(n, str) and IDENT.match(n) for n in names)                         or len({n.lower() for n in names}) != len(names):
                    die(fname, f'[{sec}]: a table section needs `names` (unique built-in row names)')
                rows = s.get('rows', len(names))
                mx = s.get('max')
                if mx is None:
                    die(fname, f'[{sec}]: a table section needs `max` (pool limit)')
                if rows != len(names):
                    die(fname, f'[{sec}]: `rows` ({rows}) must equal the number of `names` ({len(names)})')
                check_int(f'{fname}: [{sec}]', rows, 1, 255, 'rows')
                check_int(f'{fname}: [{sec}]', mx, rows, 255, 'max')
            st, tg, lay = s.get('struct', y.get('struct')), s.get('target', y.get('target')), s.get('layout', y.get('layout'))
            for need, v in (('struct', st), ('target', tg), ('layout', lay)):
                if v is None:
                    die(fname, f'[{sec}]: missing `{need}` (neither in the section nor in the file)')
            if not singleton and (st, tg) in blocks:
                die(fname, f'[{sec}]: a table section needs its own struct and target')
            if (st, tg) in blocks and blocks[(st, tg)]['singletons'] != singleton:
                die(fname, f'[{sec}]: struct {st} mixes singleton and table sections')
            blk = blocks.setdefault((st, tg), {'struct': st, 'target': tg, 'layout': list(lay), 'members': {}, 'sections': [],
                                               'singletons': singleton, 'rows': rows, 'max': mx})
            keys, knames = [], set()
            for key, k in (s.get('keys') or {}).items():
                d = load_table_key(fname, sec, key, k, blk['members'], names, enums) if not singleton                     else load_key(fname, sec, key, k, blk['members'], enums)
                for n in [d['key']] + d['aliases']:
                    if n.lower() in knames or n.lower() == 'base':
                        die(d['where'], f'key or alias {n!r} is not unique (or reserved)')
                    knames.add(n.lower())
                keys.append(d)
            sec_d = {'kind': sec, 'doc': ' '.join(str(s.get('doc', '')).split()), 'singleton': singleton, 'names': names,
                     'rows': rows, 'max': mx, 'keys': keys, 'struct': st, 'target': tg}
            blk['sections'].append(sec_d)
            sections.append(sec_d)
        for blk in blocks.values():
            lay = blk['layout']
            if sorted(lay) != sorted(blk['members']) or len(set(lay)) != len(lay):
                die(fname, f'`layout` must list every member exactly once ({blk["struct"]}); schema has '
                           f'{sorted(blk["members"])}, layout {sorted(lay)}')
        files.append({'file': y['file'], 'title': y.get('title', y['file']), 'blocks': list(blocks.values()),
                      'sections': sections, 'name': fname, 'enums': enums})
    if not files:
        die(schema_dir, 'no *.yaml schema')
    return files


# ---- C++ -----------------------------------------------------------------------------------------------------------------
def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def key_by_member(blk):
    out = {}
    for sec in blk['sections']:
        for k in sec['keys']:
            out[k['member']] = (sec, k)
            if k.get('count_member'):
                out[k['count_member']] = (sec, k)
    return out


def init_text(k, member):
    if k.get('count_member') == member:
        return str(len(k['default']))
    t = k['type']
    if t == 'string':
        return cstr(k['default'])
    if t == 'list':
        v = list(k['default']) + [0] * (k['count_max'] - len(k['default']))
        return '{' + ', '.join(str(x) for x in v) + '}'
    return str(k['default'])


def row_init(blk, kd_index, row=None):
    """The `{...}` initialiser of one struct row: `row` picks the per-row default of a table section."""
    km = key_by_member(blk)
    parts = []
    for m in blk['layout']:
        sec, k = km[m]
        kr = k['rows'][row] if row is not None else k
        what = f'[{sec["kind"]}] {k["key"]}' + (' (count)' if k.get('count_member') == m else '')
        parts.append(f'{init_text(kr, m)},  // {m}: {what}')
    return parts


def gen_defaults(files, header):
    o = ['// Generated by tools/gen_moddata.py from tools/mod_schema/*.yaml: DO NOT EDIT (derived data, build/gen is git-ignored).',
         f'#include <stddef.h>', f'#include "{header}"', '', 'namespace ms {', 'namespace game {', '']
    blocks = [b for fl in files for b in fl['blocks']]
    for blk in blocks:
        st, lay = blk['struct'], blk['layout']
        for a, b in zip(lay, lay[1:]):
            o.append(f'static_assert(offsetof({st}, {a}) < offsetof({st}, {b}), "{st}: schema layout order of {a}, {b}");')
        if not blk['singletons']:
            o.append(f'static_assert(sizeof(((GameData *)0)->{blk["target"]}) == {blk["max"]} * sizeof({st}), '
                     f'"GameData::{blk["target"]}: {blk["max"]} rows of {st}");')
    for a, b in zip(blocks, blocks[1:]):
        o.append(f'static_assert(offsetof(GameData, {a["target"]}) < offsetof(GameData, {b["target"]}), '
                 f'"GameData: schema order of {a["target"]}, {b["target"]}");')
    init = []
    for blk in blocks:
        if blk['singletons']:
            init.append(f'	{{  // {blk["target"]} ({blk["struct"]})')
            init += ['		' + ln for ln in row_init(blk, None)]
            init.append('	},')
        else:
            sec = blk['sections'][0]
            init.append(f'	{{  // {blk["target"]}: {blk["rows"]} built-in rows of {blk["struct"]}, the pool up to {blk["max"]} is zero')
            for r, n in enumerate(sec['names']):
                init.append(f'		{{  // {n}')
                init += ['			' + ln for ln in row_init(blk, None, r)]
                init.append('		},')
            init.append('	},')
    o += ['', 'const GameData kDefaults = {'] + init + ['};', '',
          '// Host and emulator tests (no startup code that copies kDefaults into g_gameData): an initialised live copy.',
          '#ifdef MS_GAMEDATA_LIVE_COPY', 'GameData g_gameData = {'] + init + ['};', '#endif', '',
          '}  // namespace game', '}  // namespace ms', '']
    return '\n'.join(o)


def gen_schema(files, header):
    o = ['// Generated by tools/gen_moddata.py from tools/mod_schema/*.yaml: DO NOT EDIT.',
         '#include <stddef.h>', f'#include "{header}"', '#include "game/data/modschema.hpp"', '', 'namespace ms {',
         'namespace game {', '']
    secs = []
    shared_done = set()
    for fl in files:
        for s in fl['sections']:
            fields = []
            for k in s['keys']:
                tag = f'{s["kind"]}_{k["key"]}'
                en, al = '0', '0'
                if k.get('enum_ref'):
                    en = f'kEnumShared_{k["enum_ref"]}'
                    if en not in shared_done:
                        shared_done.add(en)
                        o.append(f'static const EnumVal {en}[] = {{' +
                                 ', '.join(f'{{{cstr(n)}, {v}}}' for n, v in k['values'].items()) + ', {0, 0}};')
                elif 'values' in k:
                    o.append(f'static const EnumVal kEnum_{tag}[] = {{' +
                             ', '.join(f'{{{cstr(n)}, {v}}}' for n, v in k['values'].items()) + ', {0, 0}};')
                    en = f'kEnum_{tag}'
                if k['aliases']:
                    o.append(f'static const char *const kAlias_{tag}[] = {{' + ', '.join(cstr(a) for a in k['aliases']) + ', 0};')
                    al = f'kAlias_{tag}'
                t = {'int': 'FT_INT', 'bool': 'FT_BOOL', 'enum': 'FT_ENUM', 'string': 'FT_STRING', 'list': 'FT_LIST'}[k['type']]
                lcount = (k['count_min'], k['count_max']) if k['type'] == 'list' else (0, 0)
                coff = f'offsetof({s["struct"]}, {k["count_member"]})' if k.get('count_member') else 'MOD_NO_COUNT'
                size = k['size']
                fields.append(f'\t{{{cstr(k["key"])}, {al}, {t}, {size}, offsetof({s["struct"]}, {k["member"]}), {k["min"]}, '
                              f'{k["max"]}, {en}, {lcount[0]}, {lcount[1]}, {coff}}},')
            if s['names']:
                o.append(f'static const char *const kNames_{s["kind"]}[] = {{' + ', '.join(cstr(n) for n in s['names']) + ', 0};')
            o.append(f'static const FieldDesc kFields_{s["kind"]}[] = {{')
            o += fields
            o += ['};', '']
            secs.append((fl, s))
    o.append('const SectionDesc kModSections[] = {')
    for fl, s in secs:
        o.append(f'\t{{{cstr(s["kind"])}, kFields_{s["kind"]}, {len(s["keys"])}, {"true" if s["singleton"] else "false"}, '
                 f'sizeof({s["struct"]}), offsetof(GameData, {s["target"]}), {s["rows"]}, {s["max"]}, '
                 f'{"kNames_" + s["kind"] if s["names"] else "0"}}},')
    o += ['};', f'const uint8_t kModSectionCount = {len(secs)};', '', 'const ModFileDesc kModFiles[] = {']
    first = 0
    for fl in files:
        o.append(f'\t{{{cstr(fl["file"])}, {first}, {len(fl["sections"])}}},')
        first += len(fl['sections'])
    o += ['};', f'const uint8_t kModFileCount = {len(files)};', '', '}  // namespace game', '}  // namespace ms', '']
    return '\n'.join(o)


def gen_names(files):
    """build/gen/mod_names.cpp: for every shared enum with symbols, the address table (index = the enum value; 0 = none).
    The names of mod files resolve to the original's cells here, so the data never holds an address (ARCHITECTURE 1.2 rule 5)."""
    enums = [(fl, en, e) for fl in files for en, e in fl['enums'].items() if e['symbols']]
    if not enums:
        return None
    o = ['// Generated by tools/gen_moddata.py from tools/mod_schema/*.yaml: DO NOT EDIT.', '#include <stdint.h>', '']
    o.append('extern "C" {')
    seen = set()
    for _, _, e in enums:
        for _, sym in e['symbols']:
            if sym and sym not in seen:
                seen.add(sym)
                o.append(f'extern uint8_t {sym}[];')
    o += ['}', '', 'namespace ms {', 'namespace game {', '']
    for _, en, e in enums:
        o.append(f'extern const void *const kModSyms_{en}[];')
        o.append(f'extern const uint16_t kModSymCount_{en};')
        o.append(f'const void *const kModSyms_{en}[] = {{')
        o.append('	0,')
        o += [f'	{sym if sym else "0"},  // {n}' for n, sym in e['symbols']]
        o += ['};', f'const uint16_t kModSymCount_{en} = {len(e["symbols"]) + 1};', '']
    o += ['}  // namespace game', '}  // namespace ms', '']
    return chr(10).join(o)


# ---- ini reference and docs ------------------------------------------------------------------------------------------------
def ini_value(k):
    t = k['type']
    if t == 'string':
        return cstr(k['default'])
    if t == 'bool':
        return 'on' if k['default'] else 'off'
    if t == 'enum':
        return k['default_name']
    if t == 'list':
        return ', '.join(str(x) for x in (k['default_names'] if k['elem'] == 'enum' else k['default']))
    return str(k['default'])


def allowed(k):
    t = k['type']
    if t == 'int':
        return f'{k["min"]}..{k["max"]}'
    if t == 'bool':
        return 'on, off (yes/no, true/false, 1/0)'
    if t == 'enum':
        if k.get('enum_ref'):
            return f'a name of the `{k["enum_ref"]}` list (docs/MOD_KEYS.md)'
        return ', '.join(k['values'])
    if t == 'string':
        return f'quoted text, up to {k["max"]} characters'
    names = (f'names of the `{k["enum_ref"]}` list' if k.get('enum_ref') else f'names: {", ".join(k["values"])}') if k['elem'] == 'enum' else f'numbers {k["min"]}..{k["max"]}'
    return f'list of {k["count_min"]}..{k["count_max"]} ({names})'


def comment(text, width=100):
    return ['# ' + ln for ln in textwrap.wrap(text, width)]


def gen_ini(fl):
    o = [f'# {fl["file"]} - {fl["title"]}',
         '# Reference of every key with its default value (generated by tools/gen_moddata.py from',
         f'# tools/mod_schema/{fl["name"]}).  Copy to PROGDIR:mods/ and change what you want; keys you leave out keep their',
         '# default, and so does a missing file.  Lines up to 120 bytes; # or ; starts a comment.', '']
    for s in fl['sections']:
        if s['singleton']:
            o.append(f'[{s["kind"]}]')
            if s['doc']:
                o += comment(s['doc'])
            for k in s['keys']:
                o.append('')
                o += comment(k['doc'])
                o.append(f'# allowed: {allowed(k)}' + (f'; old names: {", ".join(k["aliases"])}' if k['aliases'] else ''))
                o.append(('# ' if k['type'] == 'list' and not k['default'] else '') + f'{k["key"]} = {ini_value(k)}'.rstrip())
            o.append('')
            continue
        # a table: the keys are documented once, then every built-in row at its values (parsing this file gives kDefaults)
        o.append(f'# ---- [{s["kind"]} <name>] ----')
        if s['doc']:
            o += comment(s['doc'])
        o += comment(f'A new row: a name that does not exist yet, with `base = <existing row>` as its first key (the table '
                     f'holds at most {s["max"]} rows).')
        for k in s['keys']:
            o += comment(f'{k["key"]}: {k["doc"]}')
            o.append(f'# allowed: {allowed(k)}' + (f'; old names: {", ".join(k["aliases"])}' if k['aliases'] else ''))
        o.append('')
        for r, n in enumerate(s['names']):
            o.append(f'[{s["kind"]} {n}]')
            for k in s['keys']:
                kr = k['rows'][r]
                o.append(('# ' if kr['type'] == 'list' and not kr['default'] else '') + f'{k["key"]} = {ini_value(kr)}'.rstrip())
            o.append('')
    text = '\n'.join(o)
    for ln in text.split('\n'):
        if len(ln.encode()) > 120:
            raise SchemaError(f'{fl["file"]}: generated line over 120 bytes: {ln[:50]}...')
    return text


def md(s):
    return s.replace('|', '\\|')


def gen_doc(files):
    o = ['# Mod keys', '',
         'Generated by `py tools/gen_moddata.py --doc docs/MOD_KEYS.md` from `tools/mod_schema/*.yaml`: do not edit by hand',
         '(`tests/test_gen_moddata.py` checks it is current). The files live in `PROGDIR:mods/`; format and rules in',
         '`docs/ARCHITECTURE.md` section 3. A missing file or key keeps the built-in value; a file with an error is ignored',
         'as a whole. The complete reference with every key at its built-in value is generated into `build/gen/defaults/`',
         '(`hd/mods/defaults/` on the hard disk install). Built-in values are not listed here.', '']
    for fl in files:
        o += [f'## {fl["file"]} - {fl["title"]}', '']
        for en, e in fl['enums'].items():
            o += [f'### Names of the `{en}` list', '', e['doc'], '',
                  ', '.join(f'`{n}`' for n in e['values']), '']
        for s in fl['sections']:
            head = f'[{s["kind"]}]' if s['singleton'] else f'[{s["kind"]} <name>]'
            o += [f'### `{head}`', '']
            if s['doc']:
                o += [s['doc'], '']
            if not s['singleton']:
                o += [f'Built-in rows: {", ".join(s["names"])}. A new row: a name that does not exist yet, with '
                      f'`base = <existing row>` as its first key (at most {s["max"]} rows in total).', '']
            o += ['| key | type | allowed | meaning |', '|---|---|---|---|']
            for k in s['keys']:
                doc = md(k['doc']) + (f' Old name{"s" if len(k["aliases"]) > 1 else ""}: {", ".join(k["aliases"])}.' if k['aliases'] else '')
                o.append(f'| `{k["key"]}` | {k["type"]} | {md(allowed(k))} | {doc} |')
            o.append('')
    return '\n'.join(o).rstrip('\n') + '\n'


# ---- main ----------------------------------------------------------------------------------------------------------------
def write_if_changed(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = text.encode('utf-8')
    if os.path.isfile(path):
        with open(path, 'rb') as f:
            if f.read() == data:
                return False
    with open(path, 'wb') as f:
        f.write(data)
    return True


def generate(schema_dir, out_dir, header):
    files = load_schema(schema_dir)
    outs = {os.path.join(out_dir, 'mod_defaults.cpp'): gen_defaults(files, header),
            os.path.join(out_dir, 'mod_schema.cpp'): gen_schema(files, header)}
    names = gen_names(files)
    if names:
        outs[os.path.join(out_dir, 'mod_names.cpp')] = names
    for fl in files:
        outs[os.path.join(out_dir, 'defaults', fl['file'])] = gen_ini(fl)
    return files, outs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out-dir', default=DEFAULT_OUT)
    ap.add_argument('--schema-dir', default=SCHEMA_DIR)
    ap.add_argument('--header', default='game/api/data.hpp', help='the header declaring GameData (as #include path)')
    ap.add_argument('--doc', help='also write the key reference (docs/MOD_KEYS.md) here')
    ap.add_argument('--check-doc', help='fail if this key reference is not what the schema generates')
    a = ap.parse_args()
    try:
        files, outs = generate(a.schema_dir, a.out_dir, a.header)
    except SchemaError as e:
        print(f'gen_moddata: {e}', file=sys.stderr)
        return 1
    if a.doc:
        outs[a.doc] = gen_doc(files)
    if a.check_doc:
        with open(a.check_doc, 'rb') as f:
            if f.read() != gen_doc(files).encode('utf-8'):
                print(f'gen_moddata: {a.check_doc} is stale: py tools/gen_moddata.py --doc {a.check_doc}', file=sys.stderr)
                return 1
    n = 0
    for path, text in outs.items():
        n += write_if_changed(path, text)
    print(f'gen_moddata: {len(files)} file(s), {sum(len(s["keys"]) for fl in files for s in fl["sections"])} key(s), '
          f'{n} output(s) written')
    return 0


if __name__ == '__main__':
    sys.exit(main())
