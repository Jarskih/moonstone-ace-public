#!/usr/bin/env python3
"""asm_remaining.py -- which asm routines and data are still LIVE (ROADMAP 4.7 reachability, 7.1 input).

Applies the reachability model of docs/DEAD_RT.md section 1 to the generated asm/<bin>.s:

* unit = a prg_/mog_ label and the lines up to the next such label;
* edges = every prg_/mog_ label named in a live line of the unit (any operand, `LAB+N` counts as `LAB`) plus
  fall-through into the next unit (same section);
* an unconditional RTS/RTE/RTR/JMP/BRA ends the unit (lines after it are dead; this is what makes the asm body
  behind a `JMP rt_x` patch dead); a JMP/BRA with a `(PC,Dn)` operand is not terminal (jump tables);
* a label that is the target of a `LAB+N` reference keeps its whole unit live;
* a unit of a CODE hunk that holds only DC/DS lines (a data cell or table: IRA decodes some as instructions, and
  asm/patches/*.json `as_data` patches re-emit them as DC.B) is not executed and does not fall through into the unit after it
  (ROADMAP 7.1h: the flyer's cell LAB_0234 kept the whole brawler / caster / script-op family alive through the fall-through);
* roots = `SECSTRT_0` of each binary and every prg_/mog_ symbol that C++ *uses* (src/rt, src/engine, src/game,
  include/rt, include/ms/linked.hpp, include/game).  Pure `extern` declarations and comments do not count as uses
  (so a stale declaration is not a root); `S_n_beg/_end` markers are section bounds, not roots.
  src/lifted/** and include/ms/gen/* are not roots (they exist only in the optional MS_LINK_LIFTED build).

Output: build/inventory/asm_remaining.json (everything the doc is built from) and a text summary on stdout.

    py tools/asm_remaining.py [--json PATH] [--list mog|program] [--quiet]

Library use (tests): `analyse()` returns the result dict, `is_live(result, 'mog', 'LAB_0B82')` etc.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINS = {'program': 'prg_', 'mog': 'mog_'}
SYM_RE = re.compile(r'\b((?:prg|mog)_[A-Za-z0-9_]+)(\+\d+)?')
LABEL_RE = re.compile(r'^((?:prg|mog)_[A-Za-z0-9_]+):')
SECTION_RE = re.compile(r'^\s+SECTION\s+(\w+?)_S_(\d+)(\.MEMF_CHIP)?,(CODE|DATA|BSS)', re.I)
PATCH_RE = re.compile(r'^ps_([A-Za-z0-9_]+):')
DIRECTIVES = {'XDEF', 'XREF', 'SECTION', 'EVEN', 'ORG', 'IFGT', 'IFLT', 'IFEQ', 'IFNE', 'ENDC', 'FAIL', 'DC', 'DS',
              'DCB', 'END', 'INCLUDE', 'ALIGN', 'CNOP', 'OPT'}
BRANCH_RE = re.compile(r'^(JSR|BSR|JMP|BRA|B(HI|LS|CC|CS|NE|EQ|VC|VS|PL|MI|GE|LT|GT|LE|HS|LO)|DB\w+)$')
TERMINAL = {'RTS', 'RTE', 'RTR', 'JMP', 'BRA'}
CPP_DIRS = ['src/rt', 'src/engine', 'src/game', 'include/rt', 'include/game', 'include/engine']
CPP_FILES = ['include/ms/linked.hpp']
CPP_SKIP = ('image_tab.cpp', 'hunk_tab.cpp', 'abs_stubs.cpp')  # generated section/hunk tables
BOUND_RE = re.compile(r'_S_\d+_(beg|end)$')


def strip_comment(line):
    out, q = [], False
    for ch in line:
        if ch == '"':
            q = not q
        if ch == ';' and not q:
            break
        out.append(ch)
    return ''.join(out)


def mnemonic(line):
    if not line.startswith(('\t', ' ')):
        return None
    toks = line.split()
    if not toks:
        return None
    m = toks[0].upper()
    base = m.split('.')[0]
    return base


def parse_asm(binary):
    """-> units: ordered dict label -> unit dict; order list."""
    path = os.path.join(ROOT, 'asm', ('program' if binary == 'program' else 'mog') + '.s')
    units, order = {}, []
    cur = None
    sect = None
    with open(path, encoding='latin-1') as f:
        for ln, raw in enumerate(f, 1):
            raw = raw.rstrip('\n').rstrip('\r')
            ms = SECTION_RE.match(raw)
            if ms:
                sect = {'hunk': int(ms.group(2)), 'chip': bool(ms.group(3)), 'type': ms.group(4).upper()}
                cur = None
                continue
            ml = LABEL_RE.match(raw)
            if ml:
                lab = ml.group(1)
                cur = {'label': lab, 'line': ln, 'section': sect, 'lines': [], 'order': len(order)}
                units[lab] = cur
                order.append(lab)
                continue
            mp = PATCH_RE.match(raw)
            if cur is None:
                continue
            code = strip_comment(raw)
            if not code.strip():
                # patch comment lines carry no code but keep the patch marker
                continue
            cur['lines'].append((ln, code, mp.group(1) if mp else None))
            if mp:
                pass
    return units, order


def line_info(code):
    m = mnemonic(code)
    if m is None:
        return None, False
    if m in DIRECTIVES or m.startswith('DC') or m.startswith('DS'):
        return m, False
    return m, True


def unit_live_part(unit, whole):
    """Lines of the unit that execute/are data-live, and whether control falls through."""
    out = []
    falls = True
    in_patch = []
    for ln, code, pid in unit['lines']:
        out.append((ln, code))
        m = mnemonic(code)
        if m in TERMINAL and not whole:
            ops = code.split(None, 1)[1] if len(code.split(None, 1)) > 1 else ''
            if not re.search(r'\(PC,\s*[DA]\d', ops, re.I):
                falls = False
                break
    if unit['section']['type'] != 'CODE':
        falls = True  # data: no code-end semantics, falls through harmlessly
    elif out and all(not line_info(code)[1] for _ln, code in out if mnemonic(code) is not None):
        falls = False  # only DC/DS lines in a CODE hunk: a data cell, nothing executes it or falls through it
    return out, falls


# build options whose OFF branch is assumed gone for the 7.1 analysis (decided: ROADMAP 4.6, ptplayer) or whose A/B switch is OFF by default
# (MS_SYNTH_ASM, ROADMAP 7.1g: the C++ synth is the default, the original asm synth only runs with -DMS_SYNTH_ASM=ON)
CFG = {'MS_MUSIC_PTPLAYER': True, 'MS_SYNTH_ASM': False}


def apply_cfg(text):
    """Evaluate `#if NAME` / `#else` / `#endif` for the NAME in CFG only; every other conditional keeps both branches."""
    out, stack = [], []   # stack of (name or None, active)
    for line in text.split('\n'):
        t = line.strip()
        if t.startswith('#if'):
            parts = t.split()
            name = parts[1] if len(parts) > 1 and t.startswith('#if ') and parts[1] in CFG else None
            stack.append([name, True if name is None else CFG[name]])
        elif t.startswith('#else') and stack:
            if stack[-1][0] is not None:
                stack[-1][1] = not CFG[stack[-1][0]]
        elif t.startswith('#endif') and stack:
            stack.pop()
        elif all(a for _n, a in stack):
            out.append(line)
    return '\n'.join(out)


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


EXTERN_RE = re.compile(r'\bextern\b[^;{}]*;')
# plain function prototypes `void rt_x(args);` (a declaration, not a call): `return rt_x(..);` is not one
PROTO_RE = re.compile(r'^[ \t]*(?:(?:static|inline|const|unsigned|struct|volatile)[ \t]+)*'
                      r'(?!return\b|else\b|case\b|goto\b|throw\b)[A-Za-z_][\w:<>]*[ \t\*&]+\**'
                      r'(?:rt|prg|mog)_[A-Za-z0-9_]+[ \t]*\([^;{}]*\)[ \t]*(?:asm\([^)]*\))?[ \t]*;', re.M)
# address used only as a handler identity key (compared, never called): not an execution root
IDENT_RE = re.compile(r'\bjobAddr\(\s*(?:prg|mog)_[A-Za-z0-9_]+\s*\)')
RAW_ASM_RE = re.compile(r'\basm\s*\(\s*R"(\w*)\((.*?)\)\1"\s*\)\s*;', re.S)
RT_RE = re.compile(r'\b(rt_[A-Za-z0-9_]+)')
ASM_LABEL_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*):', re.M)
ASM_DIRECTIVE_RE = re.compile(r'^[ \t]*\.(?:globl|global|weak|type|size|align|text|data|section|balign)\b[^\n]*', re.M)


# Macro-built symbol names:
#  * `#define E(n) {0x##n, (ULONG)&prg_LAB_##n}`: E(0185) names prg_LAB_0185
#  * `#define BIND(PFX, CELL) ... asm(#PFX "_" #CELL)`: BIND(prg, LAB_04DE) names prg_LAB_04DE
MACRO_DEF_RE = re.compile(r'^[ \t]*#define[ \t]+(\w+)\(([^)]*)\)((?:[^\n]*\\\n)*[^\n]*)', re.M)
STRINGIFY_RE = re.compile(r'#(\w+)[ \t]+"_"[ \t]+#(\w+)')


def macro_symbols(text):
    out = set()
    for m in MACRO_DEF_RE.finditer(text):
        name = m.group(1)
        params = [x.strip() for x in m.group(2).split(',')]
        rest = m.group(3)
        pasted = []
        if len(params) == 1:
            pm = re.search(r'\b(prg|mog)_(LAB_|SECSTRT_)?##' + params[0] + r'\b', rest)
            if pm and not rest.lstrip().startswith('extern') and 'jobAddr(' not in rest[:pm.start()]:
                pasted.append(pm)
        pairs = [(a, b) for a, b in STRINGIFY_RE.findall(rest) if a in params and b in params]
        if not pasted and not pairs:
            continue
        for im in re.finditer(r'\b' + name + r'\(([^()]*)\)', text):
            if m.start() <= im.start() < m.end():
                continue
            args = [x.strip() for x in im.group(1).split(',')]
            for pm in pasted:
                if re.fullmatch(r'[0-9A-Fa-f]+', args[0]):
                    out.add(pm.group(1) + '_' + (pm.group(2) or '') + args[0].upper())
            if len(args) == len(params):
                for a, b in pairs:
                    pa, pb = args[params.index(a)], args[params.index(b)]
                    if pa in ('prg', 'mog') and re.fullmatch(r'(LAB|SECSTRT)_[0-9A-Fa-f]+', pb):
                        out.add(pa + '_' + pb)
    return out


def cpp_files():
    files = []
    for d in CPP_DIRS:
        for dp, _dn, fn in os.walk(os.path.join(ROOT, d)):
            if '.vs' in dp.replace('\\', '/').split('/'):
                continue
            for n in fn:
                if n.endswith(('.cpp', '.hpp', '.h', '.c')) and n not in CPP_SKIP:
                    files.append(os.path.join(dp, n))
    for fl in CPP_FILES:
        files.append(os.path.join(ROOT, fl))
    return files


def scan_cpp():
    """Scan the C++ side.

    uses[sym]       -> files using prg_/mog_ sym outside extern declarations/comments (C++ body, or an asm string that
                       is not a labelled raw-string trampoline: conservative roots)
    declared[sym]   -> files that mention it at all (declaration only when not in uses)
    rt_roots[sym]   -> files using an rt_ symbol in the C++ body (a call, table entry or address-of)
    pieces[rt_x]    -> [file, {prg_/mog_/rt_ symbols}] for each labelled raw-string asm trampoline `rt_x:`
    plus            -> symbols referenced with `+N`
    """
    uses, declared, rt_roots, pieces, plus = {}, {}, {}, {}, set()
    for p in cpp_files():
        try:
            with open(p, encoding='utf-8', errors='replace') as f:
                raw = f.read()
        except OSError:
            continue
        rel = os.path.relpath(p, ROOT).replace('\\', '/')
        text_nc = strip_comments(apply_cfg(raw))
        body_parts, last = [], 0
        for m in RAW_ASM_RE.finditer(text_nc):
            body_parts.append(text_nc[last:m.start()])
            last = m.end()
            text = ASM_DIRECTIVE_RE.sub('', m.group(2))
            labs = list(ASM_LABEL_RE.finditer(text))
            body_parts.append(text[:labs[0].start()] if labs else text)   # before the first label: conservative
            cur = None
            for i, lm in enumerate(labs):
                seg = text[lm.end():(labs[i + 1].start() if i + 1 < len(labs) else len(text))]
                syms = {x.group(1) for x in SYM_RE.finditer(seg)} | {x.group(1) for x in RT_RE.finditer(seg)}
                name = lm.group(1)
                if name.startswith(('rt_', 'ms_')):
                    cur = pieces.setdefault(name, [rel, set()])
                if cur is not None:      # numeric/local helper labels stay with the enclosing rt_ piece
                    cur[1] |= syms
                else:
                    body_parts.append(seg)
            for x in SYM_RE.finditer(text):
                if x.group(2):
                    plus.add(x.group(1))
        body_parts.append(text_nc[last:])
        body = PROTO_RE.sub('', EXTERN_RE.sub('', ''.join(body_parts)))
        body = IDENT_RE.sub('', body)
        for m in SYM_RE.finditer(body):
            sym = m.group(1)
            if BOUND_RE.search(sym):
                continue
            uses.setdefault(sym, set()).add(rel)
            if m.group(2):
                plus.add(sym)
        for sym in macro_symbols(text_nc):
            uses.setdefault(sym, set()).add(rel)
        for m in RT_RE.finditer(body):
            rt_roots.setdefault(m.group(1), set()).add(rel)
        for m in SYM_RE.finditer(text_nc):
            sym = m.group(1)
            if sym not in uses or rel not in uses[sym]:
                declared.setdefault(sym, set()).add(rel)
    return uses, declared, rt_roots, pieces, plus


def load_inventory():
    inv = {}
    p = os.path.join(ROOT, 'build', 'inventory', 'routines.json')
    if not os.path.exists(p):
        return inv
    with open(p) as f:
        data = json.load(f)['binaries']
    for b in BINS:
        inv[b] = data[b]
    return inv


def load_syms(binary):
    p = os.path.join(ROOT, 'build', 'reasm', binary + '.symbols.json')
    if not os.path.exists(p):
        return {}
    with open(p) as f:
        return json.load(f)


def cpp_owned():
    """prg_/mog_ names C++ owns since ROADMAP 7.1r (tools/gen_data.py): the code-hunk data cells (`extern_cells`) and the labels a
    pointer cell now resolves to a C++ routine (`link_names`).  They are not asm any more: nothing reaches the asm through them."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import gen_data
    cells, links, _weak = gen_data.load_cells()
    return {BINS[b] + c['label'] for b in BINS for c in cells[b]} | {BINS[b] + l for b in BINS for l in links[b]}


