#!/usr/bin/env python3
"""thunks.py -- swap lifted C++ bodies in for asm routines behind generated thunks (ROADMAP 3.2a-3.2d).

3.2a  contracts
  Every routine in the swap list needs a register contract.  For an ordinary routine it is
  derived from build/inventory/routines.json:  regs_in = reads_before_write, regs_out = writes,
  ccr_out = false, sp_delta = 0.  A routine with any of `approx` (unknown/indirect/library
  callees: the register sets are not guaranteed complete), `uses_sp_tricks`, `ccr_live_out` or
  `self_modifying` is REFUSED unless tools/contracts.yaml carries a hand-written contract:

      <binary>:
        <LABEL>:
          regs_in:  [a2]            # registers the routine reads (documentation + test input)
          regs_out: [d0, a1, a2]    # registers the thunk writes back (all others keep entry value)
          ccr_out:  false           # true: the CCR the lifted body leaves is returned to the caller
          sp_delta: 4               # bytes popped beyond the own return address (default 0)
          reason:   "why this contract is right"   # mandatory

  A hand contract also replaces the derived one for a routine that would pass automatically.
  contracts.yaml is validated here (tools/routines.py owns symbols.yaml and is not touched).
  A routine without a lifted body (src/lifted/<bin>/*.cpp defining `lab_[<bin>_]<LABEL>`) is
  refused as well.

3.2b  thunk
  Swap list (tools/swap/*.txt, CMake -DMS_SWAP_LIST=<file>): one `<binary>:<LABEL>` per line.
  tools/resource.py --swap-list renames the original entry label to `<prefix><LABEL>__asm` (the
  body stays assembled, so any routine goes back to asm by dropping it from the list) and defines
  `<prefix><LABEL>` as this thunk, appended to the *same section* (so 16-bit PC-relative
  BSR.W/BRA.W/LEA(PC) reach it like the original; refs that cannot are left on the asm body):

      lea   -FRAME(sp),sp                 ; FRAME = MARGIN + 72; flags untouched
      movem.l d0-d7/a0-a6,(sp)            ; ThunkFrame.reg   (include/ms/thunk_run.hpp)
      move.w ccr,d0 / move.w d0,64(sp)    ; entry CCR
      lea FRAME+4(sp),a0 / move.l a0,60(sp)   ; vsp = SP after the return-address pop
      move.l FRAME(sp),68(sp)             ; return address copy
      push delta, mask, fn, &frame ; jsr ms_thunk_run     ; src/rt/thunks.cpp -> thunkRun()
      move.l 68(sp),FRAME(sp)             ; restore the return slot the lifted code may have used
      movem.l (sp),d0-d7/a0-a6 ; move.w 64(sp),ccr ; lea FRAME+delta(sp),sp ; rts

  The lifted code believes it runs on the game stack: the MARGIN bytes between the frame and the
  caller's SP are *its* stack (return-address slots at a[7]-4, pushes), and nothing else lives
  there, so an interrupt (it pushes below the real SP) cannot touch them.  The Regs are on the
  stack, not static, so recursion and nesting through asm are safe.  The thunk reloads all
  registers from the frame, so a register outside regs_out keeps its entry value whatever the
  lifted code did to it.

3.2c  asmCall -- src/rt/thunks.cpp (ms_call_asm) + include/ms/linked.hpp.

Usage
  py tools/thunks.py --list [--bins mog program]     contract status of every lifted routine
  py tools/thunks.py --check <swapfile>              validate a swap list (exit 1 on refusal)
  py tools/thunks.py --gen-pure [--out tools/swap/pure.txt]   every replay-PASS pure routine
  py tools/thunks.py --gen-static                    src/rt/gen/hunk_tab.cpp (MS_HUNK table)
  py tools/thunks.py --thunk mog LAB_03CA            print the thunk asm of one routine
"""
import argparse, glob, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

INVENTORY = os.path.join(ROOT, 'build', 'inventory', 'routines.json')
CONTRACTS = os.path.join(ROOT, 'tools', 'contracts.yaml')
LIFTED = os.path.join(ROOT, 'src', 'lifted')
RUN_LOG = os.path.join(ROOT, 'build', 'run_host.log')
SWAP_DIR = os.path.join(ROOT, 'tools', 'swap')
HUNK_TAB = os.path.join(ROOT, 'src', 'rt', 'gen', 'hunk_tab.cpp')

