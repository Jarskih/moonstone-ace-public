# Building moonstone-ace

Cross-build of an ACE C++ program (Bartman gcc 15.1, 68020) to `.elf` ->
Amiga hunk `.exe` -> bootable `.adf`. Git Bash on Windows.

## From a public clone and your ADFs: `tools/setup.py` (ROADMAP 10.2)

```sh
py tools/setup.py <folder with the three ADFs | A.adf B.adf C.adf> [--dry-run] [--fetch-deps] [--toolchain DIR] [--cmake-toolchains DIR] [--ace DIR] [--kickstart ROM] [--debug] [--hd DIR]
```

One command for someone who cloned the public repository (no `private/`, `asm/` or `build/` data). Disks are identified by content,
so names and order do not matter; a folder is searched (one level of subfolders) for 901,120-byte files. Errors name the file and
the SHA-1 found and expected (`tools/setup.py` `KNOWN_DUMPS`; disk A's `program`/`mog`/`nb` against `tools/facts/*.json`);
`--allow-unknown-dump` accepts a disk with the right files but another hash. It then checks Python 3.8+ + PyYAML, CMake, Ninja, the
cross gcc (`--toolchain`, `MS_TOOLCHAIN`, `tools/toolchain`, the VS Code extension `bartmanabyss.amiga-debug-*`, or `PATH`),
AmigaCMakeCrossToolchains, ACE (`ace/` submodule or `../ace`) and `tools/bartman_gcc_support`, and prints for everything missing how to
get it (`--fetch-deps` clones the pinned bartman_gcc_support itself). Then: copy to `build/adf`, extract to `build/disks`, `build/reasm`
stand-ins, configure + build `build/` (Release), `--target hdinstall` -> `build/hd`, write `build/moonstone-ace-hd.uae` (the release
config of `docs/packaging` with DH0 = `build/hd`) and print the WinUAE command. `--dry-run` does the read-only checks and lists the
steps, writing nothing. Test: `tests/test_setup.py` (synthetic disks). Shipping archives for players: "Release packages" below.

## Toolchain PATH (every shell)

```sh
export PATH="/d/Amiga/moonstone-ace/tools/toolchain/opt/bin:/d/Amiga/moonstone-ace/tools/toolchain:/d/Amiga/moonstone-ace/tools/toolchain/opt/m68k-amiga-elf/bin:/c/msys64/mingw64/bin:$PATH"
```

## Configure

```sh
P=D:/Amiga/moonstone-ace
TC="-DCMAKE_TOOLCHAIN_FILE=$P/tools/AmigaCMakeCrossToolchains/m68k-bartman.cmake -DTOOLCHAIN_PREFIX=m68k-amiga-elf -DTOOLCHAIN_PATH=$P/tools/toolchain/opt -DM68K_CPU=68020"

# Release  -> build/        (note: build/ also holds git-ignored extracted disk data)
cmake -S $P -B $P/build -G Ninja $TC -DCMAKE_BUILD_TYPE=Release
# Debug    -> build-debug/  (-g -O0, no LTO, KPrintF kept)
cmake -S $P -B $P/build-debug -G Ninja $TC -DCMAKE_BUILD_TYPE=Debug
```

ACE is shared at `../ace/`; its Bartman support library is in `tools/bartman_gcc_support/`,
so configuring with these local copies does not require a network fetch.
Dependencies can be copied from the original workspace with `py tools/vendor_local.py`.
Use a fresh build directory when switching toolchains; existing CMake caches retain
their compiler and dependency paths. `ACE_PATH` and `MS_VASM` remain overridable.

## Build

```sh
cmake --build $P/build                       # moonstone.elf + moonstone (hunk) + moonstone.exe
cmake --build $P/build --target adf          # build/moonstone.adf (bootable, LF startup-sequence)
cmake --build $P/build-debug && cmake --build $P/build-debug --target adf
```

## Run

```sh
"/c/Program Files/WinUAE/winuae64.exe" -config="D:\Amiga\moonstone-ace\moonstone-ace.uae"
```

A1200 / AGA / 68020 / KS3.1, boots `build/moonstone.adf`. ESC or joystick
fire exits. Screenshot automation: `uaeshot.ps1` in this project.

## Layout / notes

- Sources are globbed from `src/**/*.cpp` and `src/**/*.c`; headers in `include/` and `src/`.
- `src/rt/` holds runtime subsystems (system, display, text); `src/main.cpp` is the ACE entry.
- vasm hook: fill `MS_ASM_SOURCES` in `CMakeLists.txt`; each is assembled with
  `tools/toolchain/vasmm68k_mot.exe -Felf -m68020 -devpac` and linked in.
- Do not add `-fno-omit-frame-pointer` (GCC 15.1 ICE in bartman_gcc_support).
- C++ is built `-fno-exceptions -fno-rtti -fno-threadsafe-statics`; no STL, no
  `new`/`delete`, global constructors do not run.
- Debug needs `-fno-lto -fno-whole-program` for **C and C++** (set in CMakeLists);
  otherwise the whole-program link hides ACE symbols from non-LTO C++ objects.
- CMake text writes on Windows are CRLF; the startup-sequence is written via
  Python to guarantee LF.
- Release links with `--wrap=KPrintF` (stock WinUAE lacks Bartman's debug trap).

## Compiler cache (opt-in): ccache

`-DMS_CCACHE=ON` routes every C/C++ compile of the m68k cross build (and of the `MS_PLATFORM=SDL` PC build) through `ccache` (or
`sccache`) via `CMAKE_<LANG>_COMPILER_LAUNCHER` (`cmake/ccache.cmake`). It needs the program on `PATH` (or `-DMS_CCACHE_PROGRAM=<path>`);
with `ON` and none found CMake warns and builds as before. **ccache is not installed on the owner's machine** (checked 2026-10-11:
nothing named ccache/sccache on `PATH`, in the toolchain or in msys64): install it once (for example `choco install ccache`, or a release
zip on `PATH`) and configure with the flag:

```sh
cmake -S $P -B $P/build-game-debug -G Ninja $TC -DCMAKE_BUILD_TYPE=Debug -DMS_CCACHE=ON
```

Measured without it: a clean `build-autoplay` (Debug, 300 objects) configures in 15 s and builds in 12-19 s on this machine, so the gain is
modest; the host test drivers have their own cache that needs no installation (`tools/hostcache.py`, below).

## Fast loop for agents

Where the time went (2026-10-11): `check.py --quick` ran every test module one after the other (about 55 min of work in 134 modules), and the
*mutation* tests (a deliberate bug in a copy of an engine file, a host driver recompiled with clang, the comparison must fail) were
~1100 s of it. The loop now:

| when | command | what |
|---|---|---|
| after every change | `py tools/check.py --changed` | only the test modules the change since the merge-base touches (it prints which and why: a module that mentions the changed file's name, or the name of a file that includes the changed header) plus the cheap structural tests (`test_layers`, `test_raii`, `test_hostcache`), in parallel; `--dry-run` only lists them |
| before you report / merge | `py tools/check.py --quick` | every test module in parallel (jobs = cores/2, `--jobs N`), resource verify, routines, status, survey; no replay, no builds, **no mutation tests** |
| nightly, or after changing a checker | `py tools/check.py --mutations` (or `MS_MUTATIONS=1`) | also the mutation tests (slow: each recompiles a driver) |
| end of a batch, integrator only | `py tools/integrate.py --boot ...` | one emulator batch; task agents run no emulator (docs/AGENT_BRIEF.md) |

* One or a few modules: `py tools/testrun.py test_copper test_paula [--mutations] [--top 15]` (parallel, timed, the slowest tests listed,
  logs in `build/testrun/<module>.log`, last durations in `build/testtimes.json` schedule the longest first). `py -m unittest tests.test_x`
  still works and honours the same mutation switch.
* **Mutation switch** (`tests/mutation_gate.py`, installed by `tests/__init__.py` and `tools/testrun.py`): a test class or method whose name
  contains "mutat", or one decorated `@mutation_gate.mutation_test`, is *skipped* ("mutation test: set MS_MUTATIONS=1") unless
  `MS_MUTATIONS=1`; a skipped class does not run its `setUpClass` either. Name a new mutation test accordingly and it is gated with no further
  work (`test_layers` and `test_raii` are exempt: their mutations are in-memory text rewrites, ~1 s). A test named explicitly always runs.
  `unittest discover -s tests` bypasses the gate: use testrun/check.
* **Host compile cache** (`tools/hostcache.py`, `build/hostcache/v<N>/`): a `clang`/`clang++` call of a test is answered from the cache when
  the compiler, the flags, the sources and every header they include are unchanged. The key holds the **resolved compiler path, the full
  `clang++ --version` text and `-dumpmachine`** (asked once per process), the flags (include directories excluded: their effect is in the
  text), the **preprocessed text** of each source (so the content of every header) and the bytes of linked objects. Any change of compiler
  (the host clang went 11 -> 23.1.3 on 2026-10-11) is a miss. `CACHE_FORMAT` (the `v<N>` directory name, now 2 = the first format with a
  compiler in the key) is bumped to drop every older entry; `prune()` deletes other formats and trims to `MS_HOSTCACHE_MAX_MB` (3000),
  least recently used first. `tests/test_hostcache.py` pins the key: same inputs, same key; another `--version` text, compiler path,
  target, flag or header content, another key. `MS_HOSTCACHE=0` / `--no-cache` bypass it; `py tools/hostcache.py` shows size, `--clear`
  empties it. testrun/check/integrate set it up (`PYTHONPATH=tools/hostcache_site`, so Python children of a test are covered too); a
  command line it does not understand (`-E`, `-g`, response files, no `-o`) simply runs uncached.
* **Emulator lock** (`tools/emulock.py`): one WinUAE job at a time on the machine. The file is `D:\Amiga\.emu.lock` (= `<main checkout>/.emu.lock`,
  found with `git rev-parse --git-common-dir`, so every worktree shares it). `uaeshot.ps1`, `integrate.py --boot` (once for the whole batch)
  and `netpair.py` (once for both windows) take it; a second job waits and prints who holds it ("waiting for integrate.py --boot (pid N,
  since ..., in <dir>)"). It is a Windows byte-range lock on a byte past the end of a one-line JSON note, so the OS releases it when the holder
  dies (a note left by a dead holder is stale and is overwritten by the next taker, `status` calls it stale). `py tools/emulock.py status`;
  `py tools/emulock.py run -- <cmd>` holds it around any command; `MS_EMU_LOCK=off` / `uaeshot.ps1 -NoLock` skip it; `MS_EMU_LOCK_TIMEOUT=<s>`
  gives up instead of waiting for ever (exit 3). Nested jobs do not deadlock (`MS_EMU_LOCK_HELD=<pid>`). Jobs started before this existed
  do not take it: ask the owner before a batch if something else is running.
* **Boot runs at warp speed** (`uaeshot.ps1 -Speed real|max|warp`, env `MS_EMU_SPEED`, `integrate.py --boot-speed`): `integrate.py --boot` runs at
  WinUAE's `warp=true` by default (`DEFAULT_BOOT_SPEED`): `tests/boot/play_options.txt` takes 40-45 s instead of 126 s, with all 8 shots
  PASS and no SHOT-LATE (autoplay frames are VBL counts, the guest freeze of a shot is 400 VBLs, so frame-based waits are unchanged). Warp can
  move an input relative to a redraw ("pulse MISSED" notes in one of three measured warp runs), so a script that fails at warp is run again at
  real speed and only that verdict counts; `perf_*` scripts and `--update-refs-rel` always run at real speed. `netpair.py` stays at real speed
  (`--speed` opts in): its join delay and net timeouts are wall-clock. `max` (= `cpu_speed=max` only) saved 12 s of 126 s and missed two pulses: not recommended.
* **Intro skip**: the play scripts (`tests/boot/src/mkplay.py` `@PRE`, `play_options.mk` and the other .mk files with a boot prologue) wait for the
  game's own event, `wait file mindscape max 900` (the intro opened its first picture, so the skip is armed), then tap Space 20 frames later
  (skipped at frame ~140 instead of 300). Prefer `wait file` / `wait log` / `wait var` / `wait input` to a fixed `frame N` offset wherever
  the game logs the event (docs/AUTOPLAY.md).

## Release packages (ROADMAP 10.6 / 11.12)

One command writes the player-facing archives into `build/release/` (git-ignored; nothing is uploaded or published by it):

```sh
cmake --build build                                        # the Amiga Release exe (toolchain PATH above)
cmake -S . -B build-sdl -G Ninja -DMS_PLATFORM=SDL -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/pc-i686-clang.cmake
cmake --build build-sdl                                    # PC build: moonstone.exe + SDL3.dll (docs/PC_PORT.md)
py tools/package.py [--version 0.2.0] [--amiga] [--pc] [--dry-run]
```

* `moonstone-ace-<ver>-amiga.zip`: the `Moonstone/` HD drawer (`moonstone`, `s/startup-sequence`, `disks/` for the player's ADFs, `mods/defaults`,
  `mods_examples`), `README.txt` (install, how the data is found, bsdsocket, net.ini, ports 6502 TCP+UDP, the lobby), a WinUAE config
  (`moonstone-ace-a1200.uae`: A1200, 68020, 2 MB chip + 8 MB fast, bsdsocket on) with `start-winuae.bat`, `docs/MODDING.md`, `SHA256SUMS.txt`.
  It is a **.zip**: no lha/lzx packer is installed on the build machine. No icon (`.info`) is shipped (none exists in the repo; the README says to give
  `moonstone` any Tool icon) and **no boot `.adf`**: the boot floppy is dropped (ROADMAP 10.6 owner note) and the game's arenas do not fit one (H4).
* `moonstone-ace-<ver>-windows-preview.zip`: `moonstone.exe`, `SDL3.dll` (+ licence), `README.txt`, `disks/` for the ADFs, `mods_examples`, `SHA256SUMS.txt`.
  It is named and marked **preview** while `PC_PREVIEW` in `tools/package.py` is True (the PC build reaches the title menu only, ROADMAP 11.1d):
  flip the constant (or pass `--pc-final`) when it plays a whole game.
* `<archive>.sha256` next to each archive, and `RELEASE_NOTES_<ver>_DRAFT.md` (`docs/RELEASE_NOTES_TEMPLATE.md` filled from the ROADMAP release plan; the edited
  draft of 0.2.0 is `docs/RELEASE_NOTES_0.2.0_DRAFT.md`). Texts and configs are in `docs/packaging/`.
* The version is `include/engine/version.hpp` (major.minor; patch free); `--version` must agree or the run fails. Archives are reproducible (sorted members, fixed time stamp).
* **No original data, checked:** `guard_entries` refuses any file whose type is not on the allow-list (`.adf .cel .piv .ob ...` never pass), that is named like an
  original file, whose SHA-256/SHA-1 is an original (the three ADFs, `tools/facts`, every file of `build/disks`) or that looks like a floppy image, and
  `tools/leakscan.py` scans every member against `build/disks` (executables may hold the small constant tables of source, runs of <= 128 bytes, 8 KB in all; the
  vendor `SDL3.dll`, byte-identical to the kit's, is not scanned). Without `build/disks` the names/hash guard still runs and the scan is reported as not run.
  `--dry-run` lists every member with size and hash. Test: `py tools/testrun.py test_package`.
