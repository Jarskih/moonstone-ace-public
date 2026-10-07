"""Compare WinUAE screenshots (build/shots/<name>.png, from uaeshot.ps1) with references (build/shots/ref/<name>.png).

  py tools/shotcmp.py menu f1500                 compare both against their references
  py tools/shotcmp.py menu --tol 0.2             allow 0.2 % differing pixels (default 0.5)
  py tools/shotcmp.py menu --promote             make the current shot the reference (creates build/shots/ref/)
  py tools/shotcmp.py --all                      every reference that has a current shot

Both images are reduced to the Amiga display area of the WinUAE window (640x400) before comparing; the title bar, the status bar
and the black border are not part of the game. The area is located in the picture itself, so the result does not depend on the host's
display scaling, the title bar height or extra white canvas around the window: the window's 1 px frame lines give the client
origin and its scale (the client is 720 px wide at 100 %), the display sits at a fixed offset (59,32) in it and is resampled
(nearest neighbour) to 640x400. If no WinUAE window is recognised the fixed crop 70,77,640,400 is used. Override with
--crop x,y,w,h (fixed, no detection), or --no-crop to compare whole files. A pixel differs when any channel differs by more than --pixel-tol (default 8
of 255, which absorbs the scaler's dithering). The verdict is the percentage of differing pixels against --tol. A red-marked diff
image goes to build/shots/diff/<name>-diff.png (the reference's pixels dimmed, differing ones red).

Exit status: 0 all within tolerance, 1 at least one over it (or a size mismatch), 2 a shot or reference is missing.
"""
import argparse
import os
import shutil
import sys

try:
    from PIL import Image, ImageChops
except ImportError:  # pragma: no cover
    sys.exit('shotcmp needs Pillow (py -m pip install pillow)')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CROP = (70, 77, 640, 400)
DISPLAY_SIZE = (640, 400)
CLIENT_WIDTH = 721        # frame line to frame line, at 100 % scale
DISPLAY_OFFSET = (59, 32)  # display origin from the client origin (first column after the left frame line, first row after the title bar)
CHROME_GRAY = 43


def _is_chrome(px):
    return all(abs(c - CHROME_GRAY) <= 4 for c in px)


def _is_black(px):
    return max(px) <= 8


def find_display_box(im):
    """Locate the Amiga display inside a WinUAE window picture: returns (x, y, w, h) as floats in picture pixels, or None.

    The window may sit at the top-left of a larger canvas and be drawn at any scale; chrome is the 43,43,43 grey, the client
    black. Title bar end = first row whose probe pixels are nearly all black; the left and right frame lines are the first
    two grey columns on a client row above the display."""
    W, H = im.size
    px = im.load()
    probes = range(20, min(W, 600), 7)
    if len(probes) < 10:
        return None
    y0 = None
    for y in range(3, min(H, 400)):
        n = sum(1 for x in probes if _is_black(px[x, y]))
        if n >= 0.9 * len(probes):
            y0 = y
            break
    if y0 is None:
        return None
    # a scale-independent row inside the client but above the display: a few rows below the title bar
    for dy in (6, 4, 8, 3):
        y = y0 + dy
        if y >= H:
            return None
        xl = next((x for x in range(0, min(W, 200)) if _is_chrome(px[x, y])), None)
        if xl is None:
            continue
        x = xl
        while x < W and _is_chrome(px[x, y]):
            x += 1
        xr = next((xx for xx in range(x, W) if _is_chrome(px[xx, y])), None)
        if xr is None:
            continue
        scale = (xr - xl) / float(CLIENT_WIDTH)
        if not 0.5 <= scale <= 4.0:
            continue
        # the client origin: first column after the (possibly wider than 1 px) left frame line
        cx = xl
        while cx < W and _is_chrome(px[cx, y]):
            cx += 1
        cx = xl + 1 if scale < 1.2 else cx
        bx = cx + DISPLAY_OFFSET[0] * scale
        by = y0 + DISPLAY_OFFSET[1] * scale
        w, h = DISPLAY_SIZE[0] * scale, DISPLAY_SIZE[1] * scale
        if bx + w > W + 0.5 or by + h > H + 0.5:
            return None
        return (bx, by, w, h)
    return None