BINARIES = ('mog', 'program')
PREFIX = {'program': 'prg_', 'mog': 'mog_'}
REGS = [f'd{i}' for i in range(8)] + [f'a{i}' for i in range(7)]
NL = chr(10)

# thunk layout (kept in step with include/ms/thunk_run.hpp by static_asserts there)
MARGIN = 1024                  # bytes of "virtual stack" between the caller's SP and the frame
F_SIZE, F_VSP, F_CCR, F_RET = 72, 60, 64, 68
FRAME = MARGIN + F_SIZE
THUNK_SPACE = 112              # upper bound of one thunk's code size, used for PC16 range checks
PC16_LIMIT = 30000
CONTRACT_KEYS = {'regs_in', 'regs_out', 'ccr_out', 'sp_delta', 'reason'}
DEFN = re.compile(r'extern\s+"C"\s+void\s+(lab_\w+)\s*\(')


class Refused(Exception):
    pass


# ---------------------------------------------------------------------------- inputs
def load_routines():
    if not os.path.exists(INVENTORY):
        raise SystemExit(f'{INVENTORY} missing: run `py tools/reassemble.py && py tools/callgraph.py && py tools/routines.py`')
    with open(INVENTORY, encoding='utf-8') as f:
        d = json.load(f)
    return {b: {r['label']: r for r in d['binaries'][b]['routines']} for b in BINARIES}


def fn_name(binary, label):
    """C++ symbol of the lifted body (tools/lift.py naming)."""
    return f'lab_{label}' if binary == 'mog' else f'lab_{binary}_{label}'


def lifted_functions():
    """{(binary, label): path} for every lifted body under src/lifted/<binary>."""
    out = {}
    for b in BINARIES:
        for p in glob.glob(os.path.join(LIFTED, b, '*.cpp')):
            with open(p, encoding='utf-8') as f:
                text = f.read()
            for m in DEFN.finditer(text):
                name = m.group(1)
                pre = 'lab_' if b == 'mog' else f'lab_{b}_'
                if name.startswith(pre):
                    out[(b, name[len(pre):])] = p
    return out


def replay_passes():
    """{(binary, label)} that PASSed the host replay (build/run_host.log)."""
    out = set()
    if os.path.exists(RUN_LOG):
        with open(RUN_LOG, encoding='utf-8', errors='replace') as f:
            for l in f:
                m = re.match(r'PASS (\w+)/(\S+?):', l)
                if m:
                    b, lab = m.group(1), m.group(2)
                    if lab.startswith(b + '_'):
                        lab = lab[len(b) + 1:]
                    out.add((b, lab))
    return out


# ---------------------------------------------------------------------------- contracts
def load_contracts(path=None, routines=None):
    """tools/contracts.yaml -> {(binary, label): dict}; raises ValueError listing every problem."""
    path = path or CONTRACTS
    if not os.path.exists(path):
        return {}
    try:
        import yaml
    except ImportError:
        raise SystemExit('PyYAML missing: py -m pip install pyyaml')
    with open(path, encoding='utf-8') as f:
        doc = yaml.safe_load(f) or {}
    errs, out = [], {}
    if not isinstance(doc, dict):
        raise ValueError('contracts.yaml: top level must be a mapping binary -> label -> contract')
    for b, labels in doc.items():
        if b not in BINARIES:
            errs.append(f'{b}: unknown binary (expected one of {BINARIES})')
            continue
        for lab, c in (labels or {}).items():
            where = f'{b}/{lab}'
            if routines is not None and lab not in routines[b]:
                errs.append(f'{where}: not a routine entry in routines.json')
            if not isinstance(c, dict):
                errs.append(f'{where}: must be a mapping')
                continue
            extra = set(c) - CONTRACT_KEYS
            if extra:
                errs.append(f'{where}: unknown keys {sorted(extra)}')
            for k in ('regs_in', 'regs_out', 'ccr_out', 'reason'):
                if k not in c:
                    errs.append(f'{where}: missing `{k}`')
            for k in ('regs_in', 'regs_out'):
                v = c.get(k)
                if v is not None and (not isinstance(v, list) or any(r not in REGS for r in v)):
                    errs.append(f'{where}: `{k}` must be a list of {REGS[0]}..{REGS[-1]} (got {v!r})')
            if 'ccr_out' in c and not isinstance(c['ccr_out'], bool):
                errs.append(f'{where}: `ccr_out` must be true/false')
            sd = c.get('sp_delta', 0)
            if not isinstance(sd, int) or isinstance(sd, bool) or sd < 0 or sd > 64 or sd % 2:
                errs.append(f'{where}: `sp_delta` must be an even integer 0..64')
            if 'reason' in c and not (isinstance(c['reason'], str) and c['reason'].strip()):
                errs.append(f'{where}: `reason` must say why the contract is right')
            out[(b, lab)] = c
    if errs:
        raise ValueError('contracts.yaml invalid:' + NL + '  ' + (NL + '  ').join(errs))
    return out


