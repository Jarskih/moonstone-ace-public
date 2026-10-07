# Publishing without original material (ROADMAP M10: 10.1 audit, 10.2 start-up data, 10.3 leak scan)

Owner goal: the project goes on GitHub with no original art, data or code of Moonstone (Mindscape, 1991). Everything original
comes from the player's own three disks. Since ROADMAP 10.2 **neither the repository, nor the build, nor the released
executable contains original bytes**: the game reads them from the disks when it starts.

## 1. How it works now

**Players** (no PC step, no cross-compiler): copy the game (`moonstone`) and the three Moonstone ADF images into one drawer (or
the images into its `disks/` drawer) and start it. The images may have any names: the game identifies each disk by the files
the original itself probed to tell them apart (A: `program` + `mog`, B: `He1.ob`, C: `be1.c`, docs/FILES.md). Also supported,
as before: the disk files installed into `data/` next to the game (`hdinstall`), or the original floppies in DF0:-DF3:. If a
disk is missing, one message names every missing disk (`rt::fatal`, e.g. "Disk C not found.") and the game does not start.

**At start-up** (`src/rt/origload.cpp`, before the first overlay entry): `program` and `mog` are read once through the normal
file layer (search order `PROGDIR:data/`, `PROGDIR:`, `DF1: DF2: DF0: DF3:`, then the ADF images), parsed as hunk files
(`src/engine/origfill.cpp`, pure, host-tested), checked against the known version (size + CRC-32 from `tools/facts/<bin>.json`)
and the generated run table copies their DATA hunks, the data cells of their code hunks and the synth tables into the C++
objects. Pointer cells are not copied (they are link-time constants of our build) and patched cells keep our values (fight
script tags). A wrong version is reported as such ("a different version (checksum)").

**ADF images** (`src/engine/ofs.cpp` pure OFS/FFS root-directory reader, `src/rt/adfdisks.cpp` DOS glue): at the first file open
every 901,120-byte file of `PROGDIR:` and `PROGDIR:disks/` is mounted and identified; opens fall back to the images after the
other search places. `kn1.ob` (a 5-byte stub on disk A, the real file on B) resolves to the largest file, as `hdstage.py` does.

**Developers**: `py tools/setup.py A.adf B.adf C.adf [--hd DIR] [--debug] [--no-build]` (any order, any names): copies the
images to `build/adf/{A,B,C}.adf`, extracts them into `build/disks` (adfx), checks the executables against `tools/facts`, puts the
original executables + the facts' label table into `build/reasm/` when there is no reassembly (the unicorn oracles of the tests
run the original code from there), finds the toolchain (MS_TOOLCHAIN, `tools/toolchain`, the D:/Amiga workspace or the
amiga-debug VS Code extension), configures and builds Release (+ Debug with `--debug`) and runs `hdinstall`. The build itself
needs no original file; setup.py is a convenience, the disks are only needed to run the game and the comparison tests.

## 2. Audit (10.1): tracked files of moonstone-ace

Classified by `tools/publish.txt` (the machine-readable form of this table; `tools/publish_tree.py OUT` copies the kept files,
`tools/leakscan.py` scans them). State of this branch: 839 tracked files: **474 keep, 364 drop, 1 gen**.

