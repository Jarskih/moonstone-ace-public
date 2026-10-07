#!/usr/bin/env python3
"""Add conservative, edge-aware shading to indexed templates using colours 32..63.

py tools/artenhance.py                 # export -> export64
py tools/artenhance.py --in-place      # backup export to export-original, then enhance

Slots 32..47 are shared by every sprite and picture; 48..63 are picture-local.
"""
import argparse
from collections import defaultdict
import json
import os
from pathlib import Path
import shutil
import struct
import uuid

import numpy as np
from PIL import Image

import artconv


def shading_targets(indices, palette):
    """Blend neighbouring tones inside opaque regions, never across silhouettes.

    Strong colour discontinuities remain sharp. No dithering or resizing: every
    new tone belongs to an existing opaque pixel on the original pixel grid.
    """
    rgb = np.asarray(palette, dtype=np.float32)[indices]
    padded = np.pad(rgb, ((1, 1), (1, 1), (0, 0)), mode='edge')
    opaque = np.pad(indices != 0, 1, mode='edge')
    total = rgb.copy()
    count = np.ones(indices.shape, dtype=np.float32)
    for dy, dx in ((0, 1), (1, 0), (1, 2), (2, 1)):
        neighbour = padded[dy:dy + indices.shape[0], dx:dx + indices.shape[1]]
        valid = opaque[dy:dy + indices.shape[0], dx:dx + indices.shape[1]].copy()
        distance = np.sum((neighbour - rgb) ** 2, axis=2)
        valid &= (indices != 0) & (distance <= 110 ** 2)
        total += neighbour * valid[:, :, None]
        count += valid
    target = np.rint(rgb * 0.35 + (total / count[:, :, None]) * 0.65).astype(np.uint8)
    candidate = (indices != 0) & (np.sum((target.astype(float) - rgb) ** 2, axis=2) >= 8 ** 2)
    return target, candidate


def extra_colours(histogram, original, slots=16):
    """Weighted median-cut palette of the tones actually requested by the art."""
    if not histogram:
        return list(original[:slots])
    colours = np.array(list(histogram), dtype=np.uint8)
    weights = np.array(list(histogram.values()), dtype=float)
    # Bound the quantizer input size while keeping rare highlights represented.
    repeats = np.maximum(1, np.rint(weights / weights.sum() * 131072).astype(int))
    samples = np.repeat(colours, repeats, axis=0)
    image = Image.fromarray(samples.reshape(1, -1, 3), 'RGB')
    quantized = image.quantize(colors=slots, method=Image.Quantize.MEDIANCUT,
                               dither=Image.Dither.NONE)
    flat = quantized.getpalette()
    count = len(quantized.getcolors())
    result = [tuple(flat[i * 3:i * 3 + 3]) for i in range(count)]
    result += list(original[:slots - len(result)])
    return result


def apply_shading(indices, original, expanded, shade_end=64):
    target, candidate = shading_targets(indices, original)
    result = indices.copy()
    y, x = np.nonzero(candidate)
    extra = np.asarray(expanded[32:shade_end], dtype=np.int32)
    changed = 0
    for start in range(0, len(y), 8192):
        cy, cx = y[start:start + 8192], x[start:start + 8192]
        wanted = target[cy, cx].astype(np.int32)
        errors = np.sum((wanted[:, None, :] - extra[None, :, :]) ** 2, axis=2)
        best = errors.argmin(axis=1)
        old = np.asarray(original, dtype=np.int32)[indices[cy, cx]]
        old_error = np.sum((wanted - old) ** 2, axis=1)
        use = errors[np.arange(len(best)), best] < old_error * 0.5
        result[cy[use], cx[use]] = 32 + best[use]
        changed += int(use.sum())
    return result, changed


def shade_histogram(paths, original):
    histogram = defaultdict(int)
    for path in paths:
        indices, _ = artconv.load_indexed(path)
        target, candidate = shading_targets(indices, original)
        values, counts = np.unique(target[candidate], axis=0, return_counts=True)
        for colour, count in zip(values, counts):
            histogram[tuple(int(v) for v in colour)] += int(count)
    return histogram


