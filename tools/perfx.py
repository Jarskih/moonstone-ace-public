#!/usr/bin/env python3
"""Summarise the PERFX lines of MS_AUTOPLAY serial logs (ROADMAP 8.4a, docs/PERF.md "Fight frame split").

  py tools/perfx.py build/perf-*-stock.log [--min-frames 8] [--fit]

One row per (log, fighters f, job slots j): frames, work (stamp to the frame wait), busy (= work - flip: the flip idles until beam line $F5),
and the sections per frame in beam lines (312 per VBL, 64 us): handler, jobs (job pass without the handlers), script (combat tick
without the draws), draw (cel blits), contact, restore, flip (idle), other+post; calls per frame (handlers, draws), dirty rectangles per frame.
--model reads build/perf-<scenario>-<stock|owner>.log (scenarios duel, dragon, lair*), fits the cost model of docs/MOONSTONE2.md section 4.6
and prints the per-fighter costs and the predictions for 4 knights + 4 creatures / + the dragon:
    B1 = beam lines before the flip  = handler + job pass + script + draws (+ frame start)   per fighter: c1
    B2 = beam lines after the flip   = contact scan + background restore + low-hp/keys        per fighter: c2
    W  = 312 * ceil(B1 / 312) + B2   (the flip idles until beam line $F5, once per VBL; mean waste 156 lines)
    period = 5 VBLs (10 fps) while W < 1626 (a window of 66 lines after each multiple of 312 gives 6 VBLs), else W (3125 / W fps):
    the frame wait is budget - elapsed ticks (docs/PERF.md "Pacing quantisation") and is 0 once the work spans the budget.
(the older --fit option, kept: least-squares busy = base + a * f per log group.)
"""
import argparse
import collections
import re
import sys

LINE = re.compile(r'PERFX (.*)')


def parse(path):
    """PERFX / PERFY / PERFZ come in threes per bin (the console copy cuts a line at ~120 characters)."""
    out = []
    cur = None
    for raw in open(path, encoding='latin-1'):
        m = re.search(r'PERF([XYZ]) (.*)', raw)
        if not m:
            continue
        kv = dict(p.split('=', 1) for p in m.group(2).split() if '=' in p)
        try:
            if m.group(1) == 'X':
                cur = {'f': int(kv['f']), 'j': int(kv['j']), 'n': int(kv['n']), 'work': float(kv['work']), 'flip': float(kv['flip']),
                       'post': float(kv['post']), 'other': float(kv['oth'])}
            elif cur is not None and m.group(1) == 'Y' and int(kv['f']) == cur['f'] and int(kv['j']) == cur['j']:
                cur.update({'handler': float(kv['hand']), 'jobs': float(kv['jobs']), 'script': float(kv['scr']), 'draw': float(kv['draw']),
                            'contact': float(kv['con']), 'restore': float(kv['res'])})
            elif cur is not None and m.group(1) == 'Z' and int(kv['f']) == cur['f'] and int(kv['j']) == cur['j'] and 'draw' in cur:
                cur.update({'hcalls': int(kv['hc']) / 10, 'draws': int(kv['dr']) / 10, 'rects': int(kv['rc']) / 10, 'rmax': int(kv['rmax']),
                            'rover': int(kv['rov']), 'types': int(kv['ty'], 16)})
                out.append(cur)
                cur = None
        except (KeyError, ValueError):   # a truncated line
            cur = None
    return out


def merge(rows, min_frames):
    bins = collections.OrderedDict()
    for r in rows:
        b = bins.setdefault((r['f'], r['j']), {'n': 0, 'rmax': 0, 'rover': 0, 'types': 0, **{k: 0.0 for k in
                            ('work', 'handler', 'jobs', 'script', 'draw', 'contact', 'restore', 'other', 'flip', 'post', 'hcalls', 'draws', 'rects')}})
        for k in ('work', 'handler', 'jobs', 'script', 'draw', 'contact', 'restore', 'other', 'flip', 'post', 'hcalls', 'draws', 'rects'):
            b[k] += r[k] * r['n']
        b['n'] += r['n']
        b['rmax'] = max(b['rmax'], r['rmax'])
        b['rover'] += r['rover']
        b['types'] |= r['types']
    res = []
    for (f, j), b in sorted(bins.items()):
        if b['n'] < min_frames:
            continue
        n = b['n']
        d = {k: (b[k] / n if k not in ('n', 'rmax', 'rover', 'types') else b[k]) for k in b}
        d['f'], d['j'] = f, j
        d['busy'] = d['work'] - d['flip']
        res.append(d)
    return res


