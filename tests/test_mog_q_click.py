"""ROADMAP 7.1q: the click handler of the meeting / town / loot screens in C++ (src/rt/loot.cpp rtLootClick, with src/game/loot.cpp and
src/rt/scene_town.cpp + src/game/scene_town.cpp behind it) against the ORIGINAL asm LAB_052A (the whole chain LAB_052B..LAB_0562 with its
real sub-blocks LAB_0527 / 0528 / 0529 / 0541 / 0542 / 0551 and the knight recalcs LAB_0013 / 0019) of the reassembled mog image, in unicorn
(tests/emu_lib.py), on the same memory.  The callees outside the chain are logging stubs on both sides (tests/click_emu_support.cpp): the screen
redraw LAB_04D4, the nested screen LAB_04CF (D0 = scene), the sound request LAB_0AA2 (D0 = sequence, answers the scripted channel in D1), the
percent roll LAB_04A3 (scripted), the map mode calls LAB_0E02 / 0E05 / 0E06 and the jingles LAB_05A0 / 05A1.  The text player LAB_0BB3 is a bare
RTS in the original too (RTS patched here).  What must agree: the call log (ids, order, arguments) and every byte of the image and the test
data (knights, inventories, the screen cells), over randomised states and every button type.
Also checked: the screen setup LAB_058A (rtLootScreenSetup) against the original on random cells.
"""
import os
import random
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401
sys.path.insert(0, HERE)
import emu_lib as E  # noqa: E402

try:
    import test_scene_town as ST  # noqa: E402  (random knights / inventories)
    HAVE_ST = True
except Exception:          # pragma: no cover
    HAVE_ST = False

SOURCES = ['src/rt/loot.cpp', 'src/game/loot.cpp', 'src/rt/scene_town.cpp', 'src/game/scene_town.cpp', 'src/game/rules/stats.cpp', 'src/game/rules/rituals.cpp', 'src/game/rules/levelling.cpp', 'src/game/rules/dice.cpp', 'src/game/rules/shops.cpp', 'src/game/rules/healing.cpp', 'src/game/rules/settle.cpp', 'src/game/rules/clock.cpp', 'src/game/rules/turns.cpp',
           'src/rt/game_rules.cpp', 'src/engine/util.cpp', 'tests/click_emu_support.cpp', GAMEDATA_REL]
CALLEES = [('LAB_04D4', 'emu_o_redraw'), ('LAB_04CF', 'emu_o_screen'), ('LAB_0AA2', 'emu_o_sfx'), ('LAB_04A3', 'emu_o_rng'),
           ('LAB_0E02', 'emu_o_force'), ('LAB_0E05', 'emu_o_ambush'), ('LAB_0E06', 'emu_o_spot'), ('LAB_05A1', 'emu_o_jgood'),
           ('LAB_05A0', 'emu_o_jbad')]
CELLS = E.LOG_CELL + 8            # scripted answers: sfx channel, percent roll
KN, INV = E.DATA_BASE + 0x4000, E.DATA_BASE + 0x4800
LAIR, BTN, OBJ = E.DATA_BASE + 0x5000, E.DATA_BASE + 0x6000, E.DATA_BASE + 0x6100
SLOTS = {1: [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 0x14, 0x16], 5: [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 0x14, 0x16],
         12: [0, 2, 4, 6, 8, 0x14, 4, 4], 3: [0x47, 0x4C, 0x4C, 0x46, 0x48, 0x50, 0x4C], 10: [0x5C, 0x58, 0x4C, 0x5C, 0x58, 0x20]}
EXCH = [0x4A, 0x5C, 0x58, 0x4A, 0x20]


def be(v, n):
    return v.to_bytes(n, 'big')


