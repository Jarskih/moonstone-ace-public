"""One-command integrator: regenerate, verify, build, (optionally) test and boot.

  py tools/integrate.py [--boot STEPS] [--tests auto|all|none] [--skip-build]

Steps (stop at the first failing gate):
  1. py tools/resource.py                       regenerate asm/*.s (reference listing; asm/synth.s for MS_SYNTH_ASM), include/rt/abs.h, src/rt/abs_stubs.cpp
  2. py tools/resource.py --verify --no-write   load-equivalence per binary (fail fast)
  3. cmake --build build, then build-game-debug (+ target hdinstall)
  4. tests: --tests auto = tests/test_*.py whose text mentions a file changed vs HEAD (plus test_resource);
            all = every tests/test_*.py; none = skip.
  5. --boot STEPS: powershell uaeshot.ps1 -Joy -Steps STEPS (e.g. "wait:40,shot:integ")
     --boot (no argument) / --boot regression: configure+build build-autoplay/ (Debug, MS_AUTOPLAY) and build-autoplay-rel/
            (Release LTO, MS_AUTOPLAY) + their hdinstall, then run every tests/boot/regression*.txt headlessly on each
            (uaeshot.ps1 -Instance $MS_BOOT_INSTANCE -Autoplay) and compare the shots with build/shots/ref/ (Debug,
            `[boot     ]`) resp. build/shots/ref-rel/ (Release, `[boot rel ]`) (tools/shotcmp.py), PASS/FAIL per screen;
            docs/AUTOPLAY.md. A plain --boot also runs the per-commit play scripts (PLAY_COMMIT, ~10 min) on the Debug leg; --no-play skips
            them, --boot-play nightly runs every tests/boot/play_*.txt. --only-boot skips steps 1-4. --no-rel drops the Release leg, --rel-only the Debug one; --update-refs-rel rewrites ref-rel/
            from a Release run instead of comparing.
No commit logic. Full logs go to build/integrate/*.log. See docs/PATCHES.md.
"""
import argparse, concurrent.futures as cf, fnmatch, glob, os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGDIR = os.path.join(ROOT, 'build', 'integrate')


def _toolchain():
    """bin/<os> directory of the amiga-debug toolchain: MS_TOOLCHAIN, tools/toolchain (vendored), the D:/Amiga workspace, or the
    VS Code extension (tools/setup.py find_toolchain); '' when none is found (the PATH then has to provide it)."""
    try:
        import setup as _setup
        return _setup.find_toolchain(None)
    except SystemExit:
        return ''


TOOLCHAIN = _toolchain()
# Files that belong to another session: never select tests because of them.
EXCLUDE = ['src/lifted/*', 'tools/lift*.py', 'tools/diffharness/*', 'tests/test_lift.py',
           'tests/test_diffharness.py', 'tools/symbols.yaml']
ALWAYS = ['test_resource']


def toolchain_env():
    env = dict(os.environ)
    extra = [os.path.join(TOOLCHAIN, 'opt', 'bin'), TOOLCHAIN, os.path.join(TOOLCHAIN, 'opt', 'm68k-amiga-elf', 'bin'),
             r'C:\msys64\mingw64\bin'] if TOOLCHAIN else [r'C:\msys64\mingw64\bin']
    env['PATH'] = os.pathsep.join(extra + [env.get('PATH', '')])
    return env


def run(cmd, log, env=None, timeout=3600):
    """Run cmd in ROOT, save full output to build/integrate/<log>.log; returns (rc, text, seconds)."""
    os.makedirs(LOGDIR, exist_ok=True)
    t = time.time()
    try:
        p = subprocess.run(cmd, cwd=ROOT, env=env or toolchain_env(), capture_output=True, text=True,
                           errors='replace', timeout=timeout)
        rc, out = p.returncode, (p.stdout or '') + (p.stderr or '')
    except subprocess.TimeoutExpired:
        rc, out = 124, 'TIMEOUT'
    with open(os.path.join(LOGDIR, log + '.log'), 'w', encoding='utf-8') as f:
        f.write(out)
    return rc, out, time.time() - t