def table(name, res):
    print(f'## {name}')
    print('|f|j|frames|work|busy|handler|jobs|script|draw|contact|restore|flip|oth+post|hcalls|draws|rects|rmax|>45|types|')
    print('|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|')
    for d in res:
        print(f"|{d['f']}|{d['j']}|{d['n']}|{d['work']:.0f}|{d['busy']:.0f}|{d['handler']:.1f}|{d['jobs']:.1f}|{d['script']:.0f}|{d['draw']:.0f}|"
              f"{d['contact']:.0f}|{d['restore']:.0f}|{d['flip']:.0f}|{d['other'] + d['post']:.0f}|{d['hcalls']:.1f}|{d['draws']:.1f}|{d['rects']:.1f}|{d['rmax']}|{d['rover']}|{d['types']:x}|")


def lsq(xs, ys, ws=None):
    """y = a + b x (weighted)."""
    ws = ws or [1.0] * len(xs)
    sw = sum(ws)
    mx = sum(w * x for w, x in zip(ws, xs)) / sw
    my = sum(w * y for w, y in zip(ws, ys)) / sw
    sxx = sum(w * (x - mx) ** 2 for w, x in zip(ws, xs))
    b = sum(w * (x - mx) * (y - my) for w, x, y in zip(ws, xs, ys)) / sxx if sxx else 0.0
    return my - b * mx, b


def period_vbl(w):
    """VBLs per frame of a fight loop (budget 6) whose work (stamp to the frame wait, flip idle included) is w beam lines."""
    if w < 1626:
        return 5.0
    return w / 312.0


