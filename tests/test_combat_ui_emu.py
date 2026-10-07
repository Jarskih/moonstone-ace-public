"""Emulator check of src/rt/combat_ui.cpp (ROADMAP 7.1j): the m68k code the game build links (combat_ui.cpp, asmcall.cpp,
game/creatures.cpp, engine/util.cpp, compiled with m68k-amiga-elf-g++ -m68020, linked flat, plus tests/combat_ui_emu_support.cpp)
runs in unicorn next to the ORIGINAL routine of the reassembled mog image, on the same memory and the same registers (the
driver, the stubs for the asm that stays and the comparison are tests/test_arena_emu.py's).  Every callee outside the file
(cel blitter, text printer, loaders, fades, joystick read, sprite placement ...) is replaced in BOTH runs by a stub that logs
its registers; the two logs and every byte of the image and of the test arena must agree, plus the result registers of the
shims.  Values the original prints through LAB_0442 stay below 1000 (ms::formatNumber3 prints modulo 1000, the asm reads past its
digit table), the upper words of data registers the original leaves stale are zero.

Needs: unicorn, the m68k toolchain of AGENTS.md and build/reasm/mog.  Skipped otherwise.
"""
import os
import random
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_arena_emu as AE  # noqa: E402  (module import only: its test classes must not be collected twice)

H = AE.H
sym = AE.sym
DATA = AE.DATA_BASE

UI_FILES = ('src/rt/combat_ui.cpp', 'src/rt/asmcall.cpp', 'src/game/creatures.cpp', 'src/engine/util.cpp',
            'tests/combat_ui_emu_support.cpp', AE.GAMEDATA_REL)
JOY0 = AE.BLOB_BASE + 0x1F100                # what the LAB_00EE stub returns in D0 / D1
JOY1 = AE.BLOB_BASE + 0x1F104
UI_STUBS = {
    1: ('0CDA', 'A0 D0.w D1.w D2.w'), 2: ('0CCE', 'A0 D0.w'), 3: ('0431', 'A0 D0.w D1.w D2.w'), 4: ('0432', 'A0'),
    5: ('0D74', 'D0'), 6: ('0426+2', 'D0'), 7: ('0D72', 'A0'), 8: ('0328', ''), 9: ('0416', ''), 10: ('039E', ''),
    11: ('0131', ''), 12: ('03F0', ''), 13: ('03EB', ''), 14: ('0305', ''), 15: ('02CE', ''), 16: ('041F', 'A0 A1'),
    17: ('04C5', 'D0 A0'), 18: ('02D0', 'D0.w D1.w D2.w D3.b D5.b A0 A2'), 19: ('049E', 'A0 D0.w D1.w D2.w'),
    20: ('0418', ''), 21: ('0F8C', 'D0.w D1'), 22: ('0E55', 'D0 A0'), 23: ('045E', ''), 24: ('03F1', ''),
    25: ('0D8A', 'A0'), 26: ('03EE', 'A0'), 27: ('00EE', ''), 28: ('0E78', 'D0.w D1.w D2.w'), 29: ('0CBB', 'A0 A1'),
    30: ('SECSTRT_37', 'D0 A0 A1'), 31: ('0AA2', 'D0.w'),
}
UI_RETS = {18: {'A1': AE.SPAWN_REC}, 27: {'D0': ('mem', JOY0), 'D1': ('mem', JOY1)}}
SFX_STUB = AE.stub_addr(31)

KN = AE.KN
NAMES = DATA + 0x100
INVS = DATA + 0x200                           # six 24-byte inventories
CEL = DATA + 0x1000                           # the cel table (random bytes)
BUTTONS = DATA + 0x3000
POOL = DATA + 0x4000                          # 98 records of 24 bytes
LAIR = DATA + 0x5000
TEXTS = DATA + 0x5200


