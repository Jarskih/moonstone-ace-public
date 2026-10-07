# Migrating development to the public repository (ROADMAP 10.4)

Owner decision 2026-10-07: the public repository is `D:\moonstone-ace-public` (branch `main`, `ace/` a git submodule pinned at
9e6ce06 from https://github.com/AmigaPorts/ACE). Its layout is the contents of `moonstone-ace/` at the repository root plus
`ace/`. All development moves there; `D:\Amiga` stays as the **private archive** (moonshard, the IRA listing, the history that
contains original material, the disk images). Nothing original may ever enter the public repository (`docs/PUBLISHING.md`).

Tools (all in `tools/`, all run from a checkout of the archive's `moonstone-ace/`, i.e. the integrator's main checkout):

| Tool | Does |
|---|---|
| `export_public.py DEST` | writes the keep-list of `tools/publish.txt` of this checkout into DEST (not `.git`, not `ace/`), installs the overlays, deletes what left the keep-list, runs the leak scan on exactly the exported files, prints a summary. Idempotent. |
| `dev_setup.py --from D:/Amiga` | run **in the public checkout**: fills the git-ignored `private/` (listing, asm reference, lifted oracle, disk images), materialises it into the tree, links the toolchain, pins `bartman_gcc_support`. |
| `setup.py A.adf B.adf C.adf` | as before: extracts the disks into `build/`. `private/reference/disks/` holds the images. |
| `worktree_setup.py` | now also links `private/` into worktrees and materialises it there. |

Files used by the export: `tools/public.gitignore` (becomes `.gitignore`), `tools/public.gitattributes` (becomes
`.gitattributes`: `* -text`, no CRLF conversion, AmigaOS text files need LF), `docs/CLAUDE.public.md` (becomes `CLAUDE.md`).
The `*.uae` configs get their `D:\Amiga\moonstone-ace` paths rewritten to DEST (`--keep-uae-paths` leaves them).

## What `private/` is and how the tools find it

`private/` is git-ignored (never published). `dev_setup.py` fills it with the same relative layout the old tree had:

```
private/reference/moonshard/moonstone-main/amiga_asm/{nb,program,mog}.asm, + the executables, libmoon_assets/
private/reference/moonshard/{tools/moghunks.py, mechanics.*, planar_contact.*}
private/reference/disks/Moonstone (Mindscape) {A,B,C}.adf
private/asm/{program,mog,synth}.s, *.hw.txt, patches/<everything except *.data*.json>
private/src/lifted/**                 (312 files, the differential-test oracle)
private/tests/test_lift.py, test_diffharness.py
```

`tools/origin.py` (one owner of the paths) looks in `private/reference/...` first, then `reference/...`, then `../moonshard/...`
(the archive layout), so everything works in all three places. The ~30 other tools and tests that build
`ROOT/reference/...`, `ROOT/asm/...` or `src/lifted` paths themselves keep working because dev_setup also **materialises**
`private/` into the tree: `reference` and `src/lifted` become junctions, `asm/*.s`, `asm/*.hw.txt` and the dropped
`asm/patches/*.json` are hard links (copy if the link fails) next to the kept `*.data*.json` patches, and the two dropped tests are
copied into `tests/`. All of those paths are in `.gitignore` (`/reference/`, `/src/lifted/`, `/asm/*.s`, `/asm/*.hw.txt`,
`/asm/patches/*.json` except `*.data.json` / `*.data_*.json`, the two tests), so `git status` stays clean. Without `private/`
the listing-based steps print "skipped: needs the IRA listing" and those unit tests skip; the build and the game do not need it.
(Hard links: edit a materialised file and the `private/` copy changes too, which is intended; the editor must not replace it by
delete + write if you want that to hold, otherwise re-run `dev_setup.py`.)

The toolchain links `tools/toolchain` (Bartman gcc + elf2hunk), `tools/AmigaCMakeCrossToolchains` and
`tools/bartman_gcc_support` are junctions into the archive (`--copy-toolchain` copies instead; they are git-ignored). The
`bartman_gcc_support` revision is pinned in `dev_setup.py` (`BARTMAN_PIN`, commit cdaec3ef7056 = the vendored copy). Without the
local copy ACE's CPM fetches `latest` from the network, which drifts (docs/PUBLISHING.md section 7); `CMakeLists.txt` uses
`tools/bartman_gcc_support` automatically when it exists. A developer without the archive runs
`py tools/dev_setup.py --from <dir> --fetch-support` (clones the pinned commit) or clones it there by hand and checks out the pin.

## The real export: steps for the integrator

Preconditions: every agent branch is merged into the archive's `moonstone-ace` branch/main, `git status` there is clean, no agent
is still running in a worktree (they would have to be re-created in the new repository), `build/disks` is extracted (the leak scan
needs it), and the last `py tools/integrate.py --boot --tests auto` was green.

1. **Pre-flight, from `D:\Amiga\moonstone-ace`** (toolchain PATH of AGENTS.md):
   ```sh
   git status --short                       # clean (default.profraw and the like: delete or ignore, they are not published)
   py tools/leakscan.py                     # the archive side is clean
   py tools/export_public.py D:/moonstone-ace-public --dry-run     # what would be written
   ```
2. **Export** (does not touch `.git` or `ace/`):
   ```sh
   py tools/export_public.py D:/moonstone-ace-public
   ```
   Expect "leak scan clean" (a few "notes only" are the accepted constant tables of docs/PUBLISHING.md section 3). Exit status 1
   means original content was found: stop, do not commit. Re-run any time; it is idempotent.
3. **Check the submodule and first commit, in `D:\moonstone-ace-public`**:
   ```sh
   git config core.autocrlf false
   git submodule status                     # ace at 9e6ce06 (heads/main)
   git add -A                               # .gitignore keeps build*/, private/, reference/, asm/*.s ... out
   git add .gitmodules ace                  # the gitlink (also staged by -A once the submodule is registered)
   git status --short | grep -v "^A "       # nothing else; `git diff --cached --stat | tail -1` ~ 576 files + ace
   git ls-files | grep -E "^(private|reference|build|asm/[a-z]+\.s|src/lifted)" # must print nothing
   git commit -m "moonstone-ace: first public commit (no original data; ROADMAP 10.4)"
   ```
   Author/Co-Authored-By lines as for any commit. Do not push (owner decision 10.5: licence and the legal question come first).
4. **Make the new repository a developer checkout**:
   ```sh
   cd D:/moonstone-ace-public
   py tools/dev_setup.py --from D:/Amiga                 # private/, materialise, toolchain links, build/reasm (reassemble.py)
   ```
   `dev_setup.py` ends with `tools/reassemble.py` (needs `tools/toolchain/vasmm68k_mot.exe` and the listing) unless
   `build/reasm/mog.asm` exists: `check.py`, `routines.py` and `lift.py` read `build/reasm/*.asm`. `setup.py` alone only leaves the
   original executables there as a stand-in (enough for the public flow, not for the listing tools).
5. **Move `build/`** (the extracted disks, shot references and generated inputs are not in git and not re-creatable from the
   repository alone). Copy, do not move, until the new repository is verified; the archive keeps working:
   ```sh
   cd D:/Amiga/moonstone-ace
   robocopy build D:\moonstone-ace-public\build /E /XD CMakeFiles _deps ace hd diff resource /XF CMakeCache.txt build.ninja cmake_install.cmake *.log
   ```
   That brings `build/{disks,adf,reasm,inventory,art,shots (incl. ref, ref-rel),hunk9,ghidra,autoplay-hd,...}` and the owner's
   `build/*.uae` configs (edit their `D:\Amiga\moonstone-ace` paths to `D:\moonstone-ace-public`). The other build trees
   (`build-game-debug`, `build-autoplay`, `build-autoplay-rel`, `build-enh`, `build-synth`) hold absolute paths in their CMake
   caches: do **not** copy them, configure them fresh with the lines of BUILD.md (`P=D:/moonstone-ace-public`). If `build/disks`
   was not copied: `py tools/setup.py private/reference/disks/*.adf --no-build`-style (three arguments) re-extracts it.
   `build/reasm` from the archive is the real reassembly (`tools/reassemble.py`); `setup.py` only creates a stand-in when it is absent.
6. **Verify in the new repository** (this is what the dry run did in a scratch tree):
   ```sh
   py tools/check.py --quick
   py -m unittest discover tests
   # configure + build Release and Debug (BUILD.md lines with P=D:/moonstone-ace-public), then
   MS_BOOT_INSTANCE=<n> py tools/integrate.py --boot --no-play --no-rel --tests none
   ```
7. **Repoint the people and the tools** (below), then retire `D:\Amiga\moonstone-ace` as the working tree: leave it as is, stop
   committing there. New work happens in `D:\moonstone-ace-public` only.

The `main`-branch pointer of the archive does not change; nothing is pushed anywhere. Later re-exports from the archive are not part
of the plan (development continues in the new repository); `export_public.py` stays for a final check (`--dry-run`) and for fresh
scratch copies.

## Edits after the first commit (new repository only; the archive's files are not edited)

- `CLAUDE.md` is already the text of `docs/CLAUDE.public.md` (the export installs it). The template can be deleted from `docs/` in
  a later commit; edit `CLAUDE.md` from then on.
- **`README.md`**: replace the archive's developer text with the player text of `docs/PUBLISHING.md` section 8 (where to get the
  disks, WinUAE / real A1200, how to build with `--recursive`). Mention `docs/MIGRATION.md`/`dev_setup.py` only under "Developing".
