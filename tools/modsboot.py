"""Boot proof of the mod loader (ROADMAP 9.4c): stage a fixture mods/ folder into the autoplay HD tree, boot it headlessly,
check the serial log, remove the folder again.

  py tools/modsboot.py bad|good|smith|none [--rel]     (env MS_BOOT_INSTANCE = the WinUAE instance, like integrate.py)

  bad   tests/boot/mods/bad/{rules.ini (value out of range at line 5), typo.ini (not a data file)}: the game must still reach the
        menu, the log must name `mods/rules.ini:5: [limits] gold_cap = 99999` and the values must stay the original ones
  good  tests/boot/mods/good/rules.ini (gold_cap = 300, scaling = none): the logged values must show them
  smith tests/boot/mods/smith/{items.ini,shops.ini} (ROADMAP 9.5b: the smith's prices halved): both applied, the log lists the changed rows
  no_scaling limited_scaling cheap_shops tough_dragon cave_troll
        the example mods of mods_examples/<name>/ (ROADMAP 9.7), copied into mods/: the log shows their values; the first two use the
        lair script of `waves`, cheap_shops that of `smith`, tough_dragon tests/boot/mods_dragon.txt (the dragon fight set-up logs
        `dragon fight hp ..`), cave_troll tests/boot/mods_cave_troll.txt (the arena logs `creature row 10 like 7 hp 60`)
  none  an empty mods folder (only defaults/): the logged values are the defaults
  waves    tests/boot/mods/waves/rules.ini ([waves] scaling = none) + tests/boot/mods_waves.txt (lair 0 with a strong knight):
           the log line `waves: scaling=1 ... level=0` shows the unscaled counts (ROADMAP 9.5a)
  plain    no mods, the same lair script: `waves: scaling=0` with the original growth (level=3)
  defaults mods/defaults/*.ini (rules.ini) copied into mods/ (ROADMAP 9.4d), the same lair script: every logged value and the `waves:` line
           are those of `plain` (the boot log of `plain`, modsboot-plain.log, is compared when it exists)

Needs the autoplay build + hdinstall (py tools/integrate.py --boot).  Uses build-autoplay[-rel]/hd-<instance>/mods only.
"""
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import integrate as I  # noqa: E402

