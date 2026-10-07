"""Tests for tools/gen_data.py (ROADMAP 7.1n1, 7.1n2): the C++-owned DATA/BSS hunks.

The generated build/gen/owned_data.cpp is compiled with m68k-amiga-elf-g++ (as the game build does) and the object is
compared with the ORIGINAL binaries (reference/moonshard/.../amiga_asm/{program,mog}, parsed by moghunks), which share
no code with the generator:
  * every owned hunk: object bytes == original hunk bytes (BSS: zero), size == original hunk size;
  * every pointer cell: the object has a relocation at the same offset whose symbol+addend is the label the original
    RELOC32 points at (label map = build/reasm/<bin>.symbols.json, vasm's listing);
  * every label of the hunk: an alias symbol in the object at object-start + label offset;
  * a deliberate mutation of one byte is detected.
7.1n2 (mog S_4, typed members + strings): the plain line patches that lie inside an owned hunk (mog.fight_ops.json turns
script operands `DC.L LAB_02E6` into `DC.L $F00002E6`) are applied to the generated data, so exactly those cells differ from
the original hunk: the object holds the patch value and no relocation there (the expected set is derived from the patch
tables and the original listing, not from the generator).  The typed regions of tools/data_types.yaml / the char arrays are
checked in the generated header and by the same byte/reloc/alias comparison.
Also: resource.py leaves the owned hunks out of the elf asm (no definitions, XREF instead of XDEF), the verify build keeps
them, the generated overlay-reset tables carry the original hunk sizes, and the twin / share rules refuse bad configs.
ROADMAP 7.1r: the code-hunk cells (`extern_cells`, asm/patches/<bin>.data_cells.json) are compared with the original binary the same way
(CodeCells), and a pointer cell naming a label the asm used to define must hold the C++ routine of `link_names` (or nullptr for `asm_labels`).
Skips without the m68k toolchain, the reassembly output or the original binaries."""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_data  # noqa: E402
import reassemble as R  # noqa: E402
import resource  # noqa: E402
import origin  # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: the asm-reference tests skip in a public checkout)

TOOLCHAIN = os.path.join(ROOT, 'tools', 'toolchain')
# libc-free headers of ACE (stdint.h for game/state.hpp): the sibling checkout or, in a worktree, the main one
ACE = next((c for c in (os.path.join(ROOT, 'ace', 'include'), os.path.join(os.path.dirname(ROOT), 'ace', 'include'),
                        os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(ROOT))), 'ace', 'include'))
            if os.path.isdir(c)), os.path.join(os.path.dirname(ROOT), 'ace', 'include'))
GPP = shutil.which('m68k-amiga-elf-g++') or next(
    (p for p in (os.path.join(TOOLCHAIN, 'opt', 'bin', 'm68k-amiga-elf-g++.exe'),) if os.path.exists(p)), None)
HAVE_ORIG = origin.have_binaries() and \
    all(os.path.exists(os.path.join(ROOT, 'build', 'reasm', n + '.symbols.json')) for n in ('program', 'mog'))
PREFIX = gen_data.PREFIX
import cellnames  # noqa: E402  (ROADMAP 7.1s)
CELL_NAMES = cellnames.load()
LABEL_OF_NAME = {b: {n: lab for (bb, lab), n in CELL_NAMES.items() if bb == b} for b in ('program', 'mog')}
REFERENCED = gen_data.referenced_aliases()      # the label aliases the sources (and asm/synth.s) still spell
# ROADMAP 10.2a: these tests check the COMPILED-IN form (gen_data --full: the facts + the original executables); the default
# generation holds no original value and the game fills it at start-up (tests/test_origload.py proves blank + fill == full).
_ORIG_DATA = {b: origin.read_binary(b) for b in ('program', 'mog')} if origin.have_binaries() else None
_generate = gen_data.generate


def _full_generate(*args, **kw):
    patch_dir = args[0] if args else kw.get('patch_dir')
    if patch_dir and origin.have_listing() and 'source' not in kw:
        # a temporary patch dir (the Config tests) only means something to the listing generation: the facts carry the line
        # patches of asm/patches themselves (tools/origfacts.py)
        kw['source'] = 'listing'
    if _ORIG_DATA and kw.get('data') is None and kw.get('source') != 'listing':
        kw['data'] = _ORIG_DATA
    return _generate(*args, **kw)


_full_generate.__wrapped__ = _generate     # tests/test_origload.py calls the plain generator (same process under discover)
gen_data.generate = _full_generate
gen_data.ALL_ALIASES = True     # the comparison tests below check EVERY label of every hunk; the default (pruned) output has its own test


def alias_names(binary, lab):
    """The alias symbols the generated file must define for a label: its C++ name, and the label alias when something still spells it."""
    pre = PREFIX[binary]
    out = []
    if (binary, lab) in CELL_NAMES:
        out.append(CELL_NAMES[(binary, lab)])
    if gen_data.ALL_ALIASES or pre + lab in REFERENCED:
        out.append(pre + lab)
    return out
_CELLS, LINKS, _ASM_LABELS = gen_data.load_cells()       # LINKS: {binary: {label: C++ symbol}} (link_names)
POS_LABELS = {}                                          # {binary: {(hunk, offset): label}} of the reassembly listing


def _pos_labels():
    for b in ('program', 'mog'):
        p = os.path.join(ROOT, 'build', 'reasm', b + '.symbols.json')
        POS_LABELS[b] = {}
        if os.path.exists(p):
            with open(p, encoding='utf-8') as f:
                for lab, i in json.load(f).items():
                    POS_LABELS[b].setdefault((i['hunk'], i['offset']), []).append(lab)


_pos_labels()


