# Display handover (ROADMAP 2.4a / 2.4b)

Citations are `file:line` in `../moonshard/moonstone-main/amiga_asm/`.

## 1. Root cause of the C1 stall

`SECSTRT_29` (program.asm:9897) is not waiting on hardware. It overwrote memory it did not own.
The screen-pointer pair at the start of S_30 (program.asm:10437-10440, mog.asm:24615-24618 in S_35) holds two
**absolute chip addresses of the A500 memory map**:

| cell | original value | use |
|---|---|---|
| `SECSTRT_30` | `$75A3C` | screen B: bitplane base for the copper list (9926-9946), `LAB_054D` clear (9952) |
| `LAB_056C` | `$6BDFA` | screen A / draw screen (9950) |

Each screen is 5 planes x `$1F40` bytes = `$9C40`. `LAB_054D` (10242 ff.) clears 40000 bytes at each. Under ACE that is
`$6BDFA` and `$75A3C` in the middle of the loaded exe/ACE heap: the game zeroed its host and the machine hung or died
in the next instruction stream. The clear loop at 9899-9902 and the copper list had already been relocated (patches
`dmacon-cleared`, `copper-ptr`), the two DATA cells had not. Skipping the IRQ install did not help, and a build that skipped
`SECSTRT_29` never reached the clears, which matches the observation.

`LAB_0552`'s beam wait (`CMPI.B #$f5,VHPOSR`, 10310) and the other hardware writes were fine under ACE.

## 2. Fix

* `asm/patches/abs_symbols.json`: the `rt_screen_work` block now starts at `$6BDFA` (screen A) instead of `$6BEFA`; the old
  base is kept as the alias `rt_screen_clear` (what `LEA EXT_000f,A0` at 9899 loads, so the zero-fill still starts there), and
  `rt_screen_b` (= `$75A3C`) is a new alias. Still one contiguous chip block (size 82438 = `$14206`), so 2.4d holds.
* `asm/patches/program.display.json` / `mog.display.json`: patches `screen-a` / `screen-b` replace the two DATA longwords by
  `rt_screen_b` / `rt_screen_work` (relocated by the linker). In the `--verify` build the symbols equal the original values, so
  `resource.py --verify` still proves the rest of the image identical.
* mog already had `cmpa-clear` and `copper-ptr` for its `SECSTRT_34`.
* `tests/test_resource.py`: the clear loop now loads `rt_screen_clear`.

Result: `build/shots/C1.png` shows the Mindscape logo (`mindscape`, drawn by the original code on the original copper list,
320x200x5, palette via the copper palette words). Log: `init program 0`, then `bold.f`, `message.piv`, `mindscape`, `bg1a.piv`.

## 3. Handover (src/rt/display.cpp, `displayHandoverToGame`, called by `rtGameRun`)

ACE's view stays loaded until the game writes `COP1LCH` and reads `COPJMP1` (9964-9965); that alone replaced ACE's copper
list, so no `viewLoad(nullptr)` is needed. The AGA registers ACE leaves non-default are reset before the first overlay entry:
`FMODE = 0`, `BPLCON3 = 0`, `BPLCON4 = 0`, `BPLCON1/2 = 0` (2.4b). The logo colours are correct, so the copper palette words land in
bank 0.

## 4. Open

* After `bg1a.piv` was opened the screen went black and the machine took a Software Failure (`8000 0003`, address error; CPU
  0% afterwards). Not diagnosed (outside C1): suspects are the next intro step (IMAGEXCEL decode into a buffer, a bad return
  address from the IRQ trampoline stack, or another absolute address).
* Unpatched absolute memory in the asm: `EXT_000d/000e` (`$3820`, `$614A`) occur only in IRA mis-decoded data
  (program.asm:1567, 9340). Not accessed.

## 5. 2.4e: the crash after `bg1a.piv` (C1 -> C2, 2026-10-05)

**Result: the intro plays (build/shots/C2-1.png .. C2-3.png) and the game reaches "Select a Knight" (C2-4-knight-select.png) from HD, Debug game
build. No exception. Not verified: audio (nothing in the log or screenshots can show it) and any reaction to keyboard/joystick on the select screen.**

### 5.1 Tooling added

