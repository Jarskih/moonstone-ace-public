# Dead `rt` shims, dead patches, S_4/S_5 loaders (ROADMAP 4.7, analysis + first removal)

Citations are `file:line` in `../moonshard/moonstone-main/amiga_asm/` (IRA listings) unless a path says otherwise.

## 1. Method

M4 put JMP patches in front of asm routines (`asm/patches/*.json`, ids `*-cpp`) and left the original bodies in place as dead code
to keep the hunk layout. Earlier rt code (loader stubs, TRAP #15 stubs, file shims) was written when that code still ran.
Question per `rt_*` symbol and per patch: is it reachable from live code?

Reachability model (script was scratch, rules are exact):

* Unit = a label and the lines up to the next label. Edges: every `prg_/mog_` label named in a *live* line of the generated
  `asm/<bin>.s` (any operand, `LAB+N` counts as `LAB`), plus fall-through into the next label.
* An unconditional `RTS/RTE/RTR/JMP/BRA` ends the unit: the lines after it, up to the next label, are dead. This is what makes the
  asm body behind a `JMP rt_x` patch dead. A `JMP`/`BRA` with a `(PC,Dn)` operand is not treated as terminal (jump tables).
* A label that is the target of a `LAB+N` reference anywhere (asm or C++) keeps its whole unit live. Needed for `mog_LAB_00B4+12`,
  the unlabelled main menu that `src/rt/hunk9.cpp` returns into (it contains patch `sr-super`: live, kept).
* Roots: `SECSTRT_0` of each binary and every `prg_/mog_` symbol named in `src/rt`, `src/engine`, `include/rt`, `include/ms/linked.hpp`
  (intro scenes, job handler tables, palette variables, input tables, hunk-9 return chain, `rt_game_call`).
  `src/lifted/**` and `include/ms/gen/*` are not roots: they only exist in the optional `MS_LINK_LIFTED` build and call back into asm through thunks.
* Limits: IRA labels every direct branch target, so an unlabelled entry cannot be reached by a direct reference. A computed jump into
  the middle of a unit would be missed; none was found, and every patch removal below only *restores the original asm body*, so a
  wrong deadness call degrades to "the original routine runs" (correct, only slower or OS-unsafe for the TRAP helpers), never to a crash of C++ state.

Result: of 1533 (program) and 4293 (mog) label units, 569 and 559 are dead, listed per hunk in section 6.

## 2. Removed (this task)

| What | Where | Evidence it was dead |
|---|---|---|
| patches `ldr-cells`, `ldr-dma`, `ldr-vectors`, `ldr-exit` (program S_4, mog S_5) | `asm/patches/program.json`, `mog.json` | They patched the S_4/S_5 overlay loaders. `SECSTRT_4` is entered only from program.asm:155 and `SECSTRT_5` only from mog.asm:1652, both replaced by `run-mog`/`run-program` (`JMP rt_run_*`). Replaced by one `ldr-dead` `as_data` patch per binary (section 4). |
| patches `trap15-enter`, `trap15-leave` (both binaries) | `program.json`, `mog.json` | Unlabelled helper after `LAB_0557` / `LAB_0D7C`, and the TRAP #15 handlers `LAB_0558/0559`, `LAB_0D7D/0D7E`: no reference anywhere (`LAB_0557` is only the INTENA enable, which ends in `RTS` before them). The original text is back in the generated asm, still unreachable. |
| patch `files-skip` (program) | `program.files.json` | `LAB_03C5` has two callers, program.asm:2952 and :3115, both inside the S_4 loader. mog's skip (`LAB_0BEA`) is also called from live code and stays. |
| patch `palette-cycle-add-cpp` (program) | `program.palette.json` | Unlabelled routine behind `LAB_0576`, no caller in program (the patch text said so). mog's twin `LAB_0E56` is live and stays. |
| patches `rle-cpp` (program `LAB_0448`, mog `LAB_0C6D`) | `program.engine.json`, `mog.engine.json` | The only callers (`JSR LAB_0448(PC)` at program.asm:8535/:8587, `JSR LAB_0C6D(PC)` at mog.asm:22712/:22764) sit in unlabelled routines that nothing references; ROADMAP 4.1 already records that no disk file goes through RLE. |
| shims `rt_rle_decode` | `src/rt/engine_rle.cpp` (file deleted) | Only the removed patches referenced it. `ms::rleDecode` (`src/engine/rle.cpp`) and `tests/test_engine_rle.py` are untouched; the decoder is simply not linked into the asm any more. |
| shim `rt_palette_cycle_add_prg` | `src/rt/engine_palette.cpp` (macro split, mog entry kept) | only the removed patch referenced it |
| shim `rt_prg_file_skip` | `src/rt/files.cpp` (macro split, `rt_mog_file_skip` kept), `entry_funcs` | only the removed patch referenced it |
| stubs `rt_loader_park_irq`, `rt_loader_dma_off`, `rt_loader_exit`, `rt_trap15_enter`, `rt_trap15_leave` | `abs_symbols.json` `funcs` (generated `.weak` stubs in `src/rt/abs_stubs.cpp`) | only the removed patches referenced them. **No bare logging stub is left**, so the `{stubs}` part of `abs_stubs.cpp` is empty. |
| `rt_ldr_cells` (`constants` entry, `ldr_cells` block, two hard-coded lines in `tools/resource.py` `write_stubs`) | `abs_symbols.json`, `tools/resource.py` | only `ldr-cells` referenced it; `EXT_0008..0013` no longer appear in generated code |
| `tests/test_files.py` | expectation | program no longer patches a skip entry (`OPS_BY_BINARY`) |

Also tidied: `abs_symbols.json` `_doc` (no bare stubs remain).

## 3. Kept although suspicious

| Item | Why it stays |
|---|---|
| `rt_stub_hit` (`src/rt/stubs.cpp`), `STUB_TMPL`/`STUBS_TMPL` in `tools/resource.py`, `rt_stub_hit` decl in generated `abs.h`, `test_every_stub_logs_and_preserves_registers` | Now consumer-less (no bare stub). Removing the stub mechanism is a generator change, not a dead-code deletion; do it with the next generator touch (also `stubs` in the `CMakeLists.txt` exclude regex). |
| abs entries `rt_trk_block/state/sector/cylinder`, `rt_text_p0/p1/x/y`, `rt_trap_save_sp` | Referenced only from dead code (trackdisk driver S_13/S_18, TRAP helpers, program's unused text routines `LAB_0530..054B`). But `tests/test_absscan.py` requires every low-memory `EXT_` use in a code line to be rewritten, so deleting the entry makes the dead code fail it. They can go together with an `as_data` conversion of those dead ranges (section 6). `rt_text_p0/p1` are live in mog `SECSTRT_0`. |
| patch `sr-super` (mog `MOVE #$2000,SR` -> `NOP`) | In the main menu reached through `mog_LAB_00B4+12`: live. |
| patches `files-drive-init`, `files-drive-detect`, `files-disk-prompt` (mog), `files-init` (program) | Callers are live (`LAB_00F8`, `LAB_0100` callers, program `SECSTRT_0`). |
| `rt_irq_set_int4`, `rt_irq_install/enable/disable`, `rt_prg_intro_begin`, `rt_prg_wait_beam`, `rt_mog_hunk9_exit`, all `rt_job_*`, `rt_palette_*` (except the removed one), `rt_prg_draw_cel`, `rt_mog_draw_cel`, copy-rect, `rt_rnc_decode`, `rt_lzss_decode` (both), `rt_rng_*`, `rt_format_number3` (mog), `rt_run_*`, `rt_prg_file_init/open/read/close`, `rt_mog_file_*` | Reached from live units (patch sites are the first line of live routines; the LZSS callers `LAB_0494`/`LAB_0CB9` region is dead but `LAB_049C`/`LAB_0CC2` have live callers). |
| `rt::irqRemove()`, `rt::crashTraceStart/MemSnap/MemDiff` | No caller anywhere, but they are teardown/debug entry points that 4.x did not orphan. Left for the owner. |
| `rt_abs_a`, `rt_abs_c`, `rt_boot_flags`, `rt_copper_*`, `rt_screen_*` | Live. |
| S_5/S_6/S_7 (program) and S_6/S_7 (mog) loader DATA/BSS cells | Still in the link: the hunk table and `image_tab.cpp` keep the original layout (section 4). |
| Two patch files belong to another session and are not in `abs_symbols.json` yet (`program.timing.json` -> `rt_audio_wait`, `mog.rules.json` -> `rt_knight_recalc_endurance`) | Not touched. `resource.py` aborts on them until that session registers the symbols (see section 5). |

## 4. S_4/S_5 loader status

* Dead since 1.5: `SECSTRT_4` / `SECSTRT_5` have exactly one entry each (program.asm:155, mog.asm:1652), both replaced by `JMP rt_run_*`.
  The loader code (program S_4 = 1156 bytes, program.asm:2776-3117; mog S_5 = 1156 bytes, mog.asm:17414-17755) called `LAB_0390/03B2/03C5` and
  poked custom registers, vectors and `$3F0..$3FF`. Nothing live reaches it, nothing live is reached only through it (apart from the program `files-skip` entry, removed above).
* **Done:** the code of both loaders is now emitted as inert data (`kind: as_data`, patch id `ldr-dead`, same mechanism as mog's hunk-9 stub).
  Bytes, labels and hunk sizes are unchanged, `--verify` (B) shows `0 bytes changed / 97 relocs dropped` inside the `ldr-dead` range and nothing outside any patch range.
  The generated asm no longer mentions `rt_loader_*`, `rt_ldr_cells`, `rt_trap15_*`, and the last live `$6B000` literal of `tests/test_absscan.py` is gone from `asm/*.s`.
* **Not done (open in 4.7):** physically dropping S_4..S_7 (program) and S_5..S_7 (mog) from the link. That renumbers hunks, so it needs `--verify` (A)/(B)
  and `src/rt/image_tab.cpp` to compare/emit by section name instead of hunk index ("`--verify` scoped to the remaining asm"). The loader DATA/BSS (cells
  `LAB_0153..0160`, `LAB_0A20..0A2D`) are only referenced by the inert code.

## 5. What to regenerate

`py tools/resource.py` rewrites `asm/program.s`, `asm/mog.s`, `include/rt/abs.h`, `src/rt/abs_stubs.cpp` (5 stubs and `rt_ldr_cells` disappear, 0 bare stubs, 30 implemented, 10 entries),
`src/rt/image_tab.cpp` (unchanged). Optionally `--hw` for `asm/*.hw.txt` (stale: it was classified before the loader became data).
Until then `tests.test_resource.Prefixing.test_generated_files_in_repo_are_current` fails (expected). Two failures are unrelated to this task:
`test_implemented_funcs_have_no_stub` already fails at HEAD (the palette/intro shims are generated by macros, the test greps for a column-0 label),
and the two `Verify` tests / `--verify` stop on the other session's unregistered `rt_audio_wait` / `rt_knight_recalc_endurance` patches.
With those two patch files set aside, `--verify` passes for both binaries (29 / 31 patch ranges, nothing outside them).

## 6. Dead asm ranges (analysis result; not converted)

Label ranges that nothing live references (after this change; `S_n` = hunk). Converting them to inert data would let the abs entries of section 3 and the
remaining hardware accesses in them (`DSK*`, `CIAB_PRB`, `INTREQ`, ...) leave the generated asm. Not done here: it touches code the C++ patches sit inside and
needs its own `--verify` pass per range.

program: S_4 loader (`SECSTRT_4`..`LAB_0151`, now data), S_5-S_7 loader cells, S_12 `SECSTRT_12..LAB_028E`, **S_13 trackdisk driver** (`SECSTRT_13..LAB_02F4`, `LAB_02F6..02FE`, `LAB_0303..0311`),
S_15 `LAB_0323/0324/032E-0330/0342-034C`, **S_18 OFS reader bodies** (`SECSTRT_18..0381`, `LAB_0391..03E3`, around the patched entries),
S_19 `SECSTRT_19..03F0`, S_21 `SECSTRT_21..046B`, `LAB_0473..0490`, `LAB_0493..04A1` (RLE/PackBits/old decoder bodies), S_23 `LAB_04B6..04D7` (bodies behind the draw-cel patch),
S_29 `LAB_0530..054B`, `LAB_0554/0555`, `LAB_0558..056A` (text routines and the TRAP helpers), S_31 `LAB_0577/0578/057B/057C/057E..059C` (bodies behind the palette patches), plus single `SECSTRT_n` stubs.

mog: S_5 loader (now data), S_6/S_7 cells, S_9 hunk-9 stub (already data), **S_18 trackdisk driver** (`SECSTRT_18..0B16`, `LAB_0B19..0B21`, `LAB_0B26..0B34`),
S_20 `LAB_0B48`, `0B52..0B54`, `0B66..0B70`, **S_23 OFS reader bodies** (`LAB_0BB6..0C08` around the patched entries), S_24 `SECSTRT_24..0C15`, S_26 `SECSTRT_26..0CC7` (RLE/old decoder bodies),
S_28 `LAB_0CDC..0CFD` (bodies behind draw-cel), S_34 `LAB_0D55..0D70`, `0D79/0D7A`, `0D7D..0D82`, `0D86..0D89`, `0D8E..0D90` (text routines, TRAP helpers), S_36 `LAB_0DBF..0DC4`, `0E26`, `0E57..0E70`,
and the S_0 fragments `LAB_003B..003D`, `007D..0082`, `0087..0089`, `00DB/00DC`, `0101..010F` (disk prompt bodies), `02F4/02F5`, `030A..030C`, `03ED`, `0421`, `0443..0446`, `04A2`, `04A4`.

## 7. Open part of 4.7

* "program's hardware layer fully replaced": not true yet. Live program code still writes the blitter (outside the C++ cel/rect routines), copper/display setup (`SECSTRT_29`),
  Paula (the music player, 4.6), CIA/INTENA/INTREQ handlers (the IRQ handlers in asm; `rt/irq` only installs them), DMACON. That is 4.6 / 4.8 work, not removable-as-dead.
* Dropping the loader hunks and scoping `--verify` (section 4).
* Optional follow-up: stub mechanism removal (section 3) and the as_data conversions of section 6.

## 8. ROADMAP 7.1f2: the stub mechanism and the dead trampolines go

`tools/asm_remaining.py` (the section 1 model plus the trampoline graph) listed 14 `rt_*` definitions in `src/rt` that nothing live reaches. Checked one by one:

| Item | Verdict | What was done |
|---|---|---|
| `rt_audio_wait` (`src/rt/timing.cpp`, `asm/patches/program.timing.json`) | the two `JSR rt_audio_wait` sit in program `LAB_0072`, the tail of the ST/NT player's channel setup, dead since ptplayer is the only music path (7.1b) | `timing.cpp` and `program.timing.json` deleted; `abs_symbols.json` entry dropped |
| `rt_job_alloc` (`engine_jobs.cpp`) | the patch `job-alloc-cpp` is at program `LAB_01DA`, which no live line calls (the C++ spawner `ms::jobAlloc` is reached from `src/engine/anim.cpp`) | shim + `rtJobAlloc` deleted, patch `job-alloc-cpp` removed from `program.jobs.json`, `abs_symbols.json` entry dropped; `ms::jobAlloc` stays |
| `rt_job_run_script` (`engine_jobs.cpp`) | only a C++ declaration and the asm piece; the interpreter is C++ since 6.10 | piece and declaration deleted |
| `rt_ow_shop_apply` (`overworld.cpp`) | the patch `ow-shop-apply` is at mog `LAB_0E37`, whose only caller was the (patched) asm `LAB_0E17`; `rt_ow_arrive` calls `opShop` itself | shim + `rtOwShopApply` deleted, patch `ow-shop-apply` removed from `mog.overworld.json`, `abs_symbols.json` entry dropped |
| `rt_fighter_{knight,flyer,brawler,caster,dragon,dragon_part,drake,dagger,idle}` + `rtFighterEntry` (`fighters.cpp`) | nothing calls them: the creature dispatcher calls `rtFighterRun`; only `tests/test_fighters_emu.py` entered them | deleted from `fighters.cpp`; the test defines the same ten entries in its own link unit (`SHIM_ASM`), so the original-vs-C++ handler comparison is unchanged |
| `ms_call_asm` (`thunks.cpp`) | dead in the default build, **live in the `MS_LINK_LIFTED` build** (the lifted C++ calls back into asm through it, `include/ms/linked.hpp`) | **kept**; goes with `src/lifted/` and `thunks` in 7.1 (batch 6) |
| `rt_irq_stk3/4` (stack cells), `rt_sfx_release2/3` | not in the 14 / owned by 7.1d / 7.1b | untouched (7.1d owns `irq.cpp`) |

The logging stubs of ROADMAP 1.4 had no consumer since 4.7 (no bare name left in `abs_symbols.json "funcs"`): `rt_stub_hit`, `src/rt/stubs.cpp`,
`STUB_TMPL`, the `.rodata.rt_stub_names` table and the `rt_stub_hit` declaration of the generated `abs.h` are gone. `tools/resource.py` now raises a `PatchError` for a
`funcs` entry without `impl` (a bare name used to silently become a stub that logs and returns), so a patch that names an unimplemented `rt_*` function fails at generation
instead of at run time (`tests/test_resource.py`: `test_no_logging_stub_mechanism_is_left`, `test_a_bare_function_name_is_rejected`,
`test_generated_files_have_no_stub_text`).

The combat data loading path (`mog.combat_load`, S_0 `LAB_00F8..LAB_0155`) is C++ (`src/game/combat_load.cpp`, `src/rt/combat_load.cpp`, patches `asm/patches/mog.combat_load.json`,
26 entries `rt_cl_*`); `tests/test_combat_load.py` runs every routine against the original asm in unicorn. Left alone on purpose: `LAB_0100` (the disk prompt, a patched RTS, still called from
`LAB_00AF/016F/01A0/04BF`), `LAB_0113`/`LAB_0114` (one-instruction jumps). After the port the patches `files-drive-detect` (`LAB_0B18`) and `files-drive-init` (`LAB_0BB4`) have no caller
left (their only callers were `LAB_00F8` and the dead prompt bodies; the model still keeps `LAB_0B18` by fall-through from the trackdisk's `LAB_0B17`); the four `files-open/read/skip/close` patches still have live callers in the loaders (`LAB_0CB6`, `LAB_0CBB`,
`LAB_0C27`, `LAB_03DA`, the music loader `LAB_0AB5`): they go with 7.1f1.

## 9. ROADMAP 7.1 cleanup: dead patches and shims

The reachability model of section 1 (`tools/asm_remaining.py`, `analyse()`) was applied to the patch tables themselves: a patch is dead when its
`ps_<id>` marker line in the generated `asm/<bin>.s` is not one of the live lines of a live unit (the routine behind it has no live caller, or the marker
sits after an unconditional JMP/RTS of its unit). 149 patches (program 20, mog 129, the seven map stubs of step 1 included) were dead and are removed; the original text is back in the
generated asm, still unreachable. Check that proves nothing live changed: the text of every live line of every live unit is identical before and after
(1320 units, 0 differences), `py tools/resource.py --verify --no-write` stays green and `tools/asm_remaining.py` reports the same live set minus
the units that only the removed stubs reached.

Also removed:

* the map stubs `LAB_0DC8`, `SECSTRT_36`, `LAB_0E02`..`0E06` (patches `bm-colour-stop`, `bm-scene-setup`, `ow-mode-*`, `ow-random-spot`):
  `src/rt/combat.cpp` and `src/rt/loot.cpp` call `rtOwColourStop`, `rtOwSceneSetup`, `rtOwModeRestore`, `rtOwModeForce`, `rtOwModeAmbush`,
  `rtOwRandomSpot` directly (7.1l note);
* the `rt_*` entries that only those patches reached (336 -> 243 implemented `rt_*` functions in `include/rt/abs.h`): the `rt_ow_*` map shims, the creature dagger / blocked-mask / contact-damage
  shims, the sound pickers `rt_sound_*`, the place and town shims `rt_places_*` / `rt_town_*`, the dead `rt_scr_*`, the `rt_sfx_*` entries nothing calls,
  `rt_lzss_decode` (and `src/rt/engine.cpp`), `rt_rng_next`, `rt_irq_install`, `rt_mog_copy_rect`, `rt_mog_blob_load`, `rt_mog_hit_open`,
  `rt_loot_next_knight`, the enhanced wipe / palette-write shims;
* three patch files that became empty (`mog.blit.json`, `mog.fighters.json`, `program.intro.json`).

Kept on purpose, with no patch and no C++ caller, because a unicorn test compares them with the original asm: `rt_mog_again`, `rt_mog_job_find/kill/restart`
(`tests/test_mainloop_emu.py`; delete with the asm). The entries whose differential test lived on one shim moved into `tests/*_emu_support.cpp`
(`sfx`, `wipe`, `sprites`, `display`, `input`, plus `prims_emu_stubs.cpp` and the fighters test's own link unit): the tests keep running the C++ against the
original, the game no longer links the shim. The unicorn tests of the removed creature shims (`rt_blocked_mask`, `rt_dagger_*`, `rt_knight_contact_damage`)
and of the sound pickers `rt_sound_*` were deleted: `ms::game` is covered by `tests/test_creatures.py` / `test_fighters.py` against the lifted oracle,
the pickers by `test_script_ops` through `rtFightOpRun`.
