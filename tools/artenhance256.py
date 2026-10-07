#!/usr/bin/env python3
"""256-colour (8-plane) art templates, generated from the original 32-colour templates (ROADMAP 4.8d, art side).

py tools/artenhance256.py                  # build/art/export-original -> build/art/export256
py tools/artenhance256.py --measure        # print the shade-target measurements behind the palette split
py tools/artenhance256.py --verify DIR     # check the palette layout of an export256 tree

Palette layout (docs/ART.md section "256-colour templates"):
  0..31    original palette (picture palette, or the bg1a preview for sprites)  -- as in the 64-colour templates
  32..47   16 shared sprite shades                                              -- as in the 64-colour templates
  48..63   16 picture-local shades (black in sprite previews)                   -- as in the 64-colour templates
  64..(64+SHARED-1)   shared sprite/cel/font shades, identical in EVERY palette (sprites are drawn over any picture)
  (64+SHARED)..255    picture-local shades (black in sprite previews)
Index 0 stays black and transparent in sprites; sprites never use a picture-local index. Nothing is resized, nothing
crosses colour 0, no detail is invented: finer tones are only blends/averages of what the pixel's own neighbourhood has.
"""
import argparse
from collections import defaultdict
import json
import os
from pathlib import Path
import shutil
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import artconv
import artenhance

SHARED_START = 64
SHARED_COUNT = 96            # 64..159, see docs/ART.md for the measurements behind the split
LOCAL_START = SHARED_START + SHARED_COUNT
PALETTE_SIZE = 256
DITHER_DIST = 150            # a dither pair further apart than this (RGB distance) is a real edge, not a tone
BLEND_DIST = 110             # neighbours further apart than this are never blended (same as the 64-colour shader)
KERNEL = ((1, 2, 1), (2, 4, 2), (1, 2, 1))


def _shifted(a, dy, dx, pad):
    """a shifted so that result[y, x] = a[y + dy, x + dx] (edge replicated); pad >= |dy|,|dx|."""
    h, w = a.shape[0] - 2 * pad, a.shape[1] - 2 * pad
    return a[pad + dy:pad + dy + h, pad + dx:pad + dx + w]


def dither_mask(indices, palette):
    """Pixels inside a flat two-tone checker / ordered-dither area.

    A pixel qualifies when its 5x5 neighbourhood holds exactly two indices a,b, no colour 0, the two colours are
    within DITHER_DIST, the minority index appears at least 4 times and never 4-adjacent to itself (isolated dots,
    which is what checkerboards and 25%/75% ordered patterns look like; a real edge has connected runs).
    """
    h, w = indices.shape
    pad = 2
    p = np.pad(indices, pad, mode='edge').astype(np.int32)
    win = [_shifted(p, dy, dx, pad) for dy in range(-2, 3) for dx in range(-2, 3)]
    stack = np.stack(win)                              # (25, h, w)
    lo, hi = stack.min(axis=0), stack.max(axis=0)
    # exactly two distinct values: every element is lo or hi, both present
    two = np.all((stack == lo) | (stack == hi), axis=0) & (lo != hi) & (lo != 0)
    pal = np.asarray(palette, dtype=np.float32)
    far = np.sum((pal[lo.clip(0, len(pal) - 1)] - pal[hi.clip(0, len(pal) - 1)]) ** 2, axis=-1)
    two &= far <= DITHER_DIST ** 2
    is_hi = stack == hi
    n_hi = is_hi.sum(axis=0)
    minority_is_hi = n_hi < (25 - n_hi)
    minority = np.where(minority_is_hi[None], is_hi, ~is_hi) & two[None]
    count = minority.sum(axis=0)
    grid = minority.reshape(5, 5, h, w)
    adj = (grid[:, :-1] & grid[:, 1:]).sum(axis=(0, 1)) + (grid[:-1, :] & grid[1:, :]).sum(axis=(0, 1))
    return two & (count >= 4) & (adj == 0)