def analyse():
    units_by_bin = {}
    all_units = {}
    owned = cpp_owned()
    for b, pfx in BINS.items():
        u, order = parse_asm(b)
        units_by_bin[b] = (u, order)
        all_units.update({k: v for k, v in u.items() if k not in owned})
    # successor in the same section (fall-through)
    nxt = {}
    for b in BINS:
        u, order = units_by_bin[b]
        for i, lab in enumerate(order[:-1]):
            n = order[i + 1]
            if u[n]['section'] == u[lab]['section']:
                nxt[lab] = n
    uses, declared, rt_roots, pieces, plus = scan_cpp()
    # whole-unit set from +N refs in asm (any line; they are rare)
    for u in all_units.values():
        for _ln, code, _p in u['lines']:
            for m in SYM_RE.finditer(code):
                if m.group(2):
                    plus.add(m.group(1))
    roots = set()      # the overlay entries are C++ since 7.1r (rt_prg_main / rt_mog_entry): no asm root is left
    root_src = {}
    for sym, fs in uses.items():
        if sym in all_units:
            roots.add(sym)
            root_src[sym] = sorted(fs)
    for sym, fs in rt_roots.items():
        if sym in pieces:
            roots.add(sym)
            root_src[sym] = sorted(fs)
    live = {}      # label -> live line list
    work = list(roots)
    seen = set(work)
    parent = {r: None for r in work}
    refs_of = {}   # label -> set(labels) (live edges)
    inbound = {r: {('root', None)} for r in roots}   # label -> kinds of live references: root/cpp/call/ptr/data/fall
    while work:
        lab = work.pop()
        edges = set()
        if lab in pieces:
            edges = {x for x in pieces[lab][1] if x in all_units or x in pieces}
            for x in edges:
                inbound.setdefault(x, set()).add(('cpp', lab))
        elif lab in all_units:
            u = all_units[lab]
            part, falls = unit_live_part(u, lab in plus)
            live[lab] = part
            for _ln, code in part:
                mn = mnemonic(code) or ''
                kind = 'call' if BRANCH_RE.match(mn) else ('data' if mn.startswith('DC') else 'ptr')
                for m in SYM_RE.finditer(code):
                    if m.group(1) in all_units:
                        edges.add(m.group(1))
                        inbound.setdefault(m.group(1), set()).add((kind, lab))
                for m in RT_RE.finditer(code):
                    if m.group(1) in pieces:
                        edges.add(m.group(1))
                        inbound.setdefault(m.group(1), set()).add(('call', lab))
            if falls and lab in nxt and not BOUND_RE.search(lab):
                edges.add(nxt[lab])
                inbound.setdefault(nxt[lab], set()).add(('fall', lab))
        else:
            continue
        refs_of[lab] = edges
        for e in edges:
            if e not in seen:
                seen.add(e)
                parent[e] = lab
                work.append(e)
    # C++ modules calling each asm label: directly (body) or through a live trampoline piece
    callers = {}
    for sym, fs in uses.items():
        callers.setdefault(sym, set()).update(fs)
    for name, (rel, syms) in pieces.items():
        if name in seen:
            for x in syms:
                callers.setdefault(x, set()).add(rel)
    return {'inbound': inbound, 'parent': parent, 'units': all_units, 'order': {b: units_by_bin[b][1] for b in BINS}, 'live': live,
            'edges': refs_of, 'roots': roots, 'root_src': root_src, 'uses': callers, 'declared': declared,
            'plus': plus, 'nxt': nxt, 'pieces': pieces, 'live_pieces': sorted(p for p in pieces if p in seen)}