# ---- a minimal ELF32 big-endian reader (independent of the generator) ------------------------------------------------
class Elf:
    def __init__(self, data):
        self.d = data
        assert data[:4] == b'\x7fELF' and data[5] == 2, 'not a big-endian ELF'
        shoff, = struct.unpack('>I', data[0x20:0x24])
        shentsize, shnum, shstrndx = struct.unpack('>HHH', data[0x2E:0x34])
        self.secs = []
        for i in range(shnum):
            name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack(
                '>IIIIIIIIII', data[shoff + i * shentsize: shoff + (i + 1) * shentsize])
            self.secs.append(dict(name=name, type=typ, flags=flags, off=off, size=size, link=link, info=info, entsize=entsize))
        stroff = self.secs[shstrndx]['off']
        for s in self.secs:
            s['name'] = self.cstr(stroff + s['name'])
        symtab = next(s for s in self.secs if s['type'] == 2)
        strtab = self.secs[symtab['link']]
        self.syms = {}         # name -> (shndx, value)
        self.symlist = []
        for k in range(symtab['size'] // 16):
            nm, val, sz, info, other, shndx = struct.unpack('>IIIBBH', data[symtab['off'] + k * 16: symtab['off'] + k * 16 + 16])
            name = self.cstr(strtab['off'] + nm)
            self.symlist.append((name, shndx, val, sz))
            if name:
                self.syms[name] = (shndx, val, sz)
        self.relocs = {}       # section index -> {offset: (symname, addend)}
        for s in self.secs:
            if s['type'] == 4:                       # SHT_RELA
                tgt = s['info']
                for k in range(s['size'] // 12):
                    o, info, add = struct.unpack('>IIi', data[s['off'] + k * 12: s['off'] + k * 12 + 12])
                    self.relocs.setdefault(tgt, {})[o] = (self.symlist[info >> 8][0], add, info & 0xFF, self.symlist[info >> 8][1])

    def cstr(self, off):
        end = self.d.index(b'\0', off)
        return self.d[off:end].decode('ascii')

    def bytes_of(self, shndx, off, size):
        s = self.secs[shndx]
        if s['type'] == 8:                           # NOBITS
            return bytes(size)
        return self.d[s['off'] + off: s['off'] + off + size]


def load_original(binary):
    with open(origin.binary_path(binary), 'rb') as f:
        orig = R.parse_hunk_file(f.read())
    with open(os.path.join(ROOT, 'build', 'reasm', binary + '.symbols.json'), encoding='utf-8') as f:
        syms = json.load(f)
    return orig, syms


def compile_object(out_dir, cpp):
    obj = os.path.join(out_dir, 'owned_data.o')
    env = dict(os.environ, PATH=os.path.dirname(GPP) + os.pathsep + os.environ.get('PATH', ''))
    r = subprocess.run([GPP, '-m68020', '-msoft-float', '-std=c++17', '-fno-exceptions', '-fno-rtti', '-fno-lto',
                        '-nostdlib', '-O2', '-Wall', '-Werror', '-c', '-I', out_dir, '-I', os.path.join(ROOT, 'include'), '-I', os.path.join(ROOT, 'src'), '-I', os.path.join(ACE, 'mini_std'), '-I', ACE, cpp, '-o', obj],
                       capture_output=True, text=True, env=env)
    if r.returncode:
        raise AssertionError('compile failed:\n' + r.stdout + r.stderr)
    return obj


def patched_cells(binary, owned):
    """{hunk: {offset: (width, value)}}: the DC cells that patches inside owned hunks rewrite (independent of gen_data:
    patch tables + the vasm listing of the original).  A public checkout has neither: there the patched cells of tools/facts
    stand in (ROADMAP 10.2; tests/test_origload.py checks the facts against the listing where it exists)."""
    if not (origskip.HAVE_ASM_REF and os.path.exists(os.path.join(ROOT, 'build', 'reasm', binary + '.lst'))):
        import origfacts
        out = {}
        for h, cells in origfacts.load(binary)['patched'].items():
            if int(h) not in owned:
                continue
            for o, (w, v) in cells.items():
                out.setdefault(int(h), {})[int(o)] = (4, v[0]) if isinstance(v, list) else (w, v)
        return out
    items = resource.read_listing(binary)
    by_line = {}
    for h, o, ln in items:
        by_line.setdefault(ln, (h, o))
    out = {}
    for p in resource.load_patches(binary):
        if p.get('kind') == 'as_data':
            continue
        for k, new in enumerate(p['new']):
            ln = p['line'] + k
            if ln not in by_line or by_line[ln][0] not in owned:
                continue
            m = re.match(r'^\s*DC\.([BWL])\s+\$([0-9A-Fa-f]+)\s*(;.*)?$', new)
            x = re.match(r'^\s*DC\.L\s+(rt_\w+)\s*(;.*)?$', new)        # 7.1n4: a cell patched to an external symbol
            assert m or x, f'patch {p["id"]} inside an owned hunk is not a plain DC with a hex value or rt_ symbol: {new!r}'
            h, o = by_line[ln]
            out.setdefault(h, {})[o] = (4, x.group(1)) if x else ({'B': 1, 'W': 2, 'L': 4}[m.group(1)], int(m.group(2), 16))
    return out


def compare_object(elf, entries, originals, mutate=None):
    """-> list of differences between the compiled object and the original hunks (empty = equal)."""
    diffs = []
    for e in entries:
        binary, num = e['binary'], e['section']
        orig, symbols = originals[binary]
        h = orig.hunks[num]
        obj_name = e['object'] if not e.get('share') else next(
            x['object'] for x in entries if f"{x['binary']}:{x['section']}" == e['twin_of'])
        if obj_name not in elf.syms:
            diffs.append(f'{binary} S_{num}: object {obj_name} not in the object file')
            continue
        shndx, val, size = elf.syms[obj_name]
        want = bytes(h.data) if h.data is not None else bytes(h.size_bytes)
        got = bytearray(elf.bytes_of(shndx, val, h.size_bytes))
        if mutate is not None and mutate[0] == (binary, num):
            got[mutate[1]] ^= 0xFF
        if size != h.size_bytes:
            diffs.append(f'{binary} S_{num}: object is {size} bytes, original hunk {h.size_bytes}')
        pre = PREFIX[binary]
        relocs = elf.relocs.get(shndx, {})
        ptr_at = {g_off: (t, g_off) for g in h.reloc32 for g_off in g.offsets for t in [g.target_hunk]}
        pcells = patched_cells(binary, {x['section'] for x in entries if x['binary'] == binary}).get(num, {})
        want = bytearray(want)
        ext_cells = {}
        for poff, (pw, pv) in pcells.items():          # the patch replaces the cell (and its relocation) by a constant
            if isinstance(pv, str):                    # ... or by a reference to a global rt_* symbol: expect that relocation
                ext_cells[poff] = pv
                want[poff:poff + pw] = bytes(pw)
            else:
                want[poff:poff + pw] = pv.to_bytes(pw, 'big')
            for q in range(poff, poff + pw):
                ptr_at.pop(q, None)
        for poff, sym in ext_cells.items():
            rel = relocs.get(val + poff)
            if rel is None or rel[0] != sym or rel[1] != 0 or rel[2] != 1:
                diffs.append(f'{binary} S_{num}: cell +{poff:#x} should be a 32-bit relocation to {sym}, object has {rel}')
        for off in range(h.size_bytes):
            in_ptr = any(p <= off < p + 4 for p in ptr_at)
            if not in_ptr and want[off] != got[off]:
                diffs.append(f'{binary} S_{num}: byte +{off:#x} original {want[off]:#04x}, object {got[off]:#04x}')
        for off, (tgt_hunk, _) in ptr_at.items():
            rel = relocs.get(val + off)
            orig_off = struct.unpack('>I', want[off:off + 4])[0]
            if binary == 'mog' and tgt_hunk == 44:      # the asm synth hunk: nullptr unless MS_SYNTH_ASM (asm_labels)
                if rel is not None or any(got[off:off + 4]):
                    diffs.append(f'{binary} S_{num}: pointer at +{off:#x} into the asm synth hunk should be nullptr')
                continue
            lab_t = next((l for l in POS_LABELS[binary].get((tgt_hunk, orig_off), []) if l in LINKS[binary]), None)
            if lab_t is not None:                  # the C++ routine that took the asm routine's place
                if rel is None or rel[0] != LINKS[binary][lab_t] or rel[1] != 0 or rel[2] != 1:
                    diffs.append(f'{binary} S_{num}: pointer at +{off:#x} to {lab_t} should be a relocation to {LINKS[binary][lab_t]}, object has {rel}')
                continue
            if rel is None:
                diffs.append(f'{binary} S_{num}: no relocation at +{off:#x}')
                continue
            name, addend, typ, sym_sh = rel
            lab = name[len(pre):] if name.startswith(pre) else LABEL_OF_NAME[binary].get(name)
            ref = symbols.get(lab) if lab else None
            if not name:
                # a label inside a C++-owned hunk: the assembler turned the reference into section symbol + offset
                tgt = next((x for x in entries if x['binary'] == binary and x['section'] == tgt_hunk), None)
                tobj = tgt and (tgt['object'] if not tgt.get('share') else next(
                    x['object'] for x in entries if f"{x['binary']}:{x['section']}" == tgt['twin_of']))
                tsh, tval, _ = elf.syms[tobj] if tobj else (None, None, None)
                if typ == 1 and tobj and sym_sh == tsh and addend == tval + orig_off:
                    continue
            if typ != 1 or ref is None or ref['hunk'] != tgt_hunk or ref['offset'] + addend != orig_off:
                diffs.append(f'{binary} S_{num}: pointer at +{off:#x}: object {name}{addend:+d} (type {typ}), original '
                             f'hunk {tgt_hunk} +{orig_off:#x}')
        extra = [r for r in relocs if val <= r < val + h.size_bytes and r - val not in ptr_at and r - val not in ext_cells]
        if extra:
            diffs.append(f'{binary} S_{num}: unexpected relocations at {[hex(r - val) for r in extra][:4]}')
        # label aliases
        names = [(n, v['offset']) for k, v in symbols.items() if v['hunk'] == num for n in alias_names(binary, k)]
        for name, off in names:
            if name not in elf.syms:
                diffs.append(f'{binary} S_{num}: alias {name} missing')
                continue
            a_sh, a_val, _ = elf.syms[name]
            if (a_sh, a_val) != (shndx, val + off):
                diffs.append(f'{binary} S_{num}: alias {name} at section {a_sh}+{a_val}, want {shndx}+{val + off}')
    return diffs


@unittest.skipUnless(GPP and HAVE_ORIG, 'needs m68k-amiga-elf-g++, the original binaries and build/reasm/*.symbols.json')
class OwnedData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='gen_data_')
        gen_data.write(cls.tmp)
        cls.obj = compile_object(cls.tmp, os.path.join(cls.tmp, 'owned_data.cpp'))
        with open(cls.obj, 'rb') as f:
            cls.elf = Elf(f.read())
        cls.entries = gen_data.load_config()
        cls.originals = {b: load_original(b) for b in ('program', 'mog')}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_object_equals_original_hunks(self):
        self.assertEqual(compare_object(self.elf, self.entries, self.originals), [])

    # ---- ROADMAP 7.1r: the data cells that lay inside CODE hunks ---------------------------------------------------------
    def cell_diffs(self, mutate=None):
        """Differences between every `extern_cells` object and the original binary (independent of the generator: the extent is the
        distance to the next label of the reassembly listing, the bytes and the relocations come from the original hunk)."""
        cells, links, _asm = gen_data.load_cells()
        diffs = []
        for binary in ('program', 'mog'):
            orig, syms = self.originals[binary]
            pre = PREFIX[binary]
            for c in cells[binary]:
                lab = c['label']
                info = syms[lab]
                h = orig.hunks[info['hunk']]
                self.assertIsNotNone(h.data, lab)
                after = [i['offset'] for i in syms.values() if i['hunk'] == info['hunk'] and i['offset'] > info['offset']]
                size = c.get('size') or ((min(after) if after else h.size_bytes) - info['offset'])
                obj = f'g_cell_{pre}{lab}'
                if obj not in self.elf.syms:
                    diffs.append(f'{binary} {lab}: object {obj} missing')
                    continue
                shndx, val, osize = self.elf.syms[obj]
                if osize != size:
                    diffs.append(f'{binary} {lab}: object is {osize} bytes, want {size}')
                for alias in alias_names(binary, lab):
                    if self.elf.syms.get(alias, (None, None))[:2] != (shndx, val):
                        diffs.append(f'{binary} {lab}: alias {alias} is not at the object')
                got = bytearray(self.elf.bytes_of(shndx, val, size))
                if mutate == (binary, lab):
                    got[0] ^= 0xFF
                ptrs = {o - info['offset'] for g in h.reloc32 for o in g.offsets if info['offset'] <= o < info['offset'] + size}
                for k in range(size):
                    if any(p <= k < p + 4 for p in ptrs):
                        continue
                    if got[k] != h.data[info['offset'] + k]:
                        diffs.append(f'{binary} {lab}: byte +{k} original {h.data[info["offset"] + k]:#04x}, object {got[k]:#04x}')
                rels = self.elf.relocs.get(shndx, {})
                have = {o - val for o in rels if val <= o < val + size}
                if have != ptrs:
                    diffs.append(f'{binary} {lab}: relocations at {sorted(have)}, the original has pointers at {sorted(ptrs)}')
        return diffs

    def test_code_hunk_cells_equal_the_original(self):
        cells, _links, _asm = gen_data.load_cells()
        self.assertGreaterEqual(len(cells['mog']) + len(cells['program']), 50)
        self.assertEqual(self.cell_diffs(), [])

    def test_code_hunk_cell_mutation_is_detected(self):
        self.assertTrue(self.cell_diffs(mutate=('mog', 'LAB_0122')))
        self.assertTrue(self.cell_diffs(mutate=('program', 'LAB_05B8')))

    def test_cells_are_not_in_the_reset_tables(self):
        text = gen_data.generate()[1]
        self.assertNotIn('g_cell_', text[text.index('const ImageSection g_imageProgram'):text.index('}  // namespace rt')])

    def test_every_extern_hunk_is_covered(self):
        self.assertGreaterEqual(len(self.entries), 17)
        for e in self.entries:
            h = self.originals[e['binary']][0].hunks[e['section']]
            self.assertIn(h.kind, ('DATA', 'BSS'), e)

    def test_mutation_is_detected(self):
        # sensitivity: flip one byte of the scancode table (program S_16 +0x20) and one of the copper template
        # (mog S_4: +0x60 is the first knight name char, +0x116e a patched script operand, +0x5c a TextItem list tail)
        for mut in ((('program', 16), 0x20), (('program', 26), 0x60), (('mog', 39), 5), (('mog', 4), 0x60),
                    (('mog', 4), 0x116e), (('mog', 4), 0xe2c), (('mog', 4), 0x7dc0 + 3)):
            d = compare_object(self.elf, self.entries, self.originals, mutate=mut)
            self.assertTrue(d, f'mutation {mut} not detected')

    def test_no_global_constructors(self):
        names = [s[0] for s in self.elf.symlist]
        # (the overlay-reset tables rt::g_image* are namespaced constants: constant-initialised, no constructor)
        self.assertFalse([n for n in names if 'GLOBAL__sub_I' in n or (n.startswith('_Z') and not n.startswith('_ZN2rt') and 'g_image' not in n)],
                         'dynamic initialisation')
        self.assertFalse([s['name'] for s in self.elf.secs if s['name'].startswith('.init_array')])

    def test_state_hunks_are_owned_and_mutation_detected(self):
        # ROADMAP 7.1n3: mog S_1 (GameState) and the small mog hunks; flip one byte in each class of hunk
        owned = {(e['binary'], e['section']) for e in self.entries}
        for n in (1, 2, 10, 11, 13, 14, 17, 19, 41, 42):
            self.assertIn(('mog', n), owned)
        for mut in ((('mog', 1), 0x8F0), (('mog', 2), 0x40), (('mog', 42), 0x200), (('mog', 19), 7)):
            self.assertTrue(compare_object(self.elf, self.entries, self.originals, mutate=mut), f'mutation {mut}')

    def test_gamestate_members_follow_state_bind(self):
        # every state_bind.hpp declaration that lives in S_1 is a GameState member at the original label offset
        with open(os.path.join(ROOT, 'include', 'game', 'state_bind.hpp'), encoding='utf-8') as f:
            text = f.read()
        decls = resource.re.findall(r'extern "C" [\w:]+ mog_(LAB_[0-9A-F]+)(?:\[\d+\])?;', text)
        decls += [LABEL_OF_NAME['mog'][n] for n in resource.re.findall(r'extern "C" [\w:]+ (\w+)(?:\[\d+\])?;', text) if n in LABEL_OF_NAME['mog']]
        syms = self.originals['mog'][1]
        with open(os.path.join(self.tmp, 'owned_data.hpp'), encoding='utf-8') as f:
            hpp = f.read()
        found = 0
        for lab in decls:
            if lab in syms and syms[lab]['hunk'] == 1:
                found += 1
                self.assertRegex(hpp, r'offsetof\(GameState, \w+\) == %d,' % syms[lab]['offset'], lab)
        self.assertGreaterEqual(found, 20)
        self.assertIn('ms::game::Knight knights[5];', hpp)
        self.assertIn('ms::game::Knight * curKnight;', hpp)
        self.assertIn('sizeof(GameState) == 8780', hpp)

    def test_pointer_cells_are_link_time_constants(self):
        ptrs = [(o, r) for sec, rs in self.elf.relocs.items() for o, r in rs.items()]
        self.assertTrue(ptrs, 'the program S_16 handler pointers must be relocations')

    # ---- ROADMAP 7.1n5: the last DATA/BSS hunks ---------------------------------------------------------------------
    @origskip.need_asm_ref
    def test_every_data_and_bss_hunk_is_owned(self):
        """After 7.1n5 no DATA/BSS hunk of program or mog is left to the asm: every non-CODE hunk of the ORIGINAL binary is an
        extern_data entry, and the generated elf asm carries no DATA/BSS section at all."""
        owned = {(e['binary'], e['section']) for e in self.entries}
        for binary in ('program', 'mog'):
            orig = self.originals[binary][0]
            data = {i for i, h in enumerate(orig.hunks) if h.kind in ('DATA', 'BSS')}
            self.assertTrue(data)
            self.assertEqual(sorted(i for i in data if (binary, i) not in owned), [], binary)
            text = resource.Gen(binary, 'elf').generate()
            self.assertEqual(re.findall(r'(?m)^\s*SECTION\s+\S+,(?:DATA|BSS)\S*$', text), [], binary)

    def test_last_hunks_are_owned_with_twins_and_shares(self):
        by = {(e['binary'], e['section']): e for e in self.entries}
        for key in (('program', 5), ('program', 6), ('program', 7), ('program', 14), ('program', 19), ('program', 28),
                    ('mog', 3), ('mog', 6), ('mog', 7), ('mog', 8), ('mog', 24), ('mog', 33), ('mog', 38), ('mog', 45)):
            self.assertIn(key, by)
        self.assertEqual(by[('mog', 6)]['twin_of'], 'program:5')
        for n, base in ((7, 6), (8, 7), (24, 19)):                   # zero twins share ONE object
            self.assertEqual(by[('mog', n)]['twin_of'], f'program:{base}')
            self.assertTrue(by[('mog', n)]['share'])
            self.assertNotIn(by[('mog', n)]['object'], self.elf.syms)
        self.assertNotIn('share', by[('mog', 6)])                    # S_5/S_6 hold non-zero constants: own instance
        sh, val, _ = self.elf.syms['g_prgWork']
        self.assertEqual(self.elf.syms['mog_SECSTRT_24'][:2], (sh, val))
        self.assertEqual(self.elf.syms['mog_LAB_0C0A'][:2], (sh, val + 24))
        self.assertEqual(self.elf.syms['prg_LAB_03E5'][:2], (sh, val + 24))

    def test_last_hunks_mutations_are_detected(self):
        for mut in ((('program', 5), 0x14), (('program', 14), 4), (('program', 28), 264), (('mog', 3), 98),
                    (('mog', 3), 6), (('mog', 6), 0x1c), (('mog', 33), 400), (('mog', 38), 5), (('mog', 45), 168),
                    (('mog', 45), 300), (('mog', 45), 2010)):
            self.assertTrue(compare_object(self.elf, self.entries, self.originals, mutate=mut), f'mutation {mut}')

    def test_font_is_a_typed_glyph_array(self):
        with open(os.path.join(self.tmp, 'owned_data.hpp'), encoding='utf-8') as f:
            hpp = f.read()
        self.assertIn('struct __attribute__((packed)) Glyph8 {', hpp)
        self.assertIn('static_assert(sizeof(Glyph8) == 8', hpp)
        self.assertEqual(hpp.count('Glyph8 font[128];'), 2)           # program S_28 and mog S_33
        self.assertIn('static_assert(sizeof(PrgFont) == 1024', hpp)
        # glyph '!' (ASCII 33) is the first non-blank one: bytes 264.. of the hunk
        h = self.originals['program'][0].hunks[28]
        self.assertEqual(bytes(h.data[33 * 8:34 * 8]), bytes.fromhex('183c3c1818001800'))
        self.assertEqual(bytes(h.data[:33 * 8]), bytes(264))

    def test_synth_data_is_one_chip_object_both_synths_use(self):
        shndx, val, size = self.elf.syms['g_mogSynthData']
        self.assertEqual(size, 2016)
        self.assertEqual(self.elf.secs[shndx]['name'], '.chipdata.MEMF_CHIP')
        self.assertEqual(self.elf.syms['mog_SECSTRT_45'][:2], (shndx, val))
        self.assertEqual(self.elf.syms['mog_LAB_10A2'][:2], (shndx, val + 168))       # the asm synth's table (MS_SYNTH_ASM)
        self.assertEqual(self.elf.syms['mog_LAB_10A3'][:2], (shndx, val + 294))
        # the C++ synth's kSynthWave (game build: alias of the first 170 bytes) is the hunk's own bytes
        with open(os.path.join(ROOT, 'include', 'engine', 'synth_data.hpp'), encoding='utf-8') as f:
            h = f.read()
        self.assertIn('kSynthWave[kSynthWaveSize] asm("g_mogSynthData")', h)
        self.assertIn('MS_GAME_BUILD && !defined(MS_SYNTH_STANDALONE)', h)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import origskip   # ROADMAP 10.2a: the compiled-in synth tables are generated from the original mog, not in the repository
        with open(origskip.synth_data_cpp(), encoding='utf-8') as f:
            src = f.read()
        self.assertIn('#if !MS_SYNTH_WAVE_IN_HUNK', src)
        body = src[src.index('kSynthWave[kSynthWaveSize] = {'):]
        copy = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body[:body.index('};')]))
        self.assertEqual(copy, bytes(self.originals['mog'][0].hunks[45].data[:170]))