| Files | Count | Derived from the originals? | Decision |
|---|---|---|---|
| `asm/program.s`, `asm/mog.s`, `asm/synth.s` | 3 | the reassembled IRA listing (original code as text) | **drop**: reference of `resource.py --verify` and the optional `MS_SYNTH_ASM` A/B build; both need the listing anyway |
| `asm/program.hw.txt`, `asm/mog.hw.txt` | 2 | hardware-register usage counts of the listing | **drop** |
| `asm/patches/*.json` except `*.data*.json` | 45 | patch tables against listing lines, 155 `orig` instruction lines quoted | **drop** (their data effect is in `tools/facts`: the `patched` cells) |
| `asm/patches/<bin>.data*.json` | 10 | none: which hunks/cells C++ owns, object names, types | **keep** (gen_data input) |
| `src/lifted/**` | 312 | routines lifted literally from the listing (68k semantics in C++) | **drop** (test oracle of the lift pipeline only) |
| `tests/test_lift.py`, `tests/test_diffharness.py` | 2 | tests of that oracle | **drop** (owned by the lifting session; they could skip via `tests/origskip.py` instead) |
| `src/engine/synth_data.cpp` | 1 | **37 KB of original synth tables** (S_44/S_45 bytes) | **gen**: never compiled since 10.2a; `gen_synth_tables.py` writes it to `build/` for MS_DATA_COMPILED and the tests |
| `tools/facts/{program,mog,nb}.json` (new) | 3 | facts only: hunk kinds/sizes, label -> hunk+offset, data cell layout (width/count/kind, no values), pointer targets by name, the zero-byte ends of texts, our patch values, CRC/size of the known version | **keep** |
| `include/ms/gen/*_syms.hpp`, `include/ms_linked/**`, `src/rt/gen/hunk_tab.cpp` | 7 | label addresses (facts), hunk section names | **keep** |
| `tools/symbols.yaml`, `tools/cell_names.yaml`, `tools/tables.yaml`, `tools/data_types.yaml` | 4 | our names, notes and layouts keyed by label | **keep**; game messages quoted in comments were reworded in this task (183 matching windows in cell_names.yaml, 1 in tables.yaml) |
| `docs/**` | 23 | explanations that cite labels and listing line numbers, a few quoted instructions | **keep** |
| `src/{engine,game,rt}/**`, `include/**` | 175 | our C++; transcribed small constant tables (see 3.) and instruction citations in comments (see 4.) | **keep** |
| `tests/**` (rest) | 148 | Python models of the asm, unicorn oracles that run the original code from `build/reasm` at test time | **keep** (no original bytes; see 3. for the constant tables) |
| `tools/**` (rest) | 60 | our tools | **keep** |
| `*.uae`, `uaeshot.ps1`, `CMakeLists.txt`, `README.md`, `BUILD.md`, `ROADMAP.md`, `.gitignore` | 7 | none (the .uae files hold absolute D:/Amiga paths: to be made relative or documented in 10.4) | **keep** |

Outside moonstone-ace (not exported at all: the public repository is moonstone-ace + the ace submodule): `moonshard/`
(amiga_asm `nb/program/mog` executables + IRA `.asm`, `res/*.bin`, `textures/`), the disk images.

### Build and test inputs that came from the originals

| Input | Before | Now |
|---|---|---|
| IRA listing `reference/moonshard/.../{program,mog}.asm` | gen_data parsed it for every build (CMake DEPENDS) | not read by the build; `tools/facts` (written by `py tools/origfacts.py extract`, checked by `tests/test_origload.py` while the listing exists) |
| original executables `amiga_asm/{program,mog}` | gen_data initialised every owned object with their bytes | read by the **game** at start-up; the build reads them only for `-DMS_DATA_COMPILED=ON` (the comparison build) |
| `build/reasm/<bin>.symbols.json` | label table for the code-hunk cells | `tools/facts` labels |
| `src/engine/synth_data.cpp` | compiled in | filled at start-up (`origload.cpp`, `ms::synthDecodeInstruments`) |
| `build/disks` | `hdinstall` (data/ for the HD install) | the same (now including `program`, `mog`); optional for players who use ADFs |
| `asm/synth.s` | scanned by gen_data for alias names in every build | only with `--asm-labels` (MS_SYNTH_ASM) |

## 3. Leak scan (10.3)

`py tools/leakscan.py` (tests/test_leakscan.py, step `[leakscan]` of `tools/integrate.py`, ~4 s) compares every published file
(tracked + new, minus the drop/gen entries) with every file of the extracted disks: byte runs of >= 24 bytes (indexed every 8
bytes, windows with < 8 distinct values ignored), integer lists in sources decoded as bytes / BE words / BE longs, and the
letters+digits of texts (case and spacing ignored) against the printable texts of the originals. It prints positions, never the
matched bytes. `--tree DIR` scans an exported tree, `--all` includes the dropped files (the audit view), `-v` shows the notes.