class Contract:
    def __init__(self, regs_in, regs_out, ccr_out, sp_delta, origin):
        self.regs_in, self.regs_out = sorted(set(regs_in), key=REGS.index), sorted(set(regs_out), key=REGS.index)
        self.ccr_out, self.sp_delta, self.origin = bool(ccr_out), int(sp_delta), origin

    @property
    def out_mask(self):
        m = sum(1 << REGS.index(r) for r in self.regs_out)
        return m | (1 << 15 if self.ccr_out else 0)

    def __repr__(self):
        return (f'Contract({self.origin}: in={self.regs_in} out={self.regs_out} ccr_out={self.ccr_out} '
                f'sp_delta={self.sp_delta})')


def needs_hand_contract(r):
    """Reasons the derived register sets cannot be trusted (empty list = automatic contract is safe)."""
    why = []
    if r.get('approx'):
        why.append('approx (unknown/indirect/library call: register sets may be incomplete)')
    if r.get('uses_sp_tricks'):
        why.append('uses_sp_tricks (stack not balanced the usual way)')
    if r.get('ccr_live_out'):
        why.append('ccr_live_out (a caller tests the flags after the call)')
    if r.get('self_modifying'):
        why.append('self_modifying')
    return why


def contract_for(binary, label, routines, hand, lifted=None):
    """Contract of one routine or Refused.  `lifted` = lifted_functions() (None skips that check)."""
    r = routines[binary].get(label)
    if r is None:
        raise Refused(f'{binary}/{label}: not a routine entry in routines.json')
    if lifted is not None and (binary, label) not in lifted:
        raise Refused(f'{binary}/{label}: no lifted body (src/lifted/{binary} defines no {fn_name(binary, label)})')
    if r.get('region_class') not in ('code', 'code-chip'):
        raise Refused(f'{binary}/{label}: region class {r.get("region_class")!r} is not code')
    h = hand.get((binary, label))
    if h is not None:
        return Contract(h['regs_in'], h['regs_out'], h['ccr_out'], h.get('sp_delta', 0), 'hand')
    why = needs_hand_contract(r)
    if why:
        raise Refused(f'{binary}/{label}: needs a hand-written contract in tools/contracts.yaml: ' + '; '.join(why))
    return Contract(r['reads_before_write'], r['writes'], False, 0, 'auto')


# ---------------------------------------------------------------------------- swap list
def parse_swap_list(path):
    """[(binary, label)] from a swap file; `#` comments, `<binary>:<LABEL>` per line."""
    out, seen = [], set()
    with open(path, encoding='utf-8') as f:
        for n, l in enumerate(f, 1):
            l = l.split('#', 1)[0].strip()
            if not l:
                continue
            m = re.fullmatch(r'(\w+)\s*[:\s]\s*(LAB_[0-9A-Fa-f]+|SECSTRT_\d+)', l, re.I)
            if not m or m.group(1) not in BINARIES:
                raise Refused(f'{path}:{n}: expected `<{"|".join(BINARIES)}>:LAB_xxxx`, got {l!r}')
            lab = m.group(2)
            key = (m.group(1), 'SECSTRT' + lab[7:] if lab.upper().startswith('SECSTRT') else 'LAB_' + lab[4:].upper())
            if key in seen:
                raise Refused(f'{path}:{n}: {key[0]}:{key[1]} listed twice')
            seen.add(key)
            out.append(key)
    return out


