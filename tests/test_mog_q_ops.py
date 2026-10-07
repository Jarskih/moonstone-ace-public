"""ROADMAP 7.1p / 7.1q, small glue: (1) the $B0 script operands that used to name asm routines are tags now (asm/patches/mog.fight_tags.json,
src/rt/fighters.cpp rtFightOpRun); (2) the title step rtTitleStep (src/rt/hunk9.cpp) replaces LAB_00B4 / LAB_012D / the hunk-9 return chain.
Both run as m68k code in unicorn next to tests/ops_emu_support.cpp (tests/emu_lib.py).

Tags: every tag calls the C++ routine that replaced the asm label (logged), and the two operands that were real asm, LAB_04BA (random draw, then
sequence $99 on channel 3) and LAB_04C2 (percent draw, up to 50 sequence $9D), are compared with the ORIGINAL routines of the reassembled
image on random seeds / rolls: call log and every byte of memory (the seed LAB_0973 proves ms::rngNext == LAB_04A1; the busy bit LAB_0AA6 moved to
the C++ sfx state and is ignored).  Static: the generated asm names no tagged label in a DC.L any more, and every tagged line is a $B0 operand.
Title: the call order of the hunk-9 return chain (docs/HUNK9_STUB.md) and the dummy long popped into LAB_0714.
"""
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import origskip  # noqa: E402  (ROADMAP 10.2: tests that read the listing / asm reference skip without it)
import json
import os
import random
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emu_lib as E  # noqa: E402

ROOT = E.ROOT
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
FIGHT_SOURCES = ['src/rt/fighters.cpp', 'src/game/fighters.cpp', 'src/game/fight_ops.cpp', 'src/game/fight_creatures.cpp',
                 'src/game/creatures.cpp', 'src/engine/util.cpp', 'tests/ops_emu_support.cpp', 'src/game/rules/damage.cpp'] + ['src/game/rules/' + n + '.cpp' for n in ['ai_fight', 'ai_fight_flyer', 'ai_fight_snatcher', 'ai_fight_demon', 'ai_fight_knight', 'ai_fight_dragon', 'ai_fight_brawler', 'ai_fight_caster', 'ai_fight_drake', 'ai_fight_stalker']] + [GAMEDATA_REL]
TAGS = {0x0005: [(1, 0, 0, 0)], 0x0006: [(2, 0, 0, 0)], 0x000A: [(3, 0, 0, 0)], 0x000D: [(4, 0, 0, 0)], 0x0A9E: [(5, 0, 0, 0)],
        0x0A9F: [(5, 1, 0, 0)], 0x0427: [(6, 0, 0, 0)]}
CELLS = E.LOG_CELL + 8


