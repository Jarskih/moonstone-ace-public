"""Layout tests for include/game/*.hpp (ROADMAP 5.1).

  - the headers compile with clang++ (every static_assert on sizeof/offsetof holds; no STL is included because the
    old host clang cannot use the MSVC STL)
  - state_bind.hpp is parsed: for every `extern "C" <type> mog_LAB_XXXX[N];` the size the C++ type implies equals the
    extent mog.asm reserves for that label (DS.x / DC.x sizes summed per section), and every label inside a
    multi-label array lies on an element boundary
  - a few spot values from mog.asm that fix the knight record layout (strides, init stores)

Run: py -m unittest tests.test_game_state
"""
import os, re, shutil, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
INC = os.path.join(ROOT, 'include')
BIND = os.path.join(INC, 'game', 'state_bind.hpp')
CLANG = shutil.which('clang++')

HAVE = CLANG is not None and os.path.exists(ASM)

_SIZE = {'B': 1, 'W': 2, 'L': 4}


def _split_args(s):
    """Split a DC argument list on commas outside double quotes."""
    out, cur, q = [], '', False
    for ch in s:
        if ch == '"':
            q = not q
            cur += ch
        elif ch == ',' and not q:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def parse_asm_layout(path):
    """Return ({label: (section, offset)}, {section: (kind, size)}, {label: extent}) for BSS/DATA sections."""
    labels, sections, order = {}, {}, []
    sec, kind, off = None, None, 0
    with open(path, encoding='latin-1') as f:
        for raw in f:
            line = raw.split(';')[0].rstrip('\r\n') if '"' not in raw else raw.rstrip('\r\n')
            m = re.match(r'^\s+SECTION\s+(\w+),(\w+)', line)
            if m:
                if sec:
                    sections[sec] = (kind, off)
                sec, kind, off = m.group(1), m.group(2), 0
                continue
            if sec is None or kind not in ('BSS', 'DATA'):
                continue
            m = re.match(r'^(\w+):\s*$', line)
            if m:
                labels[m.group(1)] = (sec, off)
                order.append(m.group(1))
                continue
            m = re.match(r'^\s+(DS|DC)\.([BWL])\s+(.*)$', line)
            if not m:
                continue
            op, sz, args = m.groups()
            n = _SIZE[sz]
            if op == 'DS':
                a = args.strip()
                off += n * (int(a[1:], 16) if a.startswith('$') else int(a))
            else:
                for a in _split_args(args):
                    if a.startswith('"'):
                        off += len(a.strip('"'))
                    else:
                        off += n
    if sec:
        sections[sec] = (kind, off)
    extent = {}
    for i, lab in enumerate(order):
        s, o = labels[lab]
        nxt = None
        for l2 in order[i + 1:]:
            if labels[l2][0] == s and labels[l2][1] > o:
                nxt = labels[l2][1]
                break
        if nxt is None:
            nxt = sections[s][1]
        extent[lab] = nxt - o
    return labels, sections, extent, order


IDENT_OF = {}      # label -> the identifier state_bind.hpp declares for it (`mogCurKnight`, or `mog_LAB_0633` for a cell without a C++ name)


