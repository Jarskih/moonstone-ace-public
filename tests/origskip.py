"""origskip.py -- skip decorators for tests that need original material (ROADMAP 10.2, tools/origin.py).

A public checkout has no IRA listing and no asm/ reference (tools/publish.txt drops them); tools/setup.py adds the extracted
disks (build/disks), the executables (build/reasm stand-in) and nothing else.  Tests that compare with those sources say so:

    from origskip import need_listing, need_asm_ref, need_binaries
    @need_listing                     # class or test: the IRA listing (developer checkout)
    ...
    require_listing()                 # at module level, before imports that read the listing: skips the whole module
"""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import origin  # noqa: E402

HAVE_LISTING = origin.have_listing()
# asm/*.s + asm/patches (the reassembled listing and its patch tables): developer checkout only
HAVE_ASM_REF = os.path.isfile(os.path.join(ROOT, 'asm', 'patches', 'abs_symbols.json')) and \
    os.path.isfile(os.path.join(ROOT, 'asm', 'mog.s'))
HAVE_BINARIES = origin.have_binaries()
HAVE_DISKS = origin.have_disks()
NO_ASM_REF = 'needs asm/*.s and asm/patches (the reassembled reference listing; developer checkout only)'

need_listing = unittest.skipUnless(HAVE_LISTING, origin.NO_LISTING)
need_asm_ref = unittest.skipUnless(HAVE_ASM_REF, NO_ASM_REF)
need_binaries = unittest.skipUnless(HAVE_BINARIES, origin.NO_BINARIES)
need_disks = unittest.skipUnless(HAVE_DISKS, origin.NO_DISKS)


def require_listing():
    if not HAVE_LISTING:
        raise unittest.SkipTest(origin.NO_LISTING)


def require_asm_ref():
    if not HAVE_ASM_REF:
        raise unittest.SkipTest(NO_ASM_REF)


def need_file(path, why=None):
    """Inside a test (or setUpClass): skip it when `path` (a listing or asm reference file) is absent."""
    if not os.path.exists(path):
        raise unittest.SkipTest(why or f'needs {os.path.relpath(path, ROOT)} (developer checkout only, tools/origin.py)')


def synth_data_cpp():
    """build/test_gen/synth_data.cpp: the synth tables as compiled-in C++ (tools/gen_synth_tables.py from the original mog). Not
    in the repository since ROADMAP 10.2a (original data); generated once per run for the host / unicorn synth tests."""
    out = os.path.join(ROOT, 'build', 'test_gen', 'synth_data.cpp')
    if not getattr(synth_data_cpp, 'done', False):
        if not HAVE_BINARIES:
            raise unittest.SkipTest(origin.NO_BINARIES)
        import subprocess
        os.makedirs(os.path.dirname(out), exist_ok=True)
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gen_synth_tables.py'), '--out', out], check=True,
                       capture_output=True)
        synth_data_cpp.done = True
    return out