def sym_name(binary, label):
    return BINS[binary] + label


def is_live(result, binary, label):
    """True when the unit `label` (without prg_/mog_) of `binary` is reachable."""
    return (BINS[binary] + label) in result['live']


def why(res, label):
    """Chain of labels from a root to `label` (discovery order), for 'why is this live?'."""
    chain = []
    cur = label
    while cur is not None and len(chain) < 60:
        chain.append(cur)
        cur = res['parent'].get(cur)
    return chain


# ----------------------------------------------------------------------------------------------------------------
# Facts from the inventory (routines.json) and the reassembly symbol map
# ----------------------------------------------------------------------------------------------------------------
def build_report(res):
    inv = load_inventory()
    report = {}
    for b in BINS:
        syms = load_syms(b)
        routines = inv.get(b, {}).get('routines', [])
        by_hunk = {}
        for r in routines:
            by_hunk.setdefault(r['hunk'], []).append(r)
        for h in by_hunk:
            by_hunk[h].sort(key=lambda r: r['start'])
        names = {}
        for lab, v in inv.get(b, {}).get('other_labels', {}).items():
            if isinstance(v, dict) and v.get('name'):
                names[lab] = v['name']
        for r in routines:
            if r.get('name'):
                names[r['label']] = r['name']
        hunk_labels = {}
        for lab, s in syms.items():
            hunk_labels.setdefault(s['hunk'], []).append((s['offset'], lab))
        for h in hunk_labels:
            hunk_labels[h].sort()
        hunk_size = {int(k): v['size'] for k, v in inv.get(b, {}).get('hunks', {}).items()}
        usize = {}
        for h, lst in hunk_labels.items():
            for i, (off, lab) in enumerate(lst):
                end = lst[i + 1][0] if i + 1 < len(lst) else hunk_size.get(h, off)
                usize[lab] = max(0, end - off)

        def routine_of(label, by_hunk=by_hunk, syms=syms):
            s = syms.get(label)
            if not s:
                return None
            for r in by_hunk.get(s['hunk'], []):
                if r['start'] <= s['offset'] < r['end']:
                    return r
            return None

        report[b] = {'routine_of': routine_of, 'names': names, 'usize': usize, 'syms': syms,
                     'by_label': {r['label']: r for r in routines}, 'hunk_size': hunk_size}
    return report