def parse_bindings():
    """[(type, label, count or None)] from state_bind.hpp; the identifier is either the label alias `mog_LAB_XXXX` or the C++ name
    of tools/cell_names.yaml (ROADMAP 7.1s), which the mapping resolves back to the label."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import cellnames
    by_name = {n: lab for (b, lab), n in cellnames.load().items() if b == 'mog'}
    out = []
    with open(BIND) as f:
        for line in f:
            m = re.match(r'^extern "C" ([\w:]+) (\w+)(?:\[(\d+)\])?;', line)
            if not m:
                continue
            ident = m.group(2)
            lab = ident[len('mog_'):] if re.fullmatch(r'mog_LAB_[0-9A-F]{4}', ident) else by_name.get(ident)
            if lab is None:
                continue
            IDENT_OF[lab] = ident
            out.append((m.group(1), lab, int(m.group(3)) if m.group(3) else None))
    return out


def compile_cpp(src, extra=()):
    d = tempfile.mkdtemp()
    try:
        p = os.path.join(d, 't.cpp')
        with open(p, 'w') as f:
            f.write(src)
        exe = os.path.join(d, 't.exe')
        r = subprocess.run([CLANG, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', INC, p, '-o', exe, *extra],
                           capture_output=True, text=True)
        if r.returncode:
            return r, None
        run = subprocess.run([exe], capture_output=True, text=True)
        return r, run.stdout
    finally:
        shutil.rmtree(d, ignore_errors=True)


@unittest.skipUnless(HAVE, 'needs clang++ on PATH and moonshard/moonstone-main/amiga_asm/mog.asm')
class GameStateLayout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.labels, cls.sections, cls.extent, cls.order = parse_asm_layout(ASM)
        cls.binds = parse_bindings()

    def test_headers_compile_and_static_asserts_hold(self):
        for hdr in ('game/knight.hpp', 'game/world.hpp', 'game/state.hpp', 'game/state_bind.hpp'):
            with self.subTest(header=hdr):
                r, _ = compile_cpp('#include "%s"\nint main(){return 0;}\n' % hdr)
                self.assertEqual(r.returncode, 0, r.stderr)

    def test_asm_parser_known_extents(self):
        # the sizes the doc and the asm agree on (DS.L 33 = $84 per knight, DS.L 24 inventories, ...)
        self.assertEqual(self.extent['LAB_0613'], 132)
        self.assertEqual(self.extent['LAB_0617'], 132)
        self.assertEqual(self.extent['LAB_0618'], 96)
        self.assertEqual(self.extent['LAB_0619'], 24)
        self.assertEqual(self.extent['LAB_05E4'], 22)
        self.assertEqual(self.extent['LAB_0649'], 500)
        self.assertEqual(self.extent['LAB_06C2'], 8)

    def test_bindings_present(self):
        names = {b[1] for b in self.binds}
        for must in ('LAB_0613', 'LAB_0618', 'LAB_05E4', 'LAB_0649', 'LAB_069F', 'LAB_0633', 'LAB_08C6'):
            self.assertIn(must, names)

    def test_binding_sizes_match_asm(self):
        lines = ['#include <stdio.h>', '#include "game/state_bind.hpp"', 'int main(){']
        for typ, lab, cnt in self.binds:
            lines.append('printf("%%s %%u %%u\\n", "%s", (unsigned)sizeof(%s), (unsigned)sizeof(%s));' % (lab, IDENT_OF[lab], typ))
        lines.append('return 0;}')
        r, out = compile_cpp('\n'.join(lines))
        self.assertEqual(r.returncode, 0, r.stderr)
        sizes = {}
        for ln in out.split('\n'):
            if ln.strip():
                lab, total, elem = ln.split()
                sizes[lab] = (int(total), int(elem))
        self.assertEqual(len(sizes), len(self.binds))
        for typ, lab, cnt in self.binds:
            with self.subTest(label=lab):
                self.assertIn(lab, self.labels, 'label not in a BSS/DATA section of mog.asm')
                total, elem = sizes[lab]
                sec, off = self.labels[lab]
                end = off + total
                # the bound range must end exactly on a label boundary (or the section end) ...
                bounds = {o for (s, o) in self.labels.values() if s == sec} | {self.sections[sec][1]}
                self.assertTrue(end in bounds, '%s: %d bytes from +%d does not end on a label (asm extent %d)'
                                % (lab, total, off, self.extent[lab]))
                # ... and must not run past the section
                self.assertLessEqual(end, self.sections[sec][1])
                # labels inside the range are element boundaries (arrays that span labels)
                for l2, (s2, o2) in self.labels.items():
                    if s2 == sec and off < o2 < end:
                        self.assertEqual((o2 - off) % elem, 0,
                                         '%s: interior label %s at +%d is not on a %d-byte element boundary'
                                         % (lab, l2, o2 - off, elem))
                # when the asm gives the label its own extent it must be a whole number of elements
                if cnt is None:
                    self.assertEqual(total, self.extent[lab], '%s: scalar size != DS size' % lab)

    def test_struct_times_count_equals_ds_size(self):
        """The 5.1 deliverable: element size * count == the DS size in mog.asm (summed over the span's labels)."""
        spans = {                     # label -> last label of the span, struct size, count
            'LAB_0613': ('LAB_0617', 132, 5),   # DS.L 33 x 5
            'LAB_0618': ('LAB_0619', 24, 5),    # DS.L 24 + DS.L 6
            'LAB_05E4': ('LAB_05E4', 22, 1),    # DS.L 5 + DS.W 1
            'LAB_0649': ('LAB_0649', 50, 10),   # DS.L 125
        }
        for first, (last, size, cnt) in spans.items():
            with self.subTest(label=first):
                total = 0
                started = False
                for lab in self.order:
                    if lab == first:
                        started = True
                    if started:
                        total += self.extent[lab]
                    if lab == last:
                        break
                self.assertEqual(total, size * cnt)

    def test_map_node_table_is_nine_entries_plus_terminator(self):
        self.assertEqual(self.extent['LAB_069F'], 60)   # DC.L x 15 = 10 x 6 bytes

    def test_knight_record_stores_in_the_asm(self):
        """Offsets the struct fields rely on, taken verbatim from LAB_01C6 (knight init defaults)."""
        with open(ASM, encoding='latin-1') as f:
            txt = f.read()
        m = re.search(r'^LAB_01C6:\n(.*?)^LAB_01C7:', txt, re.S | re.M)
        self.assertTrue(m)
        body = m.group(1)
        expect = {
            r'MOVE\.B\s+#\$01,70\(A1\)': 'ubStrength',
            r'MOVE\.B\s+#\$01,71\(A1\)': 'ubConstitution',
            r'MOVE\.B\s+#\$01,72\(A1\)': 'ubEndurance',
            r'MOVE\.B\s+#\$05,73\(A1\)': 'ubLives',
            r'MOVE\.W\s+#\$0014,80\(A1\)': 'swHp',
            r'MOVE\.L\s+#\$0000001b,92\(A1\)': 'ulArmour',
            r'MOVE\.L\s+#\$00000016,88\(A1\)': 'ulSword',
            r'MOVE\.W\s+#\$0000,78\(A1\)': 'uwProgress',
            r'MOVE\.B\s+#\$0a,76\(A1\)': 'ubDaggers',
            r'MOVE\.W\s+#\$000a,74\(A1\)': 'uwGold',
            r'MOVE\.B\s+#\$ff,83\(A1\)': 'ubRecency',
            r'MOVE\.B\s+#\$00,130\(A1\)': 'ubLifeLoss',
            r'MOVE\.L\s+#\$00000000,100\(A1\)': 'ulEngagedWith',
        }
        # map each store's offset to the field header offset to prove struct and asm agree
        with open(os.path.join(INC, 'game', 'knight.hpp')) as hf:
            hdr = hf.read()
        for pat, field in expect.items():
            with self.subTest(field=field):
                self.assertTrue(re.search(pat, body), pat)
                off = int(re.search(r'(\d+)\(A1\)', pat.replace('\\', '')).group(1))
                self.assertIn('static_assert(offsetof(Knight, %s) == %d, "");' % (field, off), hdr)


if __name__ == '__main__':
    unittest.main()