def shading_targets256(indices, palette):
    """(target rgb uint8 (h,w,3), candidate bool (h,w), dithered bool (h,w))."""
    rgb = np.asarray(palette, dtype=np.float32)[indices]
    h, w = indices.shape
    opaque = indices != 0
    # soft interior edges: 3x3 gaussian over neighbours that are opaque and close in colour
    p_rgb = np.pad(rgb, ((1, 1), (1, 1), (0, 0)), mode='edge')
    p_op = np.pad(opaque, 1, mode='edge')
    total = rgb * KERNEL[1][1]
    weight = np.full((h, w), float(KERNEL[1][1]), dtype=np.float32)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dy == 0 and dx == 0:
                continue
            k = float(KERNEL[dy + 1][dx + 1])
            nb = _shifted(p_rgb, dy, dx, 1)
            ok = _shifted(p_op, dy, dx, 1) & opaque
            ok = ok & (np.sum((nb - rgb) ** 2, axis=2) <= BLEND_DIST ** 2)
            total += nb * (k * ok)[:, :, None]
            weight += k * ok
    soft = total / weight[:, :, None]
    target = rgb * 0.30 + soft * 0.70
    # de-dither: the 3x3 gaussian over a two-tone dither is exactly the intended mid tone
    dith = dither_mask(indices, palette)
    p_rgb2 = p_rgb
    dtotal = np.zeros_like(rgb)
    dw = 0.0
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            k = float(KERNEL[dy + 1][dx + 1])
            dtotal += _shifted(p_rgb2, dy, dx, 1) * k
            dw += k
    target = np.where(dith[:, :, None], dtotal / dw, target)
    target = np.rint(target).astype(np.uint8)
    candidate = opaque & (dith | (np.sum((target.astype(np.float32) - rgb) ** 2, axis=2) >= 4 ** 2))
    return target, candidate, dith


def target_histogram(paths, original):
    histogram = defaultdict(int)
    for path in paths:
        indices, _ = artconv.load_indexed(path)
        target, candidate, _ = shading_targets256(indices, original)
        values, counts = np.unique(target[candidate], axis=0, return_counts=True)
        for colour, count in zip(values, counts):
            histogram[tuple(int(v) for v in colour)] += int(count)
    return histogram


def allowed_indices(palette, indices, upto):
    """Indices a pixel of this image may be remapped to: no transparent 0, no padding black duplicates."""
    used = set(int(v) for v in np.unique(indices))
    keep = []
    for i in range(1, upto):
        if tuple(palette[i]) != (0, 0, 0) or i in used:
            keep.append(i)
    return np.array(keep, dtype=np.int32)


def apply_shading256(indices, original, palette, allowed):
    """Remap candidate pixels to the nearest allowed palette entry. Returns (result, changed, dithered_count)."""
    target, candidate, dith = shading_targets256(indices, original)
    result = indices.copy()
    y, x = np.nonzero(candidate)
    pal = np.asarray(palette, dtype=np.int32)
    cols = pal[allowed]
    old_all = pal[indices[y, x]]
    changed = 0
    for start in range(0, len(y), 4096):
        sl = slice(start, start + 4096)
        cy, cx = y[sl], x[sl]
        wanted = target[cy, cx].astype(np.int32)
        errors = np.sum((wanted[:, None, :] - cols[None, :, :]) ** 2, axis=2)
        best = errors.argmin(axis=1)
        old_error = np.sum((wanted - old_all[sl]) ** 2, axis=1)
        use = errors[np.arange(len(best)), best] < old_error * 0.6
        use |= dith[cy, cx] & (errors[np.arange(len(best)), best] <= old_error)
        use &= allowed[best] != indices[cy, cx]
        result[cy[use], cx[use]] = allowed[best[use]]
        changed += int(use.sum())
    return result, changed, int(dith.sum())


# ---------------------------------------------------------------- generator