# ----------------------------------------------------------------------------------------------------------------
# Area / group assignment (heuristic: hunk role + label ranges in source order; see docs/ASM_REMAINING.md section 2)
# ----------------------------------------------------------------------------------------------------------------
# id -> (area, title, ROADMAP task it belongs to / proposes)
GROUPS = {
    'prg.scenes': ('intro scene bodies', 'program S_0: intro/ending scene bodies and frame loops', '6.10 / 4.9 -> 7.1i'),
    'prg.music': ('music/sfx', 'program S_1: ST/NT module player (dead under MS_MUSIC_PTPLAYER, still linked)', '4.6 -> 7.1b'),
    'prg.anim_jobs': ('intro scene bodies', 'program S_10: script op handlers, sprite blit list, job-manager tails', '6.10 -> 7.1i'),
    'prg.text': ('text/font', 'program S_12: font string/number printing', '4.2 -> 7.1a'),
    'prg.trackdisk': ('file/disk', 'program S_13: trackdisk driver residue (motor/step)', '4.7 -> 7.1d'),
    'prg.irq': ('IRQ handlers/input', 'program S_15: INT1-6 handlers, keyboard/joystick, VBL hook list', '2.2/2.3 -> 7.1d'),
    'prg.file_stubs': ('file/disk', 'program S_18: file-cache entry stubs behind rt/files patches', '2.7 -> 7.1f'),
    'prg.loaders': ('file/disk', 'program S_8/S_20/S_21: file table and load dispatcher (asset_file_load, intro_run), picture and cel loaders, LZSS glue', '4.1 -> 7.1f'),
    'prg.blit_cel': ('blitter', 'program S_23: IMAGEXCEL draw_cel remnants (clip, mirror, kernel setup)', '4.4 -> 7.1c'),
    'prg.blit_copy': ('blitter', 'program S_25: copy_rect remnants, IMAGEXCEL init', '4.4 -> 7.1c'),
    'prg.display': ('display/copper', 'program S_29: custom-chip/copper/DMA init, beam wait, screen swap', '4.8 -> 7.1e'),
    'prg.palette': ('display/copper', 'program S_31: palette wipes/transitions, colour helpers', '4.5 -> 7.1e'),
    'mog.boot_loop': ('misc', 'mog S_0 boot/main-loop skeleton, RNG seed, audio fade glue', '6.1 -> 7.1l'),
    'mog.rules': ('misc', 'mog S_0 knight/rules remnants (recalc, upkeep)', '5.3 -> 7.1l'),
    'mog.overworld_nodes': ('map drawing', 'mog S_0 overworld node entries: duel, lair, dragon, place dispatch', '6.3/6.8 -> 7.1l'),
    'mog.menu': ('screen loops/UI', 'mog S_0 title/menu screen loop (hunk-9 call site)', '6.2 -> 7.1k'),
    'mog.input': ('input', 'mog joystick/fire reads (S_0 and S_20)', '2.3 -> 7.1d'),
    'mog.files_prompt': ('file/disk', 'mog S_0 drive init, disk prompts, loader call sites', '2.7 -> 7.1f'),
    'mog.combat_load': ('file/disk', 'mog S_0 LAB_0114-0155: per-creature/arena asset loads and set-up wrappers', '6.4 -> 7.1f'),
    'mog.combat_setup': ('screen loops/UI', 'mog S_0 combat set-up: fighter/creature init, tables, pool, hit test', '6.4 -> 7.1j'),
    'mog.fighters': ('fighter handlers', 'mog S_0 per-fighter handlers LAB_01C6-02F1 (asm bodies behind rtFighterRun)', '6.4a -> 7.1h'),
    'mog.job_frame': ('fighter handlers', 'mog S_0 job/frame primitives (clear, pause, frame start/wait, job pass)', '6.1/6.4 -> 7.1m'),
    'mog.script_ops': ('fighter handlers', 'mog S_0 combat script op handlers LAB_0358-039C (table entries)', '6.5 -> 7.1h'),
    'mog.contact': ('fighter handlers', 'mog S_0 frame timer, draw-buffer clear, contact/collision', '6.6 -> 7.1m'),
    'mog.display_ops': ('display/copper', 'mog S_0 sprite display, flip, custom-register helpers', '4.8 -> 7.1e'),
    'mog.palette_load': ('display/copper', 'mog S_0 palette load/fade glue, fight-screen init', '4.5 -> 7.1e'),
    'mog.copper_fx': ('display/copper', 'mog S_0 copper effects (LAB_0427/042A)', '4.8 -> 7.1e'),
    'mog.text': ('text/font', 'mog S_0 string/text/number drawing', '4.2 -> 7.1a'),
    'mog.places': ('screen loops/UI', 'mog S_0 wizard/mystic/gift/stonehenge UI helpers, gold text', '6.8 -> 7.1k'),
    'mog.town_ui': ('screen loops/UI', 'mog S_0 town/dice/exchange UI helpers', '6.7 -> 7.1k'),
    'mog.combat_ui': ('screen loops/UI', 'mog S_0 combat state entry, post-fight/stat/shop screens', '6.4/6.9 -> 7.1j'),
    'mog.loot_ui': ('screen loops/UI', 'mog S_0 loot screens and click handling', '6.9 -> 7.1j'),
    'mog.bg_blit': ('blitter', 'mog S_12: arena background blit compositor, obstacle test', '4.4/6.4 -> 7.1m'),
    'mog.sound_entry': ('music/sfx', 'mog S_16: sound request entry, channel start/stop', '4.6 -> 7.1b'),
    'mog.trackdisk': ('file/disk', 'mog S_18: trackdisk driver residue', '4.7 -> 7.1d'),
    'mog.irq': ('IRQ handlers/input', 'mog S_20: INT1-6 handlers, keyboard/joystick, VBL hook list', '2.2/2.3 -> 7.1d'),
    'mog.file_stubs': ('file/disk', 'mog S_23: file-cache entry stubs behind rt/files patches', '2.7 -> 7.1f'),
    'mog.loaders': ('file/disk', 'mog S_25/S_26: picture, cel/ob loaders, LZSS body', '4.1 -> 7.1f'),
    'mog.blit_cel': ('blitter', 'mog S_28: IMAGEXCEL draw_cel remnants', '4.4 -> 7.1c'),
    'mog.blit_copy': ('blitter', 'mog S_30: copy_rect remnants, IMAGEXCEL init', '4.4 -> 7.1c'),
    'mog.display': ('display/copper', 'mog S_34: custom-chip/copper init, screen clear, text-cell pokes', '4.8 -> 7.1e'),
    'mog.map': ('map drawing', 'mog S_36: overworld loop LAB_0DAB, map sprite/dragon/AI tails, map draw', '6.3 -> 7.1l'),
    'mog.palette': ('display/copper', 'mog S_36 tail: palette target/ramp/tick twins (patched), colour helpers', '4.5 -> 7.1e'),
    'mog.sprite_fx': ('display/copper', 'mog S_37: hardware-sprite (pointer) DMA and table-driven sprite build', '4.8 -> 7.1e'),
    'mog.fighter_handlers': ('fighter handlers', 'mog S_40: creature/dragon handler bodies (jump-table entries)', '6.4a -> 7.1h'),
    'mog.synth': ('music/sfx', 'mog S_44: four-voice synth/sequencer driver and fade', '4.6 -> 7.1g'),
}