def predict(b1, b2):
    w = 312 * (-(-int(b1) // 312)) + b2
    per = period_vbl(w)
    return w, 50.0 / per


def model(d, min_frames):
    import glob
    import os
    rows = {}
    for cfg in ('stock', 'owner'):
        for p in sorted(glob.glob(os.path.join(d, f'perf-*-{cfg}.log'))):
            sc = os.path.basename(p)[5:-len(cfg) - 5]
            for r in merge(parse(p), min_frames):
                if r['f'] == 0 or r['draws'] < 1:
                    continue
                r['b1'] = r['handler'] + r['jobs'] + r['script'] + r['draw'] + r['other']
                r['b2'] = r['contact'] + r['restore'] + r['post']
                rows.setdefault((cfg, sc), []).append(r)
    print('measured bins (frames >= min; sections in beam lines per frame; W = stamp to frame wait; fps = 15600 / W once W >= 1626, else 10.0):')
    print('|scenario|cfg|f|j|frames|W|B1|B2|handler|jobs|script|draw|contact|restore|flip (idle)|draws|rects|rmax|')
    print('|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|')
    for (cfg, sc), rs in sorted(rows.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        for r in sorted(rs, key=lambda r: -r['n'])[:2]:
            print(f"|{sc}|{cfg}|{r['f']}|{r['j']}|{r['n']:.0f}|{r['work']:.0f}|{r['b1']:.0f}|{r['b2']:.0f}|{r['handler']:.0f}|{r['jobs']:.0f}|{r['script']:.0f}|"
                  f"{r['draw']:.0f}|{r['contact']:.0f}|{r['restore']:.0f}|{r['flip']:.0f}|{r['draws']:.1f}|{r['rects']:.1f}|{r['rmax']}|")
    for cfg in ('stock', 'owner'):
        duel = max((r for r in rows.get((cfg, 'duel'), []) if r['f'] == 2), key=lambda r: r['n'], default=None)
        if not duel:
            continue
        kn1, kn2, knr = duel['b1'] / 2, duel['b2'] / 2, duel['rects'] / 2
        print(f'\n## {cfg}: knight (duel, f=2 j={duel["j"]}, {duel["n"]:.0f} frames): c1 = {kn1:.0f}, c2 = {kn2:.0f} beam lines, {knr:.1f} rects')
        print('|scenario|bins|fighters (min-max)|c1/creature|c2/creature|rects/creature|worst bin c1|')
        print('|--|--|--|--|--|--|--|')
        cre = {}
        for (c, sc), rs in sorted(rows.items()):
            if c != cfg or sc in ('duel', 'dragon'):
                continue
            use = [r for r in rs if r['f'] >= 2]
            if not use:
                continue
            nw = sum(r['n'] for r in use)
            c1 = sum((r['b1'] - kn1) / (r['f'] - 1) * r['n'] for r in use) / nw
            c2 = sum((r['b2'] - kn2) / (r['f'] - 1) * r['n'] for r in use) / nw
            cr = sum((r['rects'] - knr) / (r['f'] - 1) * r['n'] for r in use) / nw
            worst = max((r['b1'] - kn1) / (r['f'] - 1) for r in use)
            cre[sc] = (c1, c2, cr)
            print(f'|{sc}|{len(use)}|{min(r["f"] for r in use)}-{max(r["f"] for r in use)}|{c1:.0f}|{c2:.0f}|{cr:.1f}|{worst:.0f}|')
        secs = ('handler', 'jobs', 'script', 'draw', 'contact', 'restore', 'other', 'post', 'draws', 'rects')
        print(f'\nper fighter and section, beam lines per frame (knight = duel / 2; creature = mean over the lairs of (bin - knight) / (fighters - 1)):')
        print('|section|' + '|'.join(secs) + '|')
        print('|--|' + '|'.join('--' for _ in secs) + '|')
        print('|knight|' + '|'.join(f'{duel[k] / 2:.1f}' for k in secs) + '|')
        cm = []
        for k in secs:
            vals = []
            for (c, sc), rs in rows.items():
                if c != cfg or sc in ('duel', 'dragon'):
                    continue
                use = [r for r in rs if r['f'] >= 2]
                if use:
                    vals.append(sum((r[k] - duel[k] / 2) / (r['f'] - 1) * r['n'] for r in use) / sum(r['n'] for r in use))
            cm.append(sum(vals) / len(vals))
        print('|creature (mean of lairs)|' + '|'.join(f'{v:.1f}' for v in cm) + '|')
        dr = max((r for r in rows.get((cfg, 'dragon'), [])), key=lambda r: r['n'], default=None)
        if not cre:
            continue
        allw = sum(1 for _ in cre)
        mean = tuple(sum(v[i] for v in cre.values()) / allw for i in range(3))
        light = min(cre.values(), key=lambda v: v[0])
        heavy = max(cre.values(), key=lambda v: v[0])
        print(f'\nmean creature (unweighted over {allw} lairs): c1 = {mean[0]:.0f}, c2 = {mean[1]:.0f}; cheapest {light[0]:.0f}+{light[1]:.0f}, dearest {heavy[0]:.0f}+{heavy[1]:.0f}')
        print('\n|case|B1|B2|W (lines)|period|fps|rects avg|')
        print('|--|--|--|--|--|--|--|')
        for label, k, n, c in (('2 knights (duel, measured)', 2, 0, None), ('4K + 4 light', 4, 4, light), ('4K + 4 mean', 4, 4, mean), ('4K + 4 heavy', 4, 4, heavy),
                               ('4K + 3 mean', 4, 3, mean), ('4K + 2 mean', 4, 2, mean), ('2K + 4 mean', 2, 4, mean), ('1K + 4 mean (lair0 like)', 1, 4, mean)):
            b1 = k * kn1 + (n * c[0] if c else 0)
            b2 = k * kn2 + (n * c[1] if c else 0) + (6 * max(0, k + n - 2) if n else 0)   # the contact scan grows a little faster than linear
            rc = k * knr + (n * c[2] if c else 0)
            w, fps = predict(b1, b2)
            print(f'|{label}|{b1:.0f}|{b2:.0f}|{w:.0f}|{w / 312:.1f} VBL|{fps:.1f}|{rc:.0f}|')
        if dr:
            dk1, dk2 = dr['b1'] - kn1, dr['b2'] - kn2   # the dragon job plus its parts and bats on top of one knight
            print(f'\ndragon fight (1 knight + dragon, j={dr["j"]}, {dr["n"]:.0f} frames): B1 = {dr["b1"]:.0f}, B2 = {dr["b2"]:.0f}; dragon+parts c1 = {dk1:.0f}, c2 = {dk2:.0f}, rects {dr["rects"]:.1f}')
            for k in (1, 2, 4):
                b1, b2 = k * kn1 + dk1, k * kn2 + dk2 + 6 * (k - 1)
                w, fps = predict(b1, b2)
                print(f'  {k}K + dragon: B1 {b1:.0f} B2 {b2:.0f} W {w:.0f} fps {fps:.1f}  rects {k * knr + dr["rects"] - knr:.0f}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('logs', nargs='*')
    ap.add_argument('--min-frames', type=int, default=8)
    ap.add_argument('--fit', action='store_true')
    ap.add_argument('--model', action='store_true')
    ap.add_argument('--dir', default='build')
    a = ap.parse_args()
    if a.model:
        return model(a.dir, a.min_frames)
    allres = {}
    for p in a.logs:
        res = merge(parse(p), a.min_frames)
        res = [d for d in res if d['f'] > 0]
        table(p, res)
        allres[p] = res
    if a.fit:
        for grp in ('stock', 'owner'):
            pts = [(d['f'], d['busy'], d['n'], p) for p, rs in allres.items() if grp in p for d in rs]
            if len(pts) < 2:
                continue
            c, k = lsq([x[0] for x in pts], [x[1] for x in pts], [x[2] for x in pts])
            print(f'\nfit {grp}: busy = {c:.0f} + {k:.0f} * f   ({len(pts)} bins)')


if __name__ == '__main__':
    sys.exit(main())
