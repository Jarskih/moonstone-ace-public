"""Layer lint (ROADMAP 9.3c, docs/ARCHITECTURE.md section 1): which directory may include what, plus the pointer rules
for the pure rule code.  A static scan of src/ and include/ (no compiler).

Layers (path -> layer):
  L0   platform   src/rt, src/main.cpp, include/rt, include/ms*, include/game/state_bind.hpp   anything
  L1   engine     src/engine, include/engine                                                   engine/ + <stdint.h>
  L2   game API   src/game/api, include/game/api                                               engine/ (util), game/api, shared state POD types
  L3   rules      src/game/rules, include/game/rules, include/game/rules.hpp                   game/api, game/rules, shared state POD types
  DATA            src/game/data, include/game/data                                             game/api, game/data, engine/inifile
  L2b  scenes     the rest of src/game and include/game                                        engine/, game/ (not state_bind), no platform

"Platform" headers (ace/, hardware/, dos/, exec/, proto/, intuition/, rt/, ms/, game/state_bind.hpp, owned_data.hpp, hosted
libc I/O) are never allowed above L0.  Rules (L3) must not use LAB_ names in code, integer-to-pointer casts or the
Knight pointer fields.

Known violations live in tests/layers_allow.json; each entry has a reason and the ROADMAP task that removes it.  The lint
fails on any violation that is not listed AND on any listed entry that no longer occurs (so the list can only shrink).
"""
import json
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOW_FILE = os.path.join(ROOT, 'tests', 'layers_allow.json')

SKIP_DIRS = ('src/lifted', 'src/.vs', 'src/rt/gen')
SCAN_EXT = ('.cpp', '.hpp', '.h', '.c')
STD_OK = {'stdint.h', 'stddef.h', 'stdarg.h', 'stdbool.h', 'limits.h'}
# never above L0 (ARCHITECTURE 1.2 rule 1): ACE / OS / hardware headers, the platform glue and the owned cells
PLATFORM_PREFIX = ('ace/', 'hardware/', 'dos/', 'exec/', 'proto/', 'intuition/', 'devices/', 'graphics/', 'clib/',
                   'rt/', 'ms/', 'ms_linked/')
PLATFORM_EXACT = {'game/state_bind.hpp', 'owned_data.hpp', 'stdio.h', 'stdlib.h', 'string.h', 'malloc.h', 'time.h'}

STATE_POD = {'game/state.hpp', 'game/knight.hpp', 'game/constants.hpp'}   # the typed records every layer above L0 shares
L1_GAME = {'src/game/combat_script.cpp', 'src/game/creatures.cpp', 'src/game/mogjobs.cpp', 'src/game/arena.cpp',
           'src/game/combat_load.cpp', 'include/game/combat_script.hpp', 'include/game/creatures.hpp',
           'include/game/mogjobs.hpp', 'include/game/arena.hpp', 'include/game/combat_load.hpp'}   # L1 per ARCHITECTURE 1.1

KNIGHT_POINTER_FIELDS = ('ulInventory', 'ulEngagedWith', 'ulWalkScripts')


def rel(p):
    return os.path.relpath(p, ROOT).replace('\\', '/')


def layer_of(path):
    """path relative to the project root -> layer name, or None when not scanned."""
    if any(path == d or path.startswith(d + '/') for d in SKIP_DIRS):
        return None
    if path.startswith(('include/ms/', 'include/ms_linked/')) or path == 'src/main.cpp' or path.startswith('src/rt/'):
        return 'L0'
    if path.startswith('include/rt/') or path == 'include/game/state_bind.hpp':
        return 'L0'
    if path.startswith(('src/engine/', 'include/engine/')):
        return 'L1'
    if path in L1_GAME:
        return 'L1g'
    if path.startswith(('src/game/api/', 'include/game/api/')):
        return 'L2'
    if path.startswith(('src/game/rules/', 'include/game/rules/')) or path == 'include/game/rules.hpp':
        return 'L3'
    if path.startswith(('src/game/data/', 'include/game/data/')):
        return 'DATA'
    if path.startswith(('src/game/', 'include/game/')):
        return 'L2b'
    return None


def is_platform(inc):
    return inc.startswith(PLATFORM_PREFIX) or inc in PLATFORM_EXACT or re.match(r'owned_data\.', os.path.basename(inc))


def allowed(layer, inc):
    """Is `inc` an allowed include for a file of `layer`?  (platform headers are handled by the caller)"""
    if inc in STD_OK:
        return True
    if '/' not in inc:
        return True                       # same-directory header
    if layer == 'L0':
        return True
    if layer == 'L1':
        return inc.startswith('engine/')
    if layer == 'L1g':                    # engine-level game code: engine + game types, but not the API / rules above it
        return inc.startswith('engine/') or (inc.startswith('game/') and not inc.startswith(('game/api/', 'game/rules', 'game/scene_', 'game/data/')))
    if layer == 'L2':
        return inc.startswith(('game/api/', 'engine/')) or inc in STATE_POD
    if layer == 'L3':
        return inc.startswith(('game/api/', 'game/rules/')) or inc == 'game/rules.hpp' or inc in STATE_POD
    if layer == 'DATA':
        return inc.startswith(('game/api/', 'game/data/')) or inc == 'engine/inifile.hpp' or inc in STATE_POD
    if layer == 'L2b':
        return inc.startswith(('engine/', 'game/'))
    return False


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def scan_files():
    for top in ('src', 'include'):
        for d, _dirs, files in os.walk(os.path.join(ROOT, top)):
            for f in files:
                if f.endswith(SCAN_EXT):
                    p = rel(os.path.join(d, f))
                    if layer_of(p):
                        yield p