TASK_OF = {'prg.text': 'a', 'mog.text': 'a', 'prg.music': 'b', 'mog.sound_entry': 'b', 'prg.blit_cel': 'c', 'prg.blit_copy': 'c', 'mog.blit_cel': 'c', 'mog.blit_copy': 'c', 'prg.irq': 'd', 'mog.irq': 'd', 'mog.input': 'd', 'prg.trackdisk': 'd', 'mog.trackdisk': 'd', 'prg.display': 'e', 'prg.palette': 'e', 'mog.display': 'e', 'mog.palette': 'e', 'mog.palette_load': 'e', 'mog.display_ops': 'e', 'mog.copper_fx': 'e', 'mog.sprite_fx': 'e', 'prg.loaders': 'f', 'mog.loaders': 'f', 'mog.combat_load': 'f', 'mog.files_prompt': 'f', 'prg.file_stubs': 'f', 'mog.file_stubs': 'f', 'mog.synth': 'g', 'mog.fighters': 'h', 'mog.fighter_handlers': 'h', 'mog.script_ops': 'h', 'mog.contact': 'm', 'mog.job_frame': 'm', 'mog.bg_blit': 'm', 'prg.scenes': 'i', 'prg.anim_jobs': 'i', 'mog.combat_setup': 'j', 'mog.combat_ui': 'j', 'mog.loot_ui': 'j', 'mog.menu': 'k', 'mog.places': 'k', 'mog.town_ui': 'k', 'mog.map': 'l', 'mog.overworld_nodes': 'l', 'mog.boot_loop': 'l', 'mog.rules': 'l'}

# lettered ROADMAP tasks (all under 7.1, in suggested execution order) and their batch
TASKS = {
    'a': ('Text/font printing', 1), 'b': ('Audio front end: drop the module player, sound request entry', 1),
    'c': ('Blitter remnants (IMAGEXCEL, both binaries)', 1), 'd': ('IRQ handlers, keyboard/joystick, trackdisk residue', 2),
    'e': ('Display, copper, palette, hardware sprites', 2), 'f': ('Asset loaders and the file layer', 2),
    'g': ('Music/sfx synth driver (mog S_44)', 3), 'h': ('Fighter handlers (asm bodies behind rtFighterRun) and combat script ops', 3),
    'i': ('Intro/ending scene bodies and animation job handlers (program)', 3),
    'j': ('Combat set-up, combat/loot screens', 4), 'k': ('Menu, places and town screens', 4),
    'l': ('Overworld map, boot/main-loop skeleton, rules remnants', 4),
    'm': ('Contact/collision, job and frame primitives, arena background blit', 4),
}

