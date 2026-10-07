# Remaining asm: what is still live, grouped, with a porting order (ROADMAP 7.1 input)

> **ROADMAP 7.1r (2026-10-07): nothing is live.** `py tools/asm_remaining.py` reports 0 live units for both binaries: the overlay entries, the script-callback
> addresses and the data cells inside code hunks are C++ (docs/PATCHES.md, last section), and no asm is linked. The tables below are the 7.1 history;
> the only asm that can still be linked is mog's S_44 synth (`asm/synth.s`, CMake `MS_SYNTH_ASM`).

Analysis only, 2026-10-06. Nothing under `asm/patches/`, `src/`, `ROADMAP.md` or the build was touched. Everything here is
reproducible with `py tools/asm_remaining.py` (writes `build/inventory/asm_remaining.json`; `--md FILE` writes the
tables of the appendix; `--list mog|program` prints every live routine). `tests/test_asm_remaining.py` checks the model
(patched bodies are dead, dead loaders stay dead, a C++ trampoline nobody calls is not a root).

Counts are label units and instruction lines of the generated `asm/{program,mog}.s`; "~instr" is the smaller of the live
lines and the `routines.json` instruction count (IRA decoded some data tables as code, mostly mog S_44, which would
otherwise inflate it). Treat every number as +-10 %.

## 1. Method (DEAD_RT section 1, extended)

The reachability rules of `docs/DEAD_RT.md` section 1 are unchanged (unit = label up to the next label; edges = every
`prg_/mog_` name in a live line plus fall-through; `RTS/RTE/RTR/JMP/BRA` end a unit unless `(PC,Dn)`; `LAB+N` keeps a whole
unit; this is what makes the body behind a `JMP rt_x` patch dead). The 4.7 script was scratch, so `tools/asm_remaining.py`
re-implements it and adds five things the old model lacked:

1. **Trampoline graph.** The `asm(R"(...)")` blocks in `src/rt/*.cpp` are split at their `rt_*:` labels; each piece is a node with
   edges to the `prg_/mog_/rt_` names it mentions. A piece is live only if some live asm line (`JMP rt_x` patch) or some
   C++ use names it. 220 pieces exist, 204 are live. Before this, every `jsr prg_LAB_01F1` inside a never-called trampoline
   (`rt_job_run_script`) kept the asm alive.
2. **C++ roots are uses, not declarations.** Comments, `extern` lines and plain prototypes (`void rt_x(int);`) are stripped;
   `jobAddr(mog_LAB_xxxx)` (an identity key compared against job handlers, never called) is not a root. 15 asm labels are
   only ever declared in C++ (stale `extern`s; list in `asm_remaining.json`, `declared_only_roots`).
3. **Build option.** `MS_MUSIC_PTPLAYER` is evaluated as 1 (decided in 4.6); the `#else` "original player" path of
   `src/rt/audio.cpp` no longer counts. All other options (`MS_ENHANCED`, `MS_ACE_DISPLAY`, ...) keep both branches, so the
   result is a superset for the default build.
4. **Macro-built names.** `E(0185)` tables (`engine_intro.cpp`, `engine_anim.cpp`: the scene step lists resolve label ids through them)
   and `CEL_BIND(prg, LAB_04DE, ...)` (`engine_blit.cpp`, data cells) are expanded to the `prg_LAB_xxxx` names they build.
5. **Indirect-only flag.** A routine whose only live references are address-taking instructions or `DC.L` table entries
   (no call, no fall-through, no C++ root) is marked `*` in the appendix. These are job/script handler tables whose
   entries are intercepted by C++ dispatchers (`rtFighterRun`, the combat script engine, the anim interpreter). They are
   live in the model only because the table is, and are the first candidates for "prove it never runs, then delete".

Limits (all err on the side of "still live"): a C++ function that is never called but names an asm label is still a root;
`DC.L` tables keep every entry alive; a computed jump into the middle of a unit is invisible (none known); the S_0 group
boundaries below come from hunk roles (DOC_TECHNIQUE) and label ranges in source order checked by sampling code, not from
a call-graph proof, so edges at the borders of a range can be off by a routine.

Compared with DEAD_RT (569 / 559 dead label units then) the dead set is now 694 (program) and 990 (mog) of 1527 / 4147 label
units: the 6.x ports, the trampoline graph and the ptplayer decision account for about 550 more.

## 2. Result

| | program | mog | total |
|---|---|---|---|
| label units live / total | 833 / 1527 | 3157 / 4147 | 3990 / 5674 |
| live code routines (routines.json keys) | 206 | 611 | 817 |
| ~live instructions | 2950 | 10865 | 13815 |
| of which indirect-only (table entries) | 39 routines / 394 | 116 routines / 1442 | 155 / 1836 |
| live DATA/BSS label units | 307 | 1353 | 1660 |
| live DATA/BSS bytes | 71,204 | 100,062 | 171,266 |
| asm labels C++ calls or uses directly | 120 | 391 | 511 |
| patch sites (JMP-to-C++ and ACE/enhanced rewrites) in `asm/patches/*.json` | | | 245 |

Roll-up by the areas asked for (routines / ~instr; the 13 proposed tasks are in section 3):

| area | program | mog | total | indirect-only routines |
|---|---|---|---|---|
| IRQ handlers (INT1-6, keyboard, VBL hook list) | 12 / 249 | 13 / 253 | 25 / 502 | 12 |
| input (joystick/fire reads, mog S_0) | 0 | 6 / 87 | 6 / 87 | 0 |
| blitter primitives (IMAGEXCEL remnants, bg compositor) | 15 / 243 | 23 / 531 | 38 / 774 | 0 |
| display / copper / palette / hardware sprites | 34 / 564 | 41 / 611 | 75 / 1175 | 5 |
| text / font printing | 4 / 93 | 10 / 149 | 14 / 242 | 0 |
| file / disk layer (trackdisk residue, file stubs, loaders, load call sites) | 61 / 784 | 81 / 1262 | 142 / 2046 | 10 |
| music / sfx (program module player; mog entry + synth) | 5 / 100 | 130 / 1161 | 135 / 1261 | 24 |
| intro / ending scene bodies, anim/job handlers | 75 / 917 | 0 | 75 / 917 | 23 |
| fighter handlers, script ops (asm bodies of 6.4a/6.5/6.6) | 0 | 143 / 3088 | 143 / 3088 | 57 |
| screen loops / UI primitives behind trampolines (menu, combat, loot, town, places) | 0 | 100 / 2852 | 100 / 2852 | 22 |
| map drawing (overworld loop, node entries) | 0 | 46 / 669 | 46 / 669 | 0 |
| misc (boot skeleton, rules remnants) | 0 | 18 / 202 | 18 / 202 | 2 |
| **total** | 206 / 2950 | 611 / 10865 | 817 / 13815 | 155 |

What the numbers say:

* The hardware layer is small: blitter + display + IRQ + input + palette is about 2,500 instructions, and twin code
  (program S_23/25/29/15/31 against mog S_28/30/34/20/36) can be ported once for both binaries (ROADMAP 3.7).
* The bulk is mog S_0 game code that C++ already shadows: 143 routines / 3,088 instr of fighter and script handlers
  (57 of them indirect-only) and 100 / 2,852 of screen/set-up code reached from `rt/{combat,mainloop,scene_*}.cpp`
  trampolines. Much of the first block can be deleted rather than ported once the dispatchers are proven to bypass it.
* C++ names only 511 distinct asm labels, so porting a routine rarely needs a new trampoline; the other ~3,480 live label
  units are reached from those entries.
* 14 `rt_*` trampolines (plus 2 stack cells) are dead (section 5), `rt_job_run_script` and `rt_fighter_*` among them.

## 3. Groups

Group ids are `prg.*` / `mog.*`. A group is a set of live routines in one hunk (or one label range of mog S_0) that one
task can own. Columns: routines, ~instr, indirect-only (*), direct C++ callers (modules whose code or live trampolines
name a routine of the group), the groups its routines call into (routine edges), patch sites inside live code (`enh` =
`MS_ENHANCED` rewrites), hardware touched. The full table with labels is in the appendix; the short version:

| task | groups (hunk / label range) | routines | ~instr | C++ modules calling in | calls into |
|---|---|---|---|---|---|
| 7.1a text/font | prg.text (S_12), mog.text (S_0 0430-044D) | 14 | 242 | scene_menu (opDrawString/Text/Number) | blit_cel |
| 7.1b audio front end | prg.music (S_1), mog.sound_entry (S_16) | 28 | 224 | audio, introskip; combat, combat_script, fighters, loot | synth |
| 7.1c blitter remnants | prg.blit_cel/blit_copy (S_23/S_25), mog.blit_cel/blit_copy (S_28/S_30) | 29 | 485 | engine_anim, enhanced, combat, combat_script, overworld, scene_menu, mainloop | (loaders, 1 edge) |
| 7.1d IRQ / input / trackdisk | prg.irq (S_15), mog.irq (S_20), mog.input (S_0 00EA-00F7, 0575-057C), prg/mog.trackdisk (S_13 / S_18) | 43 | 734 | input; combat, overworld, scene_menu, fighters, hunk9 | display, file_stubs |
| 7.1e display / palette / copper | prg.display (S_29), prg.palette (S_31), mog.display (S_34), mog.palette (S_36 tail), mog.palette_load / display_ops / copper_fx (S_0 03EB-042F), mog.sprite_fx (S_37) | 75 | 1175 | engine_intro, engine_palette, enhanced; combat, scene_menu, mainloop, overworld, fighters | irq, blit_cel/copy |
| 7.1f loaders / file layer | prg.loaders (S_8/20/21), mog.loaders (S_25/26), mog.combat_load (S_0 0114-0155), mog.files_prompt (00F8-0113), prg/mog.file_stubs (S_18 / S_23) | 130 | 1901 | enhanced, files; combat, mainloop, overworld, scene_town | file_stubs |
| 7.1g synth | mog.synth (S_44) | 107 | 1037 | engine_palette (fade `LAB_0FC2`) | none (leaf) |
| 7.1h fighters + script ops | mog.fighters (S_0 01C6-02F1), mog.fighter_handlers (S_40), mog.script_ops (0358-039C) | 118 | 2804 | combat, combat_script, fighters, hunk9, mainloop | job_frame, sound_entry, boot_loop |
| 7.1i intro/ending scenes | prg.scenes (S_0), prg.anim_jobs (S_10) | 75 | 917 | engine_intro, engine_anim, engine_jobs, introskip, enhanced, game | display, palette, blit |
| 7.1j combat/loot screens | mog.combat_setup, mog.combat_ui, mog.loot_ui (S_0 0156-01C5, 044E-0457, 04CF-05A1) | 76 | 2153 | combat, loot, mainloop, scene_town, fighters | combat_load, palette_load, fighters, text, input |
| 7.1k menu / places / town screens | mog.menu, mog.places, mog.town_ui (S_0 00B4-00E9, 0458-04A0, 04A6-04CE) | 24 | 699 | hunk9, mainloop; overworld, scene_places | display_ops, text, sound_entry, palette_load |
| 7.1l map / boot / rules | mog.map (S_36), mog.overworld_nodes (S_0 0036-00B3), mog.boot_loop (0001-0012, 04A1-04A5), mog.rules (0013-0035) | 64 | 871 | mainloop, overworld, loot, audio, game | combat_load, combat_setup, palette_load, map |
| 7.1m contact / job / bg blit | mog.contact (S_0 039D-03EA), mog.job_frame (02F2-0357), mog.bg_blit (S_12) | 34 | 573 | combat, creatures, fighters, mainloop, overworld, hunk9, scene_menu | script_ops, blit_copy |

Notes per group that matter for the plan:

* **Twins.** program S_15 / mog S_20 (IRQ), S_23 / S_28 (draw cel), S_25 / S_30 (copy rect + init), S_20+21 / S_25+26
  (loaders), S_18 / S_23 (file stubs), S_13 / S_18 (trackdisk), S_29 / S_34 (custom init), S_31 / S_36 tail (palette): port
  each pair as one C++ body parameterised by binary (ROADMAP 3.7, `tools/thunks.py`), one task per pair, not two.
* **Enhanced-mode patches** (`enh_*`) sit inside 7.1c, 7.1e, 7.1f, 7.1i, 7.1m: porting those routines means folding
  `rt_enh_planes` into the C++ body and retiring the patch ids. All of them live in `asm/patches/{program,mog}.enhanced.json`;
  two tasks of one batch editing that file by id is a trivial merge, but say so in the task text.
