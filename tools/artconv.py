#!/usr/bin/env python3
"""artconv.py -- original Moonstone graphics <-> indexed PNG templates (ROADMAP 5.2b).

    py tools/artconv.py export [--disks build/disks] [--out build/art/export] [--only GLOB] [--masks]
                               [--preview-palette build/disks/A/bg1a.PIV]
    py tools/artconv.py import <export-dir> [--planes 5|6|8] [--out build/art/import/<disk>] [--min-planes|--keep-planes]
    py tools/artconv.py import --all [--export build/art/export] [--out build/art/import] [--planes 5|6]
    py tools/artconv.py palettes [--disks build/disks] [--out build/art/export]     (also part of `export`)

Formats (the asm is the authority: program.asm LAB_0496 / LAB_03FC / LAB_049C / LAB_04B5; see docs/ART.md):
  cel family (.cel .ob .c .f .font)  10-byte header (BE16 frames, BE32 packed size, BE32 = 8 * decoded size), 10 bytes
        per frame (BE32 offset, BE16 w, BE16 h, byte hot_x<<4|flags, byte plane_bits), then an LZSS stream. A frame is
        planeCount planes of (ceil(w/16)*2 bytes x h rows), planes in ascending bit order of plane_bits. The blitter
        derives the mask as the OR of the present planes, so nothing is stored: colour index 0 is transparent.
  piv (.piv .p, "mindscape")          BE16 planes (4|5), BE32 packed size, (1<<planes) palette words, LZSS stream that
        decodes to the planes stored one after another (8000 bytes each: 200 rows x 40 bytes).
  stile                               480 big-endian words, a tile index map (no pixels).
Not graphics, skipped: hunk executables (nb/program/mog/Crystal), .a (8SVX audio), .cmp (RNC music), .t (arena
object tables), .hit, kn1.ob on disk A (5-byte stub).

Needs Pillow and numpy. Output under build/ is git-ignored (the original art is personal-use material).
"""
import argparse
import fnmatch
import hashlib
import json
import os
import re
import struct
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError as e:  # pragma: no cover
    sys.exit('artconv needs Pillow and numpy: %s' % e)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DISKS = os.path.join(ROOT, 'build', 'disks')
DEFAULT_EXPORT = os.path.join(ROOT, 'build', 'art', 'export')
DEFAULT_IMPORT = os.path.join(ROOT, 'build', 'art', 'import')
MOG_ASM = os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog.asm')
SIDECAR = 'sidecar.json'
PIV_W, PIV_H, PIV_PLANE_BYTES = 320, 200, 8000
PAL_MAGIC = b'MSPL'
TOOL_VERSION = 1


# ------------------------------------------------------------------ LZSS (program.asm LAB_049C)

def lzss_decode(src):
    """Same semantics as ms::lzssDecode: flag byte MSB first; 0 = literal, 1 = BE word, bits 15-11 = 34 - length,
    bits 10-0 = distance. The source end is checked after every item. Distance 0 is rejected (the asm leaves the
    output byte as it was; no file uses it)."""
    out = bytearray()
    p, n = 0, len(src)
    while p < n:
        flags = src[p]
        p += 1
        for _ in range(8):
            if p >= n:
                break
            if not flags & 0x80:
                out.append(src[p])
                p += 1
            else:
                w = (src[p] << 8) | src[p + 1]
                p += 2
                d = w & 0x7FF
                if d == 0:
                    raise ValueError('LZSS distance 0 (undefined output)')
                if d > len(out):
                    raise ValueError('LZSS back reference before start')
                for _ in range(34 - (w >> 11)):
                    out.append(out[-d])
            flags = (flags << 1) & 0xFF
    return bytes(out)