PRG_HUNK_GROUP = {0: 'prg.scenes', 1: 'prg.music', 8: 'prg.loaders', 10: 'prg.anim_jobs', 12: 'prg.text', 13: 'prg.trackdisk',
                  15: 'prg.irq', 18: 'prg.file_stubs', 20: 'prg.loaders', 21: 'prg.loaders', 23: 'prg.blit_cel',
                  25: 'prg.blit_copy', 29: 'prg.display', 31: 'prg.palette'}
MOG_HUNK_GROUP = {12: 'mog.bg_blit', 16: 'mog.sound_entry', 18: 'mog.trackdisk', 20: 'mog.irq', 23: 'mog.file_stubs',
                  25: 'mog.loaders', 26: 'mog.loaders', 28: 'mog.blit_cel', 30: 'mog.blit_copy', 34: 'mog.display',
                  37: 'mog.sprite_fx', 40: 'mog.fighter_handlers', 44: 'mog.synth'}
# mog S_0 by label (hex), narrowest first; mog S_36 split: map vs palette tail
MOG_S0_RANGES = [
    (0x0001, 0x0012, 'mog.boot_loop'), (0x0013, 0x0035, 'mog.rules'), (0x0036, 0x00B3, 'mog.overworld_nodes'),
    (0x00B4, 0x00E9, 'mog.menu'), (0x00EA, 0x00F7, 'mog.input'), (0x00F8, 0x0113, 'mog.files_prompt'),
    (0x0114, 0x0155, 'mog.combat_load'), (0x0156, 0x01C5, 'mog.combat_setup'), (0x01C6, 0x02F1, 'mog.fighters'), (0x02F2, 0x0357, 'mog.job_frame'),
    (0x0358, 0x039C, 'mog.script_ops'), (0x039D, 0x03EA, 'mog.contact'), (0x03EB, 0x03EF, 'mog.display_ops'),
    (0x03F0, 0x0415, 'mog.palette_load'), (0x0416, 0x0426, 'mog.display_ops'), (0x0427, 0x042F, 'mog.copper_fx'),
    (0x0430, 0x044D, 'mog.text'), (0x044E, 0x0457, 'mog.combat_setup'), (0x0458, 0x04A0, 'mog.places'),
    (0x04A1, 0x04A5, 'mog.boot_loop'), (0x04A6, 0x04CE, 'mog.town_ui'), (0x04CF, 0x0526, 'mog.combat_ui'),
    (0x0527, 0x0574, 'mog.loot_ui'), (0x0575, 0x057C, 'mog.input'), (0x057D, 0x0587, 'mog.loot_ui'),
    (0x0588, 0x0589, 'mog.combat_setup'), (0x058A, 0x05A1, 'mog.loot_ui'),
]


def group_of(binary, label, hunk):
    m = re.fullmatch(r'LAB_([0-9A-F]{4})', label)
    val = int(m.group(1), 16) if m else None
    if binary == 'program':
        return PRG_HUNK_GROUP.get(hunk, 'prg.scenes')
    if hunk == 0:
        if val is None:
            return 'mog.boot_loop'
        for lo, hi, g in MOG_S0_RANGES:
            if lo <= val <= hi:
                return g
        return 'mog.boot_loop'
    if hunk == 36:
        if val is not None and 0x0E53 <= val <= 0x0E7F:
            return 'mog.palette'
        return 'mog.map'
    return MOG_HUNK_GROUP.get(hunk, 'mog.boot_loop')


def summarise(res, rep):
    """Group live code units into routines and routines into areas; collect live data units."""
    out = {}
    lab2key = {}
    for b, pfx in BINS.items():
        r = rep[b]
        routines = {}
        data_units = []
        for lab in res['order'][b]:
            if lab not in res['live']:
                continue
            u = res['units'][lab]
            part = res['live'][lab]
            base = lab[len(pfx):]
            ninstr = sum(1 for _ln, code in part if line_info(code)[1])
            lines_live = {ln for ln, _c in part}
            ptch = [p for ln, _c, p in u['lines'] if p and ln in lines_live]
            is_code = u['section']['type'] == 'CODE' and ninstr > 0
            ro = r['routine_of'](base)
            if not is_code or (ro is None and u['section']['type'] != 'CODE'):
                data_units.append({'label': base, 'hunk': u['section']['hunk'], 'sect': u['section']['type'],
                                   'bytes': r['usize'].get(base, 0), 'cpp': sorted(res['uses'].get(lab, [])),
                                   'in_code': u['section']['type'] == 'CODE'})
                continue
            key = ro['label'] if ro else base
            e = routines.setdefault(key, {
                'label': key, 'hunk': u['section']['hunk'], 'name': r['names'].get(key), 'instr_lines': 0,
                'instr_inv': ro['instructions'] if ro else None, 'embedded': ro.get('embedded_data_bytes', 0) if ro else 0,
                'size': ro['size'] if ro else None, 'units': [], 'patches': [], 'cpp': set(),
                'hardware': sorted((ro.get('hardware') or {}).keys()) if ro else [],
                'bytes': 0, 'tags': set()})
            e['instr_lines'] += ninstr
            e['bytes'] += r['usize'].get(base, 0)
            e['units'].append(base)
            e['patches'] += ptch
            e['cpp'] |= set(res['uses'].get(lab, []))
            lab2key[lab] = (b, key)
        for e in routines.values():
            e['instr'] = min(e['instr_lines'], e['instr_inv']) if e['instr_inv'] is not None else e['instr_lines']
            e['cpp'] = sorted(e['cpp'])
            e['group'] = group_of(b, e['label'], e['hunk'])
        out[b] = {'routines': routines, 'data': data_units}
    # routine-level edges and "address-only" classification
    for b, pfx in BINS.items():
        for e in out[b]['routines'].values():
            callees, ext_kinds = set(), set()
            own = {pfx + u for u in e['units']}
            for u in e['units']:
                for t in res['edges'].get(pfx + u, ()):
                    k = lab2key.get(t)
                    if k and k != (b, e['label']):
                        callees.add(f'{k[0]}:{k[1]}')
                for kind, src in res['inbound'].get(pfx + u, ()):
                    if src is None or src not in own:
                        ext_kinds.add(kind)
            e['callees'] = sorted(callees)
            e['inbound'] = sorted(ext_kinds)
            e['addr_only'] = not (ext_kinds & {'call', 'fall', 'root', 'cpp'})
    return out, lab2key


