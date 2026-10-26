#!/usr/bin/env python3
"""Random-key soak of the PC build's local co-op with computer knights (ROADMAP 8.3b).

    py tests/pc_play/coop_soak.py --players 4 --frames 60000 --seeds 1 2 3 [--jobs 6] [--out DIR]

For every seed one headless run (SDL dummy drivers, --fast) of build-sdl/moonstone.exe: the title menu is walked to a co-op game of --players
knights where knights 2.. are set to "Computer" on the knight screen (one Right before each of their control fires), then --frames frames of
random keys of knight 1 (the cursor keys, Right_Ctrl / Space, E, 1 2 3, Return) are played.  The run fails when the exe exits non-zero (a crash
report, a trap) or its log shows a crash / assert line.  The key script is written to <out>/soak<seed>.keys (replay it with --keys-file).
The key names and the menu route are those of tests/pc_play/pc_play.sh (segments coop / coop4)."""
import argparse
import os
import random
import subprocess
import sys
import concurrent.futures as cf

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIRE = '+Space @8 -Space'

# Title -> Local -> Players / Mode -> Select Knight (pc_play.sh CO_PRE / CO4); then three fires per knight: picture, name, controller line.
MENU = {
    2: '@199 {f} @60 +Right @8 -Right @20 +Down @8 -Down @20 +Right @8 -Right @30 +Down @8 -Down @20 +Down @8 -Down @20 {f} @500',
    4: '@199 {f} @60 +Down @8 -Down @20 +Right @8 -Right @30 +Up @8 -Up @20 +Right @8 -Right @20 +Right @8 -Right @30 +Down @8 -Down @20 '
       '+Down @8 -Down @20 +Down @8 -Down @20 {f} @500',
}


def preamble(players, computers):
    """The menu route plus the knight screen: knights 2..1+computers pick Computer (one Right: it follows the last controller of their ring)."""
    parts = [MENU[players].format(f=FIRE)]
    for knight in range(players):
        for step in range(3):                                   # picture, name, controller line
            if step == 2 and 1 <= knight <= computers:
                parts.append('+Right @8 -Right @30')
            parts.append('%s @300' % FIRE)
    return ' '.join(parts)


KEYS = ['Up', 'Down', 'Left', 'Right']
FIRES = ['Right_Ctrl'] * 29 + ['Space']       # Space opens the status sheet on the map (it must be closed on its pillar): rare, so the run keeps playing
# a status sheet / loot screen is left with the cursor on the pillar and fire (pc_play.sh CO_CLOSE_J1): now and then, so the run does not stay in one
CLOSE = '+Right +Down @200 -Right -Down @5 +Left @37 -Left @3 +Up @23 -Up @10 +Right_Ctrl @10 -Right_Ctrl @200'



def random_keys(rng, frames):
    """Random play of knight 1: held directions, fire taps, now and then E (end turn) and a menu digit."""
    out = []
    at = 0
    while at < frames:
        r = rng.random()
        if r > 0.985:
            out.append(CLOSE)
            at += 500
            continue
        if r < 0.55:

            ks = rng.sample(KEYS, rng.choice([1, 1, 2]))
            hold = rng.randint(4, 70)
            out.append(' '.join('+' + k for k in ks) + ' @%d ' % hold + ' '.join('-' + k for k in ks))
        elif r < 0.85:
            k = rng.choice(FIRES)
            out.append('+%s @%d -%s' % (k, rng.randint(2, 12), k))
            hold = rng.randint(2, 20)
        elif r < 0.92:
            out.append(rng.choice(['E', 'Return', '1', '2', '3']))
            hold = rng.randint(2, 20)
        else:
            k = rng.choice(FIRES)
            d = rng.choice(KEYS)
            out.append('+%s +%s @%d -%s -%s' % (k, d, rng.randint(4, 30), k, d))
            hold = rng.randint(2, 20)
        if r >= 0.55:
            out.append('@%d' % hold)
        else:
            hold = 0
        gap = rng.randint(1, 30)
        out.append('@%d' % gap)
        at += gap + hold + 30
    return ' '.join(out)


def run(seed, args):
    rng = random.Random(seed)
    keys = preamble(args.players, args.computers) + ' @10 ' + random_keys(rng, args.frames)
    os.makedirs(args.out, exist_ok=True)
    kf = os.path.join(args.out, 'soak%d.keys' % seed)
    with open(kf, 'w') as fh:
        fh.write(keys)
    pref = os.path.join(args.out, 'pref%d' % seed)
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy')
    cmd = [args.exe, '--disks', args.disks, '--pref', pref, '--skip-intro', '--fast', '--frames', str(args.frames), '--no-grab',
           '--dump-frame', os.path.join(args.out, 'soak%d.png' % seed), '--keys-file', kf]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=args.timeout)
    log = r.stdout + r.stderr
    with open(os.path.join(args.out, 'soak%d.log' % seed), 'w') as fh:
        fh.write(log)
    bad = [l for l in log.splitlines() if any(w in l.lower() for w in ('crash', 'assert', 'access violation', 'exception', 'fatal'))]
    ok = r.returncode == 0 and not bad and ('%d frames' % args.frames) in log
    return seed, ok, r.returncode, bad[:3]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--players', type=int, choices=(2, 4), default=4)
    ap.add_argument('--computers', type=int, default=None, help='knights 2.. played by the computer (default: all but the first)')
    ap.add_argument('--frames', type=int, default=60000)
    ap.add_argument('--seeds', type=int, nargs='+', default=[1, 2, 3])
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'soak'))
    ap.add_argument('--exe', default=os.path.join(ROOT, 'build-sdl', 'moonstone.exe'))
    ap.add_argument('--disks', default=os.environ.get('MS_DISKS', os.path.join(ROOT, 'disks')))
    ap.add_argument('--timeout', type=int, default=3600)
    args = ap.parse_args()
    if args.computers is None:
        args.computers = args.players - 1
    failed = 0
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for seed, ok, rc, bad in ex.map(lambda s: run(s, args), args.seeds):
            print('seed %d: %s (exit %d)%s' % (seed, 'ok' if ok else 'FAILED', rc, ' ' + '; '.join(bad) if bad else ''))
            failed += 0 if ok else 1
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
