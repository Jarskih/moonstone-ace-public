"""Boot-bisect helper: disable patch files / patch ids, regenerate, build hdinstall, boot, restore.

  py tools/bisect_patches.py --disable mog.loaders.json --disable-id cl-select --steps "wait:40,shot:x" --shot name

--disable FILE     drop every patch in asm/patches/FILE (".json" optional); its "funcs"/"constants" stay,
                   so the rt_* symbols keep resolving
--disable-id ID    drop one patch id wherever it lives (file:id is accepted too)
--steps S          uaeshot.ps1 step script (default "wait:40,shot:boot"); with --shot TAG every "shot:<n>"
                   becomes "shot:TAG-<n>"
--shot TAG         tag for the screenshots (alone: steps "wait:40,shot:TAG")
--build-only       regenerate + build, skip the boot
asm/patches is copied byte-for-byte to build/bisect_backup/ before anything is touched and restored (then
asm/*.s regenerated) in a finally block; a stale backup left by a killed run is restored first.
"""
import argparse, json, os, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import integrate as I  # noqa: E402

ROOT = I.ROOT
PD = os.path.join(ROOT, 'asm', 'patches')
BACKUP = os.path.join(ROOT, 'build', 'bisect_backup')


def restore():
    if not os.path.isdir(BACKUP):
        return False
    for f in os.listdir(PD):
        if f.endswith('.json') and not os.path.exists(os.path.join(BACKUP, f)):
            os.remove(PD + os.sep + f)
    for f in os.listdir(BACKUP):
        shutil.copy2(os.path.join(BACKUP, f), os.path.join(PD, f))
    shutil.rmtree(BACKUP)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--disable', action='append', default=[], metavar='FILE')
    ap.add_argument('--disable-id', action='append', default=[], metavar='ID')
    ap.add_argument('--steps', default=None)
    ap.add_argument('--shot', default=None, help='screenshot tag')
    ap.add_argument('--build-only', action='store_true')
    a = ap.parse_args()
    if restore():
        print('restored patch tables from a stale backup (an earlier run was killed)')
    if not a.disable and not a.disable_id:
        ap.error('nothing to disable')

    if a.steps:
        steps = ','.join(s.replace('shot:', f'shot:{a.shot}-', 1) if a.shot and s.startswith('shot:') else s
                         for s in a.steps.split(','))
    else:
        steps = f'wait:40,shot:{a.shot or "boot"}'
    ids = set(i.split(':')[-1] for i in a.disable_id)
    files = {(f if f.endswith('.json') else f + '.json') for f in a.disable}
    for f in files:
        if not os.path.exists(os.path.join(PD, f)):
            sys.exit(f'no such patch file: {f}')

    os.makedirs(BACKUP)
    for f in os.listdir(PD):
        shutil.copy2(os.path.join(PD, f), os.path.join(BACKUP, f))
    rc = 1
    try:
        found = set()
        for f in sorted(os.listdir(PD)):
            if not f.endswith('.json') or f == 'abs_symbols.json':
                continue
            path = os.path.join(PD, f)
            with open(path, encoding='utf-8') as fh:
                d = json.load(fh)
            keep = []
            for p in d.get('patches', []):
                if f in files or p['id'] in ids:
                    found.add(p['id'])
                else:
                    keep.append(p)
            if len(keep) != len(d.get('patches', [])):
                d['patches'] = keep
                with open(path, 'w', encoding='utf-8', newline='\n') as fh:
                    fh.write(json.dumps(d, indent=1) + '\n')
        missing = ids - found
        if missing:
            print(f'patch id(s) not found: {sorted(missing)}')
            return 1
        print(f'disabled {len(found)} patches')
        for cmd, log in (([sys.executable, 'tools/resource.py'], 'bisect_resource'),
                         (['cmake', '--build', 'build-game-debug', '--target', 'hdinstall'], 'bisect_build')):
            r, out, dt = I.run(cmd, log)
            print(f'{log}: {"ok" if r == 0 else "FAILED"} ({dt:.0f}s)')
            if r:
                print(I.tail(out, 25))
                return 1
        if a.build_only:
            return 0
        r, out, dt = I.run(['powershell', '-NoProfile', '-File', 'uaeshot.ps1', '-Joy', '-Steps', steps], 'bisect_boot')
        print(f'boot: {"ok" if r == 0 else "FAILED"}; steps {steps}; shots in build/shots')
        rc = r
    finally:
        restore()
        r, out, dt = I.run([sys.executable, 'tools/resource.py'], 'bisect_restore')
        print('patch tables restored' + ('' if r == 0 else '; regenerate FAILED, see build/integrate/bisect_restore.log'))
    return rc


if __name__ == '__main__':
    sys.exit(main())