def lzss_encode(data, max_chain=400):
    """Greedy + one-step lazy matcher over 3-byte hash chains. Window 2047, length 3..34, overlap allowed (the
    decoder copies byte by byte). The stream carries no end marker: the decoder stops when the source is used up,
    so the last flag byte just has fewer items."""
    n = len(data)
    if n == 0:
        return b''
    head = {}
    prev = [-1] * n
    inserted = [0]

    def insert_upto(pos):
        i = inserted[0]
        while i < pos and i + 3 <= n:
            key = data[i:i + 3]
            prev[i] = head.get(key, -1)
            head[key] = i
            i += 1
        inserted[0] = max(inserted[0], pos)

    def find(i):
        if i + 3 > n:
            return 0, 0
        insert_upto(i)
        maxlen = min(34, n - i)
        j = head.get(data[i:i + 3], -1)
        best_len, best_d, chain = 0, 0, 0
        while j >= 0 and i - j <= 2047 and chain < max_chain:
            m = 0
            while m < maxlen and data[j + m] == data[i + m]:
                m += 1
            if m > best_len:
                best_len, best_d = m, i - j
                if m == maxlen:
                    break
            j = prev[j]
            chain += 1
        return (best_len, best_d) if best_len >= 3 else (0, 0)

    out = bytearray()
    items = []  # (is_ref, payload)
    i = 0
    while i < n:
        ln, d = find(i)
        if ln and ln < 34:
            ln2, d2 = find(i + 1)
            if ln2 > ln:
                ln = 0
        if ln:
            items.append((1, struct.pack('>H', ((34 - ln) << 11) | d)))
            i += ln
        else:
            items.append((0, data[i:i + 1]))
            i += 1
    for k in range(0, len(items), 8):
        grp = items[k:k + 8]
        flags = 0
        for b, (is_ref, _) in enumerate(grp):
            if is_ref:
                flags |= 0x80 >> b
        out.append(flags)
        for _, pl in grp:
            out += pl
    return bytes(out)


# ------------------------------------------------------------------ palettes