def hunk_exe_layout(path):
    """[(type, size_bytes, mem_flags)] of an AmigaOS hunk executable, read from its header and hunk stream (independent of
    every tool of this project): HUNK_HEADER size words carry the memory class in the top two bits (1 = MEMF_CHIP)."""
    with open(path, 'rb') as f:
        d = f.read()
    u = lambda o: struct.unpack('>I', d[o:o + 4])[0]
    assert u(0) == 0x3F3, 'not a HUNK_HEADER'
    o = 4
    while u(o):                                   # resident library names (none)
        o += 4 + 4 * u(o)
    o += 4
    n, first, last = u(o), u(o + 4), u(o + 8)
    o += 12
    sizes = [(u(o + 4 * i) & 0x3FFFFFFF) * 4 for i in range(last - first + 1)]
    flags = [u(o + 4 * i) >> 30 for i in range(last - first + 1)]
    o += 4 * len(sizes)
    out = []
    while o < len(d) and len(out) < len(sizes):
        typ = u(o)
        o += 4
        if typ in (0x3E9, 0x3EA, 0x3EB):          # CODE, DATA, BSS
            nl = u(o) & 0x3FFFFFFF
            o += 4 + (0 if typ == 0x3EB else 4 * nl)
            out.append(typ)
        elif typ == 0x3EC:                        # RELOC32: (count, target hunk, offsets) until count == 0
            while u(o):
                o += 8 + 4 * u(o)
            o += 4
        elif typ == 0x3F1:                        # DEBUG: length in longs
            o += 4 + 4 * u(o)
        elif typ == 0x3F0:                        # SYMBOL: (name longs, name, value) until 0
            while u(o):
                o += 8 + 4 * (u(o) & 0xFFFFFF)
            o += 4
        elif typ == 0x3F2:                        # END
            pass
        else:
            raise AssertionError(f'unknown hunk block {typ:#x} at {o - 4:#x}')
    assert len(out) == len(sizes), (len(out), len(sizes))
    return list(zip(out, sizes, flags))


