"""Build and run the "How to add a scene" example of docs/GAME_FLOW.md (ROADMAP 9.2a).

  py tools/flow_example.py [--no-build]

Configures build-example/ (Debug, -DMS_AUTOPLAY=ON -DMS_EXAMPLE_SCENE=ON, the other flags from build-game-debug's cache),
builds it with its hdinstall, then runs tests/boot/example/example_scene.txt headlessly (uaeshot.ps1 -Instance $MS_BOOT_INSTANCE)
and leaves the shots example-status / example-picture / example-map in build/shots/example/.  The default game is not
touched: without MS_EXAMPLE_SCENE the example's two files are not even compiled.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import integrate as I  # noqa: E402
import bootcheck  # noqa: E402

ADIR = 'build-example'
SCRIPT = os.path.join(I.ROOT, 'tests', 'boot', 'example', 'example_scene.txt')


def configure():
    d = os.path.join(I.ROOT, ADIR)
    if os.path.exists(os.path.join(d, 'CMakeCache.txt')):
        return ''
    text = open(os.path.join(I.ROOT, 'build-game-debug', 'CMakeCache.txt'), encoding='utf-8').read()
    g = lambda k: (re.search(rf'^{k}:\w+=(.*)$', text, re.M) or [None, ''])[1].strip()
    cmd = ['cmake', '-S', I.ROOT, '-B', d, '-G', 'Ninja', '-DCMAKE_TOOLCHAIN_FILE=' + g('CMAKE_TOOLCHAIN_FILE'),
           '-DTOOLCHAIN_PREFIX=' + g('TOOLCHAIN_PREFIX'), '-DTOOLCHAIN_PATH=' + g('TOOLCHAIN_PATH'),
           '-DM68K_CPU=' + g('M68K_CPU'), '-DCMAKE_BUILD_TYPE=Debug', '-DACE_PATH=' + g('ACE_PATH'),
           '-DMS_VASM=' + g('MS_VASM'), '-DMS_AUTOPLAY=ON', '-DMS_EXAMPLE_SCENE=ON']
    rc, out, _ = I.run(cmd, 'configure_build_example')
    return '' if rc == 0 else I.tail(out, 15)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--no-build', action='store_true')
    a = ap.parse_args()
    if not a.no_build:
        err = configure()
        if err:
            print('[example] configure FAILED\n' + err)
            return 1
        for tgt in (None, 'hdinstall'):
            rc, out, dt = I.run(['cmake', '--build', ADIR] + (['--target', tgt] if tgt else []), 'build_example' + ('_' + tgt if tgt else ''))
            print(f'[example] build {tgt or "exe"}: {"ok" if rc == 0 else "FAILED"} ({dt:.0f}s)')
            if rc:
                print(I.tail(out))
                return 1
    shots = os.path.join(I.ROOT, 'build', 'shots', 'example')
    os.makedirs(shots, exist_ok=True)
    cfg = I.autoplay_config(ADIR)
    info = bootcheck.parse(open(SCRIPT, encoding='utf-8').read())
    rc, out, dt = I.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'uaeshot.ps1', '-Config', cfg,
                         '-Instance', str(I.BOOT_INSTANCE), '-Autoplay', SCRIPT, '-Dir', shots,
                         '-Timeout', str(bootcheck.timeout_for(info['last_frame'], shots=len(info['shots']))),
                         '-ShotDelayMs', os.environ.get('MS_SHOT_DELAY_MS', '300')], 'boot_example')
    quit_seen = 'AUTOPLAY quit' in out
    print(f'[example] {"ran to quit" if quit_seen else "NO quit line (timeout)"} ({dt:.0f}s), log build/integrate/boot_example.log')
    for n in info['shots']:
        p = os.path.join(shots, n + '.png')
        print(f'    {n:<20} {"saved " + os.path.relpath(p, I.ROOT) if os.path.exists(p) else "MISSING"}')
    return 0 if quit_seen else 1


if __name__ == '__main__':
    sys.exit(main())