def build_swap(path, routines=None, hand=None, lifted=None):
    """Validated swap: {binary: {label: Contract}}; raises Refused listing every refused routine."""
    routines = routines or load_routines()
    hand = load_contracts(routines=routines) if hand is None else hand
    lifted = lifted if lifted is not None else lifted_functions()
    entries = parse_swap_list(path) if path else []
    out, errs = {b: {} for b in BINARIES}, []
    for b, lab in entries:
        try:
            out[b][lab] = contract_for(b, lab, routines, hand, lifted)
        except Refused as e:
            errs.append(str(e))
    if errs:
        raise Refused('swap list refused:' + NL + '  ' + (NL + '  ').join(errs))
    return out


# ---------------------------------------------------------------------------- thunk asm
def thunk_lines(prefix, binary, label, contract, aliases=()):
    """vasm (Motorola) source of one thunk; `aliases` get extra labels at the same address."""
    sym = prefix + label
    mask = contract.out_mask
    L = []
    for a in aliases:
        L.append(f'{prefix}{a}:')
    L += [f'{sym}:',
          f'\tlea\t-{FRAME}(sp),sp',
          '\tmovem.l\td0-d7/a0-a6,(sp)',
          '\tmove.w\tccr,d0',
          f'\tmove.w\td0,{F_CCR}(sp)',
          f'\tlea\t{FRAME + 4}(sp),a0',
          f'\tmove.l\ta0,{F_VSP}(sp)',
          f'\tmove.l\t{FRAME}(sp),{F_RET}(sp)',
          f'\tmove.l\t#{contract.sp_delta},-(sp)',
          f'\tmove.l\t#${mask:04X},-(sp)',
          f'\tpea\t{fn_name(binary, label)}',
          '\tpea\t12(sp)',
          '\tjsr\tms_thunk_run',
          '\tlea\t16(sp),sp',
          f'\tmove.l\t{F_RET}(sp),{FRAME}(sp)',
          '\tmovem.l\t(sp),d0-d7/a0-a6',
          f'\tmove.w\t{F_CCR}(sp),ccr',
          f'\tlea\t{FRAME + contract.sp_delta}(sp),sp',
          '\trts']
    return L


# ---------------------------------------------------------------------------- reference classification
BRANCH_RE = re.compile(r'^(?:BRA|BSR|B(?:HI|LS|CC|CS|NE|EQ|VC|VS|PL|MI|GE|LT|GT|LE|HS|LO))$')
LEADING_LABEL = re.compile(r'^\s*(?:\w+:)?\s*')