def build_data(res, rep, summ):
    """Everything C++ reaches by prg_/mog_ data symbol, and what asm-only data stays live."""
    inv_hunk = {}
    for b in BINS:
        for h, v in load_inventory().get(b, {}).get('hunks', {}).items():
            inv_hunk[(b, int(h))] = v
    cpp_data = {}
    for sym, fs in res['uses'].items():
        u = res['units'].get(sym)
        if not u:
            continue
        isdata = u['section']['type'] != 'CODE'
        if not isdata:
            # data decoded as code inside a CODE hunk: no live instructions in the unit
            part = res['live'].get(sym)
            isdata = part is not None and not any(line_info(c)[1] for _l, c in part)
        if isdata:
            b = 'program' if sym.startswith('prg_') else 'mog'
            base = sym[4:]
            cpp_data[sym] = {'binary': b, 'label': base, 'hunk': u['section']['hunk'], 'sect': u['section']['type'],
                             'bytes': rep[b]['usize'].get(base, 0), 'cpp': sorted(fs), 'live': sym in res['live']}
    return cpp_data


def typed_bindings():
    out = {}
    p = os.path.join(ROOT, 'include', 'game', 'state_bind.hpp')
    if os.path.exists(p):
        for m in re.finditer(r'extern "C"\s+([\w:<>]+)\s+(mog_\w+)', open(p).read()):
            out[m.group(2)] = m.group(1)
    return out


def table_labels():
    out = {}
    p = os.path.join(ROOT, 'tools', 'tables.yaml')
    if os.path.exists(p):
        try:
            import yaml
            for t in yaml.safe_load(open(p))['tables']:
                out[('program' if t['binary'] == 'program' else 'mog', t['label'])] = t['name']
        except Exception:
            pass
    return out


def data_hunks(res, rep, summ, lab2key):
    """Per data hunk: live units/bytes, symbols C++ reaches, C++ modules, asm groups that read/write it."""
    U = res['units']
    acc = {}
    for lab, part in res['live'].items():
        if lab not in lab2key:
            continue
        b, key = lab2key[lab]
        g = summ[b]['routines'][key]['group']
        for _ln, code in part:
            for m in SYM_RE.finditer(code):
                t = m.group(1)
                if t in U and U[t]['section']['type'] != 'CODE':
                    tb = 'program' if t.startswith('prg_') else 'mog'
                    acc.setdefault((tb, U[t]['section']['hunk']), {}).setdefault(g, set()).add(t)
    out = {}
    for b in BINS:
        for d in summ[b]['data']:
            if d['in_code']:
                k = (b, d['hunk'])
            else:
                k = (b, d['hunk'])
            e = out.setdefault(f"{b}:S_{d['hunk']}", {'binary': b, 'hunk': d['hunk'], 'kind': d['sect'],
                                                      'in_code': d['in_code'], 'units': 0, 'bytes': 0, 'cpp_syms': 0,
                                                      'cpp': set(), 'groups': {}})
            e['units'] += 1
            e['bytes'] += d['bytes']
            if d['cpp']:
                e['cpp_syms'] += 1
                e['cpp'] |= set(d['cpp'])
    for key, e in out.items():
        e['cpp'] = sorted(e['cpp'])
        e['groups'] = {g: len(v) for g, v in sorted(acc.get((e['binary'], e['hunk']), {}).items(),
                                                    key=lambda kv: -len(kv[1]))}
    return out


def aggregate(summ):
    groups = {}
    for b in BINS:
        for e in summ[b]['routines'].values():
            g = groups.setdefault(e['group'], {'id': e['group'], 'binary': b, 'routines': [], 'instr': 0, 'bytes': 0,
                                               'cpp': set(), 'addr_only': 0, 'addr_only_instr': 0, 'deps': {},
                                               'patches': set(), 'hardware': set(), 'embedded': 0})
            g['routines'].append(e)
            g['instr'] += e['instr']
            g['bytes'] += e['bytes']
            g['cpp'] |= set(e['cpp'])
            g['patches'] |= set(e['patches'])
            g['hardware'] |= set(e['hardware'])
            g['embedded'] += e['embedded'] or 0
            if e['addr_only']:
                g['addr_only'] += 1
                g['addr_only_instr'] += e['instr']
    # dependencies group -> group
    key2group = {}
    for b in BINS:
        for e in summ[b]['routines'].values():
            key2group[f"{b}:{e['label']}"] = e['group']
    for g in groups.values():
        for e in g['routines']:
            for c in e['callees']:
                tg = key2group.get(c)
                if tg and tg != g['id']:
                    g['deps'][tg] = g['deps'].get(tg, 0) + 1
    return groups


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--json', default=os.path.join(ROOT, 'build', 'inventory', 'asm_remaining.json'))
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--list', choices=list(BINS), help='print every live routine of a binary')
    ap.add_argument('--md', help='write the generated appendix (group tables, data tables) to this file')
    a = ap.parse_args(argv)
    res = analyse()
    rep = build_report(res)
    summ, lab2key = summarise(res, rep)
    groups = aggregate(summ)
    cpp_data = build_data(res, rep, summ)
    result = {'groups': {}, 'binaries': {}, 'cpp_data': cpp_data, 'data_hunks': data_hunks(res, rep, summ, lab2key), 'live_pieces': res['live_pieces'],
              'declared_only_roots': sorted(s for s in res['declared'] if s not in res['uses'] and s in res['units'])}
    for b in BINS:
        total = len(res['order'][b])
        live = sum(1 for l in res['order'][b] if l in res['live'])
        rs = list(summ[b]['routines'].values())
        result['binaries'][b] = {
            'units_total': total, 'units_live': live, 'routines': len(rs), 'instr': sum(e['instr'] for e in rs),
            'addr_only': sum(1 for e in rs if e['addr_only']),
            'addr_only_instr': sum(e['instr'] for e in rs if e['addr_only']),
            'data_units': len(summ[b]['data']), 'data_bytes': sum(d['bytes'] for d in summ[b]['data']),
            'routine_list': [{k: (sorted(v) if isinstance(v, set) else v) for k, v in e.items()} for e in
                             sorted(rs, key=lambda e: (e['hunk'], e['label']))],
            'data': summ[b]['data']}
    mods = {}
    for b in BINS:
        for e in summ[b]['routines'].values():
            for c in e['cpp']:
                m = os.path.basename(c)
                mods.setdefault(m, {}).setdefault(e['group'], 0)
                mods[m][e['group']] += 1
    result['modules'] = {m: dict(sorted(v.items(), key=lambda kv: -kv[1])) for m, v in sorted(mods.items())}
    for gid, g in groups.items():
        result['groups'][gid] = {'binary': g['binary'], 'routines': len(g['routines']), 'instr': g['instr'],
                                 'bytes': g['bytes'], 'cpp': sorted(g['cpp']), 'addr_only': g['addr_only'],
                                 'addr_only_instr': g['addr_only_instr'], 'deps': g['deps'],
                                 'patches': sorted(g['patches']), 'hardware': sorted(g['hardware']),
                                 'embedded': g['embedded'], 'labels': sorted(e['label'] for e in g['routines'])}
    os.makedirs(os.path.dirname(a.json), exist_ok=True)
    with open(a.json, 'w') as f:
        json.dump(result, f, indent=1)
    if not a.quiet:
        for b in BINS:
            d = result['binaries'][b]
            print(f"{b}: units {d['units_live']}/{d['units_total']} live; {d['routines']} live code routines, "
                  f"~{d['instr']} instructions ({d['addr_only']} routines / {d['addr_only_instr']} instr reached "
                  f"only by address); {d['data_units']} live data/bss units, {d['data_bytes']} bytes")
        for gid in sorted(result['groups']):
            g = result['groups'][gid]
            print(f"  {gid:<22} {g['routines']:>4} routines {g['instr']:>6} instr  addr-only {g['addr_only']:>3}  "
                  f"cpp={','.join(os.path.basename(c)[:-4] for c in g['cpp'])[:60]}")
    if a.list:
        for e in result['binaries'][a.list]['routine_list']:
            print(f"S_{e['hunk']:<3} {e['label']:<12} {e['instr']:>5} {e['group']:<20} {e['name'] or '':<26} "
                  f"{'addr-only ' if e['addr_only'] else ''}cpp={','.join(os.path.basename(c)[:-4] for c in e['cpp'])}")
    if a.md:
        write_md(a.md, result)
    return 0


