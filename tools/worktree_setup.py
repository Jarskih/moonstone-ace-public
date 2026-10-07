"""Prepare a git worktree of this repo for an agent: link the git-ignored inputs, configure build dirs.

  py tools/worktree_setup.py --path D:/Amiga/wt/<name> [--branch NAME] [--no-configure]
  py tools/worktree_setup.py --here        # this checkout is already a worktree: link + configure only

Creates (if --path does not exist) `git worktree add <path> -b <branch>` from HEAD, then in <path>/moonstone-ace:
  - links (junction on Windows) private/ (public repo, docs/MIGRATION.md: asm/, src/lifted, reference/ are then materialised from
    it by tools/dev_setup.py), build/{disks,reasm,inventory,art}, reference/, tests/diff and
    tools/{toolchain,AmigaCMakeCrossToolchains,bartman_gcc_support} from the main checkout (git-ignored inputs);
  - configures build/ (Release) and build-game-debug/ (Debug), both the game (no asm is linked since ROADMAP 7.1r), with the flags of the
    main checkout's build-game-debug/CMakeCache.txt (toolchain, vasm, ACE_PATH).
The links are shared, not copied: treat them as read-only from a worktree.
Remove a worktree with `git worktree remove <path>` after deleting its junctions (rmdir, not rm -r). See docs/PATCHES.md.
"""
import argparse, os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import integrate as I  # noqa: E402

LINKS = ['private', 'build/disks', 'build/reasm', 'build/inventory', 'build/art', 'reference', 'tests/diff',
         'tools/toolchain', 'tools/AmigaCMakeCrossToolchains', 'tools/bartman_gcc_support']


def git_top():
    return subprocess.run(['git', 'rev-parse', '--show-toplevel'], cwd=I.ROOT, capture_output=True,
                          text=True).stdout.strip()


def main_checkout():
    """moonstone-ace dir of the primary worktree (first entry of `git worktree list`)."""
    p = subprocess.run(['git', 'worktree', 'list', '--porcelain'], cwd=I.ROOT, capture_output=True, text=True)
    first = re.search(r'worktree (.+)', p.stdout).group(1).strip()
    return os.path.normpath(os.path.join(first, os.path.relpath(I.ROOT, git_top())))


def link(src, dst):
    src, dst = os.path.normpath(src), os.path.normpath(dst)
    if not os.path.exists(src):
        return f'skip (missing in main checkout): {src}'
    if os.path.lexists(dst):
        return f'exists: {dst}'
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.name == 'nt':
        r = subprocess.run(['cmd', '/c', 'mklink', '/J', dst, src], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'mklink /J failed for {dst}: {r.stdout}{r.stderr}')
    else:
        os.symlink(src, dst)
    return f'linked: {dst} -> {src}'


def cache_value(cache, key):
    with open(cache, encoding='utf-8') as f:
        m = re.search(rf'^{key}:\w+=(.*)$', f.read(), re.M)
    return m.group(1).strip() if m else ''


def configure(proj, main, name, build_type):
    cache = os.path.join(main, 'build-game-debug', 'CMakeCache.txt')
    if not os.path.exists(cache):
        return f'{name}: main checkout has no build-game-debug/CMakeCache.txt; use the BUILD.md configure lines'
    g = lambda k: cache_value(cache, k)
    args = ['cmake', '-S', proj, '-B', os.path.join(proj, name), '-G', 'Ninja',
            '-DCMAKE_TOOLCHAIN_FILE=' + g('CMAKE_TOOLCHAIN_FILE'), '-DTOOLCHAIN_PREFIX=' + g('TOOLCHAIN_PREFIX'),
            '-DTOOLCHAIN_PATH=' + g('TOOLCHAIN_PATH'), '-DM68K_CPU=' + g('M68K_CPU'),
            '-DCMAKE_BUILD_TYPE=' + build_type, '-DACE_PATH=' + g('ACE_PATH'), '-DMS_VASM=' + g('MS_VASM')]
    r = subprocess.run(args, env=I.toolchain_env(), capture_output=True, text=True)
    return f'{name}: configure {"ok" if r.returncode == 0 else "FAILED"}' + \
        ('' if r.returncode == 0 else '\n' + (r.stdout + r.stderr)[-1500:])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--path', help='worktree root (a repo root, e.g. D:/Amiga/wt/display); created if missing')
    ap.add_argument('--branch', help='branch for a new worktree (default: wt-<dirname>)')
    ap.add_argument('--here', action='store_true', help='this checkout is the worktree already')
    ap.add_argument('--no-configure', action='store_true')
    a = ap.parse_args()
    main_proj = main_checkout()
    if a.here:
        proj = I.ROOT
    else:
        if not a.path:
            ap.error('--path or --here required')
        wt = os.path.abspath(a.path)
        if not os.path.exists(wt):
            r = subprocess.run(['git', 'worktree', 'add', wt, '-b', a.branch or 'wt-' + os.path.basename(wt)],
                               cwd=git_top(), capture_output=True, text=True)
            print((r.stdout + r.stderr).strip())
            if r.returncode:
                return 1
        proj = os.path.normpath(os.path.join(wt, os.path.relpath(I.ROOT, git_top())))
    if os.path.normcase(proj) == os.path.normcase(main_proj):
        print('this is the main checkout; nothing to link')
        return 1
    for l in LINKS:
        print(link(os.path.join(main_proj, l), os.path.join(proj, l)))
    private = os.path.join(proj, 'private')
    if os.path.isdir(private) and not os.path.isfile(os.path.join(proj, 'asm', 'patches', 'abs_symbols.json')):
        # public repository (tools/dev_setup.py): the git-ignored asm/, src/lifted, reference are materialised from private/
        import dev_setup
        dev_setup.materialise(proj, private)
    if not a.no_configure:
        print(configure(proj, main_proj, 'build', 'Release'))
        print(configure(proj, main_proj, 'build-game-debug', 'Debug'))
    print(f'ready: cd {proj} && py tools/integrate.py')
    return 0


if __name__ == '__main__':
    sys.exit(main())