def rgb12_to_rgb24(c):
    return (((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


def rgb24_to_rgb12(rgb):
    r, g, b = (int(round(v / 17.0)) for v in rgb)
    return (r << 8) | (g << 4) | b


def piv_word_to_rgb12(w):
    """program.asm LAB_03FF: BCLR #15; if bit 15 was clear the word is doubled (3-bit guns -> 4-bit)."""
    return (w & 0x7FFF) if w & 0x8000 else ((w & 0x7FFF) << 1) & 0xFFFF


def write_pal(path, rgb_list):
    with open(path, 'wb') as f:
        f.write(PAL_MAGIC + bytes([1, 0]) + struct.pack('>H', len(rgb_list)))
        for r, g, b in rgb_list:
            f.write(bytes([r, g, b]))


def read_pal(path):
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != PAL_MAGIC:
        raise ValueError('%s: not an MSPL palette' % path)
    n = struct.unpack('>H', d[6:8])[0]
    return [tuple(d[8 + 3 * i:11 + 3 * i]) for i in range(n)]


# ------------------------------------------------------------------ container parsers

def readf(path):
    with open(path, 'rb') as f:
        return f.read()


def sha1(data):
    return hashlib.sha1(data).hexdigest()


def parse_piv(data, at=0):
    """Returns (piv dict, end offset) or raises ValueError."""
    if len(data) - at < 6:
        raise ValueError('short')
    planes = struct.unpack('>H', data[at:at + 2])[0]
    packed = struct.unpack('>I', data[at + 2:at + 6])[0]
    if planes not in (4, 5, 6, 8):  # 6, 8 = enhanced output of this tool (8: tools/artenhance256.py templates)
        raise ValueError('planes %d' % planes)
    npal = 1 << planes
    end = at + 6 + 2 * npal + packed
    if end > len(data):
        raise ValueError('packed size beyond file')
    pal = list(struct.unpack('>%dH' % npal, data[at + 6:at + 6 + 2 * npal]))
    body = lzss_decode(data[at + 6 + 2 * npal:end])
    if len(body) != planes * PIV_PLANE_BYTES:
        raise ValueError('piv body is %d bytes, expected %d' % (len(body), planes * PIV_PLANE_BYTES))
    return {'planes': planes, 'packed': packed, 'pal_raw': pal, 'body': body}, end


def parse_cel(data):
    if len(data) < 10:
        raise ValueError('short')
    count, packed, bits = struct.unpack('>HII', data[:10])
    if count == 0 or len(data) != 10 + 10 * count + packed:
        raise ValueError('not a cel (size mismatch)')
    frames = []
    for i in range(count):
        off, w, h, b8, b9 = struct.unpack('>IHHBB', data[10 + 10 * i:20 + 10 * i])
        frames.append({'offset': off, 'w': w, 'h': h, 'b8': b8, 'b9': b9})
    blob = lzss_decode(data[10 + 10 * count:])
    for f in frames:
        if f['offset'] + frame_bytes(f['w'], f['h'], f['b9']) > len(blob):
            raise ValueError('frame beyond decoded data')
    return {'count': count, 'packed': packed, 'bits': bits, 'frames': frames, 'blob': blob}


def classify(data, name):
    """'piv' | 'pivpack' | 'cel' | 'stile' | None"""
    if name.lower().endswith('.stile') and len(data) % 2 == 0 and data[:2] == b'\0\0':
        return 'stile'
    try:
        _, end = parse_piv(data)
        if end == len(data):
            return 'piv'
        recs, p = 0, 0
        while p < len(data):
            _, p = parse_piv(data, p)
            recs += 1
        if p == len(data) and recs > 1:
            return 'pivpack'
    except (ValueError, struct.error, IndexError):
        pass
    try:
        parse_cel(data)
        return 'cel'
    except (ValueError, struct.error, IndexError):
        pass
    return None


# ------------------------------------------------------------------ planar <-> indexed

def words_of(w):
    return (w + 15) // 16


def frame_bytes(w, h, plane_bits):
    return words_of(w) * 2 * h * bin(plane_bits).count('1')


def planes_to_indices(blob, off, w, h, plane_bits):
    """Planar frame at blob[off:] -> uint8 array (h, w); plane number i is bit i of the index."""
    wb = words_of(w) * 2
    idx = np.zeros((h, w), dtype=np.uint8)
    pos = off
    for i in range(8):
        if not (plane_bits >> i) & 1:
            continue
        arr = np.frombuffer(blob, dtype=np.uint8, count=wb * h, offset=pos).reshape(h, wb)
        bits = np.unpackbits(arr, axis=1)[:, :w]
        idx |= (bits << i).astype(np.uint8)
        pos += wb * h
    return idx


def indices_to_planes(idx, plane_bits):
    h, w = idx.shape
    wb = words_of(w) * 2
    out = bytearray()
    for i in range(8):
        if not (plane_bits >> i) & 1:
            continue
        bits = ((idx >> i) & 1).astype(np.uint8)
        pad = np.zeros((h, wb * 8), dtype=np.uint8)
        pad[:, :w] = bits
        out += np.packbits(pad, axis=1).tobytes()
    return bytes(out)


def needed_planes(idx):
    return int(np.bitwise_or.reduce(idx.ravel())) if idx.size else 0


def save_indexed(path, idx, rgb_list, transparent0=False):
    img = Image.fromarray(idx.astype(np.uint8), 'P')
    flat = []
    for r, g, b in rgb_list:
        flat += [r, g, b]
    img.putpalette(flat + [0] * (768 - len(flat)))
    kw = {'transparency': 0} if transparent0 else {}
    img.save(path, optimize=False, **kw)


def load_indexed(path):
    img = Image.open(path)
    if img.mode != 'P':
        raise ValueError('%s: mode %s; the art must be an indexed (palette) PNG' % (path, img.mode))
    arr = np.array(img, dtype=np.uint8)
    pal = img.getpalette() or []
    n = max(int(arr.max()) + 1 if arr.size else 1, 1)
    rgb = [tuple(pal[3 * i:3 * i + 3]) if 3 * i + 2 < len(pal) else (0, 0, 0) for i in range(max(n, len(pal) // 3))]
    return arr, rgb


def load_mask(path):
    return np.array(Image.open(path).convert('1'), dtype=np.uint8) != 0


# ------------------------------------------------------------------ export

def preview_palette_default(disks_dir):
    p = os.path.join(disks_dir, 'A', 'bg1a.PIV')
    piv, _ = parse_piv(readf(p))
    return [rgb12_to_rgb24(piv_word_to_rgb12(w)) for w in piv['pal_raw']]


def export_piv(data, outdir, rel, at=0, sub=''):
    piv, end = parse_piv(data, at)
    planes = piv['planes']
    idx = planes_to_indices(piv['body'], 0, PIV_W, PIV_H, (1 << planes) - 1)
    # the body is plane-sequential with 40-byte rows, which is exactly (plane, row, 40 bytes)
    pal = [rgb12_to_rgb24(piv_word_to_rgb12(w)) for w in piv['pal_raw']]
    png = os.path.join(sub, 'image.png') if sub else 'image.png'
    os.makedirs(os.path.join(outdir, sub), exist_ok=True)
    save_indexed(os.path.join(outdir, png), idx, pal)
    return {'format': 'piv', 'planes': planes, 'width': PIV_W, 'height': PIV_H, 'png': png.replace('\\', '/'),
            'palette_raw': piv['pal_raw'], 'palette_rgb12': [piv_word_to_rgb12(w) for w in piv['pal_raw']],
            'packed_size': piv['packed']}, end


def export_cel(data, outdir, rel, preview, masks):
    cel = parse_cel(data)
    os.makedirs(os.path.join(outdir, 'frames'), exist_ok=True)
    frames = []
    sheet_items = []
    for i, f in enumerate(cel['frames']):
        e = {'index': i, 'offset': f['offset'], 'width': f['w'], 'height': f['h'], 'hot_x': f['b8'] >> 4,
             'flags': f['b8'] & 15, 'plane_bits': f['b9'], 'png': None, 'mask_png': None}
        if f['w'] and f['h']:
            idx = planes_to_indices(cel['blob'], f['offset'], f['w'], f['h'], f['b9'])
            e['png'] = 'frames/%03d.png' % i
            save_indexed(os.path.join(outdir, e['png']), idx, preview, transparent0=True)
            if masks:
                e['mask_png'] = 'frames/%03d_mask.png' % i
                Image.fromarray(((idx != 0) * 255).astype(np.uint8), 'L').convert('1').save(
                    os.path.join(outdir, e['mask_png']))
            sheet_items.append((i, idx))
        frames.append(e)
    write_sheet(os.path.join(outdir, 'sheet.png'), sheet_items, preview)
    return {'format': 'cel', 'frame_count': cel['count'], 'packed_size': cel['packed'], 'bits_field': cel['bits'],
            'decoded_size': len(cel['blob']), 'preview_palette_note':
            'sprites carry no palette; the PNG palette is a preview (bg1a.PIV). Index = colour register.',
            'frames': frames}


def write_sheet(path, items, rgb_list, cols=8, pad=2):
    """Contact sheet for looking at a file (not read back by import)."""
    if not items:
        return
    cw = max(i.shape[1] for _, i in items) + pad
    ch = max(i.shape[0] for _, i in items) + pad
    rows = (len(items) + cols - 1) // cols
    sheet = np.full((rows * ch, min(cols, len(items)) * cw), 0, dtype=np.uint8)
    for n, (_, idx) in enumerate(items):
        y, x = (n // cols) * ch, (n % cols) * cw
        sheet[y:y + idx.shape[0], x:x + idx.shape[1]] = idx
    save_indexed(path, sheet, rgb_list)


def export_stile(data, outdir):
    words = list(struct.unpack('>%dH' % (len(data) // 2), data))
    return {'format': 'stile', 'words': words,
            'note': 'tile index map: 480 big-endian words, read by program LAB_05BA (index n -> picture n // 80, tile n % 80 '
                    'of that picture); contains no pixels. Edit only if the tile layout changes.'}


def export_all(disks, out, only=None, masks=False, preview_pal=None):
    preview = preview_pal or preview_palette_default(disks)
    summary = {'cel': 0, 'piv': 0, 'pivpack': 0, 'stile': 0, 'skipped': []}
    for disk in sorted(os.listdir(disks)):
        ddir = os.path.join(disks, disk)
        if not os.path.isdir(ddir):
            continue
        for name in sorted(os.listdir(ddir)):
            src = os.path.join(ddir, name)
            if not os.path.isfile(src) or (only and not fnmatch.fnmatch('%s/%s' % (disk, name), only)):
                continue
            data = readf(src)
            kind = classify(data, name)
            if kind is None:
                summary['skipped'].append('%s/%s' % (disk, name))
                continue
            outdir = os.path.join(out, disk, name)
            os.makedirs(outdir, exist_ok=True)
            if kind == 'cel':
                sc = export_cel(data, outdir, name, preview, masks)
            elif kind == 'piv':
                sc, _ = export_piv(data, outdir, name)
            elif kind == 'stile':
                sc = export_stile(data, outdir)
            else:  # pivpack: several piv files back to back
                recs, p, k = [], 0, 0
                while p < len(data):
                    r, p = export_piv(data, outdir, name, p, '%02d' % k)
                    recs.append(r)
                    k += 1
                sc = {'format': 'pivpack', 'records': recs}
            sc.update({'tool_version': TOOL_VERSION, 'source': '%s/%s' % (disk, name), 'source_size': len(data),
                       'source_sha1': sha1(data), 'name': name})
            with open(os.path.join(outdir, SIDECAR), 'w', encoding='utf-8') as f:
                json.dump(sc, f, indent=1)
            summary[kind] += 1
    export_palettes(disks, out)
    return summary


def export_palettes(disks, out):
    """palettes.json: every piv palette (raw words + 24-bit), per-index diversity, and the palette DATA found in
    mog.asm (LAB_08D1..LAB_08D5, patched over colours of the combat palette)."""
    pivs = {}
    for disk in sorted(os.listdir(disks)):
        ddir = os.path.join(disks, disk)
        if not os.path.isdir(ddir):
            continue
        for name in sorted(os.listdir(ddir)):
            p = os.path.join(ddir, name)
            if not os.path.isfile(p):
                continue
            data = readf(p)
            if classify(data, name) == 'piv':
                piv, _ = parse_piv(data)
                pivs['%s/%s' % (disk, name)] = {
                    'planes': piv['planes'], 'raw': ['%04x' % w for w in piv['pal_raw']],
                    'rgb12': ['%03x' % piv_word_to_rgb12(w) for w in piv['pal_raw']]}
    distinct = []
    for i in range(32):
        vals = sorted({v['rgb12'][i] for v in pivs.values() if i < len(v['rgb12'])})
        distinct.append({'index': i, 'distinct_values': len(vals), 'values': vals})
    code = {}
    if os.path.exists(MOG_ASM):
        text = open(MOG_ASM, errors='replace', encoding='utf-8').read().replace('\r', '').split('\n')
        want = {'LAB_08D1', 'LAB_08D2', 'LAB_08D3', 'LAB_08D4', 'LAB_08D5'}
        cur = None
        for ln in text:
            m = re.match(r'^(LAB_[0-9A-F]+):', ln)
            if m:
                cur = m.group(1) if m.group(1) in want else None
                if cur:
                    code[cur] = []
                continue
            if cur:
                m = re.match(r'\s+DC\.([LW])\s+(.*)', ln)
                if m:
                    for tok in m.group(2).split(','):
                        v = int(tok.strip().lstrip('$'), 16)
                        if m.group(1) == 'L':
                            code[cur] += ['%04x' % (v >> 16), '%04x' % (v & 0xFFFF)]
                        else:
                            code[cur].append('%04x' % v)
    doc = {'note': 'piv palettes: 12-bit $0RGB (bit-15 flag resolved). code_palettes_mog are DATA words of mog.asm '
                   '(LAB_08D1..LAB_08D5, copied over colours of the live combat palette by LAB_03F3/LAB_040B..); not '
                   'part of any file and not rewritten by artconv.',
           'piv': pivs, 'per_index': distinct, 'code_palettes_mog': code}
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'palettes.json'), 'w', encoding='utf-8') as f:
        json.dump(doc, f, indent=1)
    return doc


# ------------------------------------------------------------------ import

class ImportError_(Exception):
    pass


def _check_index(idx, planes, what):
    mx = int(idx.max()) if idx.size else 0
    if mx >= (1 << planes):
        raise ImportError_('%s: colour index %d does not fit %d planes (max %d)' % (what, mx, planes, (1 << planes) - 1))


def pal_rgb_from_png(rgb, n):
    pal = list(rgb[:n])
    pal += [(0, 0, 0)] * (n - len(pal))
    return pal


def build_cel(sc, srcdir, planes, keep_planes):
    frames, blob = [], bytearray()
    offset = 0
    warnings = []
    for e in sc['frames']:
        w, h = e['width'], e['height']
        idx = None
        if e['png']:
            idx, _ = load_indexed(os.path.join(srcdir, e['png']))
            h, w = idx.shape
            mp = e.get('mask_png')
            if mp and os.path.exists(os.path.join(srcdir, mp)):
                m = load_mask(os.path.join(srcdir, mp))
                if m.shape != idx.shape:
                    raise ImportError_('%s: mask size differs from frame' % mp)
                if np.any((idx != 0) & ~m):
                    idx = idx.copy()
                    idx[~m] = 0
                if np.any((idx == 0) & m):
                    warnings.append('%s: mask is opaque where the colour is 0; the game derives the mask from the planes, '
                                    'so those pixels stay transparent' % mp)
            _check_index(idx, planes, e['png'])
        need = needed_planes(idx) if idx is not None else 0
        orig = e['plane_bits'] if keep_planes else 0
        bits = orig | need
        if bits >> planes:
            raise ImportError_('frame %d: plane bits %#x exceed %d planes' % (e['index'], bits, planes))
        pix = indices_to_planes(idx, bits) if idx is not None else b''
        if idx is None and bits:
            pix = bytes(frame_bytes(w, h, bits))
        frames.append((offset, w, h, ((e['hot_x'] & 15) << 4) | (e['flags'] & 15), bits))
        blob += pix
        offset += len(pix)
    body = lzss_encode(bytes(blob))
    out = struct.pack('>HII', len(frames), len(body), 8 * len(blob))
    for off, w, h, b8, b9 in frames:
        out += struct.pack('>IHHBB', off, w, h, b8, b9)
    return out + body, warnings


def build_piv(rec, srcdir, planes, source_planes):
    arr, rgb = load_indexed(os.path.join(srcdir, rec['png']))
    if arr.shape != (PIV_H, PIV_W):
        raise ImportError_('%s: piv images are %dx%d, got %dx%d' % (rec['png'], PIV_W, PIV_H, arr.shape[1], arr.shape[0]))
    # 5-plane mode keeps the original plane count (4-plane screens exist) unless the picture needs more
    need = needed_planes(arr).bit_length()
    if planes in (6, 8):
        pl = planes
    else:
        pl = max(source_planes, need, 4)
        if pl > 5:
            raise ImportError_('%s: colour index %d needs %d planes; use --planes 6 or 8' % (rec['png'], int(arr.max()), pl))
    _check_index(arr, pl, rec['png'])
    npal = 1 << pl
    pal24 = pal_rgb_from_png(rgb, npal)
    old = rec['palette_rgb12']
    raw = []
    for i in range(npal):
        c12 = rgb24_to_rgb12(pal24[i])
        if pl == source_planes and i < len(old) and c12 == old[i]:
            raw.append(rec['palette_raw'][i])  # untouched colour keeps its original word (and flag bit)
        else:
            raw.append(0x8000 | c12)
    body = indices_to_planes(arr, (1 << pl) - 1)
    packed = lzss_encode(body)
    out = struct.pack('>HI', pl, len(packed)) + struct.pack('>%dH' % npal, *raw) + packed
    return out, pal24


def import_dir(srcdir, outroot, planes, keep_planes=None):
    """Builds the game file for one export directory into outroot/<name>. Returns (path, warnings)."""
    with open(os.path.join(srcdir, SIDECAR), encoding='utf-8') as f:
        sc = json.load(f)
    name = sc['name']
    os.makedirs(outroot, exist_ok=True)
    path = os.path.join(outroot, name)
    warnings = []
    fmt = sc['format']
    if fmt == 'cel':
        keep = (planes == 5) if keep_planes is None else keep_planes
        data, warnings = build_cel(sc, srcdir, planes, keep)
        # sprites share the screen palette; leave the PNG palette next to the file for reference
        first = next((e for e in sc['frames'] if e['png']), None)
        if first:
            _, rgb = load_indexed(os.path.join(srcdir, first['png']))
            write_pal(path + '.pal', pal_rgb_from_png(rgb, 1 << planes))
    elif fmt == 'piv':
        data, pal24 = build_piv(sc, srcdir, planes, sc['planes'])
        write_pal(path + '.pal', pal24)
    elif fmt == 'pivpack':
        data = b''
        pal24 = None
        for k, rec in enumerate(sc['records']):
            d, pal24 = build_piv(rec, srcdir, planes, rec['planes'])
            data += d
            write_pal(path + '.%02d.pal' % k, pal24)
    elif fmt == 'stile':
        data = struct.pack('>%dH' % len(sc['words']), *sc['words'])
    else:
        raise ImportError_('unknown format %r' % fmt)
    with open(path, 'wb') as f:
        f.write(data)
    return path, warnings


def import_all(export_root, out_root, planes, keep_planes=None):
    made, warns = [], []
    for disk in sorted(os.listdir(export_root)):
        ddir = os.path.join(export_root, disk)
        if not os.path.isdir(ddir):
            continue
        for name in sorted(os.listdir(ddir)):
            d = os.path.join(ddir, name)
            if os.path.exists(os.path.join(d, SIDECAR)):
                p, w = import_dir(d, os.path.join(out_root, disk), planes, keep_planes)
                made.append(p)
                warns += w
    return made, warns


# ------------------------------------------------------------------ comparison (used by tests and `verify`)

def decoded_view(data, name='x'):
    """Canonical decoded content of a game file: what the game sees after LZSS (packed bytes excluded)."""
    kind = classify(data, name)
    if kind == 'cel':
        c = parse_cel(data)
        return ('cel', c['count'], c['bits'], [(f['offset'], f['w'], f['h'], f['b8'], f['b9']) for f in c['frames']],
                c['blob'])
    if kind in ('piv', 'pivpack'):
        recs, p = [], 0
        while p < len(data):
            piv, p = parse_piv(data, p)
            recs.append((piv['planes'], tuple(piv['pal_raw']), piv['body']))
        return (kind, recs)
    if kind == 'stile':
        return ('stile', data)
    return ('raw', data)


# ------------------------------------------------------------------ CLI

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('export')
    e.add_argument('--disks', default=DEFAULT_DISKS)
    e.add_argument('--out', default=DEFAULT_EXPORT)
    e.add_argument('--only', help='glob on disk/name, e.g. "B/*.ob"')
    e.add_argument('--masks', action='store_true', help='also write derived 1-bit mask PNGs (frames/NNN_mask.png)')
    e.add_argument('--preview-palette', help='a .piv whose palette colours the sprite templates (default A/bg1a.PIV)')
    i = sub.add_parser('import')
    i.add_argument('dir', nargs='?', help='one export directory (build/art/export/<disk>/<file>)')
    i.add_argument('--all', action='store_true')
    i.add_argument('--export', default=DEFAULT_EXPORT, help='with --all: the export root')
    i.add_argument('--out', default=None, help='output root (default build/art/import[/<disk>])')
    i.add_argument('--planes', type=int, choices=(5, 6, 8), default=5)
    g = i.add_mutually_exclusive_group()
    g.add_argument('--keep-planes', dest='keep', action='store_true', default=None,
                   help="cel frames keep the original's present planes (default at 5 planes)")
    g.add_argument('--min-planes', dest='keep', action='store_false', help='cel frames store only the planes they use (default at 6)')
    p = sub.add_parser('palettes')
    p.add_argument('--disks', default=DEFAULT_DISKS)
    p.add_argument('--out', default=DEFAULT_EXPORT)
    a = ap.parse_args(argv)

    if a.cmd == 'export':
        pre = None
        if a.preview_palette:
            piv, _ = parse_piv(readf(a.preview_palette))
            pre = [rgb12_to_rgb24(piv_word_to_rgb12(w)) for w in piv['pal_raw']]
        s = export_all(a.disks, a.out, a.only, a.masks, pre)
        print('exported: %d cel-family, %d piv, %d piv packs, %d stile -> %s' % (
            s['cel'], s['piv'], s['pivpack'], s['stile'], a.out))
        print('skipped (not graphics): %d files' % len(s['skipped']))
    elif a.cmd == 'palettes':
        export_palettes(a.disks, a.out)
        print('wrote', os.path.join(a.out, 'palettes.json'))
    else:
        try:
            if a.all:
                made, warns = import_all(a.export, a.out or DEFAULT_IMPORT, a.planes, a.keep)
                print('imported %d files (%d planes) -> %s' % (len(made), a.planes, a.out or DEFAULT_IMPORT))
            else:
                if not a.dir:
                    ap.error('give an export directory or --all')
                path, warns = import_dir(a.dir, a.out or DEFAULT_IMPORT, a.planes, a.keep)
                print('wrote', path)
        except ImportError_ as ex:
            sys.exit('import failed: %s' % ex)
        for w in warns:
            print('warning:', w)


if __name__ == '__main__':
    main()