EXAMPLES = ('no_scaling', 'limited_scaling', 'cheap_shops', 'tough_dragon', 'cave_troll')
DEFAULT_VALUES = ['values [waves] scaling=original', 'values [limits] gold_cap=150 dagger_cap=10']
EXPECT = {
    'bad': (['mods/rules.ini:5: [limits] gold_cap = 99999: ', '(file ignored)', 'mods/typo.ini', 'done applied 0 skipped 1 unknown 1',
             'mods_bad_game_reached_menu', 'open PROGDIR:data/music.cmp'] + DEFAULT_VALUES, ['values [waves] scaling=none', 'gold_cap=300']),
    'good': (['done applied 1 skipped 0 unknown 0', 'values [waves] scaling=none', 'values [limits] gold_cap=300 dagger_cap=10',
              'mods_good_reached_menu', 'open PROGDIR:data/music.cmp'], ['mods/rules.ini:']),
    'waves': (['done applied 1 skipped 0 unknown 0', 'values [waves] scaling=none', 'waves: scaling=1 coop=0', 'total=10 alive=2 level=0', 'mods_waves_reached_arena'],
              ['mods/rules.ini:', 'waves: scaling=0']),
    'plain': (['holds no data file', 'done applied 0 skipped 0 unknown 0'] + DEFAULT_VALUES + ['waves: scaling=0 coop=0', 'mods_waves_reached_arena'],
              ['waves: scaling=1']),
    'defaults': (['skipped 0 unknown 0'] + DEFAULT_VALUES + ['waves: scaling=0 coop=0', 'mods_waves_reached_arena'],
                 ['mods/rules.ini:', 'waves: scaling=1']),
    'smith': (['done applied 2 skipped 0 unknown 0', 'values [armour mail]', ' price=15', 'values [armour battle]', ' price=37',
               'values [weapon claymore]', ' price=12', 'values [smith] dagger_price=1', 'mods_smith_reached_menu',
               'open PROGDIR:data/music.cmp'], ['mods/items.ini:', 'mods/shops.ini:']),
    'no_scaling': (['done applied 1 skipped 0 unknown 0', 'values [waves] scaling=none', 'waves: scaling=1 coop=0', 'total=10 alive=2 level=0',
                    'mods_waves_reached_arena'], ['mods/rules.ini:', 'waves: scaling=0']),
    'limited_scaling': (['done applied 1 skipped 0 unknown 0', 'values [waves] scaling=limited', 'waves: scaling=2 coop=0', 'total=14 alive=2 level=3', 'mods_waves_reached_arena'],
                        ['mods/rules.ini:', 'waves: scaling=0', 'waves: scaling=1']),
    'cheap_shops': (['done applied 2 skipped 0 unknown 0', 'values [armour mail]', ' price=15', 'values [armour battle]', ' price=37',
                     'values [weapon claymore]', ' price=12', 'values [smith] dagger_price=1', 'values [healer] heal_price=5 life_price=8', 'values [market] sell_shift=1',
                     'mods_smith_reached_menu'], ['mods/items.ini:', 'mods/shops.ini:']),
    'tough_dragon': (['done applied 1 skipped 0 unknown 0', 'values [creature dragon]', 'hp=250', 'dragon fight hp 250 hpmax 250 reach 60 dmg 20 60 20 60',
                      'mods_dragon_fight'], ['mods/creatures.ini:']),
    'cave_troll': (['done applied 3 skipped 0 unknown 0', 'creature row 10 like 7 hp 60 hpmax 60', 'mods_troll_arena'],
                   ['mods/creatures.ini:', 'mods/arenas.ini:', 'mods/lairs.ini:']),
    'none': (['holds no data file', 'done applied 0 skipped 0 unknown 0'] + DEFAULT_VALUES, []),
}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 1 or args[0] not in EXPECT:
        sys.exit(__doc__)
    case, rel = args[0], '--rel' in sys.argv
    adir = I.AUTOPLAY_REL_DIR if rel else I.AUTOPLAY_DIR
    cfg = I.autoplay_config(adir, stock=rel)   # also refreshes <adir>/hd-<instance> from <adir>/hd
    inst = I.BOOT_INSTANCE
    hd = os.path.join(I.ROOT, adir, 'hd' if inst == 3 else 'hd-%d' % inst)
    mods = os.path.join(hd, 'mods')
    for f in os.listdir(mods) if os.path.isdir(mods) else []:   # keep mods/defaults (staged by hdinstall), drop user files
        if f != 'defaults':
            os.remove(os.path.join(mods, f))
    if case == 'defaults':
        dflt = os.path.join(mods, 'defaults')
        for f in os.listdir(dflt):
            if f.endswith('.ini'):
                shutil.copyfile(os.path.join(dflt, f), os.path.join(mods, f))
    elif case not in ('none', 'plain'):
        os.makedirs(mods, exist_ok=True)
        src = os.path.join(I.ROOT, 'mods_examples' if case in EXAMPLES else os.path.join('tests', 'boot', 'mods'), case)
        for f in os.listdir(src):
            shutil.copyfile(os.path.join(src, f), os.path.join(mods, f))
    script = os.path.join(I.ROOT, 'tests', 'boot', 'mods_%s.txt' % {'good': 'good', 'smith': 'smith', 'waves': 'waves', 'plain': 'waves', 'defaults': 'waves',
                                                                                      'no_scaling': 'waves', 'limited_scaling': 'waves', 'cheap_shops': 'smith',
                                                                                      'tough_dragon': 'dragon', 'cave_troll': 'cave_troll'}.get(case, 'bad'))
    if case == 'none':   # the plain regression script boots too; only the log is of interest
        script = os.path.join(I.ROOT, 'tests', 'boot', 'mods_none.txt')
        open(script, 'w').write('wait log done_applied_0_skipped_0 max 3000\nframe 300 quit\n')
    shots = os.path.join(I.ROOT, 'build', 'shots')
    rc, out, dt = I.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'uaeshot.ps1', '-Config', cfg,
                         '-Instance', str(inst), '-Autoplay', script, '-Dir', shots, '-Timeout', '240'], 'modsboot_' + case)
    if case == 'none':
        os.remove(script)
    ok = 'AUTOPLAY quit' in out
    print('%s: %s (%.0fs)' % (case, 'ran to quit' if ok else 'NO quit line', dt))
    log = open(os.path.join(I.ROOT, 'build', 'uaeshot-%d.log' % inst), encoding='latin-1').read()
    logcopy = os.path.join(shots, 'modsboot-%s%s.log' % (case, '-rel' if rel else ''))
    open(logcopy, 'w', encoding='latin-1').write(log)
    for l in (l for l in log.splitlines() if 'mods' in l.lower()):
        print('    ' + l[:170])
    for want in EXPECT[case][0]:
        hit = want in log
        ok &= hit
        print('  %s  contains %r' % ('ok  ' if hit else 'MISS', want))
    for nowant in EXPECT[case][1]:
        hit = nowant in log
        ok &= not hit
        print('  %s  must not contain %r' % ('ok  ' if not hit else 'BAD ', nowant))
    for f in os.listdir(mods) if os.path.isdir(mods) else []:   # leave the tree as hdinstall staged it
        if f != 'defaults':
            os.remove(os.path.join(mods, f))
    for l in (l for l in log.splitlines() if 'waves:' in l):
        print('    ' + l[:170])
    if case == 'defaults':   # ROADMAP 9.4d: the same result as no mods
        ref = os.path.join(shots, 'modsboot-plain%s.log' % ('-rel' if rel else ''))
        if os.path.exists(ref):
            keep = lambda t: [l[l.index('mods:') if 'mods:' in l else l.index('waves:'):] for l in t.splitlines() if ' values ' in l or 'waves:' in l]
            same = keep(log) == keep(open(ref, encoding='latin-1').read())
            ok &= same
            print('  %s  values and wave line equal those of the no-mods run' % ('ok  ' if same else 'DIFF'))
        else:
            print('  (run `modsboot plain` first to compare with the no-mods log)')
    print('modsboot %s: %s' % (case, 'PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