def enhance(source, output):
    source, output = Path(source), Path(output)
    if source.resolve() == output.resolve():
        raise ValueError('source and output must differ; use --in-place for a backed-up upgrade')
    if output.exists():
        raise ValueError('output already exists: %s' % output)
    assets = []
    groups = defaultdict(list)
    for sidecar in sorted(source.glob('[ABC]/*/sidecar.json')):
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
            palette = artconv.pal_rgb_from_png(rgb, 32)
            groups[(sc['format'] == 'cel', tuple(palette))].append(path)
    if len(assets) != 77:
        raise ValueError('expected 77 asset sidecars, found %d' % len(assets))
    sprite_histogram = defaultdict(int)
    sprite_original = None
    for (sprite, original), paths in groups.items():
        if sprite:
            sprite_original = original
            for colour, count in shade_histogram(paths, original).items():
                sprite_histogram[colour] += count
    if sprite_original is None:
        raise ValueError('no sprite previews found')
    shared = extra_colours(sprite_histogram, sprite_original)
    shutil.copytree(source, output)
    report = {'method': 'edge-aware interior tone interpolation, no dithering',
              'original_indices': '0..31 retained; new shading uses 32..63',
              'palette_layout': {'32..47': 'shared sprite shades in every palette',
                                 '48..63': 'background-specific shades; unused by sprites'},
              'shared_sprite_rgb': shared,
              'palette_groups': len(groups), 'assets': len(assets), 'images': 0,
              'changed_pixels': 0, 'files': []}
    for (sprite, original), paths in groups.items():
        local = [(0, 0, 0)] * 16 if sprite else extra_colours(shade_histogram(paths, original), original)
        palette = list(original) + shared + local
        for path in paths:
            indices, _ = artconv.load_indexed(path)
            enhanced, changed = apply_shading(indices, original, palette, shade_end=48 if sprite else 64)
            dest = output / path.relative_to(source)
            with Image.open(path) as image:
                transparency = image.info.get('transparency')
            image = Image.fromarray(enhanced, 'P')
            image.putpalette([v for colour in palette for v in colour])
            kwargs = {'transparency': transparency} if transparency is not None else {}
            image.save(dest, optimize=False, **kwargs)
            with Image.open(dest) as check:
                assert check.mode == 'P' and len(check.getpalette()) == 64 * 3
                assert check.size == (indices.shape[1], indices.shape[0])
                assert np.array_equal(np.array(check) == 0, indices == 0)
                assert check.getpalette()[:96] == [v for colour in original for v in colour]
                assert check.info.get('transparency') == transparency
                assert check.getpalette()[96:144] == [v for colour in shared for v in colour]
                if sprite:
                    assert enhanced.max() < 48
            report['images'] += 1
            report['changed_pixels'] += changed
            report['files'].append({'png': path.relative_to(source).as_posix(),
                                    'changed_pixels': changed,
                                    'used_colours': int(len(np.unique(enhanced)))})
    # Contact sheets are previews only; regenerate from the enhanced frames.
    for sidecar, sc in assets:
        if sc['format'] != 'cel':
            continue
        directory = output / sidecar.parent.relative_to(source)
        items, palette = [], None
        for frame in sc['frames']:
            if frame['png']:
                indices, palette = artconv.load_indexed(directory / frame['png'])
                items.append((frame['index'], indices))
        if items:
            artconv.write_sheet(directory / 'sheet.png', items, palette)
            with Image.open(directory / 'sheet.png') as sheet:
                sheet.putpalette([v for colour in palette for v in colour])
                sheet.save(directory / 'sheet.png')
    (output / 'enhancement.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def verify_palette_layout(export):
    """Every screen has the same sprite slots; no sprite uses screen-local slots."""
    shared = None
    images = 0
    for sidecar in sorted(Path(export).glob('[ABC]/*/sidecar.json')):
        sc = json.loads(sidecar.read_text(encoding='utf-8'))
        for path in sorted(sidecar.parent.rglob('*.png')):
            if path.name.endswith('_mask.png'):
                continue
            indices, palette = artconv.load_indexed(path)
            assert len(palette) == 64, str(path)
            if shared is None:
                shared = palette[32:48]
            assert palette[32:48] == shared, 'sprite palette mismatch: %s' % path
            if sc['format'] == 'cel':
                assert indices.max() < 48, 'sprite uses background-local slot: %s' % path
            images += 1
    assert images > 0
    return images


def verify_import(export, imported):
    """Compare all rebuilt six-plane pixels and palettes against the edited PNGs."""
    checked = 0
    verify_palette_layout(export)
    for sidecar in sorted(Path(export).glob('[ABC]/*/sidecar.json')):
        sc = json.loads(sidecar.read_text(encoding='utf-8'))
        path = Path(imported) / sidecar.parent.relative_to(export)
        data = artconv.readf(path)
        if sc['format'] == 'cel':
            cel = artconv.parse_cel(data)
            assert cel['count'] == len(sc['frames'])
            for entry, frame in zip(sc['frames'], cel['frames']):
                if not entry['png']:
                    continue
                expected, _ = artconv.load_indexed(sidecar.parent / entry['png'])
                actual = artconv.planes_to_indices(cel['blob'], frame['offset'],
                                                  frame['w'], frame['h'], frame['b9'])
                np.testing.assert_array_equal(actual, expected)
            first = next((entry for entry in sc['frames'] if entry['png']), None)
            if first:
                _, palette = artconv.load_indexed(sidecar.parent / first['png'])
                assert artconv.read_pal(str(path) + '.pal') == palette[:64]
        elif sc['format'] in ('piv', 'pivpack'):
            records = sc['records'] if sc['format'] == 'pivpack' else [sc]
            at = 0
            for i, record in enumerate(records):
                piv, at = artconv.parse_piv(data, at)
                assert piv['planes'] == 6
                expected, palette = artconv.load_indexed(sidecar.parent / record['png'])
                actual = artconv.planes_to_indices(piv['body'], 0, 320, 200, 63)
                np.testing.assert_array_equal(actual, expected)
                suffix = '.%02d.pal' % i if sc['format'] == 'pivpack' else '.pal'
                assert artconv.read_pal(str(path) + suffix) == palette[:64]
            assert at == len(data)
        else:
            assert data == struct.pack('>%dH' % len(sc['words']), *sc['words'])
        checked += 1
    assert checked == 77
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', default=artconv.DEFAULT_EXPORT)
    parser.add_argument('--out', default=os.path.join(artconv.ROOT, 'build', 'art', 'export64'))
    parser.add_argument('--in-place', action='store_true')
    parser.add_argument('--original', help='with --in-place: redo from this original template backup')
    parser.add_argument('--verify-import', metavar='DIR', help='verify rebuilt assets instead of enhancing')
    args = parser.parse_args()
    source = Path(args.export)
    if args.verify_import:
        print('verified %d rebuilt assets against enhanced PNGs' % verify_import(source, args.verify_import))
        return
    if args.in_place:
        backup = source.with_name(source.name + ('-before-split' if args.original else '-original'))
        if backup.exists():
            parser.error('backup already exists: %s (upgrade will not overwrite it)' % backup)
        # Finish and validate the enhancement before replacing the live templates.
        staging = source.with_name(source.name + '-enhancing-' + uuid.uuid4().hex[:8])
        try:
            report = enhance(args.original or source, staging)
        except Exception:
            if staging.exists():
                shutil.rmtree(staging)
            raise
        source.rename(backup)
        staging.rename(source)
        print('original templates backed up to %s' % backup)
    else:
        if args.original:
            parser.error('--original requires --in-place')
        report = enhance(source, args.out)
    print('enhanced %(assets)d assets, %(images)d images, %(changed_pixels)d pixels; '
          '%(palette_groups)d palette groups with 16 universal sprite shades' % report)


if __name__ == '__main__':
    main()
