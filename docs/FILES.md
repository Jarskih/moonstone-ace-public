# File layer: the seam and its DOS replacement (ROADMAP 2.7 + 2.9)

Citations are `file:line` in `../moonshard/moonstone-main/amiga_asm/` (IRA listings, never edited).

## 1. What the original does

Both overlays carry the same three-layer stack, linked in two copies:

| Layer | program | mog | What it is |
|---|---|---|---|
| trackdisk driver | S_13 (program.asm:5292-6161) | S_18 (mog.asm:19467-20339) | raw MFM: CIAB_PRB motor/step/side, DSKPTH/DSKLEN/DSKSYNC `$4489`, one track = 11 sectors |
| OFS reader ("file cache") | S_18 (6660-7327) | S_23 (20836-21507) | hand-written AmigaDOS OFS reader on top of the driver: root block 880 (`$370`), name hash (x13, mask `$7FF`, `DIVU #$48` = 72 buckets, case-folded), header chain, data-block chain (24 byte header, `$1E8` = 488 data bytes/block). One open file at a time; its state cells live in the operands of `ORI.B #0,D0` filler words (IRA mis-decode of DATA), e.g. `LAB_0386+2` = name pointer, `LAB_037E+2` = file position, `LAB_037F+2` = file size |
| callers | 25+ `JSR LAB_0390`/`LAB_03B2` sites | 17 + 37 sites | asset loaders (`LAB_0054` "load asset by name", `LAB_0174` table loader, `LAB_0051` "message" at program.asm:1019-1033) |

The OFS reader's API (register based, JSR/RTS):

| Op | program | mog | In | Out / effect |
|---|---|---|---|---|
| init | `LAB_038F` (6692, a bare `RTS`; `JSR` at 113) | `LAB_0BB3` (`RTS`) | - | nothing (the code behind the RTS is an unreferenced entry) |
| open | `LAB_0390` (6707) | `LAB_0BB5` (20884) | A0 = NUL-terminated name | error word `LAB_0382+2` / `LAB_0BA6+2` = 0 ok, -1 not found; selects the file, resets position |
| read | `LAB_03B2` (6954) | `LAB_0BD7` (21131) | D0.L = byte count, A0 = destination | copies the next bytes (clamped to the file size) and advances |
| skip | `LAB_03C5` (7075) | `LAB_0BEA` (21252) | D0.L = byte count | advances without copying (same clamp) |
| close | `LAB_03DA` (7252) | `LAB_0BFF` (21429) | - | flush/cleanup, motor timer re-arm |