CHIP_ENTRIES = (('program', 27), ('program', 30), ('mog', 15), ('mog', 32), ('mog', 35), ('mog', 38), ('mog', 43), ('mog', 45))


@unittest.skipUnless(GPP and HAVE_ORIG, 'needs m68k-amiga-elf-g++, the original binaries and build/reasm/*.symbols.json')
class ChipData(unittest.TestCase):
    """ROADMAP 7.1n4: the CHIP DATA/BSS hunks (program S_27/S_30, mog S_15/S_32/S_35/S_43) as C++ objects in chip sections."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='gen_data_chip_')
        gen_data.write(cls.tmp)
        with open(os.path.join(cls.tmp, 'owned_data.cpp'), encoding='utf-8') as f:
            cls.cpp = f.read()
        cls.obj = compile_object(cls.tmp, os.path.join(cls.tmp, 'owned_data.cpp'))
        with open(cls.obj, 'rb') as f:
            cls.elf = Elf(f.read())
        cls.entries = gen_data.load_config()
        cls.originals = {b: load_original(b) for b in ('program', 'mog')}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def _section_of(self, obj):
        shndx, _, _ = self.elf.syms[obj]
        return self.elf.secs[shndx]

    def test_chip_hunks_are_owned(self):
        owned = {(e['binary'], e['section']) for e in self.entries}
        for key in CHIP_ENTRIES:
            self.assertIn(key, owned)
            self.assertTrue(self.originals[key[0]][0].hunks[key[1]].mem_flag, f'{key} is a CHIP hunk in the original')

    def test_objects_are_in_chip_sections_with_the_right_type(self):
        for obj in ('g_prgScreen', 'g_mogScreen', 'g_mogNoSprite', 'g_mogSynthData'):            # DATA: PROGBITS in .chipdata.MEMF_CHIP
            s = self._section_of(obj)
            self.assertEqual((s['name'], s['type']), ('.chipdata.MEMF_CHIP', 1), obj)
        for obj in ('g_prgScratch', 'g_mogTileMask', 'g_mogCursorSprite'):   # BSS: NOBITS (no 45 KB of zeros in the exe)
            s = self._section_of(obj)
            self.assertEqual((s['name'], s['type']), ('.chipbss.MEMF_CHIP', 8), obj)
        self.assertLess(len(self.elf.d), 200000, 'the object file must not carry the zeroed BSS')

    def test_twin_scratch_shares_one_chip_object(self):
        self.assertNotIn('g_mogScratch', self.elf.syms)       # S_32 is byte-congruent BSS: one copy for both overlays
        sh, val, size = self.elf.syms['g_prgScratch']
        self.assertEqual(size, 45744)
        self.assertEqual(self.elf.syms['mog_SECSTRT_32'][:2], (sh, val))
        self.assertEqual(self.elf.syms['mog_LAB_0D4F'][:2], (sh, val + 4500))
        self.assertEqual(self.elf.syms['prg_LAB_052A'][:2], (sh, val + 4500))

    def test_chip_objects_equal_the_original_hunks(self):
        ents = [e for e in self.entries if (e['binary'], e['section']) in CHIP_ENTRIES]
        self.assertEqual(len(ents), len(CHIP_ENTRIES))
        self.assertEqual(compare_object(self.elf, self.entries, self.originals), [])
        for mut in ((('program', 27), 4500), (('program', 30), 0x20), (('program', 30), 190), (('mog', 15), 199),
                    (('mog', 35), 0x56), (('mog', 43), 79), (('mog', 32), 0), (('mog', 38), 3), (('mog', 45), 100)):
            self.assertTrue(compare_object(self.elf, self.entries, self.originals, mutate=mut), f'mutation {mut}')

    def test_screen_cells_keep_the_display_patches(self):
        # program.display.json / mog.display.json: the screen cells point at rt_screen_b / rt_screen_work (not $75A3C / $6BDFA)
        for obj, binary in (('g_prgScreen', 'program'), ('g_mogScreen', 'mog')):
            sh, val, _ = self.elf.syms[obj]
            rel = self.elf.relocs[sh]
            self.assertEqual(rel[val][:2], ('rt_screen_b', 0), binary)
            self.assertEqual(rel[val + 4][:2], ('rt_screen_work', 0), binary)

    def test_chip_section_names(self):
        self.assertEqual(gen_data.CHIP_SECTION, {'DATA': '.chipdata.MEMF_CHIP', 'BSS': '.chipbss.MEMF_CHIP'})
        self.assertIn('section(".chipdata.MEMF_CHIP")', self.cpp)
        self.assertIn('.section .chipbss.MEMF_CHIP,"aw",@nobits', self.cpp)

    def test_exe_hunks_carry_the_chip_flag(self):
        """The linked game exe: the chip objects lie in ELF output sections named *.MEMF_CHIP, and the hunk of that section
        has the MEMF_CHIP flag in the exe's HUNK_HEADER (BSS stays HUNK_BSS)."""
        exes = [os.path.join(ROOT, d, 'moonstone') for d in ('build-game', 'build-game-debug')]
        exes = [x for x in exes if os.path.exists(x) and os.path.exists(x + '.elf')]
        done = 0
        for exe in exes:
            with open(exe + '.elf', 'rb') as f:
                elf = Elf(f.read())
            if 'g_prgScratch' not in elf.syms:
                continue
            alloc = [i for i, s in enumerate(elf.secs) if s['flags'] & 2 and s['size']]
            layout = hunk_exe_layout(exe)
            self.assertEqual(len(layout), len(alloc), exe)
            for obj, typ in (('g_prgScratch', 0x3EB), ('g_mogTileMask', 0x3EB), ('g_mogCursorSprite', 0x3EB),
                             ('g_prgScreen', 0x3EA), ('g_mogScreen', 0x3EA), ('g_mogNoSprite', 0x3EA),
                             ('g_mogSynthData', 0x3EA)):
                sh = elf.syms[obj][0]
                self.assertTrue(elf.secs[sh]['name'].endswith('.MEMF_CHIP'), f'{exe}: {obj} in {elf.secs[sh]["name"]}')
                htype, hsize, hmem = layout[alloc.index(sh)]
                self.assertEqual(hmem & 1, 1, f'{exe}: hunk of {obj} is not MEMF_CHIP')
                self.assertEqual(htype, typ, f'{exe}: {obj} hunk type')
                self.assertGreaterEqual(hsize, elf.secs[sh]['size'])
            done += 1
        if not done:
            self.skipTest('no game build (build-game*/moonstone + .elf with the owned objects)')


