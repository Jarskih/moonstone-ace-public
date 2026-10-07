#!/usr/bin/env python3
"""Contact sheets original 32 | current 64 | new 256 (2x nearest) -> build/shots/art256-*.png (ROADMAP 4.8d)."""
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, 'build', 'art')
TREES = ('export-original', 'export', 'export256')
BACKDROP = (40, 48, 72)
SCALE = 2
GAP = 8

PICTURE_SETS = {
    'outdoor': ['A/bg1b.PIV', 'A/bg2.PIV', 'C/HighWood.PIV', 'C/WaterDeep.PIV'],
    'arena': ['A/bg3.PIV', 'A/bg4.PIV', 'C/tav.piv', 'C/hea.piv'],
}
SPRITE_SETS = {'sprites': [('B/KN1.ob', 150), ('B/TROLL1.CEL', 150), ('B/RATMEN1.CEL', 110), ('C/Demon1.CEL', 110)]}


def rgb_of(path, backdrop=None, crop=None):
    with Image.open(path) as im:
        idx = np.array(im)
        pal = np.array(im.getpalette() + [0] * 768, dtype=np.uint8)[:768].reshape(256, 3)
    out = pal[idx]
    if backdrop is not None:
        out[idx == 0] = backdrop
    if crop:
        out = out[:crop[0], :crop[1]]
    img = Image.fromarray(out, 'RGB')
    return img.resize((img.width * SCALE, img.height * SCALE), Image.NEAREST)


def triptych(rel, backdrop=None, crop=None):
    tiles = [rgb_of(os.path.join(ART, t, rel), backdrop, crop) for t in TREES]
    w, h = tiles[0].size
    sheet = Image.new('RGB', (3 * w + 2 * GAP, h), (255, 0, 255))
    for i, t in enumerate(tiles):
        sheet.paste(t, (i * (w + GAP), 0))
    return sheet


def stack(rows, name):
    width = max(r.width for r in rows)
    sheet = Image.new('RGB', (width, sum(r.height for r in rows) + GAP * (len(rows) - 1)), (255, 0, 255))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y))
        y += r.height + GAP
    out = os.path.join(ROOT, 'build', 'shots', name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    print(out, sheet.size)


def main():
    for key, files in PICTURE_SETS.items():
        stack([triptych(f + '/image.png') for f in files], 'art256-%s.png' % key)
    for key, files in SPRITE_SETS.items():
        stack([triptych(f + '/sheet.png', BACKDROP, (rows, 280)) for f, rows in files], 'art256-%s.png' % key)


if __name__ == '__main__':
    main()
