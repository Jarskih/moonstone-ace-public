"""Pure helpers of `tools/integrate.py --boot`: what a boot-regression script (tests/boot/*.txt) expects of its screenshots.

Directives live in comment lines of the autoplay script (they are ignored by the game's parser, docs/AUTOPLAY.md):

  # shotcmp-tol <shot> <percent>          allowed percentage of differing pixels against the reference (default DEFAULT_TOL)
  # shotdiff <shotA> <shotB> <percent>    the two shots of THIS boot must differ by at least this percentage (proves that an
                                          injected input visibly changed the screen, independent of any reference)

The shot list is the script's own `frame N shot <name>` lines. Names may only use [A-Za-z0-9_-].
"""
import re

DEFAULT_TOL = 0.5
WAIT_GUESS = 1500   # frames assumed for a barrier without `max` (timeout budget only)
SHOT_SECONDS = 10   # every shot freezes the guest for 8 s (rt/autoplay SHOT_FREEZE_FRAMES) and the host needs a moment
_SHOT = re.compile(r'^\s*frame\s+\d+\s+shot\s+([A-Za-z0-9_-]+)\s*$', re.I)
_TOL = re.compile(r'^\s*[#;]\s*shotcmp-tol\s+([A-Za-z0-9_-]+)\s+([0-9.]+)\s*$', re.I)
_DIFF = re.compile(r'^\s*[#;]\s*shotdiff\s+([A-Za-z0-9_-]+)\s+([A-Za-z0-9_-]+)\s+([0-9.]+)\s*$', re.I)


def parse(text):
    """-> dict(shots=[names in script order], tol={name: pct}, differ=[(a, b, min_pct)], last_frame=int)."""
    shots, tol, differ, last, seg = [], {}, [], 0, 0
    for line in text.splitlines():
        m = _SHOT.match(line)
        if m:
            if m.group(1) in shots:
                raise ValueError('shot %s appears twice' % m.group(1))
            shots.append(m.group(1))
        m = _TOL.match(line)
        if m:
            tol[m.group(1)] = float(m.group(2))
        m = _DIFF.match(line)
        if m:
            differ.append((m.group(1), m.group(2), float(m.group(3))))
        m = re.match(r'^\s*(?:frame\s+(\d+)\s+)?(wait|sync)\b(.*)$', line.split('#')[0].split(';')[0], re.I)
        if m:   # a barrier ends the segment: frames after it count from its release (docs/AUTOPLAY.md)
            mx = re.search(r'\bmax\s+(\d+)', m.group(3), re.I)
            last += max(seg, int(m.group(1) or 0)) + (0 if m.group(2).lower() == 'sync' else int(mx.group(1)) if mx else WAIT_GUESS)
            seg = 0
            continue
        m = re.match(r'^\s*frame\s+(\d+)\s', line, re.I)
        if m:
            seg = max(seg, int(m.group(1)))
    for n in tol:
        if n not in shots:
            raise ValueError('shotcmp-tol names unknown shot %s' % n)
    for a, b, _ in differ:
        for n in (a, b):
            if n not in shots:
                raise ValueError('shotdiff names unknown shot %s' % n)
    return dict(shots=shots, tol=tol, differ=differ, last_frame=last + seg)


def timeout_for(last_frame, loads_s=150, shots=0):
    """Seconds to give uaeshot.ps1: autoplay frames run at 50 Hz, plus slack for the loads (they do not count as frames) and the shot freezes."""
    return int(last_frame / 50.0 + loads_s + shots * SHOT_SECONDS)


def judge_ref(name, pct, tol):
    """One reference comparison -> (ok, line)."""
    ok = pct <= tol
    return ok, '%-26s %s  %.3f%% differ (tolerance %.3f%%)' % (name, 'PASS' if ok else 'FAIL', pct, tol)


def judge_differ(a, b, pct, minimum):
    """A shotdiff check -> (ok, line)."""
    ok = pct >= minimum
    return ok, '%-26s %s  %.3f%% differ from %s (need >= %.3f%%)' % (b, 'PASS' if ok else 'FAIL', pct, a, minimum)