def gather(source):
    """Same grouping as artenhance.enhance: (is_sprite, original palette) -> png paths."""
    assets, groups = [], defaultdict(list)
    for sidecar in sorted(Path(source).glob('[ABC]/*/sidecar.json')):
        sc = json.loads(sidecar.read_text(encoding='utf-8'))
        directory = sidecar.parent
        if sc['format'] == 'cel':
            pngs = [f['png'] for f in sc['frames'] if f['png']]
        elif sc['format'] == 'piv':
            pngs = [sc['png']]
        elif sc['format'] == 'pivpack':
            pngs = [rec['png'] for rec in sc['records']]
        else:
            pngs = []
        assets.append((sidecar, sc))
        for png in pngs:
            path = directory / png
            indices, rgb = artconv.load_indexed(path)
            if indices.max() >= 32 or any(any(c) for c in rgb[32:]):
                raise ValueError('already enhanced or edited upper palette: %s' % path)
            groups[(sc['format'] == 'cel', tuple(artconv.pal_rgb_from_png(rgb, 32)))].append(path)
    if len(assets) != 77:
        raise ValueError('expected 77 asset sidecars, found %d' % len(assets))
    return assets, groups


def fit(colours, n):
    """Exactly n entries: truncate, or pad with black (unused slots)."""
    return (list(colours) + [(0, 0, 0)] * n)[:n]


def save_png(dest, indices, palette, transparency):
    image = Image.fromarray(indices, 'P')
    flat = [v for colour in palette for v in colour]
    image.putpalette(flat + [0] * (768 - len(flat)))
    kwargs = {'transparency': transparency} if transparency is not None else {}
    image.save(dest, optimize=False, **kwargs)