Result on this branch: **0 files with original content**. Notes (integer lists of at most 128 bytes that match, accepted as the
constant tables a decompilation transcribes; listed for the owner's decision, 10.5):

| File | Largest match | What |
|---|---|---|
| `src/rt/combat_ui.cpp` | 122 B | `kList04F3`/`kList04F5`: sprite/x/y draw lists of the stat panels (LAB_04F3..04F5) |
| `src/game/fighters.cpp` | 38 B (3 lists) | `kActionLeft/Right` (LAB_07D9/07DA), `kSoundLists` (LAB_02EE/02EF), walk steps |
| `src/game/fight_creatures.cpp` | 30 B | `kStalkStep` (LAB_0ECE) |
| `tests/test_fighters.py` | 65 B | `DATA`: the same rule tables, checked against the image when it is there |
| `include/ms/gen/{mog,program}_syms.hpp` | 94 B | hunk base addresses that happen to equal a table of the original (no content) |

The executables: the Release exe of this branch has no run longer than 123 bytes in common with the originals (the tables above,
the standard Paula period table of ptplayer, file names such as `bg5.piv`, short hardware-register code idioms); the exe of the
main branch before 10.2 had 263 runs up to 4 KB.

## 4. Open questions for the owner (10.5)

- **Instruction citations in comments**: about 2,250 comment lines in 221 published files quote the original instruction they
  transcribe (`// CMP.L D0,D2 ; BMI.S LAB_03CC`), the project's documentation style (AGENTS.md "cite the ASM label"). They are
  short and interleaved with our code; removing them would be a mechanical pass (`LAB_xxxx` citations alone carry no code).
- **Transcribed constant tables** (section 3): could also move into the start-up fill (a generated run per table) if wanted.
- **Label names** `LAB_xxxx` / `SECSTRT_n` are IRA's numbering; they are the project's only stable handle and are facts.
- The `.uae` configs and some docs hold absolute `D:\Amiga` paths (10.4).

## 5. What still needs the IRA listing (developer checkout only) and the plan

Needed only by the reference/lifting tools, never by the build or by the game: `tools/resource.py` (`--verify`, asm/*.s),
`reassemble.py`, `routines.py`, `callgraph.py`, `lift*.py` + `diffharness`, `asm_remaining.py`, `absscan.py`, `gen_tables.py`,
`fightscript.py` (the asm-reading part), `ghidra/prep.py`, `origfacts.py extract`, `check.py` steps 2-5 (they print "skipped: needs
the IRA listing" without it). Tests: the modules that model the asm from the listing skip as a whole or per test
(`tests/origskip.py`: `require_listing`, `require_asm_ref`, `need_file`); tests that run the ORIGINAL code in unicorn take it from
`build/reasm` (the reassembly, or the original executables that `setup.py` puts there) and run in a public checkout after
setup.py. Plan: these tools stay as they are for the developer checkout (the listing is regenerated by IRA from the disk if ever
needed; IRA is a free disassembler and its config would be our facts), and as the lifted oracle is retired (M3 history) the
listing-based tests can be dropped one by one. Nothing in the public flow depends on them.

Test evidence of the public layout (`tools/publish_tree.py` into a scratch directory, no reference/, no asm/, no src/lifted):
without the disks `check.py --quick` is GREEN (unit tests skip what needs originals); after `setup.py` the unit tests run with the disks: 551 tests, 0 failures (246 skipped: they need the listing or the asm reference). The unicorn oracles find ACE at ./ace (the submodule) as well.

## 6. Proof that the start-up data equals the compiled-in data (10.2a)

- `tests/test_origload.py`: (1) the facts describe the executables (size, CRC-32, hunk table); (2) `gen_data --full` from the
  facts generates exactly the text the IRA listing generated (header and objects); (3) for every owned hunk object and every
  code-hunk cell, blank initialiser + the fill runs applied to the executable's bytes == the full initialiser, byte for byte
  (pointer and patched cells identical in both); (4) `ms::origFill` in clang++: synthetic hunk files (CODE/DATA/BSS, CHIP flag,
  RELOC32, SYMBOL), wrong CRC, a run outside its hunk, a truncated and a non-hunk file, and the real program/mog: every hunk
  byte; (5) `ms::synthDecodeInstruments` == the instrument table of gen_synth_tables.py. `tests/test_adf.py`: `ms::adfRead`
  returns every file of the three ADFs exactly as adfx extracts it (two chunk sizes), case-insensitive names, and a synthetic FFS
  image with an extension block.
- In the game: `-DMS_DATA_DUMP=ON` writes every owned object + the synth tables to `PROGDIR:datadump.bin` after the start-up; a
  default build (filled from the disk) and a `-DMS_DATA_COMPILED=ON` build (compiled in, the pre-10.2 form) booted in WinUAE and
  `py tools/datadump.py filled.bin compiled.bin` compares them (pointer cells masked: their values are each build's addresses).
  Result 2026-10-07 (Debug autoplay, `tests/boot/regression.txt`, WinUAE instance 85): **EQUAL**: 93 objects, 131,392 bytes
  (2,808 pointer bytes masked) + 7,724 bytes of synth tables. A one-bit mutation of the filled dump is reported (`g_prgStrings`).
- Boot regression (`integrate.py --boot`, Debug + Release autoplay): all screens PASS with the data loaded at start-up.
- ADF-only install (no `data/`: `disk1.adf` in the drawer, `disks/Moonstone_B.adf`, `disks/zz.adf`): identified as A, C, B, every
  file read from the images (`open ADF:program 60472` ...), the regression script runs to its end with the reference screens
  (0.0-1.3 %, the same as the HD install). With disk C left out the game stops at once with "Moonstone needs its original disks
  (Mindscape 1991). Disk C not found." plus the hint where to put the images.

## 7. Building from a public checkout (proof)

`tools/publish_tree.py` exported the kept files into a scratch directory (`ace` linked in, no `reference/`, no `asm/`, no
`src/lifted`, no `build/`). There: `py tools/check.py --quick` GREEN without the disks (the listing steps print "skipped");
`py tools/setup.py B.adf C.adf A.adf --debug --toolchain ... --cmake-toolchains ...` identified the disks, extracted them, built
Release + Debug and staged `build/hd`. Compared with the same commit built in the developer checkout:

- Release exe: same size (484,232 B), CODE/DATA/BSS hunks and relocations identical; 24 bytes differ, all inside HUNK_SYMBOL
  (the hash suffix of LTO-private symbol names, derived from the source path).
- Debug exe: same size; besides symbol names one byte differs: the alignment padding after ACE's 38-byte `text` section
  (system.c's movec stubs), which elf2hunk leaves uninitialised: relinking the SAME tree twice gives different values there.
- Note for 10.4: without `tools/bartman_gcc_support` ACE's CPM fetches bartman_gcc_support from the network, and today that is
  a newer revision than the vendored one (its gcc8_a_support.s / gcc8_c_support.c differ); the public repository should pin it
  (submodule or CPM tag).

## 8. Text for the README (players), for the integrator

> **You need the original game.** Moonstone - A Hard Days Knight (Mindscape, 1991), the three Amiga floppies, as ADF images
> (`Moonstone (Mindscape) A.adf` etc., any names) or as real disks. This repository holds none of its data, art or code; the game
> reads them from your disks every time it starts.
>
> **Play on an Amiga or in WinUAE** (A1200, 68020, Kickstart 3.x; 2 MB chip RAM, fast RAM recommended): copy `moonstone` and the
> three ADF files into one drawer (or the ADFs into a `disks` drawer next to it) and double-click / run `moonstone`. Real floppies
> work too (DF0:-DF3:). If something is missing the game says which disk.
>
> **Build it yourself**: install the Amiga C/C++ toolchain (VS Code extension "Amiga C/C++ Compile, Debug & Profile" by
> BartmanAbyss), CMake and Ninja, clone with `--recursive` (ACE submodule) and AmigaCMakeCrossToolchains into `tools/`, then
> `py tools/setup.py A.adf B.adf C.adf` builds and installs into `build/hd` (the build itself needs no original file).
