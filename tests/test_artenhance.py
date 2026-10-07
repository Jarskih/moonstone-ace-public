"""Shading must preserve silhouettes and use a consistent indexed palette."""
import os
import sys
import tempfile
from pathlib import Path
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(__file__)), 'tools'))
import artenhance


class Shading(unittest.TestCase):
    def test_sprite_never_uses_background_slots_even_when_they_match_better(self):
        palette = [(0, 0, 0), (80, 80, 80), (140, 140, 140)] + [(0, 0, 0)] * 29
        indices = np.array([[1, 2], [1, 2]], dtype=np.uint8)
        expanded = palette + [(89, 89, 89), (131, 131, 131)] + [(0, 0, 0)] * 14
        expanded += [(88, 88, 88), (132, 132, 132)] + [(0, 0, 0)] * 14
        result, changed = artenhance.apply_shading(indices, palette, expanded, shade_end=48)
        self.assertGreater(changed, 0)
        self.assertLess(int(result.max()), 48)

    def test_layout_verification_detects_incompatible_screen_and_sprite_pixels(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            sprite = root / 'B' / 'sprite'
            picture = root / 'A' / 'picture'
            sprite.mkdir(parents=True)
            picture.mkdir(parents=True)
            (sprite / 'sidecar.json').write_text('{"format": "cel"}')
            (picture / 'sidecar.json').write_text('{"format": "piv"}')
            palette = [(i, i, i) for i in range(64)]
            indices = np.array([[0, 32, 47]], dtype=np.uint8)
            def save(path, values, colours):
                image = artenhance.Image.fromarray(values, 'P')
                image.putpalette([v for colour in colours for v in colour])
                image.save(path)
            save(sprite / 'image.png', indices, palette)
            save(picture / 'image.png', indices, palette)
            self.assertEqual(artenhance.verify_palette_layout(root), 2)
            wrong = palette.copy()
            wrong[40] = (255, 0, 0)
            save(picture / 'image.png', indices, wrong)
            with self.assertRaisesRegex(AssertionError, 'palette mismatch'):
                artenhance.verify_palette_layout(root)
            save(picture / 'image.png', indices, palette)
            save(sprite / 'image.png', np.array([[48]], dtype=np.uint8), palette)
            with self.assertRaisesRegex(AssertionError, 'background-local slot'):
                artenhance.verify_palette_layout(root)

    def test_intermediate_tones_preserve_transparency_and_sharp_edges(self):
        palette = [(0, 0, 0)] * 32
        palette[1:4] = [(80, 80, 80), (140, 140, 140), (255, 0, 0)]
        indices = np.array([[0, 0, 0, 0, 0], [0, 1, 2, 3, 0],
                            [0, 1, 2, 3, 0], [0, 0, 0, 0, 0]], dtype=np.uint8)
        extra = [(93, 93, 93), (127, 127, 127)] + [(0, 0, 0)] * 30
        result, changed = artenhance.apply_shading(indices, palette, palette + extra)
        self.assertGreater(changed, 0)
        np.testing.assert_array_equal(result == 0, indices == 0)
        np.testing.assert_array_equal(result[:, 3], indices[:, 3])
        self.assertTrue(np.all((result == indices) | (result >= 32)))

    def test_flat_regions_are_unchanged(self):
        palette = [(0, 0, 0), (100, 100, 100)] + [(0, 0, 0)] * 30
        indices = np.ones((8, 8), dtype=np.uint8)
        result, changed = artenhance.apply_shading(indices, palette, palette + palette)
        np.testing.assert_array_equal(result, indices)
        self.assertEqual(changed, 0)


if __name__ == '__main__':
    unittest.main()
