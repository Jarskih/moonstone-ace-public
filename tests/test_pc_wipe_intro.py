"""PC build: the intro's progress wipe reads its tile map (intro.stile, big-endian bytes) with be::ld16.

The map is a file image: native u16 reads saw every code byte-swapped (0x0080 -> 0x8000), the picture index (code / 80) ran off
prgWipePictures and the read crashed in rt::wipeRows (access violation at a wild address) or, where that address happened to be mapped,
blitted from a garbage source ("blitRun: blitter address outside chip RAM").  Runs build-sdl/moonstone.exe headless-fast through the
intro wipe (no --skip-intro, 400 frames) and requires a clean exit and no blit with a source outside chip RAM.

Skipped without the exe or the ADF images (same lookup as test_pc_firstlight).
"""
import os
import shutil
import subprocess
import tempfile
import unittest

from tests.test_pc_firstlight import EXE, find_adf_dir

FRAMES = 400


@unittest.skipUnless(os.path.isfile(EXE), 'build-sdl/moonstone.exe not built')
class IntroWipe(unittest.TestCase):
    def test_intro_wipe_runs_clean(self):
        adf = find_adf_dir()
        if not adf:
            self.skipTest('the three Moonstone ADF images are not available')
        env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy')
        tmp = tempfile.mkdtemp(prefix='ms_wipe_')
        try:
            r = subprocess.run([EXE, '--fast', '--frames', str(FRAMES), '--disks', adf, '--pref', os.path.join(tmp, 'pref'),
                                '--dump-frame', os.path.join(tmp, 'f.png')],
                               cwd=tmp, env=env, capture_output=True, text=True, timeout=300)
            out = r.stdout + r.stderr
            self.assertEqual(r.returncode, 0, out[-2000:])
            self.assertNotIn('outside chip RAM', out)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