@unittest.skipUnless(HAVE_ORIG, 'needs the original binaries and build/reasm')
@origskip.need_asm_ref
@origskip.need_listing
class ResourceSide(unittest.TestCase):
    def test_elf_asm_leaves_owned_hunks_out(self):
        for binary in ('program', 'mog'):
            ext = gen_data.extern_sections(binary)
            self.assertTrue(ext)
            text = resource.Gen(binary, 'elf').generate()
            for n, e in ext.items():
                self.assertNotIn(f'SECTION {PREFIX[binary]}S_{n},', text)
                self.assertNotIn(f'SECTION {PREFIX[binary]}S_{n}.', text)
                self.assertNotIn(f'{PREFIX[binary]}S_{n}_beg:', text)
            owned = resource.Gen(binary, 'elf').ext_labels
            self.assertTrue(owned)
            for lab in owned:
                self.assertNotIn('\tXDEF\t' + lab + '\n', text, lab)
                self.assertNotIn(lab + ':\n', text, lab)
            used = [lab for lab in owned if lab[len(PREFIX[binary]):].startswith('LAB_') and
                    resource.re.search(r'\b' + lab + r'\b', text)]
            self.assertTrue(used)
            for lab in used:
                self.assertIn('\tXREF\t' + lab + '\n', text, lab)

    def test_verify_build_keeps_the_data(self):
        for binary in ('program', 'mog'):
            g = resource.Gen(binary, 'verify', patches_on=False)
            self.assertEqual(g.ext_skip, set())
            text = g.generate()
            for n in gen_data.extern_sections(binary):
                self.assertIn(f'SECTION S_{n},', text)

    def test_reset_tables_match_the_original(self):
        """ROADMAP 7.1r: the generated overlay-reset tables (rt::g_imageProgram / g_imageMog) have one row per owned hunk with the
        original hunk size; a BSS hunk and an all-zero DATA hunk are BSS rows (zeroed at each entry), the others DATA rows."""
        text = gen_data.generate()[1]
        for binary, ident in (('program', 'Program'), ('mog', 'Mog')):
            orig = load_original(binary)[0]
            zero = gen_data.zero_data_sections(binary)
            block = text[text.index(f'const ImageSection g_image{ident}[] = {{'):]
            block = block[:block.index('};')]
            rows = re.findall(r'\{reinterpret_cast<unsigned char \*>\(&(\w+)\), (\d+)u, (true|false)\},  // S_(\d+)', block)
            self.assertEqual(sorted(int(r[3]) for r in rows), sorted(gen_data.extern_sections(binary)))
            for obj, size, bss, num in rows:
                h = orig.hunks[int(num)]
                self.assertEqual(int(size), h.size_bytes, f'{binary} S_{num}')
                self.assertEqual(bss, 'true' if (h.kind == 'BSS' or int(num) in zero) else 'false', f'{binary} S_{num}')

    def test_no_patch_inside_an_owned_hunk(self):
        for binary in ('program', 'mog'):
            resource.Gen(binary, 'elf')      # raises PatchError when a patch lies in an extern_data hunk