* `src/rt/crash.cpp` / `crash.hpp`: `rtGameRun` installs raw handlers on CPU vectors 2-7, 9-11 (not 8: exec's `Supervisor()` and ACE's
  `systemUse/Unuse` use the privilege-violation vector). A handler pops the 68020 frame, saves registers and the frame, RTEs into user mode
  on a private 4 KB stack and writes `PROGDIR:crash.log` (vector, SR, PC, fault address, D0-A7, code bytes at PC, 48 stack words below USP plus
  32 at SSP, the exe's segment list, optionally the trace ring and a memory diff), then parks. The ACE vectors are untouched.
* `tools/crashmap.py <crash.log>`: maps PC and stack words back to `.text` symbols or, for the asm hunks, to the original label and the
  `asm/*.s` line (segment list = `objdump -h` section order). Needs the toolchain on PATH.
* `crashTraceStart()` sets T1 (via `Supervisor`) and logs the last 1024 PCs; `crashMemSnap()/crashMemDiff()` report which 256-byte blocks of
  the first 2 MB changed in between. Both are dormant; they found this bug and are meant to be called from a temporary line in `files.cpp`.
* `crashArenaHighWater(i)`: logged as `arena-high-chip/fast` in `files.log` at every file open (last non-zero longword of each arena).
* `tools/absscan.py` (+ `tests/test_absscan.py`), see 5.3.

### 5.2 Crash and root cause

First crash (A1200 config, KS 3.1): `vec=03 PC=00000005 fmt=B fault address 00000005`, user mode. The jump to 5 is inside the ROM's dos.library
BCPL glue (ROM `$1FDE0`, `jsr 4(a3)` with `a3 = 1`) reached from `Close()`: `rt_prg_file_close -> OsGuard -> Close`. The T-bit ring showed dos
taking an allocation-failure/error path and calling a BCPL entry through a corrupted vector. The OS was already corrupted before.
`crashMemSnap/Diff` around `bg1a.piv` showed the only unexpected writes: 15.5 KB starting inside the last block of the fast arena, i.e.
beyond its end.

Root cause: **the overlays use the memory above their carve-out as scratch.** `LAB_0044` (program.asm:920-955) bumps the cells `LAB_00C2`
(chip) and `LAB_00C4` (fast) by the size of its fixed buffers and then keeps them as "free memory" pointers: `LAB_0185` (program.asm:3310)
loads `bg1a.piv` to `LAB_00C4` and `LAB_0402` decodes from there; `LAB_0496` loads and unpacks into `LAB_00C2`. The original loader passed
all free memory in A0/D0 and A1/D1. `rtGameRun` allocated exactly the largest bump (`0x5BF18` chip, `0x58116` fast), so every scratch load ran
past the arena end into OS heap (dos.library structures), and the next `Close()` crashed in dos. Nothing to do with PIV decoding or the
IRQ trampoline: the decoder and the file layer were correct.

### 5.3 Fix and measurements

* `src/rt/game.cpp`: arena = bump + `SCRATCH_CHIP` (`0x10000`) / `SCRATCH_FAST` (`0x28000`); the loader registers (A0/A1/D0/D1) now describe the
  whole arena. High-water mark measured so far (intro + knight select): chip +1.3 KB, fast +28 KB over the bump. These sizes are a guess with
  headroom, to be tightened (2.11) once mog's later scenes are measured; AvailMem at the select screen was about 170 KB with the earlier,
  larger tails (0x20000/0x28000).
* `absscan.py` findings: the precise asm scan finds exactly one remaining live A500 pointer, `MOVE.L #$0006b000,D1` in the dead S_4/S_5
  loaders (program.s:4444, mog.s:21850), and 9 `EXT_` low-memory uses (`$614A`, `$3820`, `$3A`, `$1B2`, `$79C`, `$1194`, `$2294`, `$3A3C`),
  all inside IRA mis-decoded text/data (flagged `data?`). The raw byte scan (`--bytes`) is noisy by design (sprite and script data fall in
  `$60000-$80000`); it does confirm the two S_30 screen cells are the only absolute screen pointers (the same bytes at program
  LAB_0266/0267 are write-before-read scratch, program.asm:4922). So the first suspect of the roadmap (another stale absolute) is ruled out.
* Cross-section `(PC)` references (hunks are loaded separately now): only `MOVE.L LAB_038B+2(PC),LAB_03F0` (program S_18 -> S_19, mog S_23 ->
  S_24), in the replaced trackdisk/file-cache code. Not executed.

### 5.4 Still open / notes

* `OsGuard` in `rt/files` (`systemUse` ... `systemUnuse`) still reloads ACE's view and does not restore the game's `COP1LC`/`DMACON`. It did not
  matter here: the screen is correct after every load, so the game rewrites its copper pointer on each screen setup, but a scene that loads files
  while showing a static picture may blank. Re-check at C4.
* With keys: any key during the intro skips to the select screen. The select screen did not react to cursor keys sent through the Windows
  clipboard/SendKeys path (the game wants fire/mouse); C3 is still open.
* FPS dips to about 2 during the very first intro frames (cel decode, CPU 594%) and then runs at 50 fps; no timing patch applied.

## 6. Fight background restore (smear), static analysis 2026-10-06 (ROADMAP 6.0a)

Symptom: in Practice (`LAB_0002` -> `LAB_0165` -> `LAB_0036`) the walking fighter leaves a copy of itself at every tick
(`build/shots/b4-fight.png`, `t2-fight*.png`); a fighter that stays put looks fine because it is redrawn over itself.
Present with every mog C++ port patch removed. Citations are `mog.asm:line` unless stated.

**Result: no relocation / display-buffer fault was found. Every buffer the restore touches is addressed correctly in the
linked image, so no `abs_symbols.json` / `mog.display.json` change is proposed (see 6.4). The root cause is NOT proven;
6.5 lists what is left and the one-run checks that separate the candidates.** Confidence that the cause is a stale or
mis-relocated absolute address: very low (under 5%).

### 6.1 How the original restores the background

Per fight tick (`LAB_0036` loop, 606-622): `LAB_0328` (combat tick, 7166), `LAB_0416` (8834), `LAB_03BE`, `LAB_039E` (7915), ...,
`LAB_031F` (pacing wait, `LAB_05BA` = 6 vblanks, 3558).

* Draw: every script entry of every job is blitted straight onto the draw screen `LAB_0D92` through the five plane
  pointers `LAB_0CFF..0D03` (`LAB_0CDA`, 23157). The pointers are set by `LAB_0426+2` (8901 ff.), which is `MOVEA.L D0,A1`
  hidden inside the mis-decoded `CMPA.L ...(PC),A6` at 8900; the linked bytes at mog_LAB_0426 are `bdfa 2240`, so the entry is intact.
* Record (`LAB_033A`, 7324-7335): unless script flag bit 4 is set (bit 4 = also draw into the clean background, 7311-7323),
  the box (x, y, w, h) = `12/14/16/18(A1)` is appended at `LAB_0641` (8 bytes per entry, `$ffff` terminator written at +12,
  at most `$2d` = 45 entries per tick, counter `LAB_0645`).
* Swap (`LAB_0416`, 8834-8844): `LAB_0D71` (24387) writes the copper plane pointers from `LAB_0D92`, then swaps `LAB_0D92` with
  `SECSTRT_35` (24415-24416); the two lists `LAB_063E`/`LAB_063F` are swapped in the same routine and `LAB_0641 := LAB_063E`
  (8836-8840); then the plane pointers are pointed at the new draw screen (8841-8842).
  `LAB_0D71` is only ever called from `LAB_0416`, so screens and lists always flip in lock step: the pairs are
  (A, `LAB_064E`) and (B, `LAB_064D`) for the whole run (lists initialised once at boot by `LAB_0303`, 6925-6926, and
  filled with `$ff` by `LAB_03A7`, 8015, from `LAB_0305`, 6940).
* Restore (`LAB_039E`, 7915-7933, and `LAB_03A2`, 7936-8014): for each entry of `LAB_063E` (the boxes drawn on this screen two ticks
  ago) copy that rectangle, word aligned and clamped to 40 x 200, from the clean background `LAB_05C0` to `LAB_0D92`, five planes
  of `$1F40` bytes, with `LAB_0D07` (23564: wait, A -> D blit `$09f0`, modulo 40 - 2w, `BLTSIZE` = h << 6 | w).
  The loop ends at the first entry with `$ffff`, width 0 or height 0 (7920, 7924-7927).
* The clean background is built before the fight by `LAB_013C` (3023 ff.): `LAB_0140` calls `LAB_014E`/`LAB_0142`, which decode the
  arena picture into the planes at `LAB_05C0` (`LAB_0426+2`, then `LAB_0C21` and the LZSS routine `LAB_0CC2` at 22902, which writes
  through `LAB_0CFF`). `LAB_0418` (8845) copies `LAB_05C0` to both screens (called at 8706 from the palette/arena setup `LAB_0401`).

### 6.2 Buffer inventory and how each is linked (build-game-debug/moonstone.elf, 09:38 build)

| buffer | original | in the image | status |
|---|---|---|---|
| screen A (draw at start) `LAB_0D92`, mog.asm:24618 | `$6BDFA` | `DC.L rt_screen_work` (`mog.display.json` `screen-b`) = `.chipbss` +0 | ok (cell reads `a92f4`) |
| screen B `SECSTRT_35`, mog.asm:24616 | `$75A3C` | `DC.L rt_screen_b` (`screen-a`) = +`$9C42` | ok (cell reads `b2f36`); A ends +`$9C40`, B ends +`$13882`, text cells start +`$13888`, no overlap |
| copper list, plane value words `EXT_0024..002D` | `$7F6AE..` | `rt_copper_list`/`rt_copper_bplNh/l` aliases, 4-byte spacing | ok (consecutive `$00E0/E2/E4..` MOVEs) |
| clear loop `LEA EXT_001d` / `CMPA.L #$80000` (24076, 24079) | `$6BEFA..$80000` | `rt_screen_clear` (+`$100`) to `rt_screen_work_end` (patch `cmpa-clear`) | ok, even length |
| clean background `LAB_05C0` | free chip, `LAB_05B8`+0 (222) | chip arena + 0 (`rtGameRun` passes the arena in A1/D1, game.cpp:154; mog adds `$5BF18`, 219) | ok, plain `AllocMem(MEMF_CHIP)`, outside the exe |
| sprite cel data `LAB_05C7` = `LAB_05C0` + `$9C40` (225) | | same arena | ok |
| sprite scratch `SECSTRT_32` (chip BSS, `$B2B0` = 45744 bytes; `LAB_0D08` carves `$B2AE`, 23633-23645) | | `mog_S_32.MEMF_CHIP` | ok, fits |
| dirty lists `LAB_064D`/`LAB_064E` (2 x 90 longs), cells `LAB_063E/063F/0641/0645` (12766-12780, 12797-12799) | BSS | `mog_S_1` BSS at `9bcb2..`, `9c13a`, `9c2a2` | ok, adjacent, 360 bytes each |
| plane pointer cells `LAB_0CFF..0D05` (23535-23548) | DATA | `mog_S_29` | ok |

Absolute addresses left in live mog code: only `$6B000` in the dead loader (17499, `ldr-dead`) and the low-memory reads
`EXT_0000/ADR_ERROR` (mapped to `rt_abs_a/c`) and `EXT_000e` = `rt_boot_flags` (1650). The other `EXT_00xx` equates occur only in
mis-decoded data or in the as_data hunk 9 / S_5 regions (grep of the whole file). `py tools/resource.py --verify --no-write` still
runs clean on the verify configuration.

### 6.3 Checks that came out clean

* No third buffer: nothing in the restore path reads an absolute RAM address. The only absolute chip pointers (`LAB_0424` = `$75A3C`,
  `LAB_0425` = `$6BDFA`, 8894-8898) are scratch cells of `LAB_0419` written before they are read; their bytes overlap the dead
  opcode word of `LAB_0426` (harmless, the real entry is `LAB_0426+2`).
* Screen/list association stays fixed (6.1), including the double `LAB_0416` of the select screens (9278-9280).
* `LAB_0645`, `LAB_0641`, the `$ff` fill and the list sizes agree with the record routine; the 45-entry cap is the original's.
* Blitter wait `LAB_0D1B` (23813, `BTST #6,DMACONR`) precedes every register load in `LAB_0D07`; the restore has no ordering
  dependency on CPU speed. The record, draw and restore paths use only word/long accesses that behave identically on 68000 and
  68020 (checked for `MOVEM.W`, `BTST Dn`, `LSL.L Dn`, `MULU`, `SUBA.W`, the indexed `JMP`; no `MOVE SR` or other privileged opcode in the fight path).
* Not an arena overrun: `files.log` shows chip high water `$5BF6C` (bump `$5BF18`) and fast `$5EF70` after the knight/arena loads.

### 6.4 Proposed change

None. `rt_screen_work`/`rt_screen_b`/`rt_screen_clear` already satisfy every constraint of the restore; a different split would only
move the same two 40000-byte buffers. The idea "the restore copies from a clean buffer left at an absolute address" does not apply:
the clean buffer is `LAB_05C0`, an arena pointer.

### 6.5 What is left (ranked) and one-run checks

The restore is a pure function of (list, `LAB_05C0`, `LAB_0D92`) and everything that addresses them is right, so the failure is in
the values. Ranked by fit with "old copies never go away, a stationary fighter looks fine":

1. A restore list that ends early or is empty at the tick of interest: the loop stops at the first entry with width or height 0 or
   `$ffff` (7920-7927). A zero-size cel (the draw code explicitly skips them, 23257-23260) recorded before the fighters would hide
   every later box. Check: dump `LAB_063E` (8-byte entries) while the smear is visible.
2. Boxes never recorded, or recorded into the wrong place: script flag bit 4 set on the walking frames (then the sprite is blitted into
   `LAB_05C0` itself, 7311-7323, and the "clean" background keeps the copy), or `LAB_0645` already above `$2d` (7326-7328). Check:
   compare `LAB_05C0` with a screen over the fighter's old position; if `LAB_05C0` contains the fighter it is flag bit 4 (data side).
3. Differences the bisect did not remove: the art override layer (`art/blo.cel` is accepted, 2026 vs 2099 bytes, only the LZSS stream
   differs, `files.log`), `rt_irq` (the game vblank handler `LAB_0B55` runs through an ACE trampoline and its one-shot stack move
   to address 0, 20489-20500, writes 20 bytes into the vector area including `$4`), and the WinUAE config (`moonstone-ace-hd.uae`:
   `fastmem_size=0`, so the "fast" arena is chip too; `cpu_compatible=false`). Cheapest bisect: run with `art/` removed, then with
   `cpu_compatible=true`, then with the irq patches replaced by no-ops.

Instrumentation that would settle 1 and 2 without more reading: from the existing `crashMemSnap/crashMemDiff` hook (5.1), log once per
fight tick `LAB_063E`, `LAB_0641 - LAB_063E`, `LAB_0645`, the first four entries of both lists, and a checksum of rows 130..190 of
`LAB_05C0`. No asm patch is needed; it reads `mog_LAB_063E`, `mog_LAB_063F`, `mog_LAB_0641`, `mog_LAB_0645`, `mog_LAB_05C0` from C++.

### 6.6 Runtime checks done 2026-10-06 (integrator)

* Without `hd/art/` (art override layer off): trail unchanged (`build/shots/na-fight.png`).
* WinUAE `cpu_compatible=true`: unchanged (`build/shots/cc-fight.png`).
* With every mog C++ port patch removed (only `mog.json`, `mog.display.json`, `mog.files.json`): unchanged (`build/shots/b4-fight.png`).
* The copies are evenly spaced (about one per walk cycle), not one per frame: that fits a walk frame being stamped into the
  clean background `LAB_05C0` (the script-flag bit 4 path in `LAB_0334`..`033B`) rather than restores being skipped.
* Still open: whether the original (cracked ADFs, `build/hunk9/a500*.uae`) shows the same in Practice. The cracktro only
  exits on a mouse click, which `uaeshot.ps1` cannot send; check it by hand.

## 7. ACE owns the display (ROADMAP 4.8)

Status: booted ON 2026-10-06 (logo, menu, caption, Practice). Since ROADMAP 7.1e this is the **only** display path: the CMake
option `MS_ACE_DISPLAY`, the OFF branches of the shims and the original copper-list template are gone, and the shims described
below (`rt_display_copper_init/show/setup`) are replaced by the C++ of section 8 (`rt::displayInit`, `displaySwap`,
`displayShow`, `displayStubInit`); this section keeps the design and the reasons. Mentions of the option below are history.

### 7.1 Design

```
program/mog asm                       rt shims (src/rt/display_ace.cpp)            hardware
SECSTRT_29/34 template copy  ------>  rt_display_copper_init(A1=tmpl, D1=null)     S = rt_copper_list (game memory)
5 bitplane pointer pairs     ------>  rt_display_show(D0=screen base)              ACE raw copper list (both buffers)
BPLCON0..DIWSTOP writes      ------>  rt_display_setup()                           (viewLoad did it; OFF: same 8 writes)
MOVE.L #rt_copper_list,COP1LCH        (unpatched: cop1lc := S)
MOVE.W COPJMP1,D0                     (unpatched)
LAB_054C / LAB_0D71 beam wait + swap  (unpatched; only the pointer words inside them go through rt_display_show)
```

* **Screens.** The game's two 5 x `$1F40` screens (`rt_screen_work` = A, `rt_screen_b` = B, section 2) are wrapped, not copied:
  `bitmapCreateFromMem` gives two `BMF_EXTERNAL` `tBitMap`s (planes at base + n x `$1F40`). They are the front/back bitmaps of a
  double-buffered `simpleBuffer` (`TAG_SIMPLEBUFFER_FRONT/BACK_BITMAP`) on a 320x200x5 vport of an ACE view with window start Y
  `$2C` and height 200, so `viewLoad` writes exactly the original `DIWSTRT $2C81`, `DIWSTOP $F4C1`, `BPLCON0 $5200`; the list
  carries DDF `$38/$D0`, modulos 0, `BPLCON1 0`. `viewLoad` also resets `FMODE/BPLCON3/BPLCON4` (what `displayHandoverToGame`
  did by hand). `BPLCON2` is `$24` (`$224` at 6 planes): the original never writes it, so it inherits Kickstart's `$24` (sprites in front of
  both playfields). An earlier `0` hid the loot/combat cursor sprite behind the picture (fixed 2026-10-06).
  The game's own cells (`LAB_056C`/`SECSTRT_30`, `LAB_0D92`/`SECSTRT_35`) stay the single owner of "which screen is draw / shown":
  the asm swaps them itself exactly as before; ACE's `pFront`/`pBack` are derived from the base address at every
  `rt_display_show` (shown = the base given, back = the other one), so the two cannot disagree. ACE-side drawing (4.4 blits,
  4.8a) uses `pBack`.