class SwapPlan:
    """Per-binary view of a validated swap used by tools/resource.py Gen (swap=...)."""

    def __init__(self, binary, contracts, routines, sections, hunk_sizes):
        """contracts {label: Contract}; routines = routines.json rows of this binary; sections =
        resource.image_sections(); hunk_sizes = original hunk sizes."""
        self.binary, self.prefix = binary, PREFIX[binary]
        self.contracts = contracts
        self.rows = {l: routines[l] for l in contracts}
        self.sections = {n: (kind, chip) for n, kind, chip in sections}
        self.hunk_sizes = hunk_sizes
        self.defs = {}                 # defined label (entry or alias) -> entry label
        for lab, r in self.rows.items():
            for a in r['aliases']:
                self.defs[a] = lab
            self.defs[lab] = lab
        self.slot = {}                 # entry label -> index in its hunk's thunk area
        per = {}
        for lab in sorted(self.rows, key=lambda l: (self.rows[l]['hunk'], self.rows[l]['start'])):
            h = self.rows[lab]['hunk']
            self.slot[lab] = per.get(h, 0)
            per[h] = per.get(h, 0) + 1
        self.stats = {l: {'thunk': 0, 'asm': 0, 'why': {}} for l in self.rows}

    # -- label definition lines
    def renamed_def(self, label):
        """New text of `LAB_x:` for a swapped entry/alias label (None = unchanged)."""
        return f'{self.prefix}{label}__asm' if label in self.defs else None

    def xdefs(self):
        return [f'{self.prefix}{l}__asm' for l in sorted(self.defs)]

    # -- references
    def thunk_pos(self, label):
        r = self.rows[self.defs[label]]
        return r['hunk'], self.hunk_sizes[r['hunk']] + THUNK_SPACE * self.slot[self.defs[label]]

    def classify(self, text, start, end, caller):
        """'' (= thunk) or '__asm' for the reference text[start:end] of one source line.
        caller = (hunk, offset) of the line or None."""
        code = text.split(';', 1)[0]
        tail = code[end:]
        head = code[:start]
        ent = self.defs.get(text[start:end])
        if ent is None:
            return ''
        if re.match(r'\s*[-+*/]', tail) or re.search(r'[-+*/]\s*$', head):
            return self._left(ent, 'label arithmetic (offset into the body or a difference)')
        m = re.match(r'\s*(?:\w+:)?\s*([A-Za-z]+)(?:\.([A-Za-z]))?\s*(.*)$', code)
        if not m:
            return self._left(ent, 'unparsed line')
        mn, size, ops = m.group(1).upper(), (m.group(2) or '').upper(), m.group(3)
        pc = bool(re.match(r'\s*\(\s*PC\s*\)', tail, re.I))
        if mn in ('JSR', 'JMP'):
            ok = size in ('', 'L') or (size == 'W' and pc)
            if not re.fullmatch(r'\w+(\(PC\))?', ops.strip(), re.I) or not ok:
                return self._left(ent, f'{mn} operand form')
            return self._range(ent, caller, pc)
        if BRANCH_RE.match(mn):
            if size not in ('W', 'L'):
                return self._left(ent, f'{mn}.{size or "?"} cannot reach the thunk (8-bit displacement)')
            return self._range(ent, caller, True)
        if mn == 'DC' and size == 'L':
            return self._count(ent)
        if mn in ('LEA', 'PEA') and re.match(r'\s*(\(PC\))?\s*(,|$)', tail, re.I):
            return self._range(ent, caller, pc)
        if mn in ('MOVE', 'MOVEA', 'CMPI', 'CMPA', 'CMP') and size in ('L', '') and head.rstrip().endswith('#'):
            return self._count(ent)
        return self._left(ent, f'{mn} operand is not a code pointer use')

    def _count(self, ent):
        self.stats[ent]['thunk'] += 1
        return ''

    def _left(self, ent, why):
        s = self.stats[ent]
        s['asm'] += 1
        s['why'][why] = s['why'].get(why, 0) + 1
        return '__asm'

    def _range(self, ent, caller, pc_relative):
        if not pc_relative:
            return self._count(ent)
        if caller is None:
            return self._left(ent, 'PC-relative reference from generated text')
        th, to = self.thunk_pos(ent)
        if caller[0] != th or abs(to - caller[1]) > PC16_LIMIT:
            return self._left(ent, 'PC16 reference too far from the thunk')
        return self._count(ent)

    # -- appended thunk areas
    def thunk_sections(self):
        """Source text appended after the body: one re-opened section per hunk with thunks."""
        out = []
        for h in sorted({r['hunk'] for r in self.rows.values()}):
            kind, chip = self.sections[h]
            if kind != 'CODE':
                raise Refused(f'{self.binary}: hunk {h} is {kind}, not CODE')
            out += ['', f'; ---- swapped routines of S_{h}: thunks (tools/thunks.py)',
                    f'\tSECTION\t{self.prefix}S_{h}{".MEMF_CHIP" if chip else ""},CODE']
            labs = sorted((l for l in self.rows if self.rows[l]['hunk'] == h), key=lambda l: self.slot[l])
            for lab in labs:
                al = [a for a in self.rows[lab]['aliases'] if a != lab]
                out += ['\tCNOP\t0,2'] + thunk_lines(self.prefix, self.binary, lab, self.contracts[lab], al)
        return out

    def report(self):
        rows = []
        for lab in sorted(self.stats):
            s = self.stats[lab]
            why = '; '.join(f'{n}x {w}' for w, n in sorted(s['why'].items()))
            rows.append(f'  {self.binary}:{lab:<9} {self.contracts[lab].origin:<4} '
                        f'refs -> thunk {s["thunk"]:>3}, left on asm body {s["asm"]:>3}' + (f'  ({why})' if why else ''))
        return rows


# ---------------------------------------------------------------------------- static generated files
HUNK_TAB_TMPL = """// GENERATED by tools/thunks.py --gen-static -- do not edit.
// Section start addresses behind MS_HUNK(<bin>, hh) of include/ms/linked.hpp (lifted code needs
// hunk-relative return/call addresses; the linked section is `<prefix>S_<hunk>`).
// Constant-initialised pointer tables: global constructors do not run on this target.
#include "ms/linked.hpp"

extern "C" {{
{decls}
{tables}
}}
"""