def changed_files():
    out = []
    for args in (['git', 'diff', '--name-only', '--relative', 'HEAD'],
                 ['git', 'ls-files', '--others', '--exclude-standard']):
        p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
        out += [l.strip().replace('\\', '/') for l in p.stdout.splitlines() if l.strip()]
    return sorted({f for f in out if not any(fnmatch.fnmatch(f, pat) for pat in EXCLUDE)})


def select_tests(mode):
    tdir = os.path.join(ROOT, 'tests')
    mods = sorted(f[:-3] for f in os.listdir(tdir) if re.match(r'test_\w+\.py$', f))
    if mode == 'none':
        return []
    if mode == 'all':
        return mods
    mods = [m for m in mods if not any(fnmatch.fnmatch(f'tests/{m}.py', pat) for pat in EXCLUDE)]
    files = changed_files()
    chosen = set(m for m in ALWAYS if m in mods)
    for m in mods:
        text = open(os.path.join(tdir, m + '.py'), encoding='utf-8', errors='replace').read().replace('\\', '/')
        for f in files:
            if f == f'tests/{m}.py' or f in text or os.path.basename(f) in text:
                chosen.add(m)
                break
    return sorted(chosen)


def verify_summary(out):
    rows = []
    for l in out.splitlines():
        m = re.match(r'(program|mog) \(B\) patched: all differences lie inside (\d+) patch ranges', l)
        if m:
            rows.append(f'{m.group(1)}: B ok, all differences inside {m.group(2)} patch ranges')
        elif 'NOT equivalent' in l or 'FAILED' in l or 'OUTSIDE patch ranges' in l or 'layout changed' in l:
            rows.append(l.strip()[:200])
    return rows


def run_test(mod):
    rc, out, dt = run([sys.executable, '-m', 'unittest', f'tests.{mod}'], mod)
    m = re.search(r'Ran (\d+) tests?', out)
    bad = re.findall(r'^(?:FAIL|ERROR): (\S+)', out, re.M)
    return mod, rc, (m.group(1) if m else '?'), bad, dt


def tail(out, n=12):
    return '\n'.join('    ' + l[:220] for l in out.strip().splitlines()[-n:])


AUTOPLAY_DIR = 'build-autoplay'
AUTOPLAY_REL_DIR = 'build-autoplay-rel'
# The play scripts (tests/boot/play_*.txt, docs/AUTOPLAY.md) of the per-commit tier: a plain --boot adds them to the Debug leg
# (castle 92 s + wizard 139 s + stonehenge 185 s + duel 151 s = ~9.5 min). --boot-play nightly runs all of them (~45 min serial).
PLAY_COMMIT = ['play_castle.txt', 'play_wizard.txt', 'play_stonehenge.txt', 'play_duel.txt']
BOOT_INSTANCE = int(os.environ.get('MS_BOOT_INSTANCE', '3'))   # uaeshot.ps1 -Instance: own config copy + log, kills only its own WinUAE; parallel worktrees set their own number


def ensure_autoplay_build(adir=AUTOPLAY_DIR, btype='Debug'):
    """Configure adir if needed (flags copied from build-game-debug's cache + -DMS_AUTOPLAY=ON, build type btype). -> error text or ''."""
    d = os.path.join(ROOT, adir)
    if os.path.exists(os.path.join(d, 'CMakeCache.txt')):
        return ''
    cache = os.path.join(ROOT, 'build-game-debug', 'CMakeCache.txt')
    if not os.path.exists(cache):
        return 'build-game-debug is not configured (py tools/worktree_setup.py --here, or BUILD.md): no flags to copy'
    text = open(cache, encoding='utf-8').read()
    g = lambda k: (re.search(rf'^{k}:\w+=(.*)$', text, re.M) or [None, ''])[1].strip()
    cmd = ['cmake', '-S', ROOT, '-B', d, '-G', 'Ninja', '-DCMAKE_TOOLCHAIN_FILE=' + g('CMAKE_TOOLCHAIN_FILE'),
           '-DTOOLCHAIN_PREFIX=' + g('TOOLCHAIN_PREFIX'), '-DTOOLCHAIN_PATH=' + g('TOOLCHAIN_PATH'),
           '-DM68K_CPU=' + g('M68K_CPU'), '-DCMAKE_BUILD_TYPE=' + btype, '-DACE_PATH=' + g('ACE_PATH'),
           '-DMS_VASM=' + g('MS_VASM'), '-DMS_AUTOPLAY=ON']
    rc, out, _ = run(cmd, 'configure_' + adir.replace('-', '_'))
    return '' if rc == 0 else tail(out, 15)