def lint(root_files=None, read=None):
    """-> sorted list of (file, kind, detail).  `read(path)` and `root_files` let the tests inject sources."""
    def read_file(p):
        with open(os.path.join(ROOT, p), encoding='utf-8', errors='replace') as f:
            return f.read()
    read = read or read_file
    out = set()
    for p in (root_files if root_files is not None else scan_files()):
        layer = layer_of(p)
        if not layer:
            continue
        text = read(p)
        for m in re.finditer(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]', text, flags=re.M):
            inc = m.group(1)
            if layer != 'L0' and is_platform(inc):
                out.add((p, 'include', inc))
            elif not allowed(layer, inc):
                out.add((p, 'include', inc))
        code = strip_comments(text)
        if layer in ('L2', 'L2b', 'L3', 'DATA') and p.endswith('.cpp'):
            for fld in KNIGHT_POINTER_FIELDS:
                if re.search(r'(?:\.|->)' + fld + r'\b', code):
                    out.add((p, 'knight-pointer-field', fld))
        if layer == 'L3':
            for m in re.finditer(r'\bLAB_[0-9A-Fa-f]{4}\b', code):
                out.add((p, 'lab', m.group(0)))
            if re.search(r'reinterpret_cast\s*<[^>]*\*\s*>\s*\(\s*(?:\(\s*\w+\s*\)\s*)?(?:0[xX][0-9a-fA-F]+|\d+|\w*\.?\w*(?:ul|uw)[A-Z]\w*)\b', code):
                out.add((p, 'int-to-pointer', 'reinterpret_cast from an integer'))
    return sorted(out)


def load_allow():
    with open(ALLOW_FILE, encoding='utf-8') as f:
        data = json.load(f)
    return data['allow']


def key(e):
    return (e['file'], e['kind'], e['detail'])


def check(violations, allow):
    """-> (new violations, stale allow-list entries)"""
    allowed_keys = {key(e) for e in allow}
    vio = set(violations)
    return sorted(vio - allowed_keys), sorted(allowed_keys - vio)


class Layers(unittest.TestCase):
    def test_classification(self):
        self.assertEqual(layer_of('src/engine/util.cpp'), 'L1')
        self.assertEqual(layer_of('src/game/api/party.cpp'), 'L2')
        self.assertEqual(layer_of('src/game/rules/stats.cpp'), 'L3')
        self.assertEqual(layer_of('include/game/rules.hpp'), 'L3')
        self.assertEqual(layer_of('src/game/combat.cpp'), 'L2b')
        self.assertEqual(layer_of('src/rt/files.cpp'), 'L0')
        self.assertIsNone(layer_of('src/lifted/mog/x.cpp'))

    def test_allow_list_entries_are_documented(self):
        for e in load_allow():
            self.assertTrue(e.get('reason', '').strip(), e)
            self.assertRegex(e.get('task', ''), r'^(ROADMAP )?\d+\.\d+[a-z0-9]*$', e)

    def test_tree_has_no_new_violations_and_no_stale_entries(self):
        new, stale = check(lint(), load_allow())
        msg = []
        if new:
            msg.append('NEW layer violations (fix them, or add an entry with reason + task to tests/layers_allow.json):')
            msg += ['  %s: %s %s' % v for v in new]
        if stale:
            msg.append('STALE allow-list entries (violation is gone: delete them from tests/layers_allow.json):')
            msg += ['  %s: %s %s' % v for v in stale]
        self.assertFalse(msg, '\n' + '\n'.join(msg))

    # ---- mutations: the lint must catch each forbidden pattern (injected sources, nothing on disk changes)
    def _mut(self, path, text):
        return lint([path], read=lambda p: text)

    def test_mutation_rules_include_platform(self):
        v = self._mut('src/game/rules/stats.cpp', '#include "game/rules/stats.hpp"\n#include "game/state_bind.hpp"\n')
        self.assertIn(('src/game/rules/stats.cpp', 'include', 'game/state_bind.hpp'), v)
        v = self._mut('src/game/rules/stats.cpp', '#include <ace/types.h>\n')
        self.assertEqual(len(v), 1)

    def test_mutation_game_includes_rt(self):
        v = self._mut('src/game/combat.cpp', '#include "rt/display.hpp"\n#include "game/rules.hpp"\n')
        self.assertEqual([x[2] for x in v], ['rt/display.hpp'])

    def test_mutation_engine_includes_game(self):
        v = self._mut('src/engine/util.cpp', '#include "engine/util.hpp"\n#include "game/api/world.hpp"\n')
        self.assertEqual([x[2] for x in v], ['game/api/world.hpp'])

    def test_mutation_api_includes_rules(self):
        v = self._mut('src/game/api/items.cpp', '#include "game/rules/settle.hpp"\n')
        self.assertEqual(len(v), 1)

    def test_mutation_rules_pointer_and_lab(self):
        code = 'void f(Knight &k){ Inventory *p = reinterpret_cast<Inventory *>(k.ulInventory); int a = LAB_0013; }\n// LAB_0099 in a comment is fine\n'
        v = self._mut('src/game/rules/stats.cpp', code)
        kinds = sorted(x[1] for x in v)
        self.assertEqual(kinds, ['int-to-pointer', 'knight-pointer-field', 'lab'])

    def test_clean_rules_file_passes(self):
        v = self._mut('src/game/rules/stats.cpp', '#include "game/rules/stats.hpp"\n#include <stdint.h>\n// LAB_0013\nvoid f(){}\n')
        self.assertEqual(v, [])


if __name__ == '__main__':
    unittest.main()