Never called (dead): the create/write entries (`LAB_03C9`..., and the unlabelled one behind `LAB_03AA`'s RTS, program.asm:6954-6957 / mog 21061): no `JSR`
targets them, the game has no save-to-disk path.

Disk handling above that API, mog only:

* `LAB_0BB4` (mog.asm:20870, 7 callers at 2238-2324): "drive init" = spin up and read the root block through the MFM driver.
* `LAB_0B18` (20083, callers 2173/2190/2220 inside `LAB_00F8`): probes drive units df1-df3 through the drive-ID shift register.
* `LAB_0100` (2233): D0 = wanted disk id (1/2/3 = A/B/C). Maps it to a unit through `LAB_06FE..0701`; if no unit holds it, `LAB_0110`/`LAB_0111`/`LAB_0112`
  draw an "insert disk" text and wait for fire (`LAB_00EC`), then `LAB_010C`/`LAB_010D`/`LAB_010E` open a marker file
  (`bg2.piv`, `He1.ob`, `be1.c`) and retry while the error word is -1. That is the only disk-change prompt. The ADF volume names are all
  spaces, so the game identifies a disk by those marker files, never by volume name.
* program has no such logic (it only ever needs disk A).

The VBL handler's motor-off countdown (`SECSTRT_15` program.asm:6178 / `SECSTRT_20` mog.asm:20352) reads `LAB_038C+2`/`LAB_038D` (`LAB_0BB0+2`/`LAB_0BB1`).
With the reader replaced they are never armed, so it never reaches the motor-off routine (hardware CIAB writes).

## 2. The patches

`asm/patches/program.files.json`, `asm/patches/mog.files.json` (merged by `tools/resource.py`; `--verify` passes):

| id | where | new code |
|---|---|---|
| `files-init` (program) | `LAB_038F` | `JMP rt_prg_file_init` (opens the log) |
| `files-open` / `-read` / `-skip` / `-close` | first instruction(s) of the four entries, both binaries (program has no `-skip`: its only skip caller was the dead S_4 loader, ROADMAP 4.7) | `JMP rt_<prg or mog>_file_<op>`; the rest of each original routine stays as dead code (layout and relocs preserved) |
| `files-drive-init` (mog) | `LAB_0BB4` | `RTS` |
| `files-drive-detect` (mog) | `LAB_0B18` | `MOVEQ #-1,D0 ; RTS` = no extra drive (`LAB_00F8` then keeps unit 0 / disk 1 only) |
| `files-disk-prompt` (mog) | `LAB_0100` | `RTS`: no prompt, no marker probes; all files are in one tree |

The eight `rt_*_file_*` symbols are `entry_funcs` in `abs_symbols.json` (declared in `include/rt/abs.h`, never stubbed). `src/rt/files.cpp` defines them as
register-preserving asm shims (only D0 changes, in open, which also stores the 0/-1 result into the original's error word) around four
`extern "C"` functions (`src/rt/files.hpp`): `rt_file_open/read/skip/close` (+ `rt_file_init`).

## 3. Implementation (`src/rt/files.cpp`)

* dos.library `Open/Read/Seek/Close` directly (ACE's `diskFileOpen` is `fopen` plus heap allocations per file; the game wants one big sequential
  stream). One open file, one static 32 KB read buffer, no heap.
* DOS needs the OS alive. `OsGuard` brackets every DOS call with ACE `systemUse()` + `systemReleaseBlitterToOs()` ... `systemGetBlitterFromOs()` + `systemUnuse()`,
  exactly what ACE's own file code does. A refill happens once per 32 KB, open/close once per file.
  **Caveat for M2:** `systemUnuse()` restores *ACE's* hardware state (vectors, INTENA, DMA), not what the game set up meanwhile. This is fine while the game's
  VBL/display run through ACE (`rt/irq`, `rt/display`); once `rt/display` hosts the game copper list the guard has to re-arm it.
* Absent floppies would raise "Please insert volume" requesters; `pr_WindowPtr = -1` during `Open` suppresses them.
* Log: `PROGDIR:files.log` (first 400 lines: `open <path> <size>` / `open FAIL <name>`). Stock WinUAE has no Bartman `KPrintF` trap and `ACE_DEBUG` is off.

## 4. Search path and data collisions

**Override first (ROADMAP 5.2a):** before the list below, `rt_file_open` tries `PROGDIR:art/<name>`. PROGDIR is the install directory (the exe's), so on the staged HD
that is `hd/art/`. Only plain names are tried (no `/`, no `:`, at most 30 characters; a name that is not plain skips the override); DOS folds case. If the file exists
its head (up to 32 KB) is read and classified (`ms::artClassify`, `src/engine/artcheck.cpp`): a 6-plane picture (BE16 plane count 6 at offset 0) or cel (any frame
with plane bit `$20`) is closed again and the normal search runs, with a log line. Otherwise it is served exactly like a disk file and `art override <name> <size>`
is logged once per file name. The lookup runs inside the same `OsGuard` and requester suppression as the rest of `rt_file_open`; with no `art/` directory
`Open` fails silently and nothing else changes. Details and how to use it: `docs/ART.md` "Trying your art in the game".

Then, for the original files:

`PROGDIR:data/<name>`, `PROGDIR:<name>` (files next to the exe), then `DF1:` `DF2:` `DF0:` `DF3:` (the original disks are plain OFS with the files in the root; floppies
are addressed by device name because their volume names are blanks). DOS is case-insensitive, like the original's case-folding hash.

Disks A/B/C hold 34/88/91 files. B and C share 60 byte-identical files, so the merged `data/` has 150 files. The only real clash is `kn1.ob`: disk A has a 5-byte stub, disk B
has `KN1.ob` (20,760 bytes). `tools/hdstage.py` keeps the larger. Skipped: the original binaries `nb`, `program`, `mog` and A's `s/` dir.

## 5. HD install (`hdinstall`)

```sh
py tools/adfx.py build/disks "../moonshard/Moonstone (Mindscape) A.adf" "../moonshard/Moonstone (Mindscape) B.adf" "../moonshard/Moonstone (Mindscape) C.adf"
cmake --build build-game-debug --target hdinstall    # -> build-game-debug/hd/{moonstone, s/startup-sequence, data/*}
"/c/Program Files/WinUAE/winuae64.exe" -config="D:\Amiga\moonstone-ace\moonstone-ace-hd.uae"
```

`moonstone-ace-hd.uae`: A1200/AGA/KS3.1, `chipmem_size=4` (WinUAE counts 512 KB units: **`chipmem_size=2` in `moonstone-ace.uae` is only 1 MB**, and the game's two
arenas (about 740 KB) plus the exe then fail to allocate), DH0 = the staged directory, boot priority 0, LF-only `s/startup-sequence`. Point the path at `build-game/hd` for Release.
`hd/` and `build/disks/` are git-ignored; no game data is committed.

Optional: `hdinstall` can copy `build/art/import` (5-plane `tools/artconv.py` output, per-disk subdirectories `A/ B/ C/` flattened into one `art/`) into `hd/art/` when it exists;
the CMake diff is in the 5.2a result note. `hdstage.py` removes the whole output directory first, so anything hand-placed in `hd/art/` is lost on the next `hdinstall`.

## 6. State

Debug game build in WinUAE: `rtGameRun` -> `prg_SECSTRT_0` -> file init, then the game **hangs inside `SECSTRT_29`** (custom-chip/video/IRQ init, program.asm:9897)
before it asks for a file; that is display/irq work, not the file layer. With `SECSTRT_29` bypassed in a scratch build the file layer works:
`PROGDIR:data/bold.f` (12133 bytes), `message.piv` (3694), `mindscape` (16473) are opened and read by the original loaders.

## 7. Asset loaders in C++ (ROADMAP 7.1f1)

The loaders that sit on top of the file API are C++ now: `src/engine/loaders.cpp` (pure: header parse, palette words, in-place
compaction, LZSS, cel header / table, collide.hit text) and `src/rt/loaders.cpp` (open / read / close through `rt_file_*`, the asm cells,
the register shims). Patch tables `asm/patches/{program,mog}.loaders.json` jump from the first instruction of each original routine:

| routine | program | mog | in -> out |
|---|---|---|---|
| picture from memory | `LAB_03FC` | `LAB_0C21` | A0 = image -> D0 = decoded bytes |
| picture from a file | `LAB_0402` | `LAB_0C27` | A0 = name, A1 = buffer -> D0 |
| cel / ob load | `LAB_0496` | `LAB_0CBB` | A0 = name, A1 = destination -> D0 = total bytes |
| cel size | `LAB_0491` | `LAB_0CB6` | A0 = name -> D0 |
| LZSS blob by name | - | `LAB_0CC0` | A0 = name, A1 = dest, A2 = scratch -> D0 |
| collide.hit read / lookup | - | `LAB_03DA` / `LAB_03CE` | A0 = name, A1 = cel destination -> D0 (-1 = no hit set, else the cel load's result) |

Every shim keeps D1-D7/A0-A6. The picture loaders take the depth from `rt_enh_planes`: a 6-plane header has 64 palette words, the colours go to
the 24-bit registry (`rt::enhPictureLoaded`) and plane 5 of a picture without one is zeroed (the old `rt_*_pal_mem/pal_file/pic_done` shims are gone).
`LAB_03DA` stores the size of collide.hit (`rt_file_size`) in `SECSTRT_10`, the bound of the lookup: the original reader's size cell was never written by the DOS
layer, so before 7.1f1 the bound was 0 and no hit set was found. `tests/test_loaders.py` runs every file of `build/disks` through the C++ loaders against a Python
model of the asm.

## 8. Original data at start-up and ADF images (ROADMAP 10.2a/b)

- The game's data (the DATA hunks and code-hunk cells of program / mog, the synth tables) is no longer compiled in: `src/rt/origload.cpp`
  opens `program` and `mog` through `rt_file_open` before the first overlay entry, checks size + CRC-32 and fills the objects
  (`docs/PUBLISHING.md`). `tools/hdstage.py` therefore copies them into `data/` too (only the bootstrap `nb` and `s/` are skipped).
- After the search places of section 4, `rt_file_open` tries the ADF images (`src/rt/adfdisks.cpp`): every 901,120-byte file of
  `PROGDIR:` and `PROGDIR:disks/`, identified by the marker files (A: program + mog, B: He1.ob, C: be1.c), whatever its name. A
  name held by several images resolves to the largest file (`kn1.ob`). The images stay open until `rt_file_shutdown()` (end of
  `rtGameRun`). The log line of such an open reads `open ADF:<name> <size>`.