class Config(unittest.TestCase):
    def _tmp_cfg(self, program, mog=None):
        d = tempfile.mkdtemp(prefix='gen_data_cfg_')
        self.addCleanup(shutil.rmtree, d, True)
        for name, ents in (('program', program), ('mog', mog)):
            if ents is not None:
                with open(os.path.join(d, name + '.data.json'), 'w') as f:
                    json.dump({'binary': name, 'patches': [], 'extern_data': ents}, f)
        return d

    @unittest.skipUnless(os.path.exists(os.path.join(gen_data.ASM_DIR, 'program.asm')), 'needs reference/moonshard')
    def test_bad_twin_is_refused(self):
        # program S_32 (palette constants) is not congruent with mog S_39
        d = self._tmp_cfg([{'section': 32, 'object': 'g_a'}], [{'section': 39, 'object': 'g_b', 'twin_of': 'program:32'}])
        with self.assertRaises(gen_data.GenError):
            gen_data.generate(d)

    @unittest.skipUnless(os.path.exists(os.path.join(gen_data.ASM_DIR, 'program.asm')), 'needs reference/moonshard')
    def test_share_with_pointers_is_refused(self):
        d = self._tmp_cfg([{'section': 16, 'object': 'g_a'}], [{'section': 21, 'object': 'g_b', 'twin_of': 'program:16', 'share': True}])
        with self.assertRaises(gen_data.GenError):
            gen_data.generate(d)

    def test_area_files_are_merged_and_duplicates_refused(self):
        d = self._tmp_cfg([{'section': 3, 'object': 'g_a'}], [{'section': 1, 'object': 'g_b'}])
        with open(os.path.join(d, 'mog.data_state.json'), 'w') as f:
            json.dump({'binary': 'mog', 'patches': [], 'extern_data': [{'section': 2, 'object': 'g_c'}]}, f)
        self.assertEqual({(e['binary'], e['section']) for e in gen_data.load_config(d)},
                         {('program', 3), ('mog', 1), ('mog', 2)})
        with open(os.path.join(d, 'mog.data_other.json'), 'w') as f:
            json.dump({'binary': 'mog', 'patches': [], 'extern_data': [{'section': 2, 'object': 'g_d'}]}, f)
        with self.assertRaises(gen_data.GenError):
            gen_data.load_config(d)

    def test_real_config_has_no_section_twice(self):
        cfg = gen_data.load_config()       # raises on a duplicate across the <bin>.data*.json files
        keys = [(e['binary'], e['section']) for e in cfg]
        self.assertEqual(len(keys), len(set(keys)))

    @unittest.skipUnless(os.path.exists(os.path.join(gen_data.ASM_DIR, 'mog.asm')), 'needs reference/moonshard')
    def test_typed_member_checks(self):
        # a typed run must be zero data, start at a label and have the stated extent
        ok = {'section': 1, 'object': 'g_a', 'typed': [{'label': 'LAB_0613', 'through': 'LAB_0617', 'name': 'k',
                                                          'type': 'ms::game::Knight', 'count': 5}]}
        hpp = gen_data.generate(self._tmp_cfg(None, [ok]))[0]
        self.assertIn('sizeof(ms::game::Knight) * 5 == 660', hpp)
        for bad in ({'label': 'LAB_0613', 'through': 'LAB_0617', 'name': 'k', 'type': 'u8', 'count': 7},
                    {'label': 'LAB_9999', 'name': 'k', 'type': 'u8'},
                    {'label': 'LAB_0614', 'name': 'k', 'ptr': 'void', 'count': 3}):
            with self.assertRaises(gen_data.GenError):
                gen_data.generate(self._tmp_cfg(None, [dict(ok, typed=[bad])]))

    @unittest.skipUnless(os.path.exists(os.path.join(gen_data.ASM_DIR, 'program.asm')), 'needs reference/moonshard')
    def test_names_and_widths_specs(self):
        # 7.1n4: `names` renames the member starting at a label, `widths` views a data member as u16 cells
        ok = {'section': 30, 'object': 'g_a', 'names': {'LAB_056C': 'drawScreen'}, 'widths': {'LAB_056E': 2}}
        hpp = gen_data.generate(self._tmp_cfg([ok]))[0]
        self.assertIn('u32 drawScreen;  // +4', hpp)
        self.assertIn('u16 LAB_056E[32];', hpp)
        for bad in ({'names': {'LAB_9999': 'x'}}, {'names': {'LAB_056C': 'LAB_056D'}}, {'widths': {'LAB_056E': 8}},
                    {'widths': {'LAB_0570': 2}}):           # unknown label / name clash / bad width / not a data member
            with self.assertRaises(gen_data.GenError, msg=bad):
                gen_data.generate(self._tmp_cfg([dict({'section': 30, 'object': 'g_a'}, **bad)]))

    @unittest.skipUnless(os.path.exists(os.path.join(gen_data.ASM_DIR, 'program.asm')), 'needs reference/moonshard')
    def test_chip_bss_is_nobits_and_chip_data_is_sectioned(self):
        d = self._tmp_cfg([{'section': 27, 'object': 'g_a'}, {'section': 30, 'object': 'g_b'}])
        cpp = gen_data.generate(d)[1]
        self.assertIn('.section .chipbss.MEMF_CHIP,"aw",@nobits', cpp)
        self.assertIn('g_a:' + chr(10) + chr(9) + '.space 45744', cpp)
        self.assertIn('g_b __attribute__((used, externally_visible, aligned(4), section(".chipdata.MEMF_CHIP")))', cpp)

    @unittest.skipUnless(os.path.exists(os.path.join(gen_data.ASM_DIR, 'program.asm')), 'needs reference/moonshard')
    def test_code_hunk_is_refused(self):
        d = self._tmp_cfg([{'section': 0, 'object': 'g_a'}])
        with self.assertRaises(gen_data.GenError):
            gen_data.generate(d)


