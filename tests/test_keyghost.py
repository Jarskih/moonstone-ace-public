"""Keyboard ghosting model of the two co-op key sets (ROADMAP 8.2, docs/MOONSTONE2.md section 3.2).

An Amiga keyboard reports every key as a raw code that is its position in the key matrix (row = code >> 3, column = code & 7).
A matrix without diodes ghosts: when three pressed keys form an L (two share a row, two share a column) the fourth corner reads as
pressed too.  Whether the A1200 keyboard matrix has diodes (and whether the modifier keys, raw $60-$67, are scanned inside the matrix)
is NOT known here - it cannot be measured in an emulator - so this test uses the pessimistic model: every key, modifiers included,
sits in the matrix at its raw code.  It asserts the property the set choice was made for: with both sets held at once (every
combination a player can produce on purpose: no opposite directions of one set, one fire key per set) no phantom is a key the game
reacts to (End turn E, Space = status sheet, Esc, Return, Backspace).  The phantoms that remain (LShift, CapsLock, the Amiga keys,
T, F, $49, $4B) do nothing in the game.  A known transient outside that rule: W + S + D (S pressed while W is still down) makes E.
"""
import itertools
import unittest

ARROWS = {'UP': 0x4C, 'DOWN': 0x4D, 'RIGHT': 0x4E, 'LEFT': 0x4F, 'CTRL': 0x63, 'RSHIFT': 0x61}
WASD = {'W': 0x11, 'S': 0x21, 'A': 0x20, 'D': 0x22, 'ALT': 0x64}
OPPOSITES = [('UP', 'DOWN'), ('LEFT', 'RIGHT'), ('W', 'S'), ('A', 'D'), ('CTRL', 'RSHIFT')]
GAME_KEYS = {0x12: 'E (end turn)', 0x40: 'Space (status)', 0x45: 'Esc', 0x44: 'Return', 0x41: 'Backspace'}


def pos(code):
    return code >> 3, code & 7


def phantoms(pressed_codes):
    """Raw codes that read as pressed without being pressed (every L of three pressed keys gives its fourth corner)."""
    pressed = set(pos(c) for c in pressed_codes)
    out = set()
    for corner, a, b in itertools.permutations(pressed, 3):
        if a[0] == corner[0] and b[1] == corner[1] and a[1] != corner[1] and b[0] != corner[0]:
            ph = (b[0], a[1])
            if ph not in pressed:
                out.add(ph[0] * 8 + ph[1])
    return out


class KeyGhost(unittest.TestCase):
    def test_no_game_key_appears_with_both_sets_held(self):
        keys = {**ARROWS, **WASD}
        names = list(keys)
        seen = set()
        for n in range(1, len(names) + 1):
            for sub in itertools.combinations(names, n):
                if any(a in sub and b in sub for a, b in OPPOSITES):
                    continue
                ph = phantoms([keys[k] for k in sub])
                seen |= ph
                bad = ph & set(GAME_KEYS)
                self.assertFalse(bad, '%s makes %s' % (sub, [GAME_KEYS[b] for b in bad]))
        self.assertTrue(seen, 'the model should find some harmless phantoms (self check)')

    def test_the_known_transient(self):
        self.assertEqual(phantoms([0x11, 0x21, 0x22]), {0x12})    # W + S + D: E
        self.assertEqual(phantoms([0x11, 0x20, 0x21]), {0x10})    # W + A + S: Q (no function)

    def test_two_keys_never_ghost(self):
        keys = list({**ARROWS, **WASD}.values())
        for a, b in itertools.combinations(keys, 2):
            self.assertEqual(phantoms([a, b]), set())


if __name__ == '__main__':
    unittest.main()