def enhance256(source, output, shared_count=SHARED_COUNT, log=print):
    source, output = Path(source), Path(output)
    if source.resolve() == output.resolve():
        raise ValueError('source and output must differ')
    if output.exists():
        raise ValueError('output already exists: %s' % output)
    local_start = SHARED_START + shared_count
    assets, groups = gather(source)
    # 0..63 exactly as the 64-colour templates: same calls, same inputs, same result
    sprite_hist64, sprite_original = defaultdict(int), None
    for (sprite, original), paths in groups.items():
        if sprite:
            sprite_original = original
            for colour, count in artenhance.shade_histogram(paths, original).items():
                sprite_hist64[colour] += count
    shared64 = artenhance.extra_colours(sprite_hist64, sprite_original)
    # shared 64..: sprite/cel/font targets only
    sprite_hist = defaultdict(int)
    for (sprite, original), paths in groups.items():
        if sprite:
            for colour, count in target_histogram(paths, original).items():
                sprite_hist[colour] += count
    shared = fit(artenhance.extra_colours(sprite_hist, sprite_original, slots=shared_count), shared_count)
    shutil.copytree(source, output)
    report = {'method': 'edge-aware 3x3 tone blending + flat two-tone dither removal, 8 planes / 256 colours',
              'palette_layout': {'0..31': 'original palette', '32..47': 'shared sprite shades (64-colour layout)',
                                 '48..63': 'picture-local shades (64-colour layout)',
                                 '%d..%d' % (SHARED_START, local_start - 1): 'shared sprite/cel/font shades, identical in every palette',
                                 '%d..255' % local_start: 'picture-local shades; unused by sprites'},
              'shared_start': SHARED_START, 'shared_count': shared_count, 'local_start': local_start,
              'shared_rgb': shared, 'palette_groups': len(groups), 'assets': len(assets), 'images': 0,
              'changed_pixels': 0, 'dithered_pixels': 0, 'files': []}
    for (sprite, original), paths in groups.items():
        base = list(original) + shared64
        if sprite:
            local64 = [(0, 0, 0)] * 16
            local = [(0, 0, 0)] * (256 - local_start)
        else:
            local64 = artenhance.extra_colours(artenhance.shade_histogram(paths, original), original)
            # picture-local 256 block: targets the 0..63 + shared colours still miss by more than a few units
            fixed = base + local64 + shared
            fixed_arr = np.asarray(fixed, dtype=np.int32)
            miss = defaultdict(int)
            for path in paths:
                indices, _ = artconv.load_indexed(path)
                target, candidate, dith = shading_targets256(indices, original)
                want = target[candidate].astype(np.int32)
                if not len(want):
                    continue
                best = np.empty(len(want))
                for s in range(0, len(want), 8192):
                    best[s:s + 8192] = np.sum((want[s:s + 8192, None, :] - fixed_arr[None, 1:, :]) ** 2, axis=2).min(axis=1)
                far = want[best > 6 ** 2]
                values, counts = np.unique(far, axis=0, return_counts=True)
                for colour, count in zip(values, counts):
                    miss[tuple(int(v) for v in colour)] += int(count)
            local = fit(artenhance.extra_colours(miss, original, slots=256 - local_start), 256 - local_start)
        palette = list(original) + shared64 + local64 + shared + local
        assert len(palette) == PALETTE_SIZE
        for path in paths:
            indices, _ = artconv.load_indexed(path)
            upto = 48 if sprite else 64
            allowed = np.concatenate([allowed_indices(palette, indices, upto),
                                      np.arange(SHARED_START, local_start, dtype=np.int32)]
                                     + ([] if sprite else [np.arange(local_start, 256, dtype=np.int32)]))
            enhanced, changed, dithered = apply_shading256(indices, original, palette, allowed)
            dest = output / path.relative_to(source)
            with Image.open(path) as image:
                transparency = image.info.get('transparency')
            save_png(dest, enhanced, palette, transparency)
            with Image.open(dest) as check:
                assert check.mode == 'P' and len(check.getpalette()) == 768
                assert check.size == (indices.shape[1], indices.shape[0])
                assert np.array_equal(np.array(check) == 0, indices == 0)
                assert check.info.get('transparency') == transparency
            if sprite:
                assert not np.any((enhanced >= 48) & (enhanced < SHARED_START)) and enhanced.max() < local_start
            report['images'] += 1
            report['changed_pixels'] += changed
            report['dithered_pixels'] += dithered
            report['files'].append({'png': path.relative_to(source).as_posix(), 'changed_pixels': changed,
                                    'dithered_pixels': dithered, 'used_colours': int(len(np.unique(enhanced)))})
    # sidecars: eight planes
    for sidecar, sc in assets:
        out_sc = output / sidecar.relative_to(source)
        sc['colours'] = 256
        sc['planes_original'] = sc.get('planes')
        if sc['format'] == 'piv':
            sc['planes'] = 8
        elif sc['format'] == 'pivpack':
            for rec in sc['records']:
                rec['planes'] = 8
        out_sc.write_text(json.dumps(sc, indent=1) + '\n', encoding='utf-8')
        if sc['format'] != 'cel':
            continue
        directory = output / sidecar.parent.relative_to(source)
        items, pal = [], None
        for frame in sc['frames']:
            if frame['png']:
                indices, pal = artconv.load_indexed(directory / frame['png'])
                items.append((frame['index'], indices))
        if items:
            artconv.write_sheet(directory / 'sheet.png', items, pal)
    (output / 'enhancement256.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def verify_layout256(export, export64=None):
    """Shared block identical everywhere, 0..63 as in the 64-colour tree, no sprite in a local slot."""
    export = Path(export)
    rep = json.loads((export / 'enhancement256.json').read_text(encoding='utf-8'))
    s0, local = rep['shared_start'], rep['local_start']
    shared, images = None, 0
    for sidecar in sorted(export.glob('[ABC]/*/sidecar.json')):
        sc = json.loads(sidecar.read_text(encoding='utf-8'))
        for path in sorted(sidecar.parent.rglob('*.png')):
            if path.name.endswith('_mask.png'):
                continue
            indices, pal = artconv.load_indexed(path)
            with Image.open(path) as im:
                full = im.getpalette()
            assert len(full) == 768, str(path)
            pal = [tuple(full[i * 3:i * 3 + 3]) for i in range(256)]
            assert int(indices.max()) <= 255
            if shared is None:
                shared = pal[s0:local]
            assert pal[s0:local] == shared, 'shared block mismatch: %s' % path
            if export64:
                assert pal[:64] == pal_of(export64, path)[:64], '0..63 changed: %s' % path
            if sc['format'] == 'cel':
                assert not np.any((indices >= 48) & (indices < s0)) and int(indices.max()) < local, \
                    'sprite uses a picture-local slot: %s' % path
            images += 1
    assert images > 0
    return images


def pal_of(export64, path64_like):
    rel = Path(path64_like)
    # path inside an export256 tree -> same relative path in export64
    parts = rel.parts
    for i, part in enumerate(parts):
        if part in ('A', 'B', 'C') and i + 1 < len(parts):
            target = Path(export64).joinpath(*parts[i:])
            break
    with Image.open(target) as im:
        full = im.getpalette()
    return [tuple(full[j * 3:j * 3 + 3]) for j in range(len(full) // 3)]


def measure(source, log=print):
    """Shade-target counts and quantization error vs slot budget (the numbers behind the split)."""
    assets, groups = gather(source)
    sprite_hist, sprite_original, sprite_pixels = defaultdict(int), None, 0
    pictures = []
    for (sprite, original), paths in groups.items():
        hist = target_histogram(paths, original)
        if sprite:
            sprite_original = original
            for c, n in hist.items():
                sprite_hist[c] += n
            sprite_pixels = sum(hist.values())
        else:
            pictures.append((paths, hist))

    def err(hist, k, original):
        cols = np.array(artenhance.extra_colours(hist, original, slots=k), dtype=np.float32)
        keys = np.array(list(hist), dtype=np.float32)
        wts = np.array(list(hist.values()), dtype=np.float64)
        d = np.sqrt(np.min(np.sum((keys[:, None] - cols[None]) ** 2, axis=2), axis=1))
        return float((d * wts).sum() / wts.sum()), float(d.max())

    log('sprites/cels/fonts: %d distinct target colours over %d candidate pixels' % (len(sprite_hist), sum(sprite_hist.values())))
    for k in (16, 32, 48, 64, 80, 96, 112, 128):
        m, mx = err(sprite_hist, k, sprite_original)
        log('  shared slots %3d: mean error %.2f  max %.1f (RGB distance)' % (k, m, mx))
    counts = sorted(len(h) for _, h in pictures)
    log('backgrounds: %d palette groups, distinct targets per group min %d median %d max %d'
        % (len(pictures), counts[0], counts[len(counts) // 2], counts[-1]))
    for k in (16, 32, 48, 64, 96, 112, 128):
        errs = [err(h, k, groups_original) for (paths, h), groups_original in
                zip(pictures, [o for (s, o) in groups if not s])]
        log('  local slots %3d: mean-of-group-mean error %.2f  worst group %.2f'
            % (k, sum(e[0] for e in errs) / len(errs), max(e[0] for e in errs)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--original', default=os.path.join(artconv.ROOT, 'build', 'art', 'export-original'),
                        help='unmodified 32-colour templates')
    parser.add_argument('--out', default=os.path.join(artconv.ROOT, 'build', 'art', 'export256'))
    parser.add_argument('--shared', type=int, default=SHARED_COUNT, help='size of the shared block at 64..')
    parser.add_argument('--measure', action='store_true')
    parser.add_argument('--verify', metavar='DIR')
    parser.add_argument('--against64', help='with --verify: the 64-colour tree whose 0..63 must be unchanged')
    args = parser.parse_args()
    if args.verify:
        print('verified %d images' % verify_layout256(args.verify, args.against64))
        return
    if args.measure:
        measure(args.original)
        return
    t = time.time()
    rep = enhance256(args.original, args.out, args.shared)
    print('256-colour templates: %(assets)d assets, %(images)d images, %(changed_pixels)d changed pixels '
          '(%(dithered_pixels)d dither pixels flattened), shared %(shared_start)d..%(local_start)d' % rep
          + ', %.0fs' % (time.time() - t))


if __name__ == '__main__':
    main()