* **ACE display (4.8).** `ace_*` patches (copper init, show/swap, setup) are in 7.1e. The task should first make
  `MS_ACE_DISPLAY` the default (4.8's own "after a full playthrough"), then delete the OFF path, otherwise the asm
  display init cannot go.
* **mog S_44 (7.1g)** keeps 4,674 bytes of instrument/sequence tables and voice state inside the CODE hunk (IRA decoded them
  as instructions) next to S_45 (2,016 bytes of waveforms, CHIP). That state is the 2.12 self-modifying area; do not
  port the synth without settling 2.12 (overlay re-entry).
* **7.1h / indirect-only.** 57 of its 118 routines are reached only through handler tables (fighters 30, script ops 19,
  S_40 handlers 8). The first step of 7.1h is a runtime trace (log which table entries are ever executed in Practice, lair,
  dragon, PvP), then delete what `rtFighterRun`/the script engine never lets run.
* **prg.irq / mog.irq** are installed by the `irq_install` patch (PEA of the six handler labels); they are the only live
  code that still touches CIA/INTENA/INTREQ/vectors directly. The trackdisk residue is live only through call chains from the VBL
  service (`SECSTRT_15` -> `LAB_0312-0316`, drive motor-off timeout) and the drive-detect/disk-prompt code, nothing the C++ file layer
  needs (INT1/5/6 are not installed by `rt/irq`); about 145 instr go with 7.1d.

## 4. Porting order: batches of independent tasks

Rule for "independent": different asm hunks or label ranges (no routine in two tasks), different new C++ module and patch
file, no task needs a result of another in the same batch. Where two tasks of a batch touch the same shared file
(`*.enhanced.json`, `abs_symbols.json`, `CMakeLists.txt`), they only add/remove their own entries by id. Order is
bottom-up: leaf primitives with clean register contracts first, so later ports call C++ directly instead of adding
trampolines. Every task ends the way 4.x/6.x tasks did: host differential test (lifted 68k or asm model) + a
`build/shots/` boot screenshot, `check.py` green, new patch file under `asm/patches/`, groups' abs_symbols entries
dropped when the last user goes.

### Batch 1: leaf primitives, no hardware ownership change

**7.1a Text/font printing** (`prg.text`, `mog.text`; 14 routines, ~242 instr).
* Scope: program S_12 `LAB_028F, 0291, 0297, 02A0` (string and number to glyph cels: glyph lookup through the `LAB_00F8` character map and the
  `LAB_011A` font record); mog S_0 `LAB_0431, 0432, 0435, 043B, 043D, 0442+, 0447, 0448, 044B` (draw string, draw text list, draw
  number; `fmt_num3` already C++).
* Entry from C++: mog `LAB_0431/0432/0442` from `rt/scene_menu.cpp` (`opDrawString/opDrawText/opDrawNumber`); program has no C++
  caller (scenes in asm call it; it dies with 7.1i or keeps one trampoline until then).
* Dependencies: `draw_cel` (already C++ in `engine/blit`); data: font cel tables and the string tables of mog S_4 / program S_2
  stay asm data for now (read by symbol).
* New: `src/engine/gtext.cpp` (pure glyph-run planner), shim `src/rt/engine_text.cpp`, patch files `{program,mog}.text.json`.
* Verify: host test over random strings/numbers vs the lifted routines (all four are leaves of `draw_cel`), the menu and
  knight-select screens.
* Why first: smallest group, no hardware, and it unblocks the UI ports (combat_ui, town_ui and places routines call it 7, 7 and 6 times).

**7.1b Audio front end** (`prg.music`, `mog.sound_entry`; 28 routines, ~224 instr).
* Scope: program S_1 (`LAB_005B/005C/0061/0083`, `SECSTRT_1`: stop/start glue that `introskip` and the scene stepper still
  call; most of the 1,940-byte player is dead under ptplayer) and mog S_16 (`SECSTRT_16`, `LAB_0A9B-0AB5`: start/stop/free-channel,
  `LAB_0AA2` sound request with ~60 callers, `LAB_0AA9` all-stop, `LAB_0AA7` soundtracker start).
* Entry from C++: `audio.cpp`, `introskip.cpp` (program); mog `LAB_0AA2` from `combat_script`, `fighters`, `loot`; `LAB_0AA9`
  from `combat`.
* Do: make `MS_MUSIC_PTPLAYER` unconditional and delete the original-player path and `audio_wait_*` patches; port the sound
  request / channel-allocation logic to `rt::sfx` (round-robin over channels not in `LAB_0AA6`, state moves to C++), still
  starting sequences through the synth entry `LAB_0F8C` via one trampoline until 7.1g.
* Patch sites retired: `audio_music_start/stop`, `int4_vector` stays until 7.1g (INT4 handler is the synth's).
* Touches: `program.audio.json`, `mog.audio.json`, `src/rt/audio.cpp`. Data owned: mog S_17 (68 B channel-busy flags).

**7.1c Blitter remnants, both binaries** (`prg/mog.blit_cel`, `prg/mog.blit_copy`; 29 routines, ~485 instr).
* Scope: program S_23 `LAB_04A6, 04A7, 04A8, 04B0, 04B4`, `SECSTRT_23`; S_25 `LAB_04E1-04E4, 04EC, 04EE, 04F5, 04F6`,
  `SECSTRT_25`; mog S_28 `LAB_0CCC, 0CCD, 0CCE, 0CD6, 0CDA`, S_30 `LAB_0D07-0D1B`, `SECSTRT_28/30` (clip setup, bit-reverse table, mirror/mask kernels, the
  IMAGEXCEL init that carves the work buffers, blitter wait, A-to-D copy).
* Entry from C++: `engine_anim` (`draw_cel`), `enhanced`, `combat`, `combat_script`, `overworld`, `scene_menu`, `mainloop`
  (`SECSTRT_30`).
* Do: finish 4.4 by moving the remaining entry points into `src/engine/blit.cpp`/`src/rt/engine_blit.cpp` (planner exists,
  `draw_cel`/`copy_rect` already patched), fold the `enh_mirror` and `enh_temp_buffer` rewrites in, one parameterised body for
  both binaries. Pixel compare via 3.8 is the open verification item of 4.4; do it here.
* Data owned: program S_24 / mog S_29 (26 B blitter state), S_26 / S_31 (1,058 B bit-reverse table + copyright string), the
  45,744 B CHIP scratch BSS (S_27 / S_32) shared with 7.1f.
* Patch files: `{program,mog}.blit.json` shrink; `enh_mirror`, `enh_temp_buffer` leave `*.enhanced.json`.

Independence of batch 1: a = S_12 + S_0 0430-044D, b = S_1 + S_16, c = S_23/25 + S_28/30; three different hunks, three
patch files, only c touches `*.enhanced.json`. a calls c's `draw_cel` through the existing C++ entry, not the asm body.

### Batch 2: hardware-facing groups (each pair of twins as one task)

**7.1d IRQ handlers, keyboard/joystick, trackdisk residue** (`prg.irq`, `mog.irq`, `mog.input`, `prg.trackdisk`,
`mog.trackdisk`; 43 routines, ~734 instr, 15 indirect-only).
* Scope: program S_15 `LAB_0325-0360` / mog S_20 `LAB_0B46-0B84` (INT1-6 handlers, keyboard decode, VBL hook list, `SECSTRT_15/20`), mog S_0 `LAB_00EA-00F3`
  (joystick/fire waits, `00EE` reads JOY0/1DAT + CIAA), `LAB_0575/057B`; trackdisk residue program S_13 `LAB_02EE, 02FF, 030F, 0314-0317`, mog
  S_18 `LAB_0B18-0B3A` (motor/step/CIA-B; live through the VBL service `SECSTRT_15` motor-timeout chain and the drive-detect/disk-prompt code).
* Entry from C++: `input.cpp` (`LAB_032A`/`LAB_0B4E` key ISR tail), mog `LAB_0B46`, `0B82` from combat/overworld/scene_menu,
  `00EC/00EE` from combat, mainloop, overworld, fighters, hunk9, scene_menu.
* Do: INT2/INT3/INT4 handlers become C++ (`rt/irq.cpp` already owns the frame, the private stacks and installation; level 1/5/6
  stay uninstalled), the VBL hook list `LAB_0372` / mog `LAB_0B96` becomes a C++ array, joystick/mouse reads become `rt/input`.
  Delete trackdisk and the CIA-B code; `irq_install` patch goes.
* Verify: the existing `irq_selftest` (500 VBL = 500 beam frames), keyboard A/Space/Return tables (2.3), joystick in
  knight select, intro skip, `tests/test_irq.py` rewritten for the C++ handlers.
* Data owned: program S_16 / mog S_21 (328 B handler pointers and flags), S_17 / S_22 (20 B scancode/key state), S_14 / S_19.
* Risk: highest of the hardware tasks (VBL timing, nested INT4 for the synth: keep the INT4 hand-off through
  `rt_irq_set_int4` until 7.1g).

**7.1e Display, copper, palette, hardware sprites** (8 groups; 75 routines, ~1,175 instr).
* Scope: program S_29 (`SECSTRT_29`, `LAB_054C-0567` custom-chip init, beam wait, screen swap), S_31 (`LAB_0576-05CD`, `SECSTRT_31`:
  palette wipes and transitions, colour helpers; set/ramp/tick already C++), mog S_34 (`SECSTRT_34`, `LAB_0D71-0D8D` screen clear,
  text-cell pokes, copper init), S_36 tail `LAB_0E53-0E5D`, S_37 (`LAB_0E75-0E85`, sprite DMA on/off and a table-driven sprite build, called from the joystick-cursor code; role inferred),
  mog S_0 `LAB_03EB-042F` (palette load/fade glue `03F0-0413`, fight-screen init, sprite display/flip `0416`, background blit
  entries `0418/0419`, copper effects `0427/042A`).
* Entry from C++: `engine_intro`, `enhanced`, `engine_palette`; `combat`, `scene_menu`, `mainloop`, `overworld`, `fighters`.
* Do: this is 4.8 finishing: `MS_ACE_DISPLAY` default ON, then the game's own copper list, DIW/DDF/BPLCON/COP1LC writes, bitplane
  pointer pokes and DMACON toggles disappear (ACE `copBlock`/`simpleBuffer` own them), palette words become CPU writes through
  `palette_aga`/`palette_enh`, hardware sprite via the ACE sprite manager (confirm what `LAB_0E78` builds first). Retires `ace_*`, `copper_ptr`, `dmacon_cleared`,
  `cmpa_clear`, `enh_pal_*`, `enh_copy_*`, `enh_clear_count`, `enh_wipe_*`, `irq_enable/disable`, `intro_skip_poll` sites.
* Verify: screenshots against `build/shots/` baseline (logo, intro fades, menu, fight, map), 50 Hz per scene (7.2 numbers),
  fight trail 6.0a must be understood first (it is in this layer).
* Data owned: program S_30 / mog S_35 (332 B screen pointers and copper template, CHIP), S_32 / S_39 (180 B palette constants),
  program S_33 (3,940 B fade state), mog S_38.

**7.1f Asset loaders and the file layer** (6 groups; 130 routines, ~1,901 instr, 7 indirect-only).
* Scope: program S_8 (`LAB_0163-0174, 0185, 018E, 01AA-01BE`, `SECSTRT_8`: file-name table dispatcher, `asset_file_load`, `intro_run`),
  program S_20/S_21 (`LAB_03F2-0402` picture glue, `LAB_046C-0496` cel loader, `LAB_049C` LZSS entry, `SECSTRT_20`), mog S_25/S_26
  (`LAB_0C17-0C27` picture loader, `0C91-0CC2` cel/ob loader), mog S_0 `LAB_0114-0155` (per-creature/arena asset loads:
  `combat_assets_load 012C`, collide.hit, creature cels, arena packs), `LAB_00F8-0113` (drive init, disk prompts), and the 1-instruction
  file stubs program S_18 (14) / mog S_23 (19) that are the targets of every loader `JSR`.
* Entry from C++: `enhanced` (pic/pack readers), `files`; `mainloop` (13 entries), `combat`, `overworld`, `scene_town`.
* Do: all loaders become C++ calls into `rt/files` (`ms::loadPicture`, `loadCel`, `loadSoundBank`, `loadArena`), the enhanced
  pack walk (`enh_pack_*`, nine copy-count patches) becomes the only code path, the stubs and `files_*` patches vanish with
  their last caller. Deleting the stubs is the closing step, so this is the largest task of batch 2: split it into 7.1f1 (program S_8/S_20/S_21 +
  mog S_25/S_26 loaders, twins) and 7.1f2 (mog S_0 combat_load/prompts, then stub deletion) if one agent per task is the limit.
* Data owned: program S_22 / mog S_27, the 45,744 B CHIP scratch (shared with 7.1c; whoever lands second frees it); program S_8 holds
  the file-name table that `rt/files` already mirrors.

Independence of batch 2: d = S_15/S_20 + S_13/S_18 + two S_0 ranges, e = S_29/31/34/36t/37 + S_0 03EB-042F, f = S_8/20/21/25/26 + S_18/S_23 +
S_0 0114-0155/00F8-0113. Callers cross groups (IRQ tails call display routines, display init calls the IRQ enable): those are
asm-to-asm `JSR`s that keep working when either side is ported (the callee gets a `JMP` patch), so no ordering needed.
Shared file: `*.enhanced.json` between e and f.

### Batch 3: independent engines

**7.1g Synth driver** (`mog.synth`, mog S_44; 107 routines, 1,037 instr, 4,674 B embedded tables).
* Scope: `LAB_0F66-0F8F` voices, `0F89` init, `0F8C` start sequence, `0F8F` voice loop, `0F73/0F75/0F7B` tick, envelopes,
  period/volume registers, `0F69` INT4 reload, `0FC2` fade, `0FD3/0FD4` relocation, sequence and instrument tables up to `1090`.
* Entry from C++: `engine_palette` (`LAB_0FC2` fade); from asm `sound_entry` (until 7.1b's trampoline calls C++).
* Do: C++ four-voice sequencer writing `AUDxLC/LEN/PER/VOL` (ACE `audio` helpers or direct), tables regenerated as `static const`
  (`tools/gen_tables.py`, sequence table `LAB_1098`, instrument table `LAB_10A2`, S_45 waveforms into CHIP), voice state in C++
  BSS (this also closes the self-modifying part of 2.12). A/B listening against the original is the acceptance test.

**7.1h Fighter handlers and combat script ops** (`mog.fighters`, `mog.fighter_handlers`, `mog.script_ops`; 118 routines,
~2,804 instr, 57 indirect-only).
* Scope: mog S_0 `LAB_01C6-02F1` (knight/flyer/brawler/caster/dragon/drake handlers `01CA, 0226, 0236, 0251, 027A, 0298, 029F,
  02CB`, hurt reactions `01E1-0206`, dagger code `02AC-02E9`), S_40 (`SECSTRT_40`, `LAB_0EA1-0F34`: creature handler bodies),
  `LAB_0358-039C` (combat script op handlers).
* Entry from C++: `combat`, `combat_script`, `fighters` (`rtFighterRun` falls back to asm for entries it does not own), `hunk9`,
  `mainloop`.
* Step 1 (mostly deletion): a trace build logs every handler/op address the dispatchers actually run in Practice, lair, dragon
  and PvP; table-only entries that never run are deleted (the 57 above are the candidates). Step 2: port what the trace shows is
  still executed (movement helpers, sound pickers, flyer/drake/dragon part handlers); data owned: S_41 (76 B).
* Patch sites: the 14 `creatures_*`/`fighters_sound_*` patches in live code.

**7.1i Intro/ending scene bodies and animation job handlers (program)** (`prg.scenes`, `prg.anim_jobs`; 75 routines, ~917 instr,
23 indirect-only).
* Scope: program S_0 (`LAB_0003-0054`, `SECSTRT_0`: frame loops, backdrop switch, scene bodies `001A/001B/001C/001D/001F/002C-0032/0036/0037/0039/003B`,
  credit overlay, the S_0 entry that still calls the S_29/S_25/S_10 inits; 35 routines / ~506 instr) and S_10 (`LAB_01E4-026E`, 40 routines / ~411 instr: script op handlers `0215-023B`, sprite blit
  list `0242`, palette blackout `0258`, double-buffer flip `0262`, inits).
* Entry from C++: `engine_intro`, `engine_anim`, `engine_jobs`, `introskip`, `game`, `enhanced`.
* Do: finish 6.10: the interpreter and all handlers are already C++ (`engine/anim.cpp`), so the asm handler addresses are only
  table-fill; move the scene bodies into the step lists of `engine/intro.cpp`, replace `SECSTRT_0` by `rtGameRun` calling C++
  inits directly. Program-only; the only task of the batch that touches `program.*` patches.
* Verify: `build/shots/h-*.png` vs `o-*.png` timeline (9/15/23/38/53 s), ending re-entry (2.12).

Independence of batch 3: g = mog S_44 (+S_45 data), h = mog S_0 01C6-02F1/0358-039C + S_40, i = program S_0 + S_10. No shared
hunk, binary-disjoint for i, different patch files (`mog.audio`/`mog.fighters+creatures`/`program.{intro,anim,jobs}`).

### Batches 4-6 (sketch)

* **Batch 4: screens that call the now-C++ primitives.** 7.1j combat set-up, combat/loot screens (76 routines, ~2,153 instr);
  7.1k menu, places, town screens (24 / ~699); 7.1l overworld map + boot skeleton + rules remnants (64 / ~871) with 7.1m
  contact/job/frame/bg blit (34 / ~573) as the fourth task (it is a leaf for 7.1j, so it can also move to batch 3's slot if
  7.1i slips). Together these retire most `rt/*.cpp` trampolines.
* **Batch 5: data, per hunk family (7.1n).** See section 6; code readers are gone by now, so each task only moves data.
* **Batch 6:** the old 7.1 text itself: delete `asm/`, `thunks`, `regs.hpp`, `tools/resource.py` asm emission, the vasm step,
  `MS_LINK_GAME_ASM`/`MS_LINK_LIFTED`, `src/lifted/`, `image_tab`/`hunk_tab`, the remaining `rt_` shims (section 5).

## 5. Findings outside the groups

* Dead trampolines (defined in `src/rt/*.cpp`, reachable from nothing live): `rt_audio_wait` (ptplayer build),
  `rt_fighter_{knight,flyer,brawler,caster,dragon,dragon_part,drake,dagger,idle}`, `rt_job_alloc`, `rt_job_run_script`,
  `rt_ow_shop_apply`, `ms_call_asm` (thunks only): 14. The two `rt_irq_stk3/4` hits are stack cells, not code. They and their `abs_symbols.json` entries
  can go with the next generator touch (this is 4.7 follow-up, not 7.1).
* Stale `extern` declarations (15 labels: program `LAB_01F1`, `LAB_059D`; mog fighter handlers
  `LAB_01CA/0226/0236/0251/027A/0298/02CB/02D2/0302` used only as identity keys, plus `0648`, `06C2`, `08CC`, `0E21`) are listed in
  `build/inventory/asm_remaining.json` under `declared_only_roots`.
* S_4..S_7 loader hunks (program) and S_5..S_7 (mog) are inert data (`ldr-dead`), the trackdisk body of program S_13 and mog S_18 is
  mostly dead; the hunk-drop and `--verify`-scoping item of 4.7 is a prerequisite for 7.1 (renumbering hunks) and can run in any
  batch; it only touches `tools/resource.py`, `image_tab`, `tests/test_resource.py`.

## 6. DATA/BSS still reached through `prg_`/`mog_` symbols (input for 7.1n)

All label units of data hunks that are live (a live asm line or a C++ use names them). "C++ syms" = symbols C++ names directly
(`extern` + use); the rest is read only by asm and moves with the code that reads it. "asm users" = the groups whose live
routines name the hunk's symbols (top three, count of distinct symbols). Typed today: 29 symbols in
`include/game/state_bind.hpp`, 21 curated tables in `tools/tables.yaml` (5.2).

| hunk | kind | live units | bytes | C++ syms | C++ modules | asm users | contents / target form |
|---|---|---|---|---|---|---|---|
| prg S_2 | DATA | 120 | 12,288 | 31 | engine_anim, engine_intro, enhanced, introskip | scenes 32, palette 9, text 8 | credits/UI strings, file-name table: `gen_tables` strings + ref tables |
| prg S_3 | BSS | 12 | 164 | 10 | audio, engine_*, enhanced | scenes 10, palette 2 | program globals (scene type, flags, module pointer): one C++ struct |
| prg S_9 | BSS | 11 | 520 | 5 | engine_anim | scenes 11 | loader buffers / UI state: C++ BSS |
| prg S_11 | BSS | 26 | 6,328 | 16 | engine_anim, engine_jobs | anim_jobs 12 | job pool and tables (already `struct Job`): C++ static, static_assert layout |
| prg S_14 | DATA | 4 | 24 | 0 | | trackdisk 1 | trackdisk state: delete with 7.1d |
| prg S_16 | DATA | 19 | 328 | 3 | input | irq 19 | IRQ flags, handler pointers, VBL hook list: C++ with 7.1d |
| prg S_17 | BSS | 7 | 20 | 1 | engine_anim, input | irq 6 | key state, scancode buffer: C++ with 7.1d |
| prg S_22 | DATA | 2 | 10 | 0 | | loaders 2 | loader constants |
| prg S_24 | DATA | 8 | 28 | 4 | engine_anim, engine_blit, enhanced | blit_cel 6 | blitter state (modulos, saved clip): with 7.1c |
| prg S_26 | DATA | 50 | 1,058 | 7 | engine_blit, enhanced | blit_copy 25, blit_cel 6 | bit-reverse table + IMAGEXCEL text: generated `static const` |
| prg S_27 | BSS CHIP | 2 | 45,744 | 0 | | loaders 2, blit_copy 1 | blitter/picture scratch, CHIP: `memAllocChip` at init |
| prg S_30 | DATA CHIP | 9 | 332 | 1 | engine_anim | display 5 | screen pointers + copper template: becomes ACE `copBlock` (7.1e) |
| prg S_32 | DATA | 7 | 180 | 7 | engine_palette, enhanced | palette 1 | palette constants: `static const` |
| prg S_33 | BSS | 19 | 3,940 | 2 | engine_intro, enhanced | palette 18 | fade/ramp/cycle state: C++ struct (palette engine already owns the logic) |
| mog S_1 | BSS | 249 | 8,780 | 128 | 11 modules | combat_setup 72, combat_load 30, fighters 27 | **game state** (knights, inventories, map, jobs, scheduler): `ms::game::State`, offsets static_asserted; last to move |
| mog S_2 | DATA | 5 | 160 | 5 | overworld | map 1 | map constants |
| mog S_4 | DATA | 837 | 39,320 | 75 | 11 modules | combat_setup 238, combat_load 83, fighters 63 | tables, menus, names, animation/action scripts, strings: extend `tables.yaml` family by family; scripts as typed arrays |
| mog S_10 | DATA | 12 | 156 | 5 | creatures, mainloop | contact 6 | contact tables |
| mog S_11, 13-15 | BSS/DATA | 37 | 336 | 4 | enhanced | bg_blit 36 | arena background compositor state: with 7.1m |
| mog S_17 | DATA | 13 | 68 | 0 | | sound_entry 12 | channel flags: with 7.1b |
| mog S_19, 21, 22 | DATA/BSS | 30 | 372 | 5 | combat, input, overworld, scene_menu, combat_script | irq 25 | trackdisk state, handler pointers/flags, key state: with 7.1d |
| mog S_27, 29, 31, 32 | DATA/BSS | 62 | 46,840 | 11 | combat, combat_script, engine_blit, enhanced, scene_menu | blit_copy 26, blit_cel 12, loaders 8 | twins of prg S_22/24/26/27: with 7.1c/7.1f |
| mog S_35, 38, 39 | DATA CHIP/DATA | 16 | 552 | 8 | combat, combat_script, engine_palette, enhanced, scene_menu | display 6, display_ops 3 | twins of prg S_30/32, S_38 sprite data: with 7.1e |
| mog S_41 | DATA | 11 | 76 | 1 | fighters | fighter_handlers 11 | handler tables: with 7.1h |
| mog S_42 | DATA | 36 | 1,096 | 4 | scene_town | town_ui 26 | town/exchange text and tables: with 7.1k |
| mog S_43 | BSS CHIP | 1 | 80 | 0 | | loot_ui 1 | loot cursor sprite data |
| mog S_45 | DATA CHIP | 22 | 2,016 | 0 | | synth 14 | waveforms: `static const` in CHIP section, 7.1g |
| mog S_44 (inside CODE) | embedded | ~4,674 B | | 0 | | synth | instrument/sequence tables + voice state decoded as code: 7.1g |
| program S_0, S_10, S_31 and mog S_0, S_16, S_18, S_37, S_40 inline cells | in CODE | 33 | ~320 | 9 | combat, creatures, enhanced, fighters, loot, engine_anim | | inline words and scratch cells inside code hunks (program S_0 scene cells 206 B, mog S_0 74 B, ...): become C++ state with their readers |

Structure of the move (7.1n): (1) tooling: `tools/gen_tables.py` handles curated tables with typed rows; for the 837 units of mog S_4 add
a family list (menus, names, node/lair tables, animation scripts, text lists) so every unit is either generated, typed state, or
proved unreferenced; pointers inside data become `const T*` constant initialisers (no global constructors run, so no
runtime relocation). (2) State: mog S_1 and program S_3/S_11/S_33 become C++ globals with the same field offsets until the last asm reader is
gone, then plain structs. (3) CHIP data (S_30/35 copper+screens, S_27/32 scratch, S_38/43/45) uses ACE chip allocation or a `.chip`
section attribute; both `rt_screen_work` (82 KB, 2.4d) and the arenas must be re-checked against 2.11. (4) Twin hunks (program S_16/17/22/24/26/27/30/32 against mog S_21/22/27/29/31/32/35/39) are
generated once.

Suggested data tasks (batch 5, three in parallel, then a fourth): **7.1n1** program data + engine twins (program S_2/3/9/11/16/17/22/24/26/27/30/32/33 and
mog twins), **7.1n2** mog S_4 tables family by family (extends 5.2), **7.1n3** mog S_1 state and the small mog hunks, **7.1n4** CHIP data
(screens, copper, scratch, waveforms) after 7.1e/7.1g.

## 7. Proposed ROADMAP text (not applied; ROADMAP.md is untouched)

Under 7.1, keep its current line as the final step and add, in this order, `[ ]` items:

* **7.1a** Text/font printing to C++ (program S_12, mog S_0 0430-044D); leaf of `draw_cel`; patch files `{program,mog}.text.json` - S
* **7.1b** Audio front end: make ptplayer unconditional (delete the original-player path, `audio_wait_*`), port mog `LAB_0AA2` sound request and
  channel start/stop (S_16) to `rt::sfx`, synth entry through one trampoline until 7.1g - S
* **7.1c** Blitter remnants for both binaries (S_23/S_25, S_28/S_30): clip, bit-reverse table, mirror/mask kernels, IMAGEXCEL init, blitter
  wait; fold `enh_mirror`/`enh_temp_buffer` in; closes 4.4's pixel-compare item (3.8) - M
* **7.1d** IRQ handlers, keyboard/joystick reads and the trackdisk residue (program S_15/S_13, mog S_20/S_18, mog S_0 00EA-00F3/0575-057C) to C++ on
  `rt/irq`; retire `irq_install` - M
* **7.1e** Display/copper/palette/hardware-sprite layer (program S_29/S_31, mog S_34/S_36 tail/S_37, mog S_0 03EB-042F): make `MS_ACE_DISPLAY` the
  default, delete the game's copper/DMA/bitplane code and the `ace_*`, `enh_pal_*`, `enh_wipe_*` sites; needs 6.0a understood - L
* **7.1f** Asset loaders and the file layer (program S_8/18/20/21, mog S_23/25/26, mog S_0 0114-0155 and 00F8-0113): loaders call `rt/files`
  from C++, enhanced pack walk becomes the only path, file stubs and `files_*` patches removed (split f1 loaders / f2 combat_load + stubs) - L
* **7.1g** Synth driver (mog S_44/S_45): C++ four-voice sequencer, tables generated, voice state out of the CODE hunk; settles 2.12's
  self-modifying part - L
* **7.1h** Fighter handlers and combat script op bodies (mog S_0 01C6-02F1, 0358-039C, S_40): trace which table entries run, delete the rest, port the
  remainder behind `rtFighterRun` - L
* **7.1i** Intro/ending scene bodies and anim job handlers (program S_0, S_10): scenes as step lists, `SECSTRT_0` replaced by C++ inits (finishes
  6.10) - M
* **7.1j** Combat set-up and combat/loot screens (mog S_0 0156-01C5, 044E-0457, 04CF-05A1) - L
* **7.1k** Menu, places and town screens (mog S_0 00B4-00E9, 0458-04A0, 04A6-04CE) - M
* **7.1l** Overworld loop and map drawing, boot skeleton and rules remnants (mog S_36, S_0 0001-0012/0013-00B3/04A1-04A5) - M
* **7.1m** Contact/collision, job and frame primitives, arena background blit (mog S_0 039D-03EA/02F2-0357, S_12) - S
* **7.1n** Asm DATA/BSS to C++ data (n1 program + twins, n2 mog S_4 tables, n3 mog S_1 state + small hunks, n4 CHIP data) - L
* **4.7 follow-up** (not a 7.1 sub-task): drop the S_4..S_7 loader hunks, key `--verify`/`image_tab` by section name, delete the 14 dead trampolines.

Batches: **1** = 7.1a, 7.1b, 7.1c; **2** = 7.1d, 7.1e, 7.1f; **3** = 7.1g, 7.1h, 7.1i; **4** = 7.1j, 7.1k, 7.1l (+7.1m); **5** = 7.1n1-n3; **6** = 7.1n4 and 7.1.

Re-run `py tools/asm_remaining.py` after each batch: the totals in section 2 are the burn-down chart (target 0 live code routines, then
the data table of section 6 empty).

# Appendix (generated by `py tools/asm_remaining.py --md`; label(instr), `*` = indirect-only, `^` = named from C++)

### Group summary (generated by `py tools/asm_remaining.py --md`)

| group | area | routines | ~instr | addr-only | direct C++ callers | calls into (routine edges) | patch sites | hw |
|---|---|---|---|---|---|---|---|---|
| `prg.scenes` | intro scene bodies | 35 | 506 | 3 | engine_anim, engine_intro, enhanced, game | anim_jobs 49, display 12, palette 7, file_stubs 4 | 16 (3 enh) | vector |
| `prg.music` | music/sfx | 5 | 100 | 1 | engine_intro, introskip |  | 2 | custom |
| `prg.anim_jobs` | intro scene bodies | 40 | 411 | 20 | engine_anim, engine_intro, engine_jobs, introskip | palette 3, display 3, blit_copy 2, blit_cel 2 | 8 (5 enh) | custom |
| `prg.text` | text/font | 4 | 93 | 0 |  | blit_cel 1 | 0 |  |
| `prg.trackdisk` | file/disk | 6 | 75 | 2 |  | display 1 | 0 | abs,cia,custom |
| `prg.irq` | IRQ handlers/input | 12 | 249 | 6 | input | display 3, file_stubs 2, trackdisk 1 | 1 | cia,custom,vector |
| `prg.file_stubs` | file/disk | 14 | 14 | 0 | enhanced, files |  | 4 |  |
| `prg.loaders` | file/disk | 41 | 695 | 4 | engine_intro, enhanced | file_stubs 15, anim_jobs 7, palette 4, scenes 3 | 6 (4 enh) |  |
| `prg.blit_cel` | blitter | 6 | 102 | 0 | engine_anim | blit_copy 1 | 2 (1 enh) | custom |
| `prg.blit_copy` | blitter | 9 | 141 | 0 | enhanced | loaders 1 | 3 (1 enh) | custom |
| `prg.display` | display/copper | 11 | 126 | 0 | engine_intro, enhanced | irq 1 | 11 (2 enh) | abs,custom |
| `prg.palette` | display/copper | 23 | 438 | 3 | engine_intro | anim_jobs 23, display 5, scenes 4, blit_copy 3 | 6 (3 enh) | custom |
| `mog.boot_loop` | misc | 12 | 72 | 2 | audio, game, loot, mainloop | combat_load 4, job_frame 2, blit_cel 2, rules 2 | 8 (1 enh) | abs,custom |
| `mog.rules` | misc | 6 | 130 | 0 |  | boot_loop 1, overworld_nodes 1, places 1 | 2 |  |
| `mog.overworld_nodes` | map drawing | 10 | 290 | 0 | mainloop, overworld | combat_load 5, combat_setup 5, palette_load 4, map 4 | 17 | vector |
| `mog.menu` | screen loops/UI | 2 | 3 | 0 | hunk9, mainloop | palette_load 3, display_ops 3, text 2, input 1 | 3 |  |
| `mog.input` | input | 6 | 87 | 0 | combat, fighters, hunk9, mainloop, overworld, scene_menu | sprite_fx 4, loot_ui 1 | 0 | abs,cia,custom |
| `mog.files_prompt` | file/disk | 5 | 81 | 0 | combat, mainloop | file_stubs 10, trackdisk 3, map 1 | 1 |  |
| `mog.combat_load` | file/disk | 33 | 800 | 0 | combat, mainloop, overworld | loaders 35, display_ops 16, sound_entry 12, files_prompt 11 | 15 (14 enh) |  |
| `mog.combat_setup` | screen loops/UI | 50 | 1395 | 18 | combat, fighters, mainloop | fighters 16, combat_load 13, palette_load 11, job_frame 6 | 4 |  |
| `mog.fighters` | fighter handlers | 64 | 1592 | 30 | combat, combat_script, fighters, hunk9, mainloop | job_frame 22, fighter_handlers 11, boot_loop 6, sound_entry 5 | 14 | custom |
| `mog.job_frame` | fighter handlers | 15 | 90 | 0 | combat, creatures, fighters, mainloop, overworld | script_ops 20, contact 1 | 12 | vector |
| `mog.script_ops` | fighter handlers | 21 | 253 | 19 |  | file_stubs 1, sound_entry 1, job_frame 1, fighters 1 | 0 |  |
| `mog.contact` | fighter handlers | 10 | 194 | 0 | combat, hunk9, scene_menu | file_stubs 4, blit_copy 1, loaders 1 | 3 (1 enh) |  |
| `mog.display_ops` | display/copper | 7 | 51 | 0 | combat, combat_script, overworld, scene_menu | display 1, blit_copy 1, copper_fx 1, combat_ui 1 | 4 (4 enh) | custom |
| `mog.palette_load` | display/copper | 8 | 244 | 0 | combat, mainloop, scene_menu | palette 5, display 4, display_ops 4, sound_entry 1 | 0 |  |
| `mog.copper_fx` | display/copper | 2 | 31 | 1 | fighters | text 2 | 0 | custom |
| `mog.text` | text/font | 10 | 149 | 0 | scene_menu | blit_cel 1 | 1 |  |
| `mog.places` | screen loops/UI | 10 | 297 | 0 | overworld, scene_places | display_ops 10, text 6, input 5, palette_load 5 | 7 |  |
| `mog.town_ui` | screen loops/UI | 12 | 399 | 3 |  | display_ops 14, sound_entry 9, text 7, display 7 | 6 (1 enh) |  |
| `mog.combat_ui` | screen loops/UI | 17 | 631 | 0 | combat, loot, scene_town | text 7, blit_cel 5, rules 2, display_ops 1 | 2 |  |
| `mog.loot_ui` | screen loops/UI | 9 | 127 | 1 | combat, loot, mainloop, scene_town | input 3, sprite_fx 3, sound_entry 2, display 2 | 14 |  |
| `mog.bg_blit` | blitter | 9 | 289 | 0 | fighters | blit_copy 1, loaders 1, contact 1 | 2 (2 enh) | custom |
| `mog.sound_entry` | music/sfx | 23 | 124 | 0 | combat, combat_script, fighters, loot | synth 9, file_stubs 4 | 1 | vector |
| `mog.trackdisk` | file/disk | 6 | 70 | 1 |  | display 1 | 1 | cia |
| `mog.irq` | IRQ handlers/input | 13 | 253 | 6 | combat, input, overworld, scene_menu | display 3, file_stubs 2, trackdisk 1 | 1 | cia,custom,vector |
| `mog.file_stubs` | file/disk | 19 | 19 | 1 | enhanced, files, mainloop, scene_town |  | 5 |  |
| `mog.loaders` | file/disk | 18 | 292 | 2 | enhanced | file_stubs 12 | 4 (3 enh) |  |
| `mog.blit_cel` | blitter | 6 | 102 | 0 | combat_script, overworld, scene_menu | blit_copy 1 | 2 (1 enh) | custom |
| `mog.blit_copy` | blitter | 8 | 140 | 0 | combat, combat_script, enhanced, mainloop | loaders 1 | 2 (1 enh) | custom |
| `mog.display` | display/copper | 12 | 135 | 0 | combat, enhanced, mainloop, overworld, scene_menu | irq 1 | 10 (2 enh) | abs,custom |
| `mog.map` | map drawing | 36 | 379 | 0 | combat, loot, mainloop, overworld | job_frame 5, display_ops 5, blit_cel 4, irq 3 | 29 (1 enh) | vector |
| `mog.palette` | display/copper | 6 | 13 | 1 | combat, mainloop, scene_menu |  | 4 | custom |
| `mog.sprite_fx` | display/copper | 6 | 137 | 0 |  |  | 0 | custom |
| `mog.fighter_handlers` | fighter handlers | 33 | 959 | 8 |  | fighters 27, job_frame 15, sound_entry 10, boot_loop 7 | 0 |  |
| `mog.synth` | music/sfx | 107 | 1037 | 23 | engine_palette |  | 0 | abs,custom |

### Totals per proposed task

| task | batch | what | groups | routines | ~instr | indirect-only routines |
|---|---|---|---|---|---|---|
| 7.1a | 1 | Text/font printing | prg.text, mog.text | 14 | 242 | 0 |
| 7.1b | 1 | Audio front end: drop the module player, sound request entry | prg.music, mog.sound_entry | 28 | 224 | 1 |
| 7.1c | 1 | Blitter remnants (IMAGEXCEL, both binaries) | prg.blit_cel, prg.blit_copy, mog.blit_cel, mog.blit_copy | 29 | 485 | 0 |
| 7.1d | 2 | IRQ handlers, keyboard/joystick, trackdisk residue | prg.irq, mog.irq, mog.input, prg.trackdisk, mog.trackdisk | 43 | 734 | 15 |
| 7.1e | 2 | Display, copper, palette, hardware sprites | prg.display, prg.palette, mog.display, mog.palette, mog.palette_load, mog.display_ops, mog.copper_fx, mog.sprite_fx | 75 | 1175 | 5 |
| 7.1f | 2 | Asset loaders and the file layer | prg.loaders, mog.loaders, mog.combat_load, mog.files_prompt, prg.file_stubs, mog.file_stubs | 130 | 1901 | 7 |
| 7.1g | 3 | Music/sfx synth driver (mog S_44) | mog.synth | 107 | 1037 | 23 |
| 7.1h | 3 | Fighter handlers (asm bodies behind rtFighterRun) and combat script ops | mog.fighters, mog.fighter_handlers, mog.script_ops | 118 | 2804 | 57 |
| 7.1i | 3 | Intro/ending scene bodies and animation job handlers (program) | prg.scenes, prg.anim_jobs | 75 | 917 | 23 |
| 7.1j | 4 | Combat set-up, combat/loot screens | mog.combat_setup, mog.combat_ui, mog.loot_ui | 76 | 2153 | 19 |
| 7.1k | 4 | Menu, places and town screens | mog.menu, mog.places, mog.town_ui | 24 | 699 | 3 |
| 7.1l | 4 | Overworld map, boot/main-loop skeleton, rules remnants | mog.map, mog.overworld_nodes, mog.boot_loop, mog.rules | 64 | 871 | 2 |
| 7.1m | 4 | Contact/collision, job and frame primitives, arena background blit | mog.contact, mog.job_frame, mog.bg_blit | 34 | 573 | 0 |

### Live routines, program

#### `prg.scenes` - program S_0: intro/ending scene bodies and frame loops

35 routines, ~506 instructions. Roadmap: 6.10 / 4.9 -> 7.1i.

0003(1) 0006(9) 0007^(16) 000B(17) 000F(9) 0010(13) 0014*(3) 0015(1) 0016(1) 0017(2) 0018(11) 001A^(20) 001B^(10) 001C^(20) 001D(13) 001F(18) 002C^(1) 002D^(1) 002E^(1) 002F^(1) 0030(1) 0031(1) 0032*(19) 0036^(45) 0037^(44) 0039^(46) 003B^(38) 003C(1) 003F(7) 0040*(21) 0041(7) 0044^(41) 0051(12) 0054^(26) SECSTRT_0^(29)

#### `prg.music` - program S_1: ST/NT module player (dead under MS_MUSIC_PTPLAYER, still linked)

5 routines, ~100 instructions. Roadmap: 4.6 -> 7.1b.

005B^(1) 005C^(1) 0061(36) 0083*(61) SECSTRT_1(1)

#### `prg.anim_jobs` - program S_10: script op handlers, sprite blit list, job-manager tails

40 routines, ~411 instructions. Roadmap: 6.10 -> 7.1i.

01E4^(1) 01E8(1) 01EC(1) 020E^(35) 0215*(7) 0218*(9) 021A*(15) 021E*(2) 021F*(6) 0220*(1) 0221*(5) 0222*(34) 022C*(1) 022D*(1) 022F*(22) 0232*(2) 0233*(2) 0234*(2) 0235*(3) 0236*(8) 0237*(20) 023B*(20) 023F*(6) 0241*(1) 0242(17) 0246(71) 024B^(5) 0258^(8) 025A(4) 025B(5) 025D(1) 025F^(8) 0260(6) 0262^(8) 0263^(7) 0264^(17) 0268+2(4) 026C^(11) 026E^(1) SECSTRT_10(33)

#### `prg.text` - program S_12: font string/number printing

4 routines, ~93 instructions. Roadmap: 4.2 -> 7.1a.

028F(18) 0291(44) 0297(23) 02A0(8)

#### `prg.trackdisk` - program S_13: trackdisk driver residue (motor/step)

6 routines, ~75 instructions. Roadmap: 4.7 -> 7.1d.

02EE*(7) 02FF(13) 030F*(1) 0314(22) 0316(19) 0317(13)

#### `prg.irq` - program S_15: INT1-6 handlers, keyboard/joystick, VBL hook list

12 routines, ~249 instructions. Roadmap: 2.2/2.3 -> 7.1d.

0325(14) 0326*(14) 032A^(36) 0331*(32) 0337*(24) 033C*(11) 033F*(13) 034D(65) 0359(13) 035E(8) 0360*(1) SECSTRT_15(18)

#### `prg.file_stubs` - program S_18: file-cache entry stubs behind rt/files patches

14 routines, ~14 instructions. Roadmap: 2.7 -> 7.1f.

0382^(1) 0383(1) 0385(1) 0386(1) 0388(1) 0389(1) 038A(1) 038B(1) 038C(1) 038D(1) 038F(1) 0390(1) 03B2^(1) 03DA(1)

#### `prg.loaders` - program S_8/S_20/S_21: file table and load dispatcher (asset_file_load, intro_run), picture and cel loaders, LZSS glue

41 routines, ~695 instructions. Roadmap: 4.1 -> 7.1f.

0163(1) 0164(1) 0165(1) 0166(1) 0167(2) 0168(1) 0169(1) 016A(5) 016B(2) 016C(4) 016D(2) 016E(2) 016F(2) 0170(2) 0171(2) 0172(2) 0174^(19) 0185^(176) 018E^(126) 01AA*(3) 01B7(4) 01B9(29) 01BE(30) SECSTRT_8*(1) 03F2(5) 03F3(6) 03F4(4) 03F5(6) 03F6(3) 03FA(18) 03FB(1) 03FC(26) 0402^(38) SECSTRT_20*(7) 046C*(4) 046D(4) 046E(56) 046F(22) 0491(19) 0496(56) 049C^(1)

#### `prg.blit_cel` - program S_23: IMAGEXCEL draw_cel remnants (clip, mirror, kernel setup)

6 routines, ~102 instructions. Roadmap: 4.4 -> 7.1c.

04A6(6) 04A7(8) 04A8(61) 04B0(14) 04B4^(7) SECSTRT_23(6)

#### `prg.blit_copy` - program S_25: copy_rect remnants, IMAGEXCEL init

9 routines, ~141 instructions. Roadmap: 4.4 -> 7.1c.

04E1^(1) 04E2(1) 04E3(12) 04E4(61) 04EC(8) 04EE(44) 04F5(1) 04F6(3) SECSTRT_25(10)

#### `prg.display` - program S_29: custom-chip/copper/DMA init, beam wait, screen swap

11 routines, ~126 instructions. Roadmap: 4.8 -> 7.1e.

054C(9) 054D(55) 054F^(8) 0552(4) 0556(2) 0557(2) 055E^(8) 0565(5) 0566(1) 0567(1) SECSTRT_29(31)

#### `prg.palette` - program S_31: palette wipes/transitions, colour helpers

23 routines, ~438 instructions. Roadmap: 4.5 -> 7.1e.

0576(1) 0579(3) 057A(1) 057D*(1) 059E(21) 059F(23) 05A0(22) 05A1(44) 05A5^(33) 05AB(21) 05AC(19) 05AE*(2) 05AF*(2) 05B2(25) 05B7(4) 05BA(48) 05BF(24) 05C1(22) 05C5(36) 05C8(33) 05CC(30) 05CD(17) SECSTRT_31(6)


### Live routines, mog

#### `mog.boot_loop` - mog S_0 boot/main-loop skeleton, RNG seed, audio fade glue

12 routines, ~72 instructions. Roadmap: 6.1 -> 7.1l.

0001(1) 0003^(9) 0004^(1) 0005*(1) 0006(1) 000A*(12) 000D(6) 0011^(9) 04A1(1) 04A3^(1) 04A5^(8) SECSTRT_0^(22)

#### `mog.rules` - mog S_0 knight/rules remnants (recalc, upkeep)

6 routines, ~130 instructions. Roadmap: 5.3 -> 7.1l.

0013(1) 0019(1) 001C(31) 0021(34) 0029(41) 0030(22)

#### `mog.overworld_nodes` - mog S_0 overworld node entries: duel, lair, dragon, place dispatch

10 routines, ~290 instructions. Roadmap: 6.3/6.8 -> 7.1l.

0036^(1) 004F^(1) 005B^(1) 0064(1) 0065(8) 0069(2) 007B^(31) 0083(127) 009B(31) 009C(87)

#### `mog.menu` - mog S_0 title/menu screen loop (hunk-9 call site)

2 routines, ~3 instructions. Roadmap: 6.2 -> 7.1k.

00B4^(2) 00D3^(1)

#### `mog.input` - mog joystick/fire reads (S_0 and S_20)

6 routines, ~87 instructions. Roadmap: 2.3 -> 7.1d.

00EA(5) 00EC^(7) 00EE^(29) 00F3(16) 0575^(21) 057B^(9)

#### `mog.files_prompt` - mog S_0 drive init, disk prompts, loader call sites

5 routines, ~81 instructions. Roadmap: 2.7 -> 7.1f.

00F8^(63) 0100^(5) 010C(6) 010E(6) 0113(1)

#### `mog.combat_load` - mog S_0 LAB_0114-0155: per-creature/arena asset loads and set-up wrappers

33 routines, ~800 instructions. Roadmap: 6.4 -> 7.1f.

0114^(1) 0115^(40) 0116^(36) 0118(26) 011A(23) 011C(22) 011E(15) 011F(35) 0121^(32) 0123(20) 0125(34) 0126(25) 0128^(16) 0129(14) 012B^(18) 012C^(60) 012D(16) 012E(10) 012F(20) 0131(35) 0133(13) 0134^(17) 0136(10) 0137^(18) 0138(14) 013A^(17) 013C^(53) 0142(60) 014A(11) 014C(11) 014E(21) 0152^(8) 0155^(49)

#### `mog.combat_setup` - mog S_0 combat set-up: fighter/creature init, tables, pool, hit test

50 routines, ~1395 instructions. Roadmap: 6.4 -> 7.1j.

0156^(202) 015F^(12) 0161^(16) 0164*(1) 0165^(1) 0166^(1) 0167(1) 0168*(14) 0169*(15) 016A*(14) 016B(5) 016D(6) 016F^(10) 0170*(15) 0171^(9) 0174(15) 0175*(18) 0176*(13) 0177(66) 0188(21) 0189*(5) 018B*(14) 018C*(30) 018D*(5) 018F*(32) 0192^(3) 0195(15) 0196*(17) 0197*(2) 0198*(12) 019A(14) 019B(5) 019D*(15) 019E*(17) 019F*(14) 01A0(37) 01A3(12) 01A4^(11) 01A5(8) 01A6(8) 01A7(11) 01A8(1) 01A9^(24) 01AE^(161) 01B7(33) 01BE^(55) 044E^(9) 0451^(37) 0456(154) 0588^(179)

#### `mog.fighters` - mog S_0 per-fighter handlers LAB_01C6-02F1 (asm bodies behind rtFighterRun)

64 routines, ~1592 instructions. Roadmap: 6.4a -> 7.1h.

01C6(20) 01C8*(6) 01CA*(103) 01D9(10) 01DB(8) 01DC(27) 01E1*(22) 01E6(36) 01ED*(7) 01EF*(16) 01F2(22) 01F6*(18) 01F9*(14) 01FD*(10) 0200*(6) 0201(5) 0203*(8) 0204(5) 0205*(10) 0206*(26) 020B(9) 020E(4) 020F^(33) 0210*(2) 0211*(33) 0215(20) 021B(1) 0226^(89) 0236(127) 0251(316) 027A*(150) 028E(45) 0297(14) 0298(43) 029F^(113) 02AC*(2) 02AD*(2) 02AE*(5) 02AF(71) 02BA(7) 02BB(6) 02BC(10) 02BF(14) 02C2(11) 02C4(12) 02C6(11) 02C8(12) 02CA*(1) 02CB*(16) 02CE^(5) 02D0^(3) 02D2*(5) 02D3(1) 02DB(2) 02DC*(1) 02DE*(1) 02E0*(1) 02E1*(1) 02E2*(1) 02E3*(1) 02E4*(1) 02E6*(1) 02E8*(1) 02E9^(9)

#### `mog.job_frame` - mog S_0 job/frame primitives (clear, pause, frame start/wait, job pass)

15 routines, ~90 instructions. Roadmap: 6.1/6.4 -> 7.1m.

02F2^(5) 02F6(1) 02FD^(29) 0302(9) 0303^(35) 0305^(1) 030D(1) 0310^(1) 0315(1) 0319^(1) 031B(1) 031D^(2) 031F^(1) 0322^(1) 0328^(1)

#### `mog.script_ops` - mog S_0 combat script op handlers LAB_0358-039C (table entries)

21 routines, ~253 instructions. Roadmap: 6.5 -> 7.1h.

0358*(11) 035B*(9) 035D*(15) 0361*(12) 0362*(6) 0363*(13) 0366(1) 0367*(7) 0368*(34) 0372*(10) 0374*(14) 038B*(12) 038C*(16) 038E*(8) 0390*(13) 0391*(7) 0392*(10) 0393*(20) 0397*(20) 039B*(3) 039C(12)

#### `mog.contact` - mog S_0 frame timer, draw-buffer clear, contact/collision

10 routines, ~194 instructions. Roadmap: 6.6 -> 7.1m.

039E^(17) 03A2(71) 03A7^(5) 03A9(1) 03BE^(1) 03CA(13) 03CE(56) 03D8(10) 03D9(7) 03DA(13)

#### `mog.display_ops` - mog S_0 sprite display, flip, custom-register helpers

7 routines, ~51 instructions. Roadmap: 4.8 -> 7.1e.

03EB^(8) 03EE^(1) 0416^(9) 0418^(7) 0419^(17) 041F(5) 0422^(4)

#### `mog.palette_load` - mog S_0 palette load/fade glue, fight-screen init

8 routines, ~244 instructions. Roadmap: 4.5 -> 7.1e.

03F0^(8) 03F1^(11) 03F2^(6) 03F3^(136) 0403(29) 0409^(36) 0412^(15) 0413(3)

#### `mog.copper_fx` - mog S_0 copper effects (LAB_0427/042A)

2 routines, ~31 instructions. Roadmap: 4.8 -> 7.1e.

0427^(16) 042A*(15)

#### `mog.text` - mog S_0 string/text/number drawing

10 routines, ~149 instructions. Roadmap: 4.2 -> 7.1a.

0430(1) 0431^(7) 0432^(31) 0435(49) 043B(7) 043D(26) 0442^(1) 0447(8) 0448(9) 044B(10)

#### `mog.places` - mog S_0 wizard/mystic/gift/stonehenge UI helpers, gold text

10 routines, ~297 instructions. Roadmap: 6.8 -> 7.1k.

045E^(4) 046C(1) 0471(1) 047C(65) 048E^(59) 0495(77) 049B(63) 049C(4) 049D(4) 049E(19)

#### `mog.town_ui` - mog S_0 town/dice/exchange UI helpers

12 routines, ~399 instructions. Roadmap: 6.7 -> 7.1k.

04A6(128) 04AB(10) 04AC(38) 04B4(42) 04B7(28) 04BA*(7) 04BC(1) 04BF(71) 04C2*(6) 04C4*(4) 04C5(34) 04CA(30)

#### `mog.combat_ui` - mog S_0 combat state entry, post-fight/stat/shop screens

17 routines, ~631 instructions. Roadmap: 6.4/6.9 -> 7.1j.

04CF^(1) 04D4^(1) 04EA^(15) 04ED(30) 04F8^(179) 04FE^(174) 0511(21) 0516(3) 0517(17) 051A(26) 051B^(31) 051D^(10) 051F^(12) 0521(26) 0522^(55) 0523(14) 0524^(16)

#### `mog.loot_ui` - mog S_0 loot screens and click handling

9 routines, ~127 instructions. Roadmap: 6.9 -> 7.1j.

0527(1) 0528(1) 052A^(7) 052F^(47) 0572^(10) 057D*(38) 058A^(1) 05A0^(9) 05A1^(13)

#### `mog.bg_blit` - mog S_12: arena background blit compositor, obstacle test

9 routines, ~289 instructions. Roadmap: 4.4/6.4 -> 7.1m.

0A60(26) 0A64(62) 0A66(36) 0A6A(17) 0A6B(30) 0A6C(8) 0A6D(25) 0A71^(57) SECSTRT_12(28)

#### `mog.sound_entry` - mog S_16: sound request entry, channel start/stop

23 routines, ~124 instructions. Roadmap: 4.6 -> 7.1b.

0A9B(3) 0A9C(3) 0A9D(3) 0A9E(4) 0A9F(4) 0AA0(4) 0AA1(4) 0AA2^(11) 0AA7(15) 0AA9^(6) 0AAA(5) 0AAB(5) 0AAC(5) 0AAD(5) 0AAE(5) 0AAF(5) 0AB0(5) 0AB1(5) 0AB2(5) 0AB3(5) 0AB4(5) 0AB5(9) SECSTRT_16(3)

#### `mog.trackdisk` - mog S_18: trackdisk driver residue

6 routines, ~70 instructions. Roadmap: 4.7 -> 7.1d.

0B18(2) 0B22(13) 0B32*(1) 0B37(22) 0B39(19) 0B3A(13)

#### `mog.irq` - mog S_20: INT1-6 handlers, keyboard/joystick, VBL hook list

13 routines, ~253 instructions. Roadmap: 2.2/2.3 -> 7.1d.

0B46^(4) 0B49(14) 0B4A*(14) 0B4E^(36) 0B55*(32) 0B5B*(24) 0B60*(11) 0B63*(13) 0B71(65) 0B7D(13) 0B82^(8) 0B84*(1) SECSTRT_20(18)

#### `mog.file_stubs` - mog S_23: file-cache entry stubs behind rt/files patches

19 routines, ~19 instructions. Roadmap: 2.7 -> 7.1f.

0BA3*(1) 0BA4(1) 0BA5(1) 0BA6^(1) 0BA7(1) 0BA9(1) 0BAA(1) 0BAC(1) 0BAD(1) 0BAE(1) 0BAF(1) 0BB0(1) 0BB1(1) 0BB3^(1) 0BB4(1) 0BB5(1) 0BD7^(1) 0BEA(1) 0BFF^(1)

#### `mog.loaders` - mog S_25/S_26: picture, cel/ob loaders, LZSS body

18 routines, ~292 instructions. Roadmap: 4.1 -> 7.1f.

0C17(5) 0C18(6) 0C19(4) 0C1A(6) 0C1B(3) 0C1F(18) 0C20(1) 0C21(26) 0C27^(38) SECSTRT_25*(7) 0C91*(4) 0C92(4) 0C93(56) 0C94(22) 0CB6(19) 0CBB(56) 0CC0(16) 0CC2^(1)

#### `mog.blit_cel` - mog S_28: IMAGEXCEL draw_cel remnants

6 routines, ~102 instructions. Roadmap: 4.4 -> 7.1c.

0CCC(6) 0CCD(8) 0CCE^(61) 0CD6(14) 0CDA^(7) SECSTRT_28(6)

#### `mog.blit_copy` - mog S_30: copy_rect remnants, IMAGEXCEL init

8 routines, ~140 instructions. Roadmap: 4.4 -> 7.1c.

0D07^(1) 0D08(12) 0D09(61) 0D11(8) 0D13(44) 0D1A(1) 0D1B^(3) SECSTRT_30^(10)

#### `mog.display` - mog S_34: custom-chip/copper init, screen clear, text-cell pokes

12 routines, ~135 instructions. Roadmap: 4.8 -> 7.1e.

0D71(9) 0D72^(55) 0D74^(8) 0D77^(5) 0D7B(2) 0D7C^(2) 0D83^(8) 0D8A^(5) 0D8B(1) 0D8C(1) 0D8D^(8) SECSTRT_34^(31)

#### `mog.map` - mog S_36: overworld loop LAB_0DAB, map sprite/dragon/AI tails, map draw

36 routines, ~379 instructions. Roadmap: 6.3 -> 7.1l.

0D9B^(18) 0D9E(22) 0DA3(19) 0DA6(4) 0DAB^(114) 0DBD(2) 0DC5(21) 0DC8^(9) 0DCA(4) 0DCB(2) 0DCF^(3) 0DD8^(11) 0DDF(2) 0DEA(2) 0DED(2) 0E02^(2) 0E03^(2) 0E04(2) 0E05^(2) 0E06^(2) 0E0C(2) 0E17(2) 0E20(9) 0E22(2) 0E23(5) 0E27(8) 0E29(9) 0E2B(1) 0E2D(2) 0E35(2) 0E3D(2) 0E49^(57) 0E4E(17) 0E51(3) 0E52^(7) SECSTRT_36^(6)

#### `mog.palette` - mog S_36 tail: palette target/ramp/tick twins (patched), colour helpers

6 routines, ~13 instructions. Roadmap: 4.5 -> 7.1e.

0E53^(6) 0E55(1) 0E56(1) 0E59(3) 0E5A^(1) 0E5D*(1)

#### `mog.sprite_fx` - mog S_37: hardware-sprite (pointer) DMA and table-driven sprite build

6 routines, ~137 instructions. Roadmap: 4.8 -> 7.1e.

0E75(2) 0E76(3) 0E77(1) 0E78(52) 0E85(34) SECSTRT_37(45)

#### `mog.fighter_handlers` - mog S_40: creature/dragon handler bodies (jump-table entries)

33 routines, ~959 instructions. Roadmap: 6.4a -> 7.1h.

0EA1(6) 0EA2(7) 0EA3(3) 0EA4(102) 0EB8(3) 0EB9*(27) 0EBD*(7) 0EBE*(6) 0EC2(33) 0EC6(8) 0EC7(32) 0ED0(11) 0ED2(57) 0EDA(96) 0EEB(16) 0EEC*(8) 0EED*(3) 0EEE*(9) 0EEF(53) 0EF6*(9) 0EF7*(45) 0EFF(171) 0F1A(11) 0F1C(10) 0F1E(9) 0F1F(8) 0F20(31) 0F24(52) 0F2E(14) 0F32(11) 0F33(7) 0F34(14) SECSTRT_40(80)

#### `mog.synth` - mog S_44: four-voice synth/sequencer driver and fade

107 routines, ~1037 instructions. Roadmap: 4.6 -> 7.1g.

0F66(37) 0F67(37) 0F68(37) 0F69(37) 0F6F(28) 0F73*(10) 0F75(7) 0F76(27) 0F7B(7) 0F7C(11) 0F7F(43) 0F89(91) 0F8C(43) 0F8F(58) 0F96(70) 0FA4*(3) 0FA5*(2) 0FA6*(3) 0FA7*(5) 0FA8*(5) 0FA9*(12) 0FAB*(20) 0FAD*(2) 0FAE*(2) 0FAF*(3) 0FB0*(8) 0FB1*(7) 0FB2*(2) 0FB3*(7) 0FB5*(4) 0FB6*(8) 0FB8*(9) 0FBA*(5) 0FBB*(2) 0FBC*(10) 0FBD*(6) 0FBE(7) 0FC0(7) 0FC2^(24) 0FCD(2) 0FCF+2(2) 0FD3(1) 0FD4(70) 0FE2(1) 0FE4(1) 0FE5(1) 0FE6(1) 0FE7(1) 0FE9(1) 0FEA(1) 0FEC(1) 0FF5(1) 0FF6(1) 0FF7(1) 0FF8(1) 0FF9(20) 0FFE(9) 1005(1) 1007(1) 1009(1) 100B(1) 100D(1) 100E(1) 100F(1) 1010(1) 1011(1) 1012(1) 1017(1) 1018(1) 101A(6) 101C(6) 101E(4) 101F(7) 1028(1) 102D(3) 102F(1) 1031(1) 1032(4) 1033(4) 1034(6) 1039(6) 103D(4) 103E(4) 103F(6) 1042(4) 1043(4) 1044(6) 104B(4) 104C(4) 104D(6) 1050(4) 1051(4) 1053(4) 1054(6) 105B(10) 105C(11) 1061(2) 1069(7) 106E(7) 1076(3) 1077(3) 1079(3) 107D(2) 108E(2) 108F(6) 1090(1) SECSTRT_44*(37)


### DATA/BSS reached from C++ by prg_/mog_ symbol

| binary | hunk | kind | symbols | bytes | C++ modules |
|---|---|---|---|---|---|
| mog | S_0 | CODE | 6 | 26 | combat, creatures, enhanced, fighters, loot |
| mog | S_1 | BSS | 128 | 5847 | combat, combat_script, creatures, enhanced, fighters, loot, mainloop, overworld, scene_menu, scene_places, scene_town |
| mog | S_2 | DATA | 5 | 160 | overworld |
| mog | S_4 | DATA | 75 | 2902 | combat, combat_script, creatures, engine_util, fighters, loot, mainloop, overworld, scene_menu, scene_places, scene_town |
| mog | S_10 | DATA | 5 | 100 | creatures, mainloop |
| mog | S_14 | BSS | 4 | 14 | enhanced |
| mog | S_21 | DATA | 4 | 258 | combat, input, overworld, scene_menu |
| mog | S_22 | BSS | 1 | 4 | combat, combat_script, input, mainloop |
| mog | S_29 | DATA | 4 | 12 | combat, combat_script, engine_blit, enhanced, scene_menu |
| mog | S_31 | DATA | 7 | 104 | engine_blit, enhanced, scene_menu |
| mog | S_35 | DATA | 2 | 8 | combat, combat_script, scene_menu |
| mog | S_39 | DATA | 6 | 176 | engine_palette, enhanced |
| mog | S_41 | DATA | 1 | 2 | fighters |
| mog | S_42 | DATA | 4 | 75 | scene_town |
| program | S_0 | CODE | 1 | 2 | engine_anim |
| program | S_2 | DATA | 31 | 4790 | engine_anim, engine_intro, enhanced, introskip |
| program | S_3 | BSS | 10 | 156 | audio, engine_anim, engine_intro, engine_jobs, enhanced |
| program | S_9 | BSS | 5 | 322 | engine_anim |
| program | S_10 | CODE | 2 | 6 | enhanced |
| program | S_11 | BSS | 16 | 3938 | engine_anim, engine_jobs |
| program | S_16 | DATA | 3 | 257 | input |
| program | S_17 | BSS | 1 | 4 | engine_anim, input |
| program | S_24 | DATA | 4 | 12 | engine_anim, engine_blit, enhanced |
| program | S_26 | DATA | 7 | 104 | engine_blit, enhanced |
| program | S_30 | DATA | 1 | 4 | engine_anim |
| program | S_32 | DATA | 7 | 180 | engine_palette, enhanced |
| program | S_33 | BSS | 2 | 16 | engine_intro, enhanced |

Typed in `include/game/state_bind.hpp`: 29 symbols; curated gen_tables tables: 21.