- **`BUILD.md`**: `P=D:/Amiga/moonstone-ace` and the toolchain PATH become `P=D:/moonstone-ace-public` (or `$(pwd)`), the
  "dependencies from the original workspace" sentence is replaced by `py tools/dev_setup.py --from D:/Amiga`; `.uae` paths likewise.
- `docs/AGENT_BRIEF.md`, `docs/MODDING.md`, `docs/PATCHES.md`, `docs/FILES.md`: "Read first `D:\Amiga\CLAUDE.md`, `AGENTS.md`" becomes
  `CLAUDE.md` + `BUILD.md`; `D:/Amiga/wt/<task>` becomes `wt/<task>` (see below); the asm authority path becomes
  `private/reference/moonshard/moonstone-main/amiga_asm/`.
- `tools/vendor_local.py` copied the workspace dependencies into the archive tree; `dev_setup.py` replaces it. Remove it in the
  new repository (it hard-codes the archive layout). `tools/integrate.py` / `setup.py` already find `tools/toolchain` first.
- `ROADMAP.md`: mark 10.4 done with a note; 10.2/10.3/10.6 as their state says. Keep the numbers.
- `AGENTS.md`: the archive's file is the workspace guide for `D:\Amiga` (moonshard, breakout, BBHack...) and stays there. The public
  repository has **no** `AGENTS.md`: what it needs (toolchain PATH and configure lines in `BUILD.md`, the engine-pattern, style
  and "no heap / no global constructors" rules, the LF-only rule, the `-fno-omit-frame-pointer` warning) is in `CLAUDE.md`
  (shortened) and `docs/AGENT_BRIEF.md`; if the owner wants the long pattern list there, copy the "Code architecture" section of
  `D:\Amiga\AGENTS.md` into `docs/CODE_PATTERNS.md` of the new repository and link it from `CLAUDE.md`.