def load(path, crop):
    """crop: None = whole image, 'auto' or DEFAULT_CROP = detect the display (fixed DEFAULT_CROP if none found), or an (x, y, w, h) tuple."""
    im = Image.open(path).convert('RGB')
    if crop == DEFAULT_CROP:   # callers (tools/integrate.py) pass the default crop: that means detect the display
        crop = 'auto'
    if crop == 'auto':
        box = find_display_box(im)
        if box is None:
            crop = DEFAULT_CROP
        else:
            bx, by, w, h = box
            if (round(w), round(h)) == DISPLAY_SIZE and bx == int(bx) and by == int(by):
                return im.crop((int(bx), int(by), int(bx) + DISPLAY_SIZE[0], int(by) + DISPLAY_SIZE[1]))
            return im.resize(DISPLAY_SIZE, Image.NEAREST, box=(bx, by, bx + w, by + h))
    if crop:
        x, y, w, h = crop
        if x + w > im.width or y + h > im.height:
            raise ValueError('crop %s outside the %dx%d image %s' % (crop, im.width, im.height, path))
        im = im.crop((x, y, x + w, y + h))
    return im


def compare(shot, ref, pixel_tol=8):
    """Returns (percent of differing pixels, diff image). Both images must have the same size."""
    if shot.size != ref.size:
        raise ValueError('size mismatch: shot %s, reference %s' % (shot.size, ref.size))
    diff = ImageChops.difference(shot, ref)
    r, g, b = diff.split()
    mx = ImageChops.lighter(ImageChops.lighter(r, g), b)
    mask = mx.point(lambda v: 255 if v > pixel_tol else 0)
    count = sum(1 for v in mask.getdata() if v)
    total = shot.width * shot.height
    out = Image.eval(ref, lambda v: v // 3)
    out.paste(Image.new('RGB', shot.size, (255, 0, 0)), (0, 0), mask)
    return 100.0 * count / total, out


def parse_crop(text):
    parts = [int(v) for v in text.split(',')]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError('crop is x,y,w,h')
    return tuple(parts)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('names', nargs='*', help='shot names without .png')
    ap.add_argument('--all', action='store_true', help='every reference that has a current shot')
    ap.add_argument('--shots-dir', default=os.path.join(ROOT, 'build', 'shots'))
    ap.add_argument('--ref-dir', default=None, help='default <shots-dir>/ref')
    ap.add_argument('--diff-dir', default=None, help='default <shots-dir>/diff')
    ap.add_argument('--tol', type=float, default=0.5, help='allowed percent of differing pixels')
    ap.add_argument('--pixel-tol', type=int, default=8, help='allowed per-channel difference')
    ap.add_argument('--crop', type=parse_crop, default=None, help='fixed x,y,w,h instead of detecting the display')
    ap.add_argument('--no-crop', action='store_true')
    ap.add_argument('--promote', action='store_true', help='copy the current shot over the reference')
    args = ap.parse_args(argv)

    ref_dir = args.ref_dir or os.path.join(args.shots_dir, 'ref')
    diff_dir = args.diff_dir or os.path.join(args.shots_dir, 'diff')
    crop = None if args.no_crop else (args.crop or 'auto')
    names = list(args.names)
    if args.all:
        if os.path.isdir(ref_dir):
            names += sorted(f[:-4] for f in os.listdir(ref_dir) if f.endswith('.png') and f[:-4] not in names)
    if not names:
        ap.error('no shot names (or --all with no references)')

    status = 0
    for name in names:
        shot_path = os.path.join(args.shots_dir, name + '.png')
        ref_path = os.path.join(ref_dir, name + '.png')
        if not os.path.exists(shot_path):
            print('%-24s MISSING shot %s' % (name, shot_path))
            status = max(status, 2)
            continue
        if args.promote:
            os.makedirs(ref_dir, exist_ok=True)
            shutil.copyfile(shot_path, ref_path)
            print('%-24s PROMOTED -> %s' % (name, ref_path))
            continue
        if not os.path.exists(ref_path):
            print('%-24s MISSING reference %s (use --promote)' % (name, ref_path))
            status = max(status, 2)
            continue
        try:
            pct, diff = compare(load(shot_path, crop), load(ref_path, crop), args.pixel_tol)
        except ValueError as e:
            print('%-24s FAIL %s' % (name, e))
            status = max(status, 1)
            continue
        os.makedirs(diff_dir, exist_ok=True)
        diff_path = os.path.join(diff_dir, name + '-diff.png')
        diff.save(diff_path)
        ok = pct <= args.tol
        print('%-24s %s %.3f%% differ (tolerance %.3f%%)  %s' % (name, 'PASS' if ok else 'FAIL', pct, args.tol, diff_path))
        if not ok:
            status = max(status, 1)
    return status


if __name__ == '__main__':
    sys.exit(main())
