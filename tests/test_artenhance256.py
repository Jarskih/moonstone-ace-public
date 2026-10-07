"""256-colour templates (ROADMAP 4.8d): palette layout, 64-colour byte identity, de-dither, 8-plane import."""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import artconv
import artenhance
import artenhance256

ART = Path(ROOT) / 'build' / 'art'
HAVE_ART = (ART / 'export-original').is_dir() and (ART / 'export').is_dir()


def palette_of(path):
    with Image.open(path) as im:
        full = im.getpalette()
    return [tuple(full[i * 3:i * 3 + 3]) for i in range(len(full) // 3)]


def flat_palette():
    pal = [(0, 0, 0)] * 256
    pal[1], pal[2], pal[3] = (100, 100, 100), (140, 140, 140), (255, 0, 0)
    pal[64] = (120, 120, 120)
    pal[65] = (110, 110, 110)
    return pal


class Dither(unittest.TestCase):
    def test_checkerboard_becomes_the_intermediate_tone(self):
        pal = flat_palette()
        idx = np.zeros((16, 16), dtype=np.uint8)
        yy, xx = np.mgrid[2:14, 2:14]
        idx[2:14, 2:14] = 1 + ((yy + xx) & 1)
        allowed = np.array([1, 2, 3, 64], dtype=np.int32)
        result, changed, dithered = artenhance256.apply_shading256(idx, pal[:32], pal, allowed)
        inner = result[5:11, 5:11]               # clear of the silhouette
        self.assertTrue(np.all(inner == 64), inner)
        self.assertGreater(dithered, 0)
        np.testing.assert_array_equal(result == 0, idx == 0)       # transparency untouched

    def test_ordered_dot_pattern_flattened_and_hard_edge_kept(self):
        pal = flat_palette()
        idx = np.full((12, 24), 1, dtype=np.uint8)
        idx[::2, 0:12:2] = 2                       # 25% dots on the left half
        idx[:, 12:] = 3                            # hard red/grey edge, must stay sharp
        allowed = np.array([1, 2, 3, 64, 65], dtype=np.int32)
        result, _, _ = artenhance256.apply_shading256(idx, pal[:32], pal, allowed)
        self.assertEqual(len(np.unique(result[3:9, 3:9])), 1)       # flat tone, no dots left
        np.testing.assert_array_equal(result[:, 14:], idx[:, 14:])   # edge side untouched
        self.assertFalse(np.any(result[:, 12:] != 3))

    def test_isolated_pixels_and_edges_are_not_dither(self):
        pal = flat_palette()
        idx = np.full((12, 12), 1, dtype=np.uint8)
        idx[6, 6] = 2                              # single highlight pixel
        self.assertFalse(artenhance256.dither_mask(idx, pal[:32]).any())
        edge = np.ones((12, 12), dtype=np.uint8)
        edge[:, 6:] = 2
        self.assertFalse(artenhance256.dither_mask(edge, pal[:32]).any())

    def test_never_uses_index_0_or_leaves_range(self):
        pal = flat_palette()
        yy, xx = np.mgrid[0:16, 0:16]
        idx = (1 + ((yy + xx) & 1)).astype(np.uint8)
        allowed = artenhance256.allowed_indices(pal, idx, 64)
        self.assertNotIn(0, allowed.tolist())
        result, _, _ = artenhance256.apply_shading256(idx, pal[:32], pal, np.concatenate([allowed, [64]]))
        self.assertLessEqual(int(result.max()), 255)
        self.assertTrue(np.all(result != 0))


@unittest.skipUnless(HAVE_ART, 'needs build/art/export-original and build/art/export')
class SixtyFourColourIdentity(unittest.TestCase):
    def test_default_64_output_is_byte_identical(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'e64'
            artenhance.enhance(ART / 'export-original', out)
            mismatches, n = [], 0
            for f in sorted(out.rglob('*')):
                if f.is_file():
                    n += 1
                    ref = ART / 'export' / f.relative_to(out)
                    if not ref.is_file() or ref.read_bytes() != f.read_bytes():
                        mismatches.append(str(f.relative_to(out)))
            self.assertGreater(n, 2000)
            self.assertEqual(mismatches, [])
            self.assertEqual(n, sum(1 for f in (ART / 'export').rglob('*') if f.is_file()))


@unittest.skipUnless(HAVE_ART and (ART / 'export256' / 'enhancement256.json').is_file(), 'needs build/art/export256')
class Layout(unittest.TestCase):
    def test_generated_tree_layout(self):
        ex = ART / 'export256'
        self.assertGreater(artenhance256.verify_layout256(ex, ART / 'export'), 2000)

    def test_shared_block_identical_and_low_palette_untouched(self):
        ex = ART / 'export256'
        rep = json.loads((ex / 'enhancement256.json').read_text())
        s0, loc = rep['shared_start'], rep['local_start']
        self.assertEqual((s0, loc), (64, 160))
        shared = None
        for sidecar in sorted(ex.glob('[ABC]/*/sidecar.json')):
            sc = json.loads(sidecar.read_text())
            self.assertEqual(sc['colours'], 256)
            for png in sorted(sidecar.parent.rglob('*.png')):
                pal = palette_of(png)
                self.assertEqual(len(pal), 256)
                shared = shared or pal[s0:loc]
                self.assertEqual(pal[s0:loc], shared, str(png))
        for rel in ('A/bg3.PIV/image.png', 'C/HighWood.PIV/image.png', 'B/KN1.ob/frames/000.png'):
            p256, p64, p32 = (palette_of(ART / t / rel) for t in ('export256', 'export', 'export-original'))
            self.assertEqual(p256[:64], p64[:64])
            self.assertEqual(p256[:32], p32[:32])

    def test_transparency_sprites_and_range(self):
        ex = ART / 'export256'
        rep = json.loads((ex / 'enhancement256.json').read_text())
        for rel in ('B/KN1.ob/frames/000.png', 'B/TROLL1.CEL/frames/003.png', 'C/Demon1.CEL/frames/000.png'):
            new, _ = artconv.load_indexed(ex / rel)
            old, _ = artconv.load_indexed(ART / 'export-original' / rel)
            np.testing.assert_array_equal(new == 0, old == 0)
            self.assertEqual(new.shape, old.shape)
            self.assertFalse(np.any((new >= 48) & (new < 64)))
            self.assertLess(int(new.max()), rep['local_start'])
        for rel in ('A/bg1b.PIV/image.png', 'C/tav.piv/image.png'):
            new, _ = artconv.load_indexed(ex / rel)
            old, _ = artconv.load_indexed(ART / 'export-original' / rel)
            np.testing.assert_array_equal(new == 0, old == 0)

    def test_8_plane_import_roundtrip(self):
        ex = ART / 'export256'
        with tempfile.TemporaryDirectory() as tmp:
            done = 0
            for rel in ('A/bg3.PIV', 'B/KN1.ob', 'B/test', 'C/dice.piv'):
                if not (ex / rel).is_dir():
                    continue
                path, _ = artconv.import_dir(str(ex / rel), tmp, 8)
                data = artconv.readf(path)
                sc = json.loads((ex / rel / 'sidecar.json').read_text())
                if sc['format'] == 'cel':
                    cel = artconv.parse_cel(data)
                    for e, f in zip(sc['frames'], cel['frames']):
                        if e['png']:
                            want, _ = artconv.load_indexed(ex / rel / e['png'])
                            got = artconv.planes_to_indices(cel['blob'], f['offset'], f['w'], f['h'], f['b9'])
                            np.testing.assert_array_equal(got, want)
                else:
                    recs = sc['records'] if sc['format'] == 'pivpack' else [sc]
                    at = 0
                    for k, rec in enumerate(recs):
                        piv, at = artconv.parse_piv(data, at)
                        self.assertEqual(piv['planes'], 8)
                        want, _ = artconv.load_indexed(ex / rel / rec['png'])
                        np.testing.assert_array_equal(
                            artconv.planes_to_indices(piv['body'], 0, 320, 200, 255), want)
                        suffix = '.%02d.pal' % k if sc['format'] == 'pivpack' else '.pal'
                        self.assertEqual(artconv.read_pal(path + suffix), palette_of(ex / rel / rec['png'])[:256])
                done += 1
            self.assertGreaterEqual(done, 3)
            # a 256-colour picture cannot be imported at 6 planes: the error names the file
            with self.assertRaisesRegex(artconv.ImportError_, 'does not fit 6 planes'):
                artconv.import_dir(str(ex / 'A/bg3.PIV'), tmp, 6)


class LayoutVerifier(unittest.TestCase):
    def test_detects_shared_block_mismatch_and_sprite_in_local_slot(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'enhancement256.json').write_text(json.dumps({'shared_start': 64, 'local_start': 160}))
            for sub, fmt in (('B/s', 'cel'), ('A/p', 'piv')):
                (root / sub).mkdir(parents=True)
                (root / sub / 'sidecar.json').write_text(json.dumps({'format': fmt}))
            pal = [(i, i, i) for i in range(256)]

            def save(path, values, colours):
                im = Image.fromarray(np.array(values, dtype=np.uint8), 'P')
                im.putpalette([v for c in colours for v in c])
                im.save(path)
            save(root / 'B/s/image.png', [[0, 70, 159]], pal)
            save(root / 'A/p/image.png', [[0, 70, 255]], pal)
            self.assertEqual(artenhance256.verify_layout256(root), 2)
            bad = list(pal)
            bad[100] = (1, 2, 3)
            save(root / 'A/p/image.png', [[0, 70, 255]], bad)
            with self.assertRaisesRegex(AssertionError, 'shared block mismatch'):
                artenhance256.verify_layout256(root)
            save(root / 'A/p/image.png', [[0, 70, 255]], pal)
            save(root / 'B/s/image.png', [[0, 70, 200]], pal)
            with self.assertRaisesRegex(AssertionError, 'picture-local slot'):
                artenhance256.verify_layout256(root)


if __name__ == '__main__':
    unittest.main()