def ui_base(rng):
    """Random input state of the screens: five knights with inventories, the cel table, the button array, the record pool."""
    m = AE.Mem()
    for o in range(0, 0x2000):
        m.w8(CEL + o, rng.randrange(256))
    for o in range(0, 0x400):
        m.w8(BUTTONS + o, rng.randrange(256))
    for o in range(0, 98 * 24):
        m.w8(POOL + o, 0)
    for i in range(rng.choice([0, 1, 5, 20, 60])):                          # used records, then free ones
        for o in range(24):
            m.w8(POOL + 24 * i + o, rng.randrange(256))
        m.w16(POOL + 24 * i + 4, rng.randrange(1, 70))
        m.w32(POOL + 24 * i + 8, rng.choice([0, TEXTS + 8 * rng.randrange(8)]))
    for o in range(0, 24 * 8):
        m.w8(INVS + o, rng.randrange(256))
    for i in range(6):
        m.w8(INVS + 24 * i + 6, rng.randrange(6))                           # the ring count (max hp stays < 1000)
        m.w8(INVS + 24 * i + 2, rng.choice([0, 1, 2, 3, 4, 5, 9]))
        m.w8(INVS + 24 * i + 0, rng.choice([0, 1, 2, 3, 4, 5, 9]))
        m.w8(INVS + 24 * i + 8, rng.choice([0, 1, 2, 3, 4, 5, 9]))
        for q in range(5):                                                  # the five potion bytes (negative ones too)
            m.w8(INVS + 24 * i + 10 + 2 * q, rng.choice([0, 0, 1, 2, 3, 7, 0x80, 0xFF]))
        m.w8(INVS + 24 * i + 4, rng.choice([0, 1, 0, 2]))
    for o in range(0, 64):
        m.w8(NAMES + o, rng.randrange(65, 90))
    m.w8(NAMES + 8, 0)
    for i, k in enumerate(KN):
        for o in range(0, 132):
            m.w8(k + o, rng.randrange(256))
        m.w8(k + 71, rng.randrange(1, 9))                                   # the stat bytes are small in the game (an icon each,
        m.w8(k + 70, rng.randrange(0, 9))                                   # and the record pool holds 98 regions)
        m.w8(k + 72, rng.randrange(0, 9))
        m.w8(k + 73, rng.choice([0, 1, 3, 5, 6, 0xFF]))
        m.w8(k + 76, rng.randrange(0, 12))
        m.w16(k + 74, rng.choice([0, 1, 5, 99, 500, 999]))
        m.w16(k + 78, rng.choice([0, 1, 5, 99, 999]))
        m.w16(k + 80, rng.choice([0, 1, 5, 99, 500, 999]))
        m.w16(k + 84, rng.choice([1, 5, 99, 500, 999]))
        m.w8(k + 82, rng.choice([0, 0, 3]))
        m.w32(k + 88, rng.choice([0x16, 0x17, 0x18, 0x19, 0x1A, 0x10, 0x30]))
        m.w32(k + 92, rng.choice([0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x10, 0x30]))
        m.w32(k + 96, INVS + 24 * i)
        m.w32(k + 108, NAMES + 4 * (i % 4))
        m.w32(k + 54, rng.choice([0, 1, 2, 3, 4, 5]))
    for o in range(0, 20):
        m.w8(LAIR + o, rng.randrange(256))
    m.w32(LAIR + 0, INVS + 24 * rng.randrange(6))
    m.w16(LAIR + 8, rng.choice([0, 0, 1, 50, 999]))
    for o in range(0, 64):
        m.w8(TEXTS + o, rng.randrange(256))
    m.w32(sym('SECSTRT_14'), POOL)
    m.w32(sym('0986'), CEL)
    m.w32(sym('0688'), BUTTONS)
    for o in range(24):
        m.w8(sym('0A58') + o, rng.randrange(256))
    m.w32(sym('0987'), rng.randrange(1 << 32))
    m.w32(sym('0988'), rng.randrange(1 << 32))
    m.w32(sym('0989'), rng.randrange(1 << 32))
    m.w32(sym('068F'), rng.choice([1, 2, 3, 5, 6, 8, 9, 10, 11]))
    m.w16(sym('0985'), rng.choice([0, 0x4A, 0x96]))
    m.w16(sym('0680'), rng.choice([0, 1]))
    for c in ('0681', '0682', '0683', '0684', '0685', '0687', '0D05'):
        m.w16(sym(c), rng.randrange(0x10000) if rng.random() < 0.3 else rng.randrange(0x40))
    m.w32(sym('0686'), rng.randrange(0x100))
    m.w32(sym('0632'), rng.choice(KN))                       # the knight the stat sheet draws
    m.w32(sym('0633'), rng.choice(KN))
    m.w32(sym('068B'), rng.choice(KN[:4]))
    m.w32(sym('068D'), rng.choice(KN[:4]))
    m.w32(sym('068E'), INVS + 24 * rng.randrange(6))
    m.w32(sym('08C6'), LAIR)
    m.w32(sym('0D92'), rng.randrange(1 << 32))
    m.w32(sym('05C0'), rng.randrange(1 << 32))
    m.w32(sym('05C1'), rng.randrange(1 << 32))
    m.w32(sym('05C2'), rng.randrange(1 << 32))
    m.w32(sym('0909'), rng.randrange(1 << 32))
    m.w16(sym('090B'), rng.choice([0, 1, 2, 3, 4, 5]))
    m.w32(sym('0E8D'), rng.randrange(1 << 32))
    m.w32(sym('0E8E'), rng.randrange(1 << 32))
    m.w16(sym('097F'), rng.choice([0, 5, 0x13A, 0x13B, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(0x10000)]))
    m.w16(sym('0980'), rng.choice([0, 5, 0xC3, 0xC4, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(0x10000)]))
    m.w16(sym('0981'), rng.randrange(0x10000))
    m.w32(JOY0, rng.randrange(0x20) | rng.choice([0, 0, 0xFFFF0000]))
    m.w32(JOY1, rng.randrange(0x20) | rng.choice([0, 0, 0xFFFF0000]))
    return m


def ui_regs(rng, **fixed):
    """Registers as the game has them on these paths: the data registers hold words (the originals read the upper words stale)."""
    r = AE.make_regs(rng)
    r['d'] = [v & 0xFFFF for v in r['d']]
    for k, v in fixed.items():
        if k[0] == 'd':
            r['d'][int(k[1])] = v
        else:
            r['a'][int(k[1])] = v
    return r