def autoplay_config(adir=AUTOPLAY_DIR, stock=False):
    """<adir>/autoplay.uae = moonstone-ace-hd.uae with DH0 pointing at <adir>/hd. -> path relative to ROOT.
    stock=True (the Release leg) keeps the config's own RAM: the Release exe has to boot on the stock A1200."""
    src = open(os.path.join(ROOT, 'moonstone-ace-hd.uae'), encoding='ascii', errors='replace').read()
    hd = os.path.join(ROOT, adir, 'hd')
    if BOOT_INSTANCE != 3:   # an own copy of the HD tree: autoplay.txt and files.log are per run, so instances can run side by side
        import shutil
        hd2 = hd + '-' + str(BOOT_INSTANCE)
        for dp, _, fns in os.walk(hd):
            dst = os.path.join(hd2, os.path.relpath(dp, hd))
            os.makedirs(dst, exist_ok=True)
            for fn in fns:
                sp, dp2 = os.path.join(dp, fn), os.path.join(dst, fn)
                if fn in ('autoplay.txt', 'files.log'):
                    continue
                if not os.path.exists(dp2) or os.path.getmtime(sp) > os.path.getmtime(dp2) or os.path.getsize(sp) != os.path.getsize(dp2):
                    shutil.copy2(sp, dp2)
        hd = hd2
    out = re.sub('(?m)^filesystem2=rw,DH0:hd:.*$', lambda m: 'filesystem2=rw,DH0:hd:' + hd + ',0', src)
    # The Debug + autoplay exe no longer fits the stock 2 MB-chip / no-fast A1200 (arena alloc fails at boot); give the
    # regression machine 4 MB fast RAM. The stock budget is ROADMAP 2.11/7.2, checked on the Release build.
    if not stock:
        out = re.sub('(?m)^fastmem_size=.*$', 'fastmem_size=4', out)
    rel = os.path.join(adir, 'autoplay.uae' if BOOT_INSTANCE == 3 else f'autoplay-{BOOT_INSTANCE}.uae')
    with open(os.path.join(ROOT, rel), 'w', encoding='ascii', newline='') as f:
        f.write(out)
    return rel


