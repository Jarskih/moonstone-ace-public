"""Contract between the generated irq patch sites and src/rt/{irq,input}.cpp (ROADMAP 2.2/2.3/7.1d).

The decoding and the handlers themselves are tested in tests/test_input.py (host models, unicorn against the original routines);
this file keeps the static contracts: which patch sites hand over to which rt_* symbol, that every symbol the C++ reads exists in
the generated asm, the trampoline vectors and stacks, and that the irq2 patches sit clear of the older irq patches.
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: a public checkout has no asm/*.s + asm/patches; the whole module skips)
origskip.require_asm_ref()
import json, os, re, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(*p):
    with open(os.path.join(ROOT, *p), encoding='utf-8') as f:
        return f.read()


def patches(binary, name=None):
    d = json.loads(read('asm', 'patches', binary + ('.' + name if name else '') + '.json'))
    return d['patches']


def patch(binary, pid, name=None):
    return next(p for p in patches(binary, name) if p['id'] == pid)


def span(p):
    return (p['line'], p['line'] + len(p['orig']) - 1) if 'orig' in p else tuple(p['lines'])


class IrqContract(unittest.TestCase):
    def test_init_patches_hand_over_to_the_install(self):
        # program LAB_0325 / mog LAB_0B49 (after their beam wait): JMP to the shim that runs rt::irqInstall and returns to the caller
        self.assertEqual(patch('program', 'irq2-init', 'irq2')['new'], ['\tJMP\trt_prg_irq_init'])
        # (mog LAB_0B49 lost its patch in 7.1q: src/rt/display_ops.cpp mogIrqInit does the beam wait and calls rt_mog_irq_init itself)
        # they replace the two counter reads and the master-off call, i.e. the lines in front of the (dead) irq-install block
        self.assertEqual(patch('program', 'irq2-init', 'irq2')['orig'][-1], '\tJSR\tLAB_0556')

    def test_irq2_patches_do_not_overlap_the_older_irq_patches(self):
        for binary in ('program', 'mog'):
            mine = [span(p) for p in patches(binary, 'irq2')]
            older = [span(p) for pid in ('irq-install', 'irq-disable', 'irq-enable', 'int4-vector')
                     for p in patches(binary) if p['id'] == pid]
            for a in mine:
                for b in older:
                    self.assertTrue(a[1] < b[0] or b[1] < a[0], (binary, a, b))
            for i, a in enumerate(mine):
                for b in mine[i + 1:]:
                    self.assertTrue(a[1] < b[0] or b[1] < a[0], (binary, a, b))

    def test_set_int4_single_arg(self):
        # 7.1q: the int4-vector patch went with the asm sound-bank loader (LAB_0AA7); src/rt/soundbank.cpp sets the handler through rt::irqSetInt4(handler)
        self.assertFalse([p for p in patches('mog') if p['id'] == 'int4-vector'])
        self.assertIn('rt::irqSetInt4(', read('src', 'rt', 'soundbank.cpp'))

    def test_every_patch_target_is_implemented_and_listed(self):
        impl = {}
        for f in json.loads(read('asm', 'patches', 'abs_symbols.json'))['funcs']:
            if isinstance(f, dict):
                impl[f['name']] = f['impl']
        for binary in ('program', 'mog'):
            for p in patches(binary, 'irq2'):
                for line in p.get('new', []):
                    m = re.search(r'\b(?:JMP|JSR)\t(rt_\w+)', line)
                    if not m:
                        continue
                    sym = m.group(1)
                    self.assertIn(sym, impl, '%s: %s is not in abs_symbols.json as an implemented function' % (p['id'], sym))
                    self.assertRegex(read(*impl[sym].split('/')), r'(?m)^\s*\.globl\s+%s\b|^\s*RT_\w+\s+%s\b' % (sym, sym))

    def test_input_symbols_exist(self):
        src = read('src', 'rt', 'input.cpp')
        prg, mog = read('asm', 'program.s'), read('asm', 'mog.s')
        # program S_16/17 and mog S_21/22 are C++-owned (ROADMAP 7.1n1): their labels are defined by build/gen/owned_data.cpp
        # (tests/test_gen_data.py proves every label), so asm/*.s XREFs them instead of exporting them
        import sys
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import resource
        owned = {n: resource.Gen(n, 'elf').ext_labels for n in ('program', 'mog')}
        for sym in set(re.findall(r'\bprg_(?:LAB_[0-9A-F]+|SECSTRT_\d+)', src)):
            if sym not in owned['program']:
                self.assertRegex(prg, r'(?m)^\s*XDEF\s+%s\b' % sym)
        for sym in set(re.findall(r'\bmog_(?:LAB_[0-9A-F]+|SECSTRT_\d+)', src)):
            if sym not in owned['mog']:
                self.assertRegex(mog, r'(?m)^\s*XDEF\s+%s\b' % sym)

    def test_overlay_rows_pair_the_twin_cells(self):
        # program S_16/S_17 and mog S_21/S_22 are the same data: the rows list the twin labels in the same order
        src = read('src', 'rt', 'input.cpp')
        row = lambda name: re.search(r'"%s",(.*?)\n\t\},' % name, src, re.S).group(1)
        # ROADMAP 7.1s: the cells carry their C++ names (tools/cell_names.yaml: prgKeyLast <-> mogKeyLast ...); map them back to the labels
        import sys
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import cellnames
        def label(n):
            k = cellnames.label_of(n)
            m = re.search(r'(?:LAB_|SECSTRT_)(\w+)', k[1] if k else n)
            return m.group(1) if m else n
        prg = [label(n) for n in re.findall(r'\bprg\w+', row('program'))]
        mog = [label(n) for n in re.findall(r'\bmog\w+', row('mog'))]
        self.assertEqual(len(prg), len(mog))
        # the offset between twin labels is constant (LAB_0362 <-> LAB_0B86 ...) except the section-start labels
        pairs = [(a, b) for a, b in zip(prg, mog) if re.fullmatch(r'[0-9A-F]{4}', a) and re.fullmatch(r'[0-9A-F]{4}', b)]
        self.assertGreater(len(pairs), 10)
        deltas = {int(b, 16) - int(a, 16) for a, b in pairs}
        # 0362/0B86 .. 036A/0B8E (+0x824), 036B.. (+0x824), 0372/0B96 (+0x824), 0375/0B99, 0379/0B9D: one delta
        self.assertEqual(deltas, {0x0B86 - 0x0362})

    def test_trampoline_vectors_and_stacks(self):
        src = read('src', 'rt', 'irq.cpp')
        self.assertIn('RT_IRQ_TRAMP_C rt_irq_tramp3, rt_irq_stk3, rtIrqLevel3C', src)
        self.assertIn('RT_IRQ_TRAMP_H rt_irq_tramp4, rt_irq_h4, rt_irq_stk4, 0x70, rtIrqLevel4C', src)
        # private 4 KB stacks, one per level (a level never nests on itself)
        self.assertEqual(len(re.findall(r'rt_irq_stk[34]:\n\t\.space 4096', src)), 2)
        self.assertEqual(src.count('lea \\stk+4096,%sp'), 2)

    def test_level3_body_follows_the_original_order(self):
        # LAB_0331: BLIT (flag, ack) first, then VERTB (frame counter, mouse, POTGO, hooks, ack), COPER ack last
        src = read('src', 'rt', 'irq.cpp')
        body = src[src.index('void irqLevel3()'):src.index('void irqLevel4Default()')]
        order = [body.index(k) for k in ('INTF_BLIT', 'puwFrameOn', 'inputPointerTick', 'potgo = 1', 'runHooks', 'intreq = INTF_VERTB',
                                           'INTF_COPER')]
        self.assertEqual(order, sorted(order))

    def test_no_handler_cell_for_level3_any_more(self):
        self.assertNotIn('rt_irq_h3', read('src', 'rt', 'irq.cpp'))


if __name__ == '__main__':
    unittest.main()