IGN = [(sym('0514'), 4), (sym('04F6'), 4)]            # scratch cells of the original the C++ keeps in locals


@unittest.skipUnless(AE.HAVE_UC and AE.HAVE_TOOLS, 'needs unicorn, the m68k toolchain, build/reasm/mog')
class CombatUiEmuTest(AE.EmuCheck):
    base = staticmethod(ui_base)

    @classmethod
    def setUpClass(cls):
        # test-only patch: LAB_0588 falls into LAB_058A (the screen setup); the C++ ends after the tables
        cls.emu = AE.Emu(files=UI_FILES, entry='rt_cui_hit_test', stubs=UI_STUBS, rets=UI_RETS, rts_only=('0100', '058A'),
                         extra_defs={'ui_sfx_stub': SFX_STUB})
        cls.stack = (H.STACK_BASE, H.STACK_SIZE)

    def test_blob_exports(self):
        for s in ('rt_cui_pool_clear', 'rt_cui_hit_test', 'rt_cui_jingle_bad', 'rt_cui_jingle_good', 'rt_cui_cursor_tick',
                  'rt_cui_cursor_sprite', 'rt_cui_wizard', 't_stats', 't_items', 't_list', 't_lair', 't_dragon', 't_pick', 't_shop',
                  't_next', 't_tables'):
            self.assertIn(s, self.emu.syms)

    def draw(self, label, shim, n, seed, min_calls, **kw):
        kw.setdefault('regs_fn', lambda rng, mem: ui_regs(rng))
        self.check(label, shim, n, seed, keep_all=False, ignore=IGN, **kw)
        self.assertGreater(self.stats['calls'], n * min_calls, label + ': it must call the blitter / text stubs')

    # ---- the drawing routines (called by the screen loop through ScreenOps) ----
    def test_stats(self):
        self.draw('04F8', 't_stats', 30, 100, 10)

    def test_items(self):
        def inventory(rng, mem):
            mem.w32(sym('0632'), INVS + 24 * rng.randrange(6))
        self.draw('04FE', 't_items', 60, 101, 3, mem_fn=inventory)

    def test_list(self):
        self.draw('04EA', 't_list', 30, 102, 10)

    def test_loot_lair(self):
        self.draw('051B', 't_lair', 30, 103, 5)

    def test_loot_dragon(self):
        self.draw('051D', 't_dragon', 30, 104, 3)

    def test_loot_pick(self):
        self.draw('051F', 't_pick', 30, 105, 0,
                  regs_fn=lambda rng, mem: ui_regs(rng, a0=INVS + 24 * rng.randrange(6)))

    def test_shop(self):
        self.draw('0522', 't_shop', 10, 106, 6)

    def test_next_button(self):
        self.draw('0524', 't_next', 10, 107, 0.9)

    def test_tables(self):
        self.check('0588', 't_tables', 3, 108, keep_all=False, ignore=IGN)
        self.assertGreater(self.stats['bytes'], 3 * 600)

    # ---- the shims ----
    def test_pool_clear(self):
        self.check('044E', 'rt_cui_pool_clear', 5, 110)
        self.assertGreater(self.stats['bytes'], 5 * 2000)

    def test_hit_test(self):
        def regs(rng, mem):
            return ui_regs(rng, d0=rng.randrange(320), d1=rng.randrange(200))

        def pool(rng, mem):
            n = rng.choice([0, 1, 3, 10, 30])
            for i in range(n):
                base = POOL + 24 * i
                mem.w16(base + 4, rng.randrange(1, 90))
                mem.w16(base + 6, rng.randrange(1, 90))
                mem.w16(base + 12, rng.randrange(0, 330))
                mem.w16(base + 14, rng.randrange(0, 210))
                mem.w32(base + 8, rng.choice([0, TEXTS + 8 * rng.randrange(8)]))
            mem.w16(POOL + 24 * n + 4, 0)
        # the pool of ui_base has its used records first and the free ones after: rebuild it so the hit test finds boxes
        self.check('0451', 'rt_cui_hit_test', 150, 111, regs_fn=regs, mem_fn=pool, outputs=('D0', 'A0'))
        self.assertGreater(self.stats['calls'], 3, 'some cases must hit and draw the text list')

    def test_jingles(self):
        self.check('05A0', 'rt_cui_jingle_bad', 3, 112)
        self.assertGreater(self.stats['calls'], 5)
        self.check('05A1', 'rt_cui_jingle_good', 3, 113)
        self.assertGreater(self.stats['calls'], 14)

    def test_cursor_tick(self):
        self.check('057D', 'rt_cui_cursor_tick', 80, 114)

    def test_cursor_sprite(self):
        self.check('0572', 'rt_cui_cursor_sprite', 5, 115)
        self.assertGreater(self.stats['calls'], 5)

    def test_wizard(self):
        def fire(rng, mem):
            mem.w32(JOY1, 0x10)
        self.check('0456', 'rt_cui_wizard', 6, 116, mem_fn=fire)
        self.assertGreater(self.stats['calls'], 6 * 50)


if __name__ == '__main__':
    unittest.main()