def run_boot(rel=False, update_refs=False, scripts_glob='regression*.txt', scripts=None, shots_dir=None, ref_dir=None, repeat=1):
    """Run tests/boot/<scripts_glob> (or the given scripts, `repeat` times each), compare the shots (rel: the Release build
    against build/shots/ref-rel/, labelled `[boot rel ]`; update_refs: copy the shots to ref-rel/ instead). shots_dir / ref_dir
    default to build/shots and build/shots/ref[-rel] (a worktree can point at the main checkout's references).
    -> True when every screen passes."""
    import shutil, bootcheck, shotcmp
    tag = '[boot rel ]' if rel else '[boot     ]'
    shots_dir = shots_dir or os.path.join(ROOT, 'build', 'shots')
    ref_dir = ref_dir or os.path.join(shots_dir, 'ref-rel' if rel else 'ref')
    os.makedirs(shots_dir, exist_ok=True)
    cfg = autoplay_config(AUTOPLAY_REL_DIR, stock=True) if rel else autoplay_config()
    if scripts is None:
        scripts = sorted(glob.glob(os.path.join(ROOT, 'tests', 'boot', scripts_glob)))
    scripts = [x for x in scripts for _ in range(max(1, repeat))]
    runs = {}
    if not scripts:
        print(tag + ' no tests/boot/' + scripts_glob)
        return False
    ok = True
    for script in scripts:
        name = os.path.basename(script)
        info = bootcheck.parse(open(script, encoding='utf-8').read())
        for n in info['shots']:   # a stale shot must not pass for a fresh one
            try:
                os.remove(os.path.join(shots_dir, n + '.png'))
            except OSError:
                pass
        t = time.time()
        rc, out, dt = run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'uaeshot.ps1', '-Config', cfg,
                           '-Instance', str(BOOT_INSTANCE), '-Autoplay', script, '-Dir', shots_dir,
                           '-Timeout', str(bootcheck.timeout_for(info['last_frame'], shots=len(info['shots']))),
                           '-ShotDelayMs', os.environ.get('MS_SHOT_DELAY_MS', '300')], ('boot_rel_' if rel else 'boot_') + name[:-4])
        quit_seen = 'AUTOPLAY quit' in out
        runs[name] = runs.get(name, 0) + 1
        try:   # keep every run's serial log (WARN/ERR/crash lines, files: open FAIL) next to the shots
            shutil.copyfile(os.path.join(ROOT, 'build', f'uaeshot-{BOOT_INSTANCE}.log'), os.path.join(shots_dir, f'{name[:-4]}-run{runs[name]}.log'))
        except OSError:
            pass
        print(f'{tag} {name}: {"ran to quit" if quit_seen else "NO quit line (timeout)"} ({dt:.0f}s), log build/integrate/{"boot_rel_" if rel else "boot_"}{name[:-4]}.log')
        ok &= quit_seen
        try:   # serial log scan: errors fail the run, the harness notes (a wait that gave up, a press the game never read) are printed
            logtext = open(os.path.join(shots_dir, f'{name[:-4]}-run{runs[name]}.log'), encoding='latin-1').read().splitlines()
        except OSError:
            logtext = []
        bad = [l for l in logtext if re.search(r'^(ERR|WARN)\b|open FAIL|SMASHED|[Cc]rash|[Gg]uru', l)]
        notes = [l for l in logtext if re.search(r'^AUTOPLAY (wait-timeout|pulse .*MISSED|poke .*FAILED|script: .*[1-9]\d* bad lines)', l)]
        for l in bad[:8]:
            print('    LOG-ERROR ' + l[:160])
        for l in notes[:8]:
            print('    note      ' + l[:160])
        ok &= not bad
        for l in out.splitlines():
            if l.startswith('SHOT-LATE'):
                print('    ' + l)
                ok = False
        loaded = {}
        for n in info['shots']:
            sp, rp = os.path.join(shots_dir, n + '.png'), os.path.join(ref_dir, n + '.png')
            if not os.path.exists(sp):
                print(f'    {n:<26} FAIL  no screenshot taken')
                ok = False
                continue
            if update_refs:
                if not quit_seen:
                    print(f'    {n:<26} not promoted (run did not reach quit)')
                    continue
                os.makedirs(ref_dir, exist_ok=True)
                shutil.copyfile(sp, rp)
                print(f'    {n:<26} PROMOTED to build/shots/ref-rel/')
                continue
            loaded[n] = shotcmp.load(sp, shotcmp.DEFAULT_CROP)
            if repeat > 1:   # the next run deletes the shot: keep every run's own copy
                shutil.copyfile(sp, os.path.join(shots_dir, f'{n}.run{runs[name]}.png'))
            if not os.path.exists(rp):
                print(f'    {n:<26} NOREF no reference: py tools/shotcmp.py {n} --promote')
                ok = False
                continue
            pct, _ = shotcmp.compare(loaded[n], shotcmp.load(rp, shotcmp.DEFAULT_CROP))
            good, line = bootcheck.judge_ref(n, pct, info['tol'].get(n, bootcheck.DEFAULT_TOL))
            print('    ' + line)
            ok &= good
        for a, b, minimum in ([] if update_refs else info['differ']):
            if a in loaded and b in loaded:
                pct, _ = shotcmp.compare(loaded[a], loaded[b])
                good, line = bootcheck.judge_differ(a, b, pct, minimum)
                print('    ' + line)
                ok &= good
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--boot', metavar='STEPS', nargs='?', const='regression',
                    help='uaeshot.ps1 step script run after a good build; without a value (or "regression"): the headless '
                         'autoplay boot regression on build-autoplay/ (tests/boot/regression*.txt, shots vs build/shots/ref)')
    ap.add_argument('--boot-script', action='append', metavar='FILE', help='with --boot: run these scripts instead of tests/boot/regression*.txt (repeatable)')
    ap.add_argument('--boot-play', nargs='?', const='nightly', choices=['commit', 'nightly'],
                    help='with --boot: the whole-game play scripts after the regression scripts: commit = %s (~10 min, the default of a plain '
                         '--boot), nightly = every tests/boot/play_*.txt (~45 min serial)' % ' '.join(PLAY_COMMIT))
    ap.add_argument('--no-play', action='store_true', help='with --boot: no play scripts (regression*.txt only)')
    ap.add_argument('--boot-repeat', type=int, default=1, help='with --boot: run every script this many times (stability check)')
    ap.add_argument('--boot-shots', metavar='DIR', help='with --boot: shot directory (default build/shots)')
    ap.add_argument('--boot-refs', metavar='DIR', help='with --boot: reference directory (default <shots>/ref)')
    ap.add_argument('--only-boot', action='store_true', help='with --boot: skip regenerate/verify/build/tests, just boot')
    ap.add_argument('--no-rel', action='store_true', help='boot regression: skip the Release (build-autoplay-rel) leg')
    ap.add_argument('--update-refs-rel', action='store_true',
                    help='boot regression: run only the Release leg and store its shots as build/shots/ref-rel/ (instead of comparing)')
    ap.add_argument('--rel-only', action='store_true', help='boot regression: only the Release leg (no Debug autoplay build/boot)')
    ap.add_argument('--rel-practice-only', action='store_true',
                    help='boot regression: the Release leg runs only regression_practice.txt')
    ap.add_argument('--tests', choices=['auto', 'all', 'none'], default='auto')
    ap.add_argument('--skip-build', action='store_true', help='regenerate + verify (+ tests) only')
    a = ap.parse_args()
    ok = True
    auto = a.boot in ('regression', 'autoplay')
    a.rel_only = a.rel_only or a.update_refs_rel
    if a.rel_only and a.no_rel:
        ap.error('--rel-only and --no-rel exclude each other')
    if a.update_refs_rel and not auto:
        ap.error('--update-refs-rel needs --boot regression')
    if a.only_boot:
        if not auto:
            ap.error('--only-boot needs --boot regression')
        a.skip_build, a.tests = True, 'none'

    import origin
    if not a.only_boot and not origin.have_listing():
        print('[resource ] skipped: no IRA listing (public tree; asm/*.s is a developer reference, tools/origin.py)')
    elif not a.only_boot:
        rc, out, dt = run([sys.executable, 'tools/resource.py'], 'resource')
        print(f'[resource ] {"ok" if rc == 0 else "FAILED"} ({dt:.0f}s)')
        if rc:
            print(tail(out))
            return 1

        rc, out, dt = run([sys.executable, 'tools/resource.py', '--verify', '--no-write'], 'verify')
        print(f'[verify   ] {"ok" if rc == 0 else "FAILED"} ({dt:.0f}s)')
        for r in verify_summary(out):
            print('    ' + r)
        if rc:
            return 1

    if not a.only_boot:   # ROADMAP 10.3: nothing original in a published file (tools/publish.txt), ~5 s
        if not origin.have_disks():
            print('[leakscan ] skipped: no extracted disks (py tools/setup.py A.adf B.adf C.adf)')
        else:
            rc, out, dt = run([sys.executable, 'tools/leakscan.py'], 'leakscan')
            print(f'[leakscan ] {"ok" if rc == 0 else "FAILED"} ({dt:.0f}s) ' + (out.strip().splitlines() or [''])[-1])
            if rc:
                print(tail(out, 25))
                return 1

    if not a.skip_build:
        for d, tgt in (('build', None), ('build-game-debug', None), ('build-game-debug', 'hdinstall')):
            if not os.path.exists(os.path.join(ROOT, d, 'CMakeCache.txt')):
                print(f'[link     ] {d} is not configured: py tools/worktree_setup.py --here (or BUILD.md configure lines)')
                return 1
            cmd = ['cmake', '--build', d] + (['--target', tgt] if tgt else [])
            rc, out, dt = run(cmd, 'build_' + d.replace('-', '_') + ('_' + tgt if tgt else ''))
            print(f'[link     ] {d}{" " + tgt if tgt else ""}: {"ok" if rc == 0 else "FAILED"} ({dt:.0f}s)')
            if rc:
                print(tail(out, 25))
                return 1

    if auto and not a.only_boot:
        err = ensure_autoplay_build() if not a.rel_only else ''
        if err:
            print('[autoplay ] configure FAILED')
            print(err)
            return 1
        legs = [(AUTOPLAY_DIR, 'Debug', '[autoplay ]')]
        if not a.no_rel:
            legs.append((AUTOPLAY_REL_DIR, 'Release', '[autoplay rel]'))
        if a.rel_only:
            legs = legs[1:]
        for adir, btype, tag in legs:
            err = ensure_autoplay_build(adir, btype) if adir != AUTOPLAY_DIR else ''
            if err:
                print(tag + ' configure FAILED')
                print(err)
                return 1
            for tgt in (None, 'hdinstall'):
                rc, out, dt = run(['cmake', '--build', adir] + (['--target', tgt] if tgt else []),
                                  adir.replace('-', '_') + ('_' + tgt if tgt else ''))
                print(f'{tag} {adir}{" " + tgt if tgt else ""}: {"ok" if rc == 0 else "FAILED"} ({dt:.0f}s)')
                if rc:
                    print(tail(out, 25))
                    return 1

    mods = select_tests(a.tests)
    if mods:
        print(f'[tests    ] {len(mods)} module(s): {", ".join(m.replace("test_", "") for m in mods)}')
        with cf.ThreadPoolExecutor(max_workers=4) as ex:
            results = list(ex.map(run_test, mods))
        failed = [r for r in results if r[1] != 0]
        print(f'    pass {len(results) - len(failed)}, fail {len(failed)}')
        for mod, rc, n, bad, dt in failed:
            print(f'    FAIL {mod} ({n} tests): {", ".join(bad[:8]) or "see build/integrate/" + mod + ".log"}')
        ok &= not failed
    elif a.tests != 'none':
        print('[tests    ] none selected')

    if auto:
        boot_scripts = [os.path.abspath(x) for x in a.boot_script] if a.boot_script else None
        tier = a.boot_play or (None if a.no_play or boot_scripts is not None else 'commit')
        if tier:
            if boot_scripts is None:
                boot_scripts = sorted(glob.glob(os.path.join(ROOT, 'tests', 'boot', 'regression*.txt')))
            if tier == 'commit':
                boot_scripts += [os.path.join(ROOT, 'tests', 'boot', f) for f in PLAY_COMMIT]
            else:
                boot_scripts += sorted(glob.glob(os.path.join(ROOT, 'tests', 'boot', 'play_*.txt')))
        bkw = dict(scripts=boot_scripts, shots_dir=a.boot_shots, ref_dir=a.boot_refs, repeat=a.boot_repeat)
        if not a.rel_only:
            passed = run_boot(**bkw)
            print(f'[boot     ] regression: {"all screens PASS" if passed else "FAILED"}')
            ok &= passed
        if not a.no_rel or a.rel_only:
            passed = run_boot(rel=True, update_refs=a.update_refs_rel,
                              scripts_glob='regression_practice.txt' if a.rel_practice_only else 'regression*.txt',
                              shots_dir=a.boot_shots, repeat=a.boot_repeat)
            print(f'[boot rel ] regression: {"refs updated" if a.update_refs_rel else "all screens PASS" if passed else "FAILED"}')
            ok &= passed
    elif a.boot:
        rc, out, dt = run(['powershell', '-NoProfile', '-File', 'uaeshot.ps1', '-Joy', '-Steps', a.boot], 'boot')
        print(f'[boot     ] {"ok" if rc == 0 else "FAILED"} ({dt:.0f}s), screenshots in build/shots')
    print('RESULT: ' + ('OK' if ok else 'FAILED'))
    return 0 if ok else 2


if __name__ == '__main__':
    sys.exit(main())