- In the archive (`D:\Amiga`), after the move, one commit on `D:\Amiga\CLAUDE.md`/`AGENTS.md` (by the integrator, outside this
  task): a line "moonstone-ace development moved to D:\moonstone-ace-public (docs/MIGRATION.md); this tree is the frozen private
  archive with the IRA listing and the history".

## Worktrees and agents in the new repository

- Worktrees live below the repository root in `wt/` (git-ignored): `git worktree add wt/<task> -b wt-<task> main` from
  `D:\moonstone-ace-public`, then `py tools/worktree_setup.py --here` in `wt/<task>`. It links `private/`, `build/{disks,reasm,
  inventory,art}`, `reference`, `tools/{toolchain,AmigaCMakeCrossToolchains,bartman_gcc_support}` from the main checkout,
  materialises `asm/`, `src/lifted` and the dropped tests from `private/`, and configures `build/` + `build-game-debug/` with the
  main checkout's flags (it reads the main checkout's `build-game-debug/CMakeCache.txt`: build that first).
  The repository root *is* the project directory now (there is no `moonstone-ace/` level); `worktree_setup.py` handles that.
  The `ace/` submodule is not populated in a worktree: the configure uses the main checkout's `ACE_PATH`; for a manual configure
  pass `-DACE_PATH=D:/moonstone-ace-public/ace`.
- Shared links are read-only from a worktree. Remove a worktree: delete its junctions with `rmdir` (not `rm -r`, which would
  follow them into the main checkout's `build/`), then `git worktree remove`.
- Agents: same rules as `docs/AGENT_BRIEF.md` with the paths above; own worktree, commit on the worktree branch with explicit
  paths, never touch another agent's `MS_BOOT_INSTANCE` (the integrator uses 3; parallel agents get their own numbers), never kill
  other WinUAE instances, never send real keystrokes. "Never touch" files are unchanged (`src/lifted/**` etc. exist only as
  materialised, ignored files in a developer checkout, so they cannot be committed by accident any more).
- Nothing original in a commit: `py tools/leakscan.py` is a step of `integrate.py`; screenshots, disk images, `private/` and
  `build*/` are ignored by `.gitignore`.

## Dry-run evidence (2026-10-07, scratch tree `D:\ms-pub-scratch`, not the real repository)

`export_public.py D:/ms-pub-scratch` (580 files of 943, leak scan clean: 0 files with original content, 6 notes-only) -> `ace`
junction to `D:\Amigace` -> `dev_setup.py --from D:/Amiga` (private/ 34 reference + 364 dropped files, 52 materialised, 3
toolchain junctions, reassembly load-equivalent for nb/program/mog) -> `setup.py` with the three ADFs from `private/reference/disks`
(extract, Release + Debug build, `hdinstall`: both built from the exported tree) -> `check.py --quick`,
`py -m unittest discover tests` (879 tests, 71 skipped), one boot regression with `MS_BOOT_INSTANCE=85`
(`integrate.py --boot --no-play --no-rel --tests none`: resource/verify/leakscan/link/autoplay steps ok, regression.txt and
regression_practice.txt all screens PASS against `build/shots/ref` copied from the archive). Findings fixed on the way:
`tests/test_overworld.py` read the listing from `../moonshard` unguarded (23 errors in a public tree; now `origin.listing_path`),
`leakscan --tree` must skip `private/`, `reference/` and the toolchain links, and a kept `tests/diff/replay_main.cpp` was caught by
a too broad ignore rule. Test the exported tree again with `git add -A -n` after an export: it must list the exported files and
nothing from `private/`.