def gen_hunk_tab():
    import importlib.util   # not `import resource`: that name is a stdlib module on Unix
    spec = importlib.util.spec_from_file_location('ms_resource', os.path.join(ROOT, 'tools', 'resource.py'))
    res = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(res)
    decls, tabs = [], []
    for b in BINARIES:
        nums = [n for n, _, _ in res.image_sections(b)]
        assert nums == list(range(len(nums))), f'{b}: sections are not S_0..S_n in order'
        decls += [f'extern unsigned char {PREFIX[b]}S_{n}_beg[];' for n in nums]
        rows = ',\n'.join(f'\t{PREFIX[b]}S_{n}_beg' for n in nums)
        tabs.append(f'unsigned char *const ms_hunk_{b}[] = {{\n{rows}\n}};')
    return HUNK_TAB_TMPL.format(decls=NL.join(decls), tables=NL.join(tabs))


def write_static():
    text = gen_hunk_tab()
    os.makedirs(os.path.dirname(HUNK_TAB), exist_ok=True)
    with open(HUNK_TAB, 'w', newline='\n') as f:
        f.write(text)
    print(f'{os.path.relpath(HUNK_TAB, ROOT)}: section table written')


# ---------------------------------------------------------------------------- CLI
def pure_candidates(routines, hand, lifted, passes):
    out = []
    for (b, lab) in sorted(lifted):
        r = routines[b].get(lab)
        if not r or not r.get('pure') or (b, lab) not in passes:
            continue
        try:
            contract_for(b, lab, routines, hand, lifted)
        except Refused:
            continue
        out.append((b, lab))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--check', metavar='SWAPFILE')
    ap.add_argument('--gen-pure', action='store_true')
    ap.add_argument('--out', default=os.path.join(SWAP_DIR, 'pure.txt'))
    ap.add_argument('--gen-static', action='store_true')
    ap.add_argument('--thunk', nargs=2, metavar=('BIN', 'LABEL'))
    a = ap.parse_args()
    routines = load_routines()
    try:
        hand = load_contracts(routines=routines)
    except ValueError as e:
        print(e)
        return 1
    lifted = lifted_functions()
    if a.gen_static:
        write_static()
        return 0
    if a.check:
        try:
            sw = build_swap(a.check, routines, hand, lifted)
        except Refused as e:
            print(e)
            return 1
        print(f'{a.check}: ' + ', '.join(f'{b} {len(v)}' for b, v in sw.items()) + ' routines swappable')
        return 0
    if a.thunk:
        b, lab = a.thunk
        c = contract_for(b, lab, routines, hand, lifted)
        print(NL.join(thunk_lines(PREFIX[b], b, lab, c)))
        return 0
    if a.gen_pure:
        cand = pure_candidates(routines, hand, lifted, replay_passes())
        with open(a.out, 'w', newline='\n') as f:
            f.write('# every pure, replay-PASS routine with a lifted body and a safe contract\n'
                    '# generated: py tools/thunks.py --gen-pure (needs build/run_host.log); ROADMAP 3.2d gate (iii)\n')
            for b, lab in cand:
                f.write(f'{b}:{lab}\n')
        print(f'{a.out}: {len(cand)} routines')
        return 0
    # --list (default)
    passes = replay_passes()
    tot = {'auto': 0, 'hand': 0, 'refused': 0}
    for (b, lab) in sorted(lifted):
        try:
            c = contract_for(b, lab, routines, hand, lifted)
            tot[c.origin] += 1
            print(f'{b}:{lab:<9} {c.origin:<4} {"PASS" if (b, lab) in passes else "    "} in={",".join(c.regs_in)} '
                  f'out={",".join(c.regs_out)} ccr_out={c.ccr_out} sp_delta={c.sp_delta}')
        except Refused as e:
            tot['refused'] += 1
            print(f'{b}:{lab:<9} REFUSED {e}')
    print(f'{len(lifted)} lifted routines: {tot["auto"]} automatic contracts, {tot["hand"]} hand contracts, '
          f'{tot["refused"]} refused')
    return 0


if __name__ == '__main__':
    sys.exit(main())