* **Copper.** The ACE view uses a *raw* copper list of 16 commands (`simpleBuffer`'s WAIT at line 43, DDFSTOP/STRT, BPL1/2MOD,
  BPLCON1, 10 bitplane pointer MOVEs). ACE's list is double-buffered for `copProcessBlocks`; nothing calls that after `viewLoad`,
  so the rt keeps **both buffers identical** (every update is written to both) and cop1lc stays on the one `viewLoad` selected.
  `rt_copper_list` in the game's chip block shrinks to a 20-command stub: `SPR0PTH..SPR7PTL` (null sprite),
  `COP2LCH/L := ACE list`, `COPJMP2`, `$FFFFFFFE`. The game still writes `COP1LCH := rt_copper_list` + `COPJMP1` at the end of its
  init (those patches are older and untouched), which now means "run the stub, which jumps into ACE's list". Why a stub: the
  sprite code (mog `LAB_0E85`, program twin) scans `rt_copper_list` for the register word `$0120 + 4n` and patches the two value
  words in place; it keeps working without a patch. The stub is rebuilt at every overlay entry (the game clears the whole block
  first, as before).
* **Bitplane pointers.** `rt_display_show(base)` writes the ten value words of ACE's list (both buffers, one 16-bit store each)
  and sets front/back. The game calls it with `SECSTRT_30`/`SECSTRT_35` (screen B) at overlay entry and with the draw screen at
  every swap, after its own beam wait for line `$F5` (`LAB_0552`/`LAB_0D77`, kept), i.e. outside the display window (the copper
  reads the pointers at line 43).
