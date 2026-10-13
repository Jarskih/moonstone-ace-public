"""ROADMAP 11.10: first light of the PC build -- build-sdl/moonstone.exe boots the real game to the title screen.

The exe is run headless-fast (`--fast --skip-intro --frames N --dump-frame`, a real window is not needed for the dump but SDL creates one; the
sandbox of a test run has a desktop) against the player's three ADF images, and the frame it dumps is compared with the WinUAE screenshot of the same
title screen (build/shots/ref/play-options-title.png, tools/shotcmp.py).

How the two pictures are compared: WinUAE shows the 320x200 low-res screen at 640x400 and its scaler blends neighbouring columns, so only the even
columns / rows of the reference are the game's own pixels.  The PC dump is compared with the reference sampled at (2x, 2y); the verdict is the
percentage of pixels that differ by more than 8 of 255 in any channel (shotcmp's rule), tolerance 0.5 %.  At the time of writing it is 0.000 %.

Skipped without the exe, the ADFs (env MS_ADF_DIR, or ../moonshard/ of the checkout the worktree belongs to) or the reference picture.
Also checks (no exe needed) that the PC copy of the work-block cells (src/rt/sdl/abs_cells.cpp) equals the generated src/rt/abs_stubs.cpp.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'build-sdl', 'moonstone.exe')
REF_NAME = os.path.join('build', 'shots', 'ref', 'play-options-title.png')
FRAMES = 2500
TOLERANCE_PERCENT = 0.5
PIXEL_TOLERANCE = 8
# stubs the title screen is allowed to touch (every one is a TODO of ROADMAP 11.1d, logged as "STUB name (ROADMAP task) called")
ALLOWED_STUBS = {'rtOwFlowBind'}


def main_checkout():
    """The checkout the worktree was made from (it holds the disks and screenshots): the parent of the git common dir."""
    try:
        r = subprocess.run(['git', 'rev-parse', '--git-common-dir'], cwd=ROOT, capture_output=True, text=True, timeout=30)
        if r.returncode == 0:
            common = os.path.normpath(os.path.join(ROOT, r.stdout.strip()))
            return os.path.dirname(common)
    except (OSError, subprocess.SubprocessError):
        pass
    return None


def find_adf_dir():
    cands = []
    if os.environ.get('MS_ADF_DIR'):
        cands.append(os.environ['MS_ADF_DIR'])
    cands.append(os.path.join(os.path.dirname(ROOT), 'moonshard'))
    main = main_checkout()
    if main:
        cands.append(os.path.join(main, 'moonshard'))
    for d in cands:
        if os.path.isdir(d) and len([f for f in os.listdir(d) if f.lower().endswith('.adf') and 'moonstone' in f.lower()]) >= 3:
            return d
    return None


def find_reference():
    cands = [os.path.join(ROOT, REF_NAME)]
    main = main_checkout()
    if main:
        cands.append(os.path.join(main, 'moonstone-ace', REF_NAME))
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


def have_pillow():
    try:
        import PIL  # noqa: F401
        return True
    except ImportError:
        return False


class AbsCells(unittest.TestCase):
    def test_pc_cells_equal_the_generated_amiga_ones(self):
        with open(os.path.join(ROOT, 'src', 'rt', 'abs_stubs.cpp'), encoding='utf-8') as f:
            stubs = f.read()
        amiga = dict((m.group(1), int(m.group(2), 16)) for m in re.finditer(r'\.set (\w+), rt_screen_work\+(0x[0-9a-f]+)', stubs))
        with open(os.path.join(ROOT, 'src', 'rt', 'sdl', 'abs_cells.cpp'), encoding='utf-8') as f:
            text = f.read()
        pc = dict((m.group(1), int(m.group(2), 16)) for m in re.finditer(r'^MS_ABS_ALIAS\((\w+), (0x[0-9a-f]+)\)', text, re.M))
        pc.pop('rt_screen_work', None)
        self.assertEqual(pc, amiga, 'the aliases of rt/sdl/abs_cells.cpp are the offsets of the generated abs_stubs.cpp')
        size = re.search(r'\.space (0x[0-9a-f]+)', stubs)
        self.assertEqual(int(size.group(1), 16), 0x18086)


@unittest.skipUnless(os.path.isfile(EXE), 'build-sdl/moonstone.exe missing (py -m unittest tests.test_sdl_build builds it)')
@unittest.skipUnless(find_adf_dir(), 'the Moonstone ADF images are not available (MS_ADF_DIR or ../moonshard/)')
class FirstLight(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.disks = os.path.join(cls.tmp.name, 'disks')
        os.makedirs(cls.disks)
        src = find_adf_dir()
        for f in os.listdir(src):
            if f.lower().endswith('.adf') and 'moonstone' in f.lower():
                shutil.copy(os.path.join(src, f), os.path.join(cls.disks, f))
        cls.dump = os.path.join(cls.tmp.name, 'title.png')
        cls.result = subprocess.run([EXE, '--fast', '--skip-intro', '--frames', str(FRAMES), '--disks', cls.disks, '--pref',
                                     os.path.join(cls.tmp.name, 'pref'), '--dump-frame', cls.dump],
                                    cwd=os.path.dirname(EXE), capture_output=True, text=True, timeout=300)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_game_runs_and_leaves_cleanly(self):
        r = self.result
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr[-3000:])
        for line in ('rtGameRun: enter program', 'rtGameRun: enter mog', 'files: open Sel.cel', 'rtGameRun: leaving'):
            self.assertIn(line, r.stderr)
        self.assertNotIn('crash report', r.stderr)

    def test_only_known_stubs_are_touched(self):
        hit = set(re.findall(r'^STUB (\w+) \(ROADMAP', self.result.stderr, re.M))
        self.assertLessEqual(hit, ALLOWED_STUBS, 'a stub that the boot path newly reaches: implement it or list it in ALLOWED_STUBS')

    @unittest.skipUnless(have_pillow(), 'Pillow missing')
    @unittest.skipUnless(find_reference(), 'the WinUAE reference of the title screen is missing (build/shots/ref/play-options-title.png)')
    def test_title_screen_matches_the_winuae_shot(self):
        import sys
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import shotcmp
        from PIL import Image
        ref = shotcmp.load(find_reference(), 'auto')                # the 640x400 display area of the WinUAE window
        pc = Image.open(self.dump).convert('RGB')
        self.assertEqual(pc.size, (320, 200))
        low = Image.new('RGB', pc.size)                              # the game's own pixels: even columns and rows of the reference
        src, dst = ref.load(), low.load()
        for y in range(200):
            for x in range(320):
                dst[x, y] = src[2 * x, 2 * y]
        percent, _ = shotcmp.compare(pc, low, PIXEL_TOLERANCE)
        self.assertLessEqual(percent, TOLERANCE_PERCENT, 'the PC title screen differs from the WinUAE one in %.3f %% of the pixels' % percent)

    @unittest.skipUnless(have_pillow(), 'Pillow missing')
    def test_the_dump_is_not_a_black_frame(self):
        from PIL import Image
        colours = set(Image.open(self.dump).convert('RGB').getdata())
        self.assertGreater(len(colours), 8, 'the title screen shows more than a few colours')


if __name__ == '__main__':
    unittest.main()