@unittest.skipUnless(E.HAVE and HAVE_ST, 'needs unicorn, the m68k toolchain, the ACE headers, build/reasm/mog and clang++ (test_scene_town)')
class ClickEmu(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        blob = E.Blob(SOURCES, 'emu_click', defs={}, tag='click_emu')
        cls.blob_obj = blob
        cls.emu = e = E.Emu(blob, insn_limit=8_000_000)
        cls.patches = [(E.BLOB_BASE, e.blob)]
        for label, stub in CALLEES:
            cls.patches.append(E.jmp_patch(e.addr(label), e.syms[stub]))
        cls.patches.append((e.addr('LAB_0BB3'), b'\x4e\x75'))
        cls.seen = {}

    @classmethod
    def tearDownClass(cls):
        cls.blob_obj.cleanup()

    def a(self, label):
        return self.emu.addr(label)

    def state(self, rng, btype, flags, slot, mask, scene, done=False, next_button=False):
        """-> memory patches of one random screen state."""
        e = self.emu
        P = []
        knights = [bytearray(ST.rk(rng)) for _ in range(4)]
        invs = [bytearray(ST.ri(rng)) for _ in range(4)]
        for i in range(4):
            knights[i][92:96] = be(rng.choice([0x1B, 0x1C, 0x1D, 0x1E]), 4)   # armour: the exchange indexes a 4-entry table with it
            knights[i][96:100] = be(INV + 0x40 * i, 4)
            knights[i][100:104] = be(KN + 0x100 * rng.randrange(4), 4)
            P.append((KN + 0x100 * i, bytes(knights[i])))
            P.append((INV + 0x40 * i, bytes(invs[i])))
        me = rng.randrange(4)
        other = (me + rng.randrange(1, 4)) % 4          # the same knight twice would spin the dagger top-up loop forever (also in the original)
        P += [(self.a('LAB_068B'), be(KN + 0x100 * me, 4)), (self.a('LAB_068C'), be(INV + 0x40 * me, 4)),
              (self.a('LAB_068D'), be(KN + 0x100 * other, 4)), (self.a('LAB_068E'), be(INV + 0x40 * other, 4)),
              (self.a('LAB_068F'), be(scene, 4))]
        P += [(self.a('LAB_0525'), b''.join(be(KN + 0x100 * i, 4) for i in range(4))), (self.a('LAB_0526'), be(rng.choice([0, 1, 2, 3, 0xFFFF, rng.getrandbits(16)]), 2))]
        lair = bytearray(rng.getrandbits(8) for _ in range(0x30))
        lair[0:4] = be(INV + 0x40 * rng.randrange(4), 4)
        P += [(LAIR, bytes(lair)), (self.a('LAB_08C6'), be(LAIR, 4))]
        for c, n in (('LAB_0689', 2), ('LAB_053B', 2), ('LAB_053C', 2), ('LAB_0984', 2), ('LAB_05D3', 2), ('LAB_0665', 4), ('LAB_068A', 4),
                     ('LAB_06DE', 2), ('LAB_097F', 2), ('LAB_0976', 2)):
            v = rng.getrandbits(8 * n)
            if c == 'LAB_097F':
                v = rng.choice([0x40, 0x9F, 0xA0, 0xA1, 0x120, 0x8000, 0xFFF0])
            if c in ('LAB_0689', 'LAB_0984'):
                v = rng.choice([0, 1, 7, v])
            P.append((self.a(c), be(v, n)))
        P.append((self.a('LAB_0690'), bytes(bytearray(ST.ri(rng)))))
        P.append((self.a('LAB_0691'), bytes(rng.getrandbits(8) % 120 for _ in range(24))))
        P.append((self.a('LAB_05E4'), bytes(rng.getrandbits(8) for _ in range(0x20))))
        # the button record: +8 object, +16 type, +20 word (type | mask << 4), +22 slot; the object's flags word at +8
        btn = bytearray(rng.getrandbits(8) for _ in range(0x20))
        obj = bytearray(rng.getrandbits(8) for _ in range(0x20))
        obj[8:10] = be(flags, 2)
        btn[8:12] = be(self.a('LAB_09EF') if next_button else OBJ, 4)
        btn[16:20] = be(7 if done else rng.choice([0, 1, 2, 3, 5, 6, 8, 9]), 4)
        btn[20:22] = be(btype | (mask << 4), 2)
        btn[22:24] = be(slot, 2)
        P += [(BTN, bytes(btn)), (OBJ, bytes(obj))]
        P += [(CELLS, be(rng.choice([0, 1, 2, 3, 0x0F]), 4) + be(rng.choice([0, 5, 10, 11, 14, 15, 16, 50, 51, 100, rng.randrange(0, 101)]), 4))]
        return P

    def pair(self, entry_label, entry_sym, regs, extra, what):
        e = self.emu
        p = self.patches + extra
        ro, lo, ho, mo = e.run(e.addr(entry_label), dict(regs), p)
        rs, ls, hs, ms_ = e.run(e.syms[entry_sym], dict(regs), p)
        bad = e.diff_mem(mo, ms_, [(E.H.STACK_BASE, E.H.STACK_SIZE), (E.LOG_CELL, 0x1000)])
        self.assertEqual(bad[:6], [], '%s: memory differs at %s' % (what, ['%06x' % b for b in bad[:6]]))
        self.assertEqual(ls, lo, '%s: call log differs\n C++      %s\n original %s' % (what, ls, lo))
        self.assertEqual(rs['a'][7], ro['a'][7], what + ' SP')
        for r in lo:
            self.seen[r[0]] = self.seen.get(r[0], 0) + 1
        return lo

    def test_click_dispatch(self):
        rng = random.Random(0x7108)
        n = 0
        for case in range(1500):
            kind = rng.random()
            done = next_btn = False
            if kind < 0.03:
                done = True
                btype, flags, slot, mask = 0, 0, 0, 0
            elif kind < 0.06:
                next_btn = True
                btype, flags, slot, mask = 0, 0, 0, 0
            else:
                btype = rng.choice([5, 1, 3, 10, 12, 0, 2, 4, 7, 9, 13, 15])
                flags = rng.choice([0, 0x10, 0x20, 0x30, 0x40, 0x60, 0x70, 0x30 | 0x40, rng.getrandbits(8)])
                if btype == 3 and flags & 0x60 == 0x60:
                    # a temple button with both the stat bit (6) and the dagger bit (5): the original's dagger test then reads D1 as LAB_0019
                    # left it; the C++ (like the patched shims before it) keeps the slot.  No button of the game has both bits.
                    flags &= ~0x20
                slot = rng.choice(SLOTS.get(btype, EXCH))
                mask = rng.choice([0, 1, 2, 4, 7, 15, rng.getrandbits(4)])
            scene = rng.choice([1, 2, 3, 4, 5, 6, 6, 9, rng.getrandbits(3)])
            P = self.state(rng, btype, flags, slot, mask, scene, done, next_btn)
            regs = E.junk_regs(rng)
            regs['A0'] = BTN
            self.pair('LAB_052A', 'emu_click', regs, P, 'case %d type %d flags %x slot %x scene %d' % (case, btype, flags, slot, scene))
            n += 1
        # coverage: every kind of callee shows up
        for ident in (1, 2, 3, 5, 6, 7, 8, 9, 10):
            self.assertGreater(self.seen.get(ident, 0), 0, 'callee %d never logged' % ident)
        self.assertGreater(n, 0)


if __name__ == '__main__':
    unittest.main()
