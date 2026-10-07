"""Host/emulator test support for the game data (ROADMAP 9.5d): the rules read their numbers from g_gameData (the item
tables, include/game/api/data.hpp).  On the Amiga rt/modload.cpp copies the generated kDefaults into it at start-up; a host
or emulator test has no such step, so it links a wrapper that compiles the generated mod_defaults.cpp with
MS_GAMEDATA_LIVE_COPY, which also defines an initialised g_gameData.

    from moddata_lib import GAMEDATA_REL, GAMEDATA_SOURCE
    GAMEDATA_REL      'build/gen_test/mod_live.cpp': relative to the project root (emu_lib.Blob source lists, flow_lib style)
    GAMEDATA_SOURCE   the same, absolute (the host tests' clang++ command lines)
The compile line needs `-I include` (every test has it).  The files are rewritten only when their content changed.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_moddata  # noqa: E402

GAMEDATA_REL = 'build/gen_test/mod_live.cpp'
NAMES_REL = 'build/gen_test/mod_names.cpp'   # the name -> address tables (creatures.yaml enums; ROADMAP 9.5e1)
GAMEDATA_SOURCE = os.path.join(ROOT, *GAMEDATA_REL.split('/'))


def _generate():
    out = os.path.dirname(GAMEDATA_SOURCE)
    files, outs = gen_moddata.generate(gen_moddata.SCHEMA_DIR, out, 'game/api/data.hpp')
    for path, text in outs.items():
        gen_moddata.write_if_changed(path, text)
    nl = chr(10)
    gen_moddata.write_if_changed(GAMEDATA_SOURCE, '#define MS_GAMEDATA_LIVE_COPY' + nl + '#include "mod_defaults.cpp"' + nl)


_generate()