* **Palette.** Unchanged: the colours are CPU writes to `COLOR00..31` from the vertical-blank hook (`rt_palette_tick_*`,
  `LAB_0565` etc.), not copper words. `viewLoad` clears the colours once (ACE's vport palette is zero) at handover, before the
  first overlay. Moving the palette into ACE (4.5a `rtPaletteAgaWrite`, per-frame copper words) belongs to 4.8a.
* **Ownership (M2 rule, new mode).** ACE owns the view, copper list, DIW/DDF/BPLCON/modulo registers, bitplane pointers and the
  sprite-pointer stub; the game still owns blitter, Paula, CIA-B disk bits, the vertical-blank interrupt and the palette
  registers. Direct game writes that remain and are fine because ACE never re-writes the register after `viewLoad`:
  `DIWSTRT/DIWSTOP` from the fight shake (mog `LAB_0427`..`LAB_042B`, 8925-8942, and the `LAB_0D8x` DIW setter), `COLOR00`
  flashes, DMACON. `OsGuard` (files) does `systemUse/Unuse` as before: ACE's DMA tracking already contains copper and raster
  (`copCreate`, `viewLoad`), `systemUse` only clears disk and blitter DMA, and the list memory is ACE's, so the screen should
  survive file loads without a `viewLoad` (to be confirmed on boot).
  Do **not** call `viewLoad`, `copProcessBlocks`, `viewProcessManagers` or `simpleBufferProcess` on this view while the game
  runs: the first two swap the list buffers (cop1lc would leave the stub, the sprites vanish), the last two would rewrite the
  pointers from `pBack` (the draw screen) while the game still decides what is shown.

### 7.2 Files and names

* `asm/patches/{program,mog}.display_ace.json`: eight patches (`ace-copper-init`, `ace-show-init`, `ace-setup`, `ace-show-swap`
  per binary), none overlapping an older patch (`dmacon-cleared`/`cmpa-clear` and `copper-ptr` stay). Lines: program 9904-9922,
  9926-9949, 9956-9963, 10213-10236; mog 24081-24099, 24103-24126, 24133-24140, 24390-24413. They are merged into every build
  (one generated asm); with the option OFF the shims reproduce the replaced instructions.
* `src/rt/display_ace.{hpp,cpp}`: `rt_display_copper_init` (A1, D1), `rt_display_show` (D0), `rt_display_setup` (none), all
  register-preserving asm entries over `rtDisplayCopperInit/Show/Setup`; `rt::displayAceActivate/Release/IsActive`. `display.cpp`
  calls activate at the end of `displayHandoverToGame` and release in `displayDestroy` (both `#if MS_ACE_DISPLAY`).
* `abs_symbols.json` needs the three names in `funcs` as `{"name": ..., "impl": "src/rt/display_ace.cpp"}` (otherwise a `.weak`
  stub is emitted and Release/LTO fails with "symbol already defined"), then `py tools/resource.py` to regenerate `asm/*.s`,
  `src/rt/abs_stubs.cpp`, `include/rt/abs.h`.

### 7.3 Not done / open

* Never booted. If activation fails (bitmaps, view or list layout check) it logs `ERR: displayAceActivate` and every shim falls
  back to the original behaviour.
* PAL only (window start fixed at `$2C`, like the original). 6-plane enhanced mode (4.8a): `DISPLAY_BPP`, the `PLANE_BYTES`
  stride and the list slots change, `BPLCON0` comes from `viewBuildBplCon0`, the game's 5-plane copy/clear routines need the
  N-plane versions (`planCopyPlanes`).
* The swap still relies on the original beam wait; ACE's `vPortWaitForEnd` is not used.
* The old copper-list words `rt_copper_bpl*` are only written with the option OFF.

### 7.4 Boot checklist (build-game-debug + hdinstall, A1200; the checklist of section 8.6 supersedes it)

1. ACE log: `display_ace: view loaded, lists ... screens A ... B ...`, no `ERR: displayAceActivate`, no `show ... is neither
   screen A nor B`.
2. Mindscape logo (C1): colours, size and position identical to the OFF screenshot (line 44, 320x200). Black before it, no
   rolling garbage.
3. Intro, then "Select a Knight": pointer sprite visible (sprite stub), at the right position, no garbage sprites.
4. Overlay change program -> mog (stub rebuilt after the clear): at most the same black gap as OFF, then the title/menu.
5. Map: cursor/knight sprites, redraw without tearing or flicker at the swap, loot and place screens.
6. Fight (6.0a): compare the trail with OFF (`build/shots/b4-fight.png`); the DIW shake on a hit still moves the window; a file
   load during a scene (arena) leaves the picture intact.
7. Compare screenshots OFF vs ON scene by scene; a one-frame bitplane glitch at a swap points at the write timing in 7.1.

## 8. The display layer in C++ (ROADMAP 7.1e)

Status: written, patch-verified (`py tools/resource.py --verify --no-write`), compile-checked with `m68k-amiga-elf-g++` (`MS_ENHANCED`
0 and 1; the tests also build at `-O0..-O3` and with the Release flag set) and tested against the original routines in unicorn
(`tests/test_display_fx.py` on the host, `test_display.py`, `test_sprites.py`, `test_palette_glue.py`, `test_wipe.py`, `test_input.py` in unicorn); **not booted**.

ACE is the only owner of the display: the option `MS_ACE_DISPLAY`, the OFF branches of the shims, the original copper-list template
copy and the three `rt_display_copper_init/show/setup` shims are gone (`MS_ENHANCED` needs nothing else; the game is the only build since 7.1r). The routines
that touched the display hardware are C++; their asm labels stay and start with a `JMP rt_*` patch (the rest of the routine is dead
code, nothing in it is deleted), so every other asm or C++ caller keeps working.

### 8.1 Files

| file | what | replaces (program / mog) |
|---|---|---|
| `src/rt/display_ace.{hpp,cpp}` | the ACE view over the two screens; `displayShow(base)` (bitplane pointers of ACE's list + front/back), `displayStubInit(null)` (the copper stub), `displayAceActivate/Release/IsActive` (`bool` now; a failure stops the game before the first overlay) | the shims `rt_display_*` |
| `src/rt/copper_stub.hpp` | header-only: the stub at `rt_copper_list`, `copperStubSetSprite(n, addr)` (the SPRxPT value words, one 16-bit store each) | the scan of the copper list for register words |
| `src/rt/display_ops.{hpp,cpp}` | `displayInit` / `displaySwap` (per overlay, `DisplayCells`), beam wait, wait frames, screen clear, word copy, palette write, mog key table lookup | SECSTRT_29 / SECSTRT_34, LAB_054C / 0D71, 054D / 0D72 (the loop), 054F / 0D74, 0552 (mog) / 0D77, 055E / 0D83, 0565 / 0D8A, mog 0D8D |
| `src/rt/sprites.{hpp,cpp}` | `spriteInstall/Off/Place/Build/DmaOn`; the per-sprite data table is C++ state | mog LAB_0E75, 0E85 (and its callers 0E76/0E77), 0E78, SECSTRT_37 |
| `src/rt/mog_display.{hpp,cpp}` | flip with the dirty-list rotation, screen blits (5 or 6 planes), DIW shake | mog LAB_0416, 0418, 0419, 0427, 042A |
| `src/rt/palette_glue.cpp` | palette clear, fades, the fight scene palette (`LAB_03F3` with 0401 / 0403 / 0409 inlined), fight palette ramp, hook installs, slot free | mog LAB_03EB, 03F0..03F3, 0412, 0413, 0E53, 0E59; program SECSTRT_31, LAB_0579 |
| `src/rt/wipe.{hpp,cpp}` | the picture wipe (tile blocks from three pictures by a tile map, strip moves) | program LAB_05B2, 05B7, 05BA, 05BF, 05C1, 05C5 (05C8 / 05CC / 05CD inlined) |
| `src/engine/display_fx.cpp` (`include/engine/display_fx.hpp`) | the pure parts, host-tested: sprite control words, DIW shake state machine, wipe geometry (`wipeTile` = LAB_05C5/05C8/05CC/05CD, `wipeRows`, `wipeCode`), fight scene colour tables (`sceneColors`, `sceneTail`, `fighterColors`) | the arithmetic inside the routines above |
| `src/rt/input.cpp` | the joystick cursor calls the sprite routines directly (`rtInputAsm` is gone) | |
| `asm/patches/{program,mog}.display2.json` | 14 / 27 head patches (ids `d2-*`) | `{program,mog}.display_ace.json` (deleted: its patches sat inside bodies that are dead now) |

Register contracts: every entry preserves **all** registers (the originals saved or clobbered different subsets and no caller depends
on a clobber), flags are not kept, except: `rt_mog_palette_clear` returns `D0 = $0000FFFF` (the DBNE loop), `rt_mog_sprite_build`
returns `A1` = end of the data and `A0` = partner start for an attached frame, `rt_mog_key_xlat` returns `D0 = (index << 16) |
table[index]`. `LAB_03F0..03F3` / `LAB_0412` leave `D0`/`D1` as they were (the original left the frame count or the sound stop's
values). The header comment of each file has the list.

### 8.2 Differences from the asm, on purpose

* No template copy: the stub (eight SPRxPT pairs at the null sprite, `COP2LC := ACE's list`, `COPJMP2`) is built by `displayStubInit`
  at every overlay entry, after the work block is cleared. The sprite pointers are written at fixed places in the stub.
* Program beam wait: LAB_0552 already is `rt_prg_wait_beam` (intro skip poll, patch `intro-skip-poll`); the C++ reaches it through
  `DisplayCells::pfnWaitLine`, so every program frame wait still polls the skip keys.
* An attached sprite pair on an odd sprite (partner = sprite 0) is skipped: the original returned through the copper pointer it
  had pushed (a crash) and nothing builds such a pair.
* `LAB_0412` no longer calls `LAB_0BB3` (a bare `RTS` in the original image).
* `LAB_0422` copies with `rt_display_copy_words` (A0/A1 stay, D0 = 32).
* The DIW shake table is a C++ constant (checked against the asm bytes in `test_palette_glue.py`).
* The tables and cells in the asm hunks (LAB_08D1..08D9, LAB_0411, LAB_0E93, the wipe cells of S_31 / S_33, the key table) stay
  where they are (ROADMAP 7.1n); the C++ reads and writes them in place.

### 8.3 Still asm in this group

* `LAB_054D` / `LAB_0D72`: `MOVEM` + the `enh-clear-count` load of D0 (a patch in `{program,mog}.enhanced.json`), then `JMP
  rt_display_clear_tail` replaces the first 3 `CLR.L` of the loop. When `enh-clear-count` is deleted the patch can move to the head.
* mog `LAB_03EE` (palette table to the live palette: the whole routine is the patch `enh-pal-live`, already a shim) and `LAB_041F`
  (copy of 10000 longs, `enh-copy-longs`): covered by `*.enhanced.json` patches, so they were left alone.
* `LAB_0D7B/0D7C` (INTENA) and `LAB_0556/0557`: two lines each, `JSR rt_irq_disable/enable`.
* Program S_31 scene bodies LAB_059E..05AF (palette fades of the intro text pages; `LAB_05A5` is called by `engine_intro`): they
  sequence jobs, music and flips more than they draw, and they call the wipe through the labels above. 7.1i.

### 8.4 Dead patch entries (removed in the 7.1 cleanup)

Removed: `enh-pal-write` (both), `enh-wipe-a/b/c` (program), `enh-pal-clear` (mog), `dmacon-cleared` + `copper-ptr` (program),
`cmpa-clear` + `copper-ptr` (mog) and every other patch whose first line `tools/asm_remaining.py` proves unreachable (docs/DEAD_RT.md
section 8). Shims removed with them from `enhanced.cpp`: `rt_prg_lea_a1_planes`, `rt_prg_lea_a0_planes`, `rt_prg_05c5_init`,
`rt_{prg,mog}_pal_write`, `rt_mog_pal_clear`.
Still live: `enh-clear-count`, `enh-copy-longs`, `enh-copy-init` (`rt_mog_0419_init`), `enh-pal-clear` (program: `rt_prg_pal_clear`),
`enh-pal-live` and the carve patches: the display routines LAB_054D / LAB_0D72 / LAB_0419 / LAB_041F and program LAB_0258 are still asm.

### 8.5 Tests

Each routine runs in unicorn next to the original of the reassembled image (`build/reasm`) on a fake chip set; custom and CIA
registers are memory, writes are logged in order, beam reads are scripted. Compared: registers, cells, memory, hardware writes. The
routines outside a test are logging stubs in both runs (palette machine, sound stop, live-palette copy, background restore).
`py -m unittest tests.test_display_fx tests.test_display tests.test_sprites tests.test_palette_glue tests.test_wipe tests.test_input`.
`test_display_fx.py` also compiles `display_fx.cpp` for the m68k at `-Os..-O3` and fails if it calls `memset` / `memcpy` (no libc on the
target: aggregate initialisers and zeroing loops are the usual way to get one; the rt files use `asm volatile("" ::: "memory")` in
copy loops and field-by-field construction for the same reason).
Not covered in emulation: ACE's side (`displayShow` / `displayStubInit` / `displayAceActivate` are logged only), the real blitter
(not emulated: its register writes are compared), 68020 timing, the interplay with the vertical-blank handler.

### 8.6 Boot checklist (A1200, build-game-debug + hdinstall; repeat with `MS_ENHANCED=ON` for 6 planes)

1. Log: `display_ace: view loaded (5 planes) ...` (`6 planes` enhanced), no `ERR: displayAceActivate`, no `show ... neither screen`.
2. Mindscape logo and intro pages: colours, position, fades; the wipes (a picture revealed in blocks, moving up/down) are smooth.
3. Intro skip (Space / fire during the intro): leaves to the menu as before (the program beam wait path).
4. Select a Knight: the pointer sprite at the right place, moving with the joystick (sprite install/place) and the mouse.
5. Map: cursor/knight sprites, flip without tearing, loot, places, town screens; the fades between screens (LAB_03F0..03F3).
6. Fight: fight palette per region/scene, DIW shake on a hit, background restore (6.0a trail unchanged from the last ON run), a
   file load (arena) leaves the picture intact.
7. Overlay change program -> mog (stub rebuilt): the same black gap as before, then the title/menu.
8. Enhanced: palette writes/clear go through the 24-bit follower (no flash at the fight palette); blits copy 6 planes.