@unittest.skipUnless(E.HAVE, 'needs unicorn, the m68k toolchain, the ACE headers and build/reasm/mog')
class TagsEmu(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        blob = E.Blob(FIGHT_SOURCES, 'emu_op', fallback='emu_rts', tag='ops_emu')
        cls.blob_obj = blob
        cls.emu = e = E.Emu(blob)
        cls.patches = [(E.BLOB_BASE, e.blob)]
        for label, stub in (('LAB_0F8C', 'emu_o_fixed'), ('LAB_0AA2', 'emu_o_sfx'), ('LAB_04A3', 'emu_o_rng')):
            cls.patches.append(E.jmp_patch(e.addr(label), e.syms[stub]))

    @classmethod
    def tearDownClass(cls):
        cls.blob_obj.cleanup()

    def test_each_tag_calls_its_routine(self):
        e = self.emu
        for label, want in TAGS.items():
            rs, ls, hs, ms_ = e.run(e.syms['emu_op'], {'D0': 0xF0000000 | label}, self.patches)
            self.assertEqual(ls, want, hex(label))
            self.assertEqual(rs['d'][0], 1, hex(label))

    def test_an_asm_address_is_not_ours(self):
        e = self.emu
        rs, ls, hs, ms_ = e.run(e.syms['emu_op'], {'D0': e.addr('LAB_0AA7')}, self.patches)
        self.assertEqual(ls, [])
        self.assertEqual(rs['d'][0], 0)

    def test_ritual_sounds_like_the_original(self):
        e = self.emu
        rng = random.Random(0x7109)
        seen = set()
        for case in range(200):
            label = rng.choice([0x04BA, 0x04C2])
            extra = [(e.addr('LAB_0973'), E.be32(rng.getrandbits(32))), (e.addr('LAB_0AA6'), E.be16(rng.getrandbits(16))),
                     (CELLS, E.be32(rng.choice([0, 1, 2, 3, 15])) + E.be32(rng.choice([0, 10, 49, 50, 51, 100, rng.randrange(101)])))]
            p = self.patches + extra
            ro, lo, ho, mo = e.run(e.addr('LAB_%04X' % label), E.junk_regs(rng), p)
            rs, ls, hs, ms_ = e.run(e.syms['emu_op'], {'D0': 0xF0000000 | label}, p)
            bad = e.diff_mem(mo, ms_, [(E.H.STACK_BASE, E.H.STACK_SIZE), (E.LOG_CELL, 0x1000), (e.addr('LAB_0AA6'), 2)])
            self.assertEqual(bad[:6], [], 'case %d %04X: memory differs at %s' % (case, label, ['%06x' % b for b in bad[:6]]))
            self.assertEqual(ls, lo, 'case %d %04X: log differs\n C++      %s\n original %s' % (case, label, ls, lo))
            seen.add((label, tuple(r[0] for r in lo)))
        self.assertEqual(seen, {(0x04BA, (8,)), (0x04C2, (7,)), (0x04C2, (7, 9))})


@origskip.need_asm_ref
@origskip.need_listing
class TagsStatic(unittest.TestCase):
    def test_no_tagged_label_is_named_by_a_data_table(self):
        with open(os.path.join(ROOT, 'asm', 'mog.s'), encoding='latin-1') as f:
            txt = f.read()
        for lab in ('0005', '0006', '000A', '000D', '0A9E', '0A9F', '0427', '04BA', '04C2'):
            self.assertIsNone(re.search(r'\tDC\.L\tmog_LAB_%s\b' % lab, txt), lab)

    def test_every_tagged_line_follows_a_b0_opcode(self):
        src = open(os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm'), encoding='latin-1').read().split('\n')
        doc = json.load(open(os.path.join(ROOT, 'asm', 'patches', 'mog.fight_tags.json')))
        self.assertGreater(len(doc['patches']), 40)
        for p in doc['patches']:
            ln = p['line']
            self.assertEqual(src[ln - 1].strip(), p['orig'][0].strip(), p['id'])
            # the operand follows `DC.W $b000` or ends a DC.L run whose last byte is $B0 (the opcode)
            prev = src[ln - 2].strip()
            m = re.search(r'\$([0-9a-f]+)\s*$', prev, re.I)
            self.assertIsNotNone(m, p['id'] + ': ' + prev)
            v = int(m.group(1), 16)
            op = (v >> 8) & 0xFF
            self.assertEqual(op, 0xB0, p['id'] + ': ' + prev)


@unittest.skipUnless(E.HAVE, 'needs unicorn, the m68k toolchain, the ACE headers and build/reasm/mog')
class TitleEmu(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        blob = E.Blob(['src/rt/hunk9.cpp', 'tests/ops_emu_support.cpp'], 'emu_title', fallback='emu_rts', tag='title_emu')
        cls.blob_obj = blob
        cls.emu = E.Emu(blob)

    @classmethod
    def tearDownClass(cls):
        cls.blob_obj.cleanup()

    def test_the_hunk9_return_chain(self):
        """LAB_00B4: JSR LAB_03F1, JMP LAB_012D (the Sel screen), the stub's chain LAB_02CE -> LAB_00EE -> LAB_03A7, the dummy long (zero) the
        menu pops into LAB_0714, then the menu at LAB_00B4+12 (docs/HUNK9_STUB.md)."""
        e = self.emu
        rs, ls, hs, ms_ = e.run(e.syms['emu_title'], {}, [(E.BLOB_BASE, e.blob), (e.addr('LAB_0714'), E.be32(0xDEADBEEF))])
        self.assertEqual([r[0] for r in ls], [20, 21, 22, 23, 24, 25])
        self.assertEqual(ls[-1][1], 0)                      # the menu starts after LAB_0714 := 0
        self.assertEqual(rs['a'][7], E.H.STACK_TOP)         # returns to the main loop


if __name__ == '__main__':
    unittest.main()
