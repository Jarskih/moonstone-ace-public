"""Host-build inputs of the scene manager (ROADMAP 9.2a, docs/GAME_FLOW.md): the pure sources a host driver needs to link the
flow (the manager, the registry, every scene and what the scenes call).  Used by tests/test_flow.py and by the tests that link
src/game/mainloop.cpp or the map loop (test_mainloop.py, test_boot_map.py)."""
import glob
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import sys as _smd
_smd.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from moddata_lib import GAMEDATA_SOURCE, GAMEDATA_REL  # noqa: E402,F401


def flow_sources(exclude=()):
    """The flow sources plus their pure dependencies, without the files in `exclude` (paths relative to ROOT, '/' separated)."""
    rel = sorted(os.path.relpath(p, ROOT).replace(os.sep, '/') for p in
                 glob.glob(os.path.join(ROOT, 'src', 'game', 'flow', '*.cpp')) + glob.glob(os.path.join(ROOT, 'src', 'game', 'scenes', '*.cpp')))
    rel += ['src/engine/memstack.cpp', 'src/game/mainloop.cpp', 'src/game/overworld.cpp', 'src/game/rules/ai_map.cpp', 'src/game/rules/stats.cpp', 'src/game/rules/rituals.cpp', 'src/game/rules/dice.cpp', 'src/game/rules/shops.cpp', 'src/game/rules/healing.cpp', 'src/game/rules/levelling.cpp', 'src/game/rules/settle.cpp', 'src/game/rules/clock.cpp',
            'src/game/rules/turns.cpp', 'src/engine/util.cpp',
            'src/game/placevisit.cpp', 'src/game/scene_places.cpp', GAMEDATA_REL]
    out, seen = [], set(exclude)
    for r in rel:
        if r not in seen:
            seen.add(r)
            out.append(os.path.join(ROOT, *r.split('/')))
    return out


def flow_rel_sources():
    """Only the flow's own files (manager, registry, scenes, memstack) plus its rt glue, relative to ROOT: for the m68k emu
    tests that already list overworld / mainloop / rules and the rt files around them."""
    rel = sorted(os.path.relpath(p, ROOT).replace(os.sep, '/') for p in
                 glob.glob(os.path.join(ROOT, 'src', 'game', 'flow', '*.cpp')) + glob.glob(os.path.join(ROOT, 'src', 'game', 'scenes', '*.cpp')))
    return tuple(rel + ['src/engine/memstack.cpp', 'src/rt/flow.cpp', GAMEDATA_REL])
