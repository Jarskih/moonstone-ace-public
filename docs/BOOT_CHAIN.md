# Boot chain and overlay chain (ROADMAP 1.5)

Citations are `file:line` in `../moonshard/moonstone-main/amiga_asm/` (the IRA listings, never edited).

## 1. The original chain

```
bootblock -> nb (trackloader, no OS) -> loads "program" -> program SECSTRT_0 -> loads "Mog" -> mog SECSTRT_0
                                                              ^                                    |
                                                              +---- (ending/diag pass, bit 7) ----+
```

| Step | Where | What happens |
|---|---|---|
| nb sets the loader cells | nb.asm:214-218 | `$3F0`=start of fast RAM image, `$3F4`=chip pointer, `$3F8`=top of fast, `$3E0`=0 (boot flags), `$3FE`=memory config (nb.asm:139) |
| nb hunk-loads `program` | nb.asm:3048 (`SECSTRT_14`) | standard HUNK_HEADER/CODE/DATA/BSS/RELOC32 loader |
| nb tail-jumps to the entry | nb.asm:218-298 | copies a small stub (nb `LAB_000F`..`LAB_0016`) to safe RAM, parks AUTO_INT1-6 on an `RTE`, moves hunks to their final addresses, then `JMP (A2)` with **A2 = D4 = start of hunk 0 = `SECSTRT_0`** |
| program starts | program.asm:108 | `SECSTRT_0`, see 2 |
| program ends | program.asm:153-155 | `LAB_0000: LEA LAB_0004,A0 ("Mog") ; JMP SECSTRT_4` |
| program's loader `SECSTRT_4` (S_4) | program.asm:2776-3119 | reads the cells (2778-2781), opens `"Mog"` through the file cache/trackdisk (`LAB_012F` -> `LAB_0390`, `LAB_03B2`), loads and relocates its hunks (`LAB_0130`-`LAB_0150`), copies the stub `LAB_0128`..`LAB_012F` to `LAB_056C` (= `$6BDFA`, 2800-2826), parks AUTO_INT1-6 on an `RTE` (2814-2819), stops bitplane/copper/blitter DMA (`DMACON $0380`, 2803) and runs the stub, which relocates the hunks and jumps to mog's entry (2848-2872) |
| mog starts | mog.asm:141 | `SECSTRT_0`, see 2 |
| mog -> program re-entry | mog.asm:1635-1652 | in `LAB_00A8`: collects flags into D7, `ORI.W #$0080,D7 ; MOVE.W D7,EXT_000e ($3E0)`, then `LEA LAB_06D9,A0 ("program") ; JMP SECSTRT_5` |
| mog's loader `SECSTRT_5` (S_5) | mog.asm:17412-17760 | the same loader as program's `SECSTRT_4`, loading `"program"` (mog.asm:13170) |
| program second pass | program.asm:114-135, 161-180 | `$3E0` bit 7 set -> `LAB_0001` (ending/diag branch: loads assets `LAB_00A2`, runs `LAB_0036`-`LAB_003B`, `LAB_054F(25)`) -> `BRA LAB_0000` -> loads `"Mog"` again |

Both loaders end with the same tail (`program.asm:2848-2873`, `mog.asm:17500-17511`): if D3 (the `$3FE` memory-config word) is 0 they `JMP $400` with D0=0 (chip-only bootstrap restart),
otherwise `JMP (A2)` with the registers of 2. Neither overlay ever returns; the only loops are
program -> mog (always) and mog -> program (one place, `LAB_00A8`) -> mog.

`SECSTRT_4` is entered from exactly one place (program.asm:155) and `SECSTRT_5` from exactly one place (mog.asm:1652).

## 2. What an overlay entry expects

Both `SECSTRT_0` routines take **four registers** and nothing else from the loader:

| Register | Meaning (from the loader's tail, program.asm:2858-2870) | program saves it in | mog saves it in |
|---|---|---|---|
| **A0** | start of free fast RAM (`$3F4` cell + loaded fast hunks) | `LAB_00C4` (program.asm:111) | `LAB_05BE` (mog.asm:145) |
| **D0** | free fast RAM size (`$3F8` top - A0) | `LAB_00C5` | `LAB_05BF` (never read again) |
| **A1** | start of free chip RAM (below `$6B000`) | `LAB_00C2` (109) | `LAB_05BC` (143) |
| **D1** | free chip RAM size (`$6B000` - A1) | `LAB_00C3` | `LAB_05BD` (never read again) |

D2-D7 and A2-A6 are loader leftovers and are never read before being written. SP and SR are not touched by the loader (they stay the nb values).

The two pointers are the *only* memory contract. Each overlay carves its buffers from them with one `ADDI` bump per pointer and a chain of
fixed offsets:

| Overlay | chip bump | fast bump |
|---|---|---|
| program (`LAB_0044`, program.asm:920-956) | `$4536C` (921) | `$58116` (938) |
| mog (`LAB_0004`, mog.asm:218-300) | `$5BF18` (219) | `$5654D` (248) |

**Nothing is shared between the two binaries through memory.** Confirmed three ways:
(a) both objects define disjoint symbol sets and reference no symbol of the other (`tests/test_resource.py`, `Prefixing`);
(b) every buffer of each overlay is derived from its own A0/A1 in its own `SECSTRT_0` chain (above), and the screen/work area is re-cleared by each overlay's own
`SECSTRT_29` (program) / `SECSTRT_34` (mog) clear loop over `$6BEFA..$80000`;
(c) the original loader overwrote the previous overlay in place, so nothing of it could survive.
The cross-overlay state that does exist is small: the boot flags word `$3E0` (nb clears it, mog sets bit 7 at mog.asm:1650, program reads it at program.asm:114;
now `rt_boot_flags`), the hardware state (copper, DMA, INTENA, CIA), and the data files on disk.

## 3. The replacement

| Original | New |
|---|---|
| program.asm:154-155 `LEA LAB_0004,A0 ; JMP SECSTRT_4` | patch `run-mog` (asm/patches/program.json): `JMP rt_run_mog` (+3 NOPs, size kept) |
| mog.asm:1651-1652 `LEA LAB_06D9,A0 ; JMP SECSTRT_5` | patch `run-program` (asm/patches/mog.json): `JMP rt_run_program` |
| nb -> program | `rtGameRun()` (src/rt/game.cpp) calls `prg_SECSTRT_0` directly. nb is not linked. |

`rt_run_mog` / `rt_run_program` (declared in `include/rt/abs.h` as `entry_funcs` of abs_symbols.json, defined in `src/rt/game.cpp`) never return to the asm. They record the next overlay in
`rt_game_next` and unwind the stack to the frame `rt_game_call` saved, which returns to the C++ loop in `rtGameRun`:

```
rtGameRun: allocate chip arena (0x5BF18) + fast arena (0x58116)   // max of both overlays' bumps
  loop:  imageEnter(overlay)                                       // see 4
         rt_game_call(entry, A0=fast, A1=chip, D0=fast size, D1=chip size)   // saves d2-d7/a2-a6 + SP, JSR entry
         next = rt_game_next                                       // set by rt_run_*; EXIT if the entry returned
```

The unwind (instead of a nested call) keeps the stack flat across program -> mog -> program cycles, as the original loader effectively did.
The S_4 / S_5 loader hunks stay in the link so the original layout and relocation offsets are preserved byte for byte (`py tools/resource.py --verify`), but since
ROADMAP 4.7 their code is emitted as inert data (patch `ldr-dead`, `kind: as_data`) and the `ldr-*` patches and `rt_loader_*` stubs are gone; they can be dropped
when `--verify` is scoped by section instead of hunk index (docs/DEAD_RT.md section 4).

## 4. Fresh overlay state on every entry

The original reloaded the overlay from disk on each switch, so its DATA was pristine and BSS zero. Linked in, program and mog stay resident and are dirty on re-entry.
Since ROADMAP 7.1r there is no asm in the link: every DATA/BSS hunk is a C++ object (tools/gen_data.py) and the generator also writes the reset tables
`rt::g_imageProgram` / `rt::g_imageMog` (one `{object, size, isBss}` row per owned hunk, original sizes; they replaced the resource.py-generated `image_tab.cpp`
and its `<prefix>S_n_beg` labels). `rtGameRun` snapshots an overlay's DATA just before its first entry and restores DATA / clears BSS before every later entry.
`rt_*` cells (boot flags) are deliberately not in the table, nor are the code-hunk data cells (`extern_cells`: they were never reset, the dead code around them is gone).
Mog's synth state is a C++ BSS object (`ms::Synth`) that the synth init clears. The overlay entries are `rt_prg_main` and `rt_mog_entry` (the audio quiesce, then `rt_mog_main`).

## 5. What is still a stub (M2)

(Historical, M2:) every `rt_*` function (INTENA, vectors, CIA setup, DMA, TRAP #15, loader leftovers) logged via `rt_stub_hit` (src/rt/stubs.cpp, debug builds only) and returned with all registers intact.
Direct writes to custom-chip registers that were not turned into rt calls remain in the asm (`asm/*.hw.txt`), as do file loads (S_13 trackdisk, S_18 cache),
so the game is not expected to run correctly yet.