@unittest.skipUnless(os.path.exists(os.path.join(gen_data.ASM_DIR, 'mog.asm')), 'needs reference/moonshard')
class TypedTables(unittest.TestCase):
    """ROADMAP 7.1n2: mog S_4 as typed members (data_types.yaml regions, strings) and the generalised config."""

    @classmethod
    def setUpClass(cls):
        cls.hpp, cls.cpp, cls.info = gen_data.generate()

    def test_typed_regions_in_the_header(self):
        for decl in ('TextItem menuMain[6];', 'KnightName knightNames[4];', 'CreatureTypeDef creatureTypeDef[24];',
                     'Point16 creatureNodePos[24];', 'u16 creatureNodeAux[24];', 'const char *creatureNodeScript[24];',
                     'EncounterBand encounterBands[4];', 'u16 mainMenuItemY[4];', 'u8 moonFrameByIndex[8];',
                     'u16 combatActionOffsetRight[11];', 'TextItem menuText06DE[3];'):
            self.assertIn('	' + decl, self.hpp)
        self.assertIn('struct __attribute__((packed)) TextItem {', self.hpp)
        self.assertIn('static_assert(sizeof(TextItem) == 14', self.hpp)

    def test_strings_are_writable_char_arrays(self):
        self.assertIn('	char LAB_06BC[8];', self.hpp)          # "Players"
        self.assertIn('	char LAB_06B9[12];', self.hpp)         # the number field the menu writes into: not const
        self.assertNotIn('const char LAB_', self.hpp)
        self.assertIn('"Players",', self.cpp)

    def test_unknown_stays_bytes(self):
        # the fight scripts are not typed: u16/u32 runs named after their asm label
        self.assertRegex(self.hpp, r'	(u8|u16|u32) LAB_07EE\[\d+\];')

    def test_patch_values_reach_the_data(self):
        # mog.fight_ops.json: DC.L LAB_02E6 -> DC.L $F00002E6 inside S_4 (no pointer cell, no reference to the code label)
        sec = gen_data.parse_asm('mog', patched={4})[4]
        self.assertTrue(any(v == 0xF00002E6 for _, _, v in sec.cells))
        self.assertFalse([1 for _, _, v in sec.cells if isinstance(v, tuple) and v[1] == 'LAB_02E6'])
        self.assertNotIn('x_mog_LAB_02E6', self.cpp)
        self.assertGreaterEqual(sum(len(v) for v in patched_cells('mog', {4}).values()), 70)

    def test_default_output_has_only_the_aliases_the_sources_spell(self):
        """ROADMAP 7.1s: a cell with a C++ name is defined under it (and under its label only while something spells that), the other
        labels get an alias only where the sources, a pointer cell or asm/synth.s still name them."""
        gen_data.ALL_ALIASES = False
        try:
            cpp = gen_data.generate()[1]
        finally:
            gen_data.ALL_ALIASES = True
        sets = re.findall(r'(?m)^	\.set (\w+), ', cpp)
        self.assertEqual(len(sets), len(set(sets)))
        self.assertLess(len(sets), 2500)                          # was 1,757 (every label of an owned hunk, 7.1r)
        self.assertIn('mogCurKnight', sets)
        self.assertNotIn('mog_LAB_0633', sets)                    # renamed everywhere: no source spells the label alias
        for name in CELL_NAMES.values():
            self.assertIn(name, sets, name)
        pointer_targets = set(re.findall(r'asm\("(\w+)"\)', cpp))     # the targets of the pointer cells
        for name in sets:
            self.assertTrue(name in CELL_NAMES.values() or name in REFERENCED or name in pointer_targets, name)
        # CEL_BIND(...) and MS_X(...) paste the names; asm/synth.s (mog_LAB_0FC4) counts only for the MS_SYNTH_ASM build (--asm-labels)
        self.assertNotIn('mog_LAB_0FC4', sets)
        for lab in ('prg_LAB_04DE', 'mog_LAB_0D04', 'mog_LAB_08AB'):
            self.assertIn(lab, sets)
        self.assertNotIn('prg_LAB_03E5', sets)                    # nothing spells it

    def test_no_global_data_outside_the_object(self):
        # one object per hunk: S_4 is ONE extern "C" object (no per-table globals that could drift from the aliases)
        self.assertEqual(self.cpp.count('ms::owned::MogTables g_mogTables'), 1)
        self.assertIn('.set mog_LAB_06B5, g_mogTables+96', self.cpp)
        self.assertIn('.set mog_SECSTRT_4, g_mogTables' + chr(10), self.cpp)

    def _cfg(self, files):
        d = tempfile.mkdtemp(prefix='gen_data_cfg_')
        self.addCleanup(shutil.rmtree, d, True)
        for name, ents in files.items():
            binary = name.split('.')[0]
            with open(os.path.join(d, name), 'w') as f:
                json.dump({'binary': binary, 'patches': [], 'extern_data': ents}, f)
        return d

    def test_every_data_json_is_read(self):
        d = self._cfg({'mog.data.json': [{'section': 21, 'object': 'g_a'}],
                       'mog.data_tables.json': [{'section': 4, 'object': 'g_b'}],
                       'mog.data_state.json': [{'section': 1, 'object': 'g_c'}]})
        self.assertEqual(sorted(e['section'] for e in gen_data.load_config(d)), [1, 4, 21])
        self.assertEqual(sorted(gen_data.extern_sections('mog', d)), [1, 4, 21])

    def test_same_hunk_in_two_files_is_refused(self):
        d = self._cfg({'mog.data.json': [{'section': 4, 'object': 'g_a'}], 'mog.data_tables.json': [{'section': 4, 'object': 'g_b'}]})
        with self.assertRaises(gen_data.GenError):
            gen_data.load_config(d)

    def _types(self, text):
        d = tempfile.mkdtemp(prefix='gen_data_types_')
        self.addCleanup(shutil.rmtree, d, True)
        p = os.path.join(d, 'data_types.yaml')
        with open(p, 'w') as f:
            f.write(text)
        return p

    def _gen(self, region):
        cfg = self._cfg({'mog.data_tables.json': [{'section': 4, 'object': 'g_b'}]})
        return gen_data.generate(cfg, types_path=self._types('regions:\n  - ' + region + '\n'))

    def test_bad_regions_are_refused(self):
        base = '{binary: mog, section: 4, name: t, '
        for bad in (base + 'at: LAB_06AE, struct: TextItem, count: 7}',          # runs into the next label's data: a pointer field gets text
                    base + 'at: LAB_NOPE, struct: TextItem, count: 1}',            # unknown label
                    base + 'at: LAB_06AE, struct: NoSuch, count: 1}',              # unknown struct
                    base + 'at: LAB_06AE, element: u16, count: 99999}',            # past the hunk
                    base + 'at: LAB_06B5, struct: TextItem, count: 2}',            # pointer field over text bytes
                    base + 'table: no_such_table}'):
            with self.assertRaises(gen_data.GenError, msg=bad):
                self._gen(bad)

    def test_overlapping_regions_are_refused(self):
        cfg = self._cfg({'mog.data_tables.json': [{'section': 4, 'object': 'g_b'}]})
        t = self._types('regions:\n  - {binary: mog, section: 4, name: a, at: LAB_06AE, struct: TextItem, count: 2}\n'
                        '  - {binary: mog, section: 4, name: b, at: LAB_06AF, struct: TextItem, count: 1}\n')
        with self.assertRaises(gen_data.GenError):
            gen_data.generate(cfg, types_path=t)

    def test_good_inline_region_is_accepted(self):
        hpp, cpp, _ = self._gen('{binary: mog, section: 4, name: t, at: LAB_06AE, struct: TextItem, count: 6}')
        self.assertIn('TextItem t[6];', hpp)

    def test_twin_of_a_typed_entry_is_refused(self):
        cfg = self._cfg({'mog.data_tables.json': [{'section': 4, 'object': 'g_b', 'strings': True}],
                         'program.data.json': [{'section': 16, 'object': 'g_c', 'twin_of': 'mog:4'}]})
        with self.assertRaises(gen_data.GenError):
            gen_data.generate(cfg)


if __name__ == '__main__':
    unittest.main()