def write_md(path, result):
    """Appendix: one table per group (labels, names, instr, direct C++ callers), then the DATA reached by C++."""
    lines = []
    w = lines.append
    typed = typed_bindings()
    tabs = table_labels()
    w('### Group summary (generated by `py tools/asm_remaining.py --md`)\n')
    w('| group | area | routines | ~instr | addr-only | direct C++ callers | calls into (routine edges) | patch sites | hw |')
    w('|---|---|---|---|---|---|---|---|---|')
    for gid, (area, title, task) in GROUPS.items():
        g = result['groups'].get(gid)
        if not g:
            continue
        deps = ', '.join(f"{k.split('.', 1)[1]} {n}" for k, n in sorted(g['deps'].items(), key=lambda kv: -kv[1])[:4])
        mods = ', '.join(sorted({os.path.basename(c)[:-4] for c in g['cpp']}))
        enh = sum(1 for x in g['patches'] if x.startswith('enh_'))
        pat = f"{len(g['patches'])}" + (f" ({enh} enh)" if enh else '')
        w(f"| `{gid}` | {area} | {g['routines']} | {g['instr']} | {g['addr_only']} | {mods} | {deps} | {pat} | "
          f"{','.join(g['hardware'])} |")
    w('')
    w('### Totals per proposed task\n')
    w('| task | batch | what | groups | routines | ~instr | indirect-only routines |')
    w('|---|---|---|---|---|---|---|')
    for letter, (what, batch) in TASKS.items():
        gs = [g for g, t in TASK_OF.items() if t == letter and g in result['groups']]
        rs = sum(result['groups'][g]['routines'] for g in gs)
        ins = sum(result['groups'][g]['instr'] for g in gs)
        ao = sum(result['groups'][g]['addr_only'] for g in gs)
        w(f"| 7.1{letter} | {batch} | {what} | {', '.join(g for g in gs)} | {rs} | {ins} | {ao} |")
    w('')
    for b in BINS:
        bl = result['binaries'][b]
        w(f"### Live routines, {b}\n")
        by_group = {}
        for e in bl['routine_list']:
            by_group.setdefault(e['group'], []).append(e)
        for gid in sorted(by_group, key=lambda g: (list(GROUPS).index(g) if g in GROUPS else 99)):
            es = by_group[gid]
            area, title, task = GROUPS[gid]
            w(f"#### `{gid}` - {title}\n")
            w(f"{len(es)} routines, ~{sum(e['instr'] for e in es)} instructions. Roadmap: {task}.\n")
            cells = []
            for e in es:
                tag = e['label'][4:] if e['label'].startswith('LAB_') else e['label']
                if e['addr_only']:
                    tag += '*'
                if e['cpp']:
                    tag += '^'
                cells.append(f"{tag}({e['instr']})")
            w(' '.join(cells) + '\n')
        w('')
    w('### DATA/BSS reached from C++ by prg_/mog_ symbol\n')
    cd = result['cpp_data']
    by_h = {}
    for sym, d in cd.items():
        by_h.setdefault((d['binary'], d['hunk'], d['sect']), []).append((sym, d))
    w('| binary | hunk | kind | symbols | bytes | C++ modules |')
    w('|---|---|---|---|---|---|')
    for (b, h, k), lst in sorted(by_h.items()):
        mods = sorted({os.path.basename(c)[:-4] for _s, d in lst for c in d['cpp']})
        w(f"| {b} | S_{h} | {k} | {len(lst)} | {sum(d['bytes'] for _s, d in lst)} | {', '.join(mods)} |")
    w('')
    w(f"Typed in `include/game/state_bind.hpp`: {len(typed)} symbols; curated gen_tables tables: {len(tabs)}.\n")
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(lines))


if __name__ == '__main__':
    sys.exit(main())
