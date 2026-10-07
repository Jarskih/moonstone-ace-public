"""tools/shotcmp.py: crop, tolerance, diff image, promote, exit status."""
import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

try:
    from PIL import Image
    import shotcmp
    HAVE = True
except ImportError:  # pragma: no cover
    HAVE = False


def window(color=(0, 0, 34), spot=None):
    """A 742x660 'window shot': dark frame, display area at 70,77 640x400 filled with color, optional 10x10 spot."""
    im = Image.new('RGB', (742, 660), (43, 43, 43))
    im.paste(Image.new('RGB', (640, 400), color), (70, 77))
    if spot:
        im.paste(Image.new('RGB', (10, 10), spot), (100, 100))
    return im


def winuae_window(chrome_title, color=(0, 0, 34), spot=None, tw=736, th=635):
    """A WinUAE-like window at 100 %: grey title bar (chrome_title rows), 1 px grey frame lines, black client 721 wide, grey status bar;
    the 640x400 display at (59,32) from the client origin, with a title text and a status-bar blob that must be ignored."""
    im = Image.new('RGB', (tw, th), (43, 43, 43))
    client_x, client_y = 8, chrome_title
    ch = th - chrome_title - 30
    im.paste(Image.new('RGB', (tw, ch), (0, 0, 0)), (0, client_y))                   # client plus the invisible resize border
    im.paste(Image.new('RGB', (1, ch), (43, 43, 43)), (client_x - 1, client_y))      # frame lines
    im.paste(Image.new('RGB', (1, ch), (43, 43, 43)), (client_x + 720, client_y))
    im.paste(Image.new('RGB', (640, 400), color), (client_x + 59, client_y + 32))
    if spot:
        im.paste(Image.new('RGB', (10, 10), spot), (client_x + 59 + 30, client_y + 32 + 23))
    im.paste(Image.new('RGB', (120, 12), (230, 230, 230)), (40, 8))              # title text
    im.paste(Image.new('RGB', (120, 12), (230, 230, 230)), (300, th - 22))       # status bar text
    return im


def on_canvas(im, scale, size):
    big = im.resize((round(im.width * scale), round(im.height * scale)), Image.NEAREST)
    canvas = Image.new('RGB', size, (255, 255, 255))
    canvas.paste(big, (0, 0))
    return canvas


@unittest.skipUnless(HAVE, 'needs Pillow')
class ShotCmpTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        os.makedirs(os.path.join(self.dir, 'ref'))

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, name, im, ref=False):
        im.save(os.path.join(self.dir, 'ref' if ref else '', name + '.png'))

    def run_cmp(self, *args):
        return shotcmp.main(['--shots-dir', self.dir] + list(args))

    def test_identical_passes_and_outside_crop_ignored(self):
        a = window()
        b = window()
        b.paste(Image.new('RGB', (200, 30), (255, 255, 255)), (20, 10))   # title bar text / status bar differ: not compared
        self.put('x', a, ref=True)
        self.put('x', b)
        self.assertEqual(self.run_cmp('x'), 0)

    def test_tolerance(self):
        self.put('x', window(), ref=True)
        self.put('x', window(spot=(255, 0, 0)))   # 100 of 256000 pixels = 0.039 %
        self.assertEqual(self.run_cmp('x', '--tol', '0.1'), 0)
        self.assertEqual(self.run_cmp('x', '--tol', '0.01'), 1)
        self.assertTrue(os.path.exists(os.path.join(self.dir, 'diff', 'x-diff.png')))

    def test_pixel_tolerance(self):
        self.put('x', window((0, 0, 34)), ref=True)
        self.put('x', window((0, 0, 40)))
        self.assertEqual(self.run_cmp('x', '--tol', '0'), 0)                       # 6 <= 8
        self.assertEqual(self.run_cmp('x', '--tol', '0', '--pixel-tol', '2'), 1)

    def test_compare_percent_and_diff_marks(self):
        pct, diff = shotcmp.compare(window().crop((70, 77, 710, 477)), window(spot=(255, 255, 255)).crop((70, 77, 710, 477)))
        self.assertAlmostEqual(pct, 100.0 * 100 / (640 * 400), places=6)
        self.assertEqual(diff.getpixel((30, 23)), (255, 0, 0))   # the spot at (100,100) in window coordinates
        self.assertNotEqual(diff.getpixel((200, 200)), (255, 0, 0))

    def test_missing_and_promote(self):
        self.assertEqual(self.run_cmp('nope'), 2)
        self.put('y', window())
        self.assertEqual(self.run_cmp('y'), 2)                     # no reference yet
        self.assertEqual(self.run_cmp('y', '--promote'), 0)
        self.assertTrue(os.path.exists(os.path.join(self.dir, 'ref', 'y.png')))
        self.assertEqual(self.run_cmp('y'), 0)
        self.assertEqual(self.run_cmp('--all'), 0)

    def test_size_mismatch_fails(self):
        self.put('z', window(), ref=True)
        self.put('z', Image.new('RGB', (800, 700)))
        self.assertEqual(self.run_cmp('z', '--no-crop'), 1)

    def test_display_found_independent_of_scale_and_title_bar(self):
        ref = winuae_window(45)                                   # the old 742-ish capture: tall title bar
        self.assertEqual(shotcmp.find_display_box(ref)[2:], (640.0, 400.0))
        self.put('w', ref, ref=True)
        self.put('w', winuae_window(31))                          # a shorter title bar at 100 %
        self.assertEqual(self.run_cmp('w'), 0)
        self.put('w', on_canvas(winuae_window(31), 1.5, (1104, 953)))   # the same window captured at 150 % into a larger white canvas
        self.assertEqual(self.run_cmp('w'), 0)
        self.put('w', on_canvas(winuae_window(31), 1.25, (1104, 953)))
        self.assertEqual(self.run_cmp('w'), 0)

    def test_changed_screen_still_fails_after_normalising(self):
        self.put('w', winuae_window(45), ref=True)
        self.put('w', on_canvas(winuae_window(31, spot=(255, 255, 255)), 1.5, (1104, 953)))
        self.assertEqual(self.run_cmp('w', '--tol', '0.01'), 1)
        self.put('w', on_canvas(winuae_window(31, color=(120, 0, 0)), 1.5, (1104, 953)))
        self.assertEqual(self.run_cmp('w'), 1)

    def test_undetectable_window_falls_back_to_fixed_crop(self):
        self.assertIsNone(shotcmp.find_display_box(Image.new('RGB', (742, 660), (200, 200, 200))))


if __name__ == '__main__':
    unittest.main()
