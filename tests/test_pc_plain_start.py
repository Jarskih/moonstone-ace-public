"""PC build: the plain start (no --skip-intro) of a real player: the intro runs to the title by itself, or a key skips it.

Every earlier PC test used --skip-intro.  The intro runs the animation interpreter (ms::animRun) on frame tables that the scenes poke
at run time (LAB_0276, native u32 cells): reading a cel pointer from them as big-endian bytes (addr::ld32 of a cell that is not a
registered DC.L cell) crashed on the first animation job after music.cmp (frame ~750).  Runs build-sdl/moonstone.exe headless-fast
(SDL dummy drivers) through the whole intro to the title (no input), and with a fire at frame 40 (the skip), and requires a clean exit,
no crash report and no blit with a source outside chip RAM.

Skipped without the exe or the ADF images (same lookup as test_pc_firstlight).
"""
import os
import shutil
import subprocess
import tempfile
import unittest

from tests.test_pc_firstlight import EXE, find_adf_dir

INTRO_FRAMES = 5500    # the title is up at frame ~5250
SKIP_FRAMES = 400


@unittest.skipUnless(os.path.isfile(EXE), 'build-sdl/moonstone.exe not built')
class PlainStart(unittest.TestCase):
    def run_exe(self, frames, *extra):
        adf = find_adf_dir()
        if not adf:
            self.skipTest('the three Moonstone ADF images are not available')
        env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy')
        tmp = tempfile.mkdtemp(prefix='ms_plain_')
        try:
            r = subprocess.run([EXE, '--fast', '--no-grab', '--frames', str(frames), '--disks', adf,
                                '--pref', os.path.join(tmp, 'pref'), '--dump-frame', os.path.join(tmp, 'f.png'), *extra],
                               cwd=tmp, env=env, capture_output=True, text=True, timeout=600)
            out = r.stdout + r.stderr
            self.assertEqual(r.returncode, 0, out[-2000:])
            self.assertNotIn('crash report', out)
            self.assertNotIn('outside chip RAM', out)
            self.assertTrue(os.path.getsize(os.path.join(tmp, 'f.png')) > 2000, 'the last frame is blank')
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_intro_runs_to_the_title(self):
        self.run_exe(INTRO_FRAMES)

    def test_fire_skips_the_intro(self):
        self.run_exe(SKIP_FRAMES, '--keys', '@40 +Space @8 -Space')


if __name__ == '__main__':
    unittest.main()
