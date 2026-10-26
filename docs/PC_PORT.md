# PC port: platform split and pointer survey (ROADMAP 11.1, 11.2)

Static analysis of 2026-10-10 plus one build experiment; nothing in this file is generated, the numbers are reproducible with the
commands in section 8. It is the input for the rest of M11: 11.3 (byte order) and 11.4..11.12 read their scope from here.

Contents: 1 summary, 2 method, 3 inventory of `src/rt` (and everything else that is not host-clean), 4 the asm sites,
5 the platform interfaces, 6 what moved and the follow-ups, 7 the 11.2 survey (pointers as 32-bit 68k addresses), 8 reproduction.

## 1. Summary

* `src/rt` is 76 `.cpp` files (17,000 lines) plus `src/main.cpp`. Classified (section 3): **14 platform-neutral** (N, 1,400 lines),
  **31 neutral logic behind 68k register-ABI shims** (N*, 9,400 lines: portable once the shims go), **19 mixed** (B, 4,600 lines:
  the logic and the hardware access sit in one file and must be split), **11 Amiga-only** (A, 1,600 lines) and 1 retired file.
* `src/engine`, `src/game` and `include/engine`, `include/game` are host-clean already (the layer lint `tests/test_layers.py` keeps
  them free of ACE/OS includes). The only non-portable constructs above L0 are `asm volatile("" ::: "memory")` barriers (2, which
  clang/gcc accept on x86), `asm("g_mogSynthData")` in `include/engine/synth_data.hpp` (a symbol-name pin, portable) and the
  `jobAddr`/`jobPtr` casts of `include/engine/jobs.hpp` (section 7).
* The real work of M11 is not the 11 Amiga-only files. It is the **register-ABI layer**: 64 top-level `asm()` blocks defining 213
  entry labels in 47 files, 19 `jsr (An)` call helpers in 17 files and 306 `RT_FN(rt_x)` references that put *shim addresses* into
  step tables, job tables, hook lists and data cells. Every entry fronts a C++ body (section 4), so the PC build needs typed C call
  tables instead of the shims, not new logic (follow-up 11.1d).
* Platform interfaces (section 5): `include/rt/platform/{display,blit,audio,timer,input,files,log}.hpp` as ROADMAP 11.1 names them,
  plus an eighth, `system.hpp` (life cycle and memory by kind), which the code needs and the roadmap list lacks. 54 functions in
  total, every one taken from a call that exists in `src/rt` today. The headers are ACE-free (`<stdint.h>` only) and compile on host
  clang and on `m68k-amiga-elf-g++`.
* Moved (mechanical, Amiga behaviour unchanged): `crash`, `serlog`, `display_ace` (.cpp + .hpp each) to `src/rt/amiga/`. The rest is
  listed as follow-ups 11.1a..11.1e (section 6).
* 11.2: **recommend `-m32` for the first PC builds** (`i686-pc-windows-msvc`; a hello world and an SDL2 2.32 window-less init both
  link and run on this machine, section 7.4; SDL3 3.4.18 replaced SDL2 on 2026-10-10 and links the same way), base-relative offsets later as the long-term form. 702 pointer cells in 93 owned data
  objects, 374 `const char *` members in the generated structs, 13 pointer fields in `Knight` and 108 address casts all stay
  bit-identical on a 32-bit build and would need a translation layer on a 64-bit one. One known `-m32` cost: on i686 COFF C
  symbols carry a leading underscore, so the `.set` aliases `tools/gen_data.py` emits need `_` there (tested, section 7.4).

## 2. Method

* Per-file counts come from a script over `src/rt/**/*.cpp` (regexes for top-level `asm(...)`, `.globl` entry labels, `asm volatile`
  / `__asm`, `jsr (An)`, `asm("sym")` pins, `(uintptr_t)` casts, `#include <ace/...|hardware/...|proto/...|dos/...|exec/...>` and
  `g_pCustom->reg`). Classification is by reading each file's header comment and its hardware/OS touches.
* Host check: `clang++ -fsyntax-only -std=c++17 -fno-exceptions -fno-rtti -D__CODE_CHECKER__ -Iinclude -Isrc -I<ace>/include` on every
  file (`-D__CODE_CHECKER__` is the ACE switch that makes `ace/types.h` compile off-Amiga), then `-c` for the files that passed. Result:
  49 of 77 files (76 + `main.cpp`) pass the syntax check, 18 of those also assemble (the other 31 contain m68k `asm` blocks the x86
  assembler rejects). The 28 failures are: 12 ACE `managers/system.h` (pulls `graphics/gfxbase.h`), 2 ACE `managers/blit.h`, 2 ACE `simplebuffer.h` types,
  6 uses of `g_pCustom`/`g_pCia`, 3 `hardware/dmabits.h`, 2 m68k register names in `asm`, 1 `static_assert` on the 68k register block.
  Without the define not even `ace/types.h` compiles ("Compiler not supported!"), which is why section 6 lists a host `types.h` shim.
  The 18 clean files: `autoplay`, `autoplay_game`, `combat_pool`, `example_scene`, `fight_roster`, `fight_seed`, `flow`, `hunk9`,
  `knight_colours`, `palette_enh`, `party`, `party_fight`, `party_loot`, `party_start`, `party_turn`, `perf`, `serlog`, `world_bind`
  (`autoplay`, `perf` and `serlog` are empty without `MS_AUTOPLAY`).

## 3. Inventory

Classes: **N** platform-neutral (compiles on host, no OS/hardware, no asm); **N\*** neutral logic whose entry points are 68k
register-ABI shims or which does 68k-address arithmetic (portable after follow-up 11.1d and 11.2, not before); **B** needs both: a portable
core plus hardware/OS access in the same file (split it, follow-up 11.1c); **A** Amiga-only (replaced by a PC backend); **R** retired.
A header follows its `.cpp`; the exceptions are listed after the table.

Columns: `asm blocks / entry labels` = top-level `asm()` blocks and the `.globl` labels they define; `inline` = `asm volatile` /
`__asm` sites; `jsr helper` = `jsr (An)` call helpers; `asm-label cells` = `extern T x asm("sym")` pins; `addr casts` = `(uintptr_t)`
casts; `ACE/OS headers` = distinct ACE, `hardware/`, `proto/`, `dos/`, `exec/`, `intuition/` includes; `chip regs` = distinct
`g_pCustom->reg` names.

| file | class | lines | asm blocks / entry labels | inline | jsr helper | asm-label cells | addr casts | ACE/OS headers | chip regs | why |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| `amiga/crash.cpp` | A | 311 | 1 / 4 | 0 | 0 | 0 | 0 | 6 | 0 | 68020 exception frames (formats 0/2/9/A/B), CPU vector patching, dos.library crash.log |
| `amiga/display_ace.cpp` | A | 221 | 0 / 0 | 0 | 0 | 0 | 0 | 7 | 2 | ACE view + raw copper list, COP1LC, bitplane pointers; the "virtual display" seam |
| `amiga/serlog.cpp` | A | 54 | 0 / 0 | 0 | 0 | 0 | 0 | 4 | 3 | Paula serial port (SERDAT/SERPER, misc.resource); MS_AUTOPLAY only |
| `abs_stubs.cpp` | N* | 71 | 1 / 27 | 0 | 0 | 0 | 0 | 0 | 0 | generated data symbols; one top-level asm block of `.set` aliases over the chip work block |
| `adfdisks.cpp` | B | 186 | 0 / 0 | 0 | 0 | 0 | 0 | 5 | 0 | ADF/OFS image reader: scan/identify logic neutral (engine/ofs); file I/O is dos.library Open/Read/Seek |
| `arena.cpp` | N* | 1462 | 2 / 0 | 0 | 0 | 0 | 13 | 0 | 0 | creature-arena setup in C++ over mog cells; 2 asm-label emitters, rd32/st32 helpers on 68k addresses |
| `arena_bg.cpp` | B | 136 | 1 / 3 | 0 | 0 | 0 | 0 | 3 | 14 | tile compositor neutral; mask build + blit loop write the blitter registers directly |
| `asmcall.cpp` | A | 27 | 1 / 1 | 0 | 1 | 0 | 0 | 0 | 0 | `rtAsmCall`: loads D0-D3/A0-A2, `jsr (a5)`; the register-ABI call helper |
| `audio.cpp` | A | 181 | 1 / 3 | 0 | 0 | 0 | 0 | 3 | 1 | ACE ptplayer (CIA-B timers, Paula DMA), `g_pCustom->aud[]`; module sanitising is neutral and could be shared |
| `autoplay.cpp` | B | 455 | 0 / 0 | 0 | 0 | 0 | 0 | 8 | 2 | scripted-input engine neutral; reads/acks `intreq` and injects Amiga raw keys (MS_AUTOPLAY only) |
| `autoplay_game.cpp` | N | 356 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | MS_AUTOPLAY poke hooks on game cells (not built in normal builds) |
| `combat.cpp` | N | 435 | 0 / 0 | 0 | 1 | 0 | 7 | 0 | 0 | rt wiring of game/combat.cpp: FightEnv over cells; entries in `amiga/combat_entries.cpp` (11.1d part 2) |
| `combat_load.cpp` | N* | 396 | 1 / 1 | 0 | 1 | 1 | 13 | 1 | 0 | wiring of game/combat_load; asm-label cell, calls rt_file_* directly |
| `combat_pool.cpp` | N | 64 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | creature-pool glue; address helper only |
| `combat_script.cpp` | N | 129 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | CombatEnv wiring; entries in `amiga/combat_script_entries.cpp` |
| `combat_ui.cpp` | N* | 1201 | 1 / 0 | 0 | 0 | 0 | 17 | 0 | 0 | stat sheet / loot icons / cursor in C++; rd32/st32 on 68k addresses |
| `creatures.cpp` | N | 109 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | wiring of game/creatures; entries in `amiga/creatures_entries.cpp` |
| `display.cpp` | A | 49 | 0 / 0 | 0 | 0 | 0 | 0 | 2 | 5 | ACE view/vport/simplebuffer creation; AGA FMODE/BPLCON resets |
| `display_ops.cpp` | B | 280 | 1 / 6 | 3 | 0 | 2 | 0 | 1 | 5 | screen swap/clear/palette write neutral in shape; beam wait polls VHPOSR, copper stub + DMACON writes, 3 inline-asm loops |
| `engine_anim.cpp` | N* | 134 | 1 / 4 | 0 | 2 | 0 | 0 | 1 | 0 | anim interpreter entries; register-ABI entries + 2 jsr call helpers |
| `engine_blit.cpp` | B | 362 | 2 / 13 | 0 | 0 | 0 | 0 | 3 | 14 | hwBlit = BLTxxx register writes; CEL_BIND cells + register-ABI entries |
| `engine_intro.cpp` | N* | 105 | 1 / 1 | 0 | 1 | 0 | 0 | 1 | 0 | intro/ending sequencer host; 1 shim + 1 jsr helper |
| `engine_jobs.cpp` | N* | 74 | 1 / 1 | 0 | 0 | 0 | 1 | 1 | 0 | jobs manager entries; address cast |
| `engine_palette.cpp` | N* | 193 | 4 / 3 | 1 | 0 | 0 | 0 | 1 | 0 | palette machine entries; inline `jsr rt_synth_fade` |
| `engine_rnc.cpp` | N* | 30 | 1 / 1 | 0 | 0 | 0 | 0 | 1 | 0 | RNC decode entry; 1 shim |
| `engine_scenes.cpp` | N* | 330 | 1 / 3 | 0 | 1 | 2 | 0 | 1 | 0 | scene host; asm trampoline + 2 asm-label cells |
| `engine_util.cpp` | N* | 28 | 1 / 1 | 0 | 0 | 0 | 0 | 1 | 0 | RNG/util entries; 1 shim |
| `enhanced.cpp` | B | 402 | 4 / 13 | 0 | 0 | 10 | 0 | 2 | 0 | enhanced 64-colour: chip-memory carve (memAllocChipClear), register-ABI entries, 10 asm-label cells |
| `example_scene.cpp` | N | 24 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | optional example scene (MS_EXAMPLE_SCENE); not built by default |
| `fight_roster.cpp` | N | 12 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | roster storage |
| `fight_seed.cpp` | N | 42 | 0 / 0 | 0 | 0 | 0 | 1 | 0 | 0 | roster seed; 1 address cast |
| `fighters.cpp` | N | 252 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | FighterEnv wiring; trampolines in `amiga/fighters_entries.cpp` |
| `files.cpp` | B | 467 | 4 / 4 | 0 | 0 | 0 | 0 | 7 | 0 | search order, art override, buffer: neutral; Open/Read/Seek/Examine/Lock are dos.library; systemUse guards |
| `flow.cpp` | N | 124 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | scene-manager instance; calls rt::fatal + serLog |
| `game.cpp` | B | 237 | 1 / 4 | 0 | 1 | 0 | 0 | 3 | 0 | overlay entry + image snapshot/restore neutral; crash install, display handover, fast-memory backup |
| `game_rules.cpp` | N* | 42 | 1 / 2 | 0 | 0 | 0 | 1 | 0 | 0 | rules entries; 2 shims, address cast |
| `gtext.cpp` | N* | 254 | 2 / 5 | 0 | 1 | 1 | 0 | 0 | 0 | text printer entries; 5 shims, 1 jsr helper |
| `hunk9.cpp` | N | 36 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | hunk-9 title step stand-in |
| `input.cpp` | B | 337 | 1 / 7 | 0 | 0 | 0 | 0 | 4 | 2 | key decode/pad/co-op neutral; reads JOY0DAT/JOY1DAT, CIA-A PRA fire bits, ACE key manager |
| `introskip.cpp` | B | 94 | 1 / 2 | 0 | 0 | 0 | 0 | 3 | 0 | skip latch neutral; reads CIA/keys |
| `irq.cpp` | A | 404 | 1 / 5 | 0 | 1 | 0 | 0 | 6 | 6 | INTENA/INTREQ/POTGO, ACE systemSetInt, private-stack trampolines; `irqLevel3` body is neutral and moves to the timer seam |
| `knight_colours.cpp` | B | 147 | 0 / 0 | 0 | 0 | 0 | 0 | 2 | 0 | palette ramp maths neutral; chip block via memAlloc(MEMF_CHIP) |
| `loaders.cpp` | N* | 276 | 2 / 0 | 0 | 0 | 1 | 0 | 1 | 0 | loader entries over engine/loaders + rt_file_*; asm-label cell, trampolines |
| `loot.cpp` | N | 337 | 0 / 0 | 0 | 0 | 0 | 5 | 0 | 0 | click handler + screen setup; address casts |
| `mainloop.cpp` | N* | 347 | 1 / 11 | 0 | 2 | 0 | 1 | 0 | 0 | main loop wiring (MainEnv/MogJobEnv): 11 entries + 2 jsr helpers |
| `modload.cpp` | B | 161 | 0 / 0 | 0 | 0 | 0 | 0 | 2 | 0 | mod loader: parser neutral (game/data/modload); file list/read via rt_file_*; ACE log |
| `mog_display.cpp` | B | 160 | 1 / 5 | 0 | 0 | 0 | 0 | 1 | 11 | display helpers: DIWSTRT/DIWSTOP + blitter register writes |
| `noop.cpp` | N* | 10 | 1 / 1 | 0 | 0 | 0 | 0 | 0 | 0 | `rtNoop`, a bare RTS in asm (needs a C function) |
| `origload.cpp` | B | 172 | 0 / 0 | 0 | 0 | 0 | 0 | 3 | 0 | reads program+mog through the file layer at start-up (10.2a); neutral logic, uses rt::system/log |
| `overworld.cpp` | N | 913 | 0 / 0 | 0 | 0 | 0 | 12 | 0 | 0 | overworld wiring; 12 address casts; `rtOwAsm` and the dragon handler in `amiga/overworld_entries.cpp` |
| `palette_aga.cpp` | A | 42 | 0 / 0 | 0 | 0 | 0 | 0 | 2 | 2 | AGA colour registers (BPLCON3 banks + COLORxx, LOCT) |
| `palette_enh.cpp` | N | 119 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | 24-bit palette state of the enhanced mode (empty without MS_ENHANCED) |
| `palette_glue.cpp` | B | 238 | 1 / 10 | 1 | 0 | 1 | 0 | 0 | 0 | fade/scene helpers neutral; inline `jsr rt_mog_pal_copy_live` with A0 |
| `party.cpp` | N | 47 | 0 / 0 | 0 | 0 | 0 | 1 | 0 | 0 | co-op party cell storage |
| `party_fight.cpp` | N | 39 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | co-op fight hooks |
| `party_loot.cpp` | N | 164 | 0 / 0 | 0 | 0 | 0 | 4 | 0 | 0 | co-op loot; address casts |
| `party_start.cpp` | N | 58 | 0 / 0 | 0 | 0 | 0 | 1 | 0 | 0 | co-op start |
| `party_turn.cpp` | N | 266 | 0 / 0 | 0 | 0 | 0 | 6 | 0 | 0 | co-op turn cues drawn with the game text printer |
| `perf.cpp` | B | 312 | 0 / 0 | 0 | 0 | 0 | 0 | 1 | 4 | frame statistics (MS_AUTOPLAY only); beam clock reads VHPOSR/VPOSR/INTENA |
| `prg_copy.cpp` | A | 49 | 0 / 0 | 0 | 0 | 0 | 0 | 3 | 10 | whole-screen blitter copy, BLTxxx registers (a software blit on the PC) |
| `prg_ops.cpp` | B | 76 | 0 / 0 | 0 | 0 | 1 | 0 | 2 | 1 | program screen primitives; COLOR00 writes, clear/copy through the blitter |
| `prims.cpp` | N* | 200 | 1 / 8 | 0 | 0 | 0 | 0 | 1 | 0 | small primitives + dirty-rectangle restore entries; 8 shims |
| `progmain.cpp` | N* | 111 | 1 / 2 | 0 | 1 | 0 | 1 | 0 | 0 | program overlay boot sequence; entries + 1 jsr helper |
| `scene_menu.cpp` | N* | 232 | 1 / 1 | 0 | 1 | 0 | 3 | 1 | 0 | menu/knight select wiring; 1 entry block + 1 jsr helper |
| `scene_places.cpp` | N* | 236 | 1 / 3 | 0 | 0 | 0 | 8 | 0 | 0 | place screens wiring; address casts |
| `scene_town.cpp` | N* | 150 | 1 / 1 | 0 | 0 | 0 | 3 | 0 | 0 | town screens wiring |
| `screens.cpp` | N | 542 | 0 / 0 | 0 | 0 | 0 | 9 | 0 | 0 | town/place screen builders; entries in `amiga/screens_entries.cpp` (`rtScrCall` is gone) |
| `sfx.cpp` | N* | 66 | 1 / 1 | 0 | 0 | 0 | 0 | 0 | 0 | sound-request front end; synth start through a shim |
| `soundbank.cpp` | N* | 104 | 0 / 0 | 0 | 0 | 0 | 0 | 1 | 0 | sound-bank loaders over rt_file_* |
| `sprites.cpp` | B | 191 | 1 / 2 | 1 | 0 | 0 | 0 | 1 | 1 | hardware-sprite data builder neutral; SPRxPT via the copper stub, DMACON SPREN, 1 barrier |
| `synth.cpp` | B | 215 | 2 / 11 | 3 | 0 | 0 | 1 | 1 | 0 | synth driver: `SynthHw` writes raw `0xDFF000+reg`; 6 entries incl. the INT4 handler ending in `rte`; 3 barriers |
| `system.cpp` | A | 167 | 0 / 0 | 0 | 0 | 0 | 0 | 13 | 0 | ACE manager bring-up, boot log, Intuition alert, AvailMem |
| `text.cpp` | A | 53 | 0 / 0 | 0 | 0 | 0 | 0 | 1 | 0 | ACE font/blit text for the splash screen |
| `thunks.cpp` | R | 61 | 1 / 1 | 0 | 0 | 0 | 0 | 0 | 0 | retired (ROADMAP 7.1r), not compiled |
| `wipe.cpp` | N* | 189 | 1 / 1 | 0 | 1 | 0 | 0 | 0 | 0 | progress wipe; 1 shim; calls prg_copy blitter |
| `world_bind.cpp` | N | 64 | 0 / 0 | 0 | 0 | 0 | 0 | 0 | 0 | World over the owned cells (32-bit heap addresses -> pointers) |


Totals over the 77 files: 64 asm blocks / 213 entry labels, 9 inline sites, 19 `jsr` helpers, 19 asm-label cells, 108 address casts.
(ROADMAP M11 says "~45 inline-asm sites, 18 of them in engine_blit.cpp". A raw text count of `asm(` over `src/rt` gives 111 and 18 for
`engine_blit.cpp`, because it also counts the 16 `asm("sym")` pins of the `CEL_BIND` macro and comments; the columns above separate the kinds:
only 9 are inline code, the rest are entry blocks, call helpers and symbol pins.)

Headers that are not neutral although small: `guards.hpp` (A: `OsAccess`, `DosHandle`, `DosLock`, `IrqOff`, `NoRequesters`: the guards that
bracket the OS), `asmcall.hpp`, `copper_stub.hpp` (the copper stub lives at the absolute `rt_copper_list`), `serlog_logwrite.hpp` (A: the
`-include` of `MS_AUTOPLAY` builds; CMake names its path, so it did not move), `display.hpp` (A: includes ACE `simplebuffer.h`),
`system.hpp`, `text.hpp`, `files.hpp` (`rt_file_*` API is portable but the header includes `ace/types.h`), `input.hpp`, `irq.hpp`,
`audio.hpp`, `sprites.hpp`, `wipe.hpp`, `sfx.hpp`, ... (include `<ace/types.h>` only for `UWORD`/`ULONG`; neutral after the types shim).

### 3.1 Outside `src/rt`

| item | state | reason |
|---|---|---|
| `src/main.cpp` | A | ACE `genericCreate` / state manager / splash; one inline-asm stack swap (32 KB static stack, `jsr gameMain`) that a PC `main` does not need |
| `include/rt/abs.h` (generated) | A | the `rt_*` symbols: one contiguous *chip block* at the original offsets (`RT_SCREEN_WORK_SIZE 0x18086`) and the register-ABI entry prototypes |
| `include/rt/stubfn.h` | A | `RT_FN(f)` = byte pointer to a shim (306 uses) |
| `include/rt/enhanced_cells.hpp`, `enhanced.hpp`, `prgops.hpp` | N* | `asm("rt_enh_*")` label pins (portable), declarations for the shims |
| `include/ms/*` (`linked.hpp`, `thunk_run.hpp`, `regs.hpp`, `gen/*`) | R | the retired lifting/thunk pipeline (7.1r); `regs.hpp` is host code used by the test oracle only |
| `include/game/state_bind.hpp` | N* | `extern "C" mogXxx` cells with `uint32_t` pointer values; host-clean, but the cells are generated owned data (section 7) |
| `include/engine/jobs.hpp` | N* | `jobAddr(const void *)` = `(uint32_t)(uintptr_t)`, `jobPtr<T>(uint32_t)`: 98 uses; the one 32-bit-address helper above L0 |
| `include/engine/synth_data.hpp` | N | `asm("g_mogSynthData")` pin (portable) |
| `src/engine/display_fx.cpp`, `src/game/party_loot.cpp` | N | one `asm volatile("" ::: "memory")` barrier each (keeps a loop a loop); fine on clang/gcc x86 |
| everything else in `src/engine`, `src/game`, `include/engine`, `include/game` | N | no ACE/OS/hardware include, no asm (layer lint) |
| `src/lifted/**` | R | test oracle, excluded from every game build |

## 4. The asm sites

213 entry labels in 64 blocks; checked mechanically: every block that is not a `.set` data alias (`abs_stubs.cpp`, the `rt_enh_*` cells,
the `rt_*_key_reset` style one-line aliases), the retired `thunks.cpp` or `crash.cpp` (A) ends in a `jsr`/`jmp` into a C++ function
(`rtFightRun`, `rtSynthInitC`, `rtDrawCel_prg`, ...), so a C path exists for every logical entry. What differs is how it is *called*.

| kind | sites | C path today? | PC replacement |
|---|---|---|---|
| K1 register-ABI entry shim: `rt_x:` saves registers, pushes D/A as C arguments, `jsr rtXC`, restores, `rts`/`rte` | 64 blocks, ~140 real entries in 47 files (`combat`, `mainloop`, `overworld`, `screens`, `engine_blit`, `enhanced`, `synth`, `input`, `palette_glue`, `prims`, ...) | **yes**: each fronts a C++ body (`rtXxxC`, `ms::`, `ms::game::`) | typed C call tables (11.1d); Amiga shims generated from the same table so the Amiga image stays identical |
| K2 call helper: load D0-D3/D5/A0-A2 from a struct, `jsr (a5)`, store D0/D1/A0/A1 | 19 `jsr (An)` in 17 files (`asmcall`, `rtScrCall`, `rtFightAsm`, `rtMainCall`, `rtMainCallD`, `engine_anim` x2, `mainloop` x2, `irq` hook caller, ...) | **no**: they call an address, i.e. a K1 shim | a call through the typed table; the struct stays as the argument block |
| K3 inline `asm volatile` code | `display_ops` 3 (`clr.w`/`clr.l`/`move.l` loops that must not become `memset`), `engine_palette` 1 + `palette_glue` 1 (`jsr rt_synth_fade` / `rt_mog_pal_copy_live` with A0) | loops: **no** (a plain loop or `memset` on the PC); the two `jsr`s: `rtSynthFadeC` exists, `rt_mog_pal_copy_live` is a K1 block with a C++ body | plain C |
| K4 compiler barriers `asm volatile("" ::: "memory")`, `asm("" : "+r"(p))` | `sprites` 1, `synth` 3, `copper_stub.hpp` 1, `display_fx` 1, `party_loot` 1 | n/a | keep (clang/gcc accept them on x86) |
| K5 symbol pins `extern T x asm("sym")` | 19 cells in rt + `enhanced_cells.hpp` 3 + `synth_data.hpp` 1 | n/a | keep; on i686 COFF the pin has no underscore while ordinary C symbols do (section 7.4) |
| K6 stack swap in `main` | `main.cpp` 1 | n/a | not needed |
| K7 `rte` / exception-frame code | `synth` `rt_synth_int4`, `irq` level-4 trampoline, `crash` | no | replaced by the platform handler callbacks (`audioSetChannelHandler`, `timerSetVblankHandler`) |

Per file the counts are in the table of section 3. The biggest single targets: `engine_blit.cpp` (2 blocks / 13 entries), `enhanced.cpp`
(4 / 13), `mainloop.cpp` (1 / 11), `synth.cpp` (2 / 11), `screens.cpp` (2 / 9), `combat.cpp` (2 / 9), `prims.cpp` (1 / 8).

## 5. Platform interfaces

Headers: `include/rt/platform/{display,blit,audio,timer,input,files,log,system}.hpp`, namespace `rt::platform`. Design rules: ACE-free
(`<stdint.h>` and, for blit, `engine/blit.hpp`), one function per distinct action the code above L0 performs today, no function
that nothing calls, RAII guards for the three acquire/release pairs (`timer::MasterIrqOff`, `files::ScopedFile`, `system::MemBlock`; all
pinned, `MS_GUARD_PINNED`-checked). Interrupt-context legality is stated per function. The code above L0 never calls a platform
function directly (it goes through `*Env`/`*Ops` structs wired in `src/rt`); the interfaces therefore sit between the **wiring in `src/rt`**
and the hardware, which is where the 11.6..11.9 backends plug in.

### 5.1 display (`display.hpp`)

| function | today | callers today |
|---|---|---|
| `displayOpen(DisplaySpec)` / `displayClose()` / `displayIsOpen()` | `rt::displayAceActivate` / `displayAceRelease` / `displayAceIsActive` (ACE view, raw copper list, the two game screens as the buffer bitmaps) | `displayHandoverToGame`, `displayDestroy` |
| `displayShow(base)` | `rt::displayShow` (bitplane pointers of the copper list) | `displayInit`, `displaySwap` in `display_ops.cpp` |
| `displayWritePalette12(colours, n)` | `displayWritePalette` loop over `g_pCustom->color[]`, `rtPrgPalLoad` | `display_ops.cpp`, `prg_ops.cpp`, the fade jobs of `engine/palette.cpp` |
| `displayWritePalette24(rgb, n)` | `rtPaletteAgaWrite` (`ms::PaletteWriter24`: BPLCON3 banks + LOCT) | `enhPaletteWriteNow` (enhanced mode) |
| `displaySetBackdrop(rgb12)` | `COLOR00` writes (`prg_ops`, the mog error flash LAB_0D77) | `scene_menu` `pfnErrorFlash` |
| `displaySetWindow(strt, stop)` | `g_pCustom->diwstrt/diwstop` in `diwShakeTick` | `mog_display.cpp` (screen shake, LAB_042C) |
| `displaySetSprite(slot, data)` / `displaySpritesEnable(on)` | `copperStubSetSprite` (SPRxPT in the copper stub), `DMACON` SPREN | `sprites.cpp` (the joystick cursor) |

Not in the interface on purpose: the screen clear/copy loops (plain memory writes on both platforms), the copper stub (an Amiga
implementation detail of `displayOpen`), AGA FMODE/BPLCON resets (inside the Amiga `displayOpen`), the DMACON enable sequence of
`displayInit` (ditto). The PC's copper-list interpretation (11.6b) is below this interface: it needs only the stub's SPRxPT writes,
which `displaySetSprite` already carries, and the palette/window writes above.

### 5.2 blit (`blit.hpp`)

`blitWait()`, `blitBusy()`, `blitRun(const ms::BlitOp &)`. `ms::BlitOp` (`include/engine/blit.hpp`) already is the register image
(`con0/con1/afwm/alwm/modA..D/ptA..D/size/channels`); the four direct register writers collapse into it: `hwBlit` and `copyRect`
(`engine_blit.cpp`), the whole-screen copies of `mog_display.cpp` (`displayBlitScreen`, `A->D`, `BLTCON0 $09F0`, `BLTSIZE $3214` per
plane) and `prg_copy.cpp`, and the mask build/blit loop of `arena_bg.cpp`. `rtBlitWait` (K1) becomes `blitWait`.

### 5.3 audio (`audio.hpp`)

`paulaWrite16/32`, `paulaRead16` (exactly the three callbacks of `ms::SynthHw`; registers DMACON 0x96, INTENA 0x9A, INTREQ 0x9C,
ADKCON 0x9E, AUDx at 0xA0 + 0x10 * ch), `audioSetChannelHandler` (the level-4 handler: `irqSetInt4` and `irqLevel4Default`),
`musicPlay(module, isPal)` / `musicStop()` / `musicSetVolume(0..64)` (what `rt::musicStart/Stop/Fade` ask of ptplayer: create, load, enable,
master volume, stop/destroy, CIA-B timers released). The module sanitising (`sanitisePatterns`, the "original effects" mimic) stays above the
interface: it is game semantics and is portable.

### 5.4 timer (`timer.hpp`)

`timerSetVblankHandler`, `timerSetBlitDoneHandler` (the two level-3 sources `irqLevel3` serves; the COPER source is only acknowledged and
needs no event), `timerVblankCount` (`irqVblCount`), `timerBeamLine`, `timerBeamVhposr` (VHPOSR/VPOSR reads of `perf`, `irq` self-test and
the fighters' `beamPosition` seed), `timerWaitBeam(line, whileWaiting)` (`displayWaitBeam`: line `$F5` reached and left, the program overlay
polling the intro-skip keys while it waits), `timerMasterIrq` + `MasterIrqOff` (`rt_irq_disable/enable`). The body of `irqLevel3` (frame counter, pointer
tick, POTGO, hook list, autoplay tick) is neutral and stays in `rt/irq.cpp`; the 4 KB private-stack trampolines, `systemSetInt` and the
INTENA masks are the Amiga half (follow-up 11.1c).

### 5.5 input (`input.hpp`)

`inputReadPorts(PortState&)` (JOY0DAT, JOY1DAT, CIA-A PRA fire bits 6/7: the reads of `joyPoll` and `inputPointerTick`), `inputArmPots()` (POTGO),
`inputSetKeyHandler(fn(rawCode, isDown))` (the level-2 callback `inputKeyIsr` replaces ACE's `onKeyInterrupt` handshake; `inputInjectKey` is
the same entry for scripted input). The decode (`ms::keyDecode`, `joyRead`, pad/co-op mapping) stays in `engine/` and `rt/input.cpp`.

### 5.6 files (`files.hpp`)

`fileOpenRead`, `fileSize`, `fileRead`, `fileSeek`, `fileClose`, `fileWriteAll`, `fileList`, `fileDirExists` + `ScopedFile`. These are the
dos.library calls under `rt/files.cpp`: the search order, the `PROGDIR:art/` override, the 32 KB buffer, the ADF reader (`rt/adfdisks.cpp`) and the
`rt_file_*` C API stay above and are portable; `systemUse`/`OsAccess`/`NoRequesters` become internal to the Amiga implementation. Paths are the
game's logical ones (`PROGDIR:`, `DFn:`), so the PC maps `PROGDIR:` to the executable's directory and has no `DFn:`.

### 5.7 log (`log.hpp`)

`logLine`, `logFormat` (the 37 ACE `logWrite` sites of `src/rt` and `serLogWrite/serLogf`), `bootLog`, `bootLogMemory`, `fatal`,
`crashReporterInstall/Remove` (`rt::crashInstall/crashRemove`; PC: a fault handler). Named `logLine`/`logFormat` because `logWrite` is an
ACE macro and would rewrite a declaration of the same name.

### 5.8 system (`system.hpp`, not in the ROADMAP list)

`systemStart/Stop` (`rt::systemCreate/Destroy` + ACE managers; PC: SDL), `systemIsPal`, `allocChip/allocFast/freeMem`, `memFreeChip/Fast`,
`memLargestChip` (the boot log), `MemBlock` guard (the platform twin of `rt::MemBlock`). Callers: `main.cpp`, `game.cpp` (overlay backup),
`enhanced.cpp` (temp planes), `knight_colours.cpp`, `audio.cpp` (PAL flag). On the PC "chip" is the emulated chip RAM array.

## 6. What moved, and the follow-ups

Moved to `src/rt/amiga/` (pure moves; only `#include` paths, the `tests/raii_allow.json` key of `crash.cpp` and comments changed):
`crash.cpp/.hpp` (68020 exception frames), `serlog.cpp/.hpp` (Paula serial port), `display_ace.cpp/.hpp` (ACE view + copper list). `CMakeLists.txt`
globs `src/**/*.cpp`, so it needed no change. `serlog_logwrite.hpp` stays in `src/rt/` because CMake's `MS_AUTOPLAY` branch names its path.

Verification of the move (same commit with and without it, both built here): `build-game-debug` has identical section sizes (text 545131, data 58956,
bss 312760) and the same 8,907 `(symbol, size)` pairs; Release (`build`, LTO) has the same functions with the same sizes (two LTO-private suffixes
swap) and a 4-byte smaller `.text` (354464 against 354468; the hunk exe is 584408 against 584412 bytes), probably from the link order (the source glob now lists `src/rt/amiga/` as its own
directory). The object code moved only in address.

Follow-ups (numbers are suggestions; ROADMAP is the integrator's):

* **11.1a** Move the remaining A files: `audio`, `irq` (Amiga half), `palette_aga`, `prg_copy`, `text`, `system`, `display`, `asmcall`, `guards.hpp`,
  `copper_stub.hpp`; `serlog_logwrite.hpp` together with the CMake `-include` path. Mechanical, same pattern as above.
* **11.1b** Implement the eight interfaces on the Amiga side (`src/rt/amiga/{display,blit,audio,timer,input,files,log,system}.cpp`, thin wrappers over the code
  that exists) and route the callers through them, one interface per merge with the boot regression. This is where Release size is checked.
* **11.1c** Split the B files into a neutral core and a seam: `display_ops`, `engine_blit`, `files`, `input`, `irq`, `game`, `sprites`, `synth`, `enhanced`,
  `perf`, `autoplay`, `mog_display`, `arena_bg`, `palette_glue`, `prg_ops`, `modload`, `origload`, `knight_colours`, `adfdisks`.
* **11.1d** Replace the register-ABI layer by typed C calls (section 4: K1 + K2, 306 `RT_FN` references). Largest item; unblocks 11.4 (the PC
  build cannot contain a single m68k `asm` block). **Done: part 1 (2026-10-10, 20 files) and part 2 (2026-10-12, the mog scene cluster), see section 6.1; what is left is the by-address callers (arena / combat_ui / overworld), which run through PC adapters.**

### 6.1 11.1d: how it is done, what is done, what is left

Method (three patterns, all mechanical, each checked against the unicorn tests where they run):

1. **Entry shims move, bodies stay.** The `asm()` blocks of a file go verbatim to `src/rt/amiga/<file>_entries.cpp`; the C++ body each fronts
   (`rtXxxC`, `ms::`...) stays in the platform-neutral `<file>.cpp`. Amiga: the same code, linked as before for the callers that still reach a body through its
   label and for the unicorn tests (their source lists name the entries file). PC: the file compiles on host clang and is linked.
2. **C++ callers call the body.** `call(RT_FN(rt_x), d0, a0, ...)` / `rtMainCall` / `rtAsmCall` become a typed call of the body, declared in
   `src/rt/prim.hpp` ("the primitives of mog's code as typed C++ calls", one line per primitive: the shim it replaces and the file that defines the body).
   A caller never needs the shim: it only translated registers into arguments. The result is smaller Amiga code (no register block, no indirect `jsr`).
3. **Trampolines of the file itself become C++** (`rtNoop`, `rtTownRedraw`, `rtSfxSynth`) and the K3 inline `jsr`s become calls (`mogFadeHook`, `rtMogFightPaletteInitC`);
   where the A/B build `MS_SYNTH_ASM` needs the register entry into the original synth the asm stays under that `#if`.

Tests keep their oracle: a test that ran the C++ against the original asm through `rt_*` labels gets a **reverse shim** in its support code (C arguments ->
registers -> the label that is hooked or defined to the original routine), e.g. `tests/progmain_emu_support.cpp`, the `SFW` macro in `tests/test_engine_scenes.py`,
`CLW` in `tests/test_combat_load.py`, `rtCopyRect` in `tests/wipe_emu_support.cpp`. `test_engine_scenes`, `test_prog_main_emu`, `test_wipe`, `test_combat_load`
and `test_mog_q_ops` still pass, i.e. the typed host produces the same call trace as the original asm.

Files made host-clean (listed in `src/rt/host_clean.txt`, 17 -> 38 with `net_stub`): `noop`, `engine_rnc`, `engine_util`, `game_rules`, `prims`, `gtext`,
`scene_menu`, `scene_places`, `scene_town`, `sfx`, `engine_palette`, `palette_glue`, `net_stub` (logWrite via `src/rt/logwrite.hpp`), `engine_scenes`,
`engine_anim`, `engine_intro`, `engine_jobs`, `wipe`, `progmain`, `loaders`, `combat_load`. New neutral pieces: `rt/file_handle.hpp` (`rt::FileHandle`, split from
`guards.hpp`), `rt/logwrite.hpp`, `rt/prim.hpp`.

**Part 2 (2026-10-12): the mog scene cluster.**  `combat`, `combat_script`, `creatures`, `fighters`, `loot`, `overworld` and `screens` are host-clean (listed in
`host_clean.txt`: 57 -> 64 files; `MS_STUB("11.1d")` sites 8 -> 0), so the PC build links the map, the fights, the town screens and the loot screens; `arena`, `combat_ui` and
`mainloop` already were.  The method is part 1's plus one rule for what is still called by address:

1. Shim asm blocks of the seven files moved unchanged to `src/rt/amiga/<file>_entries.cpp` (`combat`, `combat_script`, `creatures`, `fighters`, `overworld`, `screens`;
   `loot` needed none: its three trampolines became direct calls of `rtRngPercent` / `rtCuiJingleGood` / `rtCuiJingleBad`).  The unicorn rigs list the entries file next
   to the file they test (`test_creatures_emu`, `test_fighters_emu`, `test_mog_q_ops`, `test_boot_map_emu`, `test_mainloop_emu`, `test_places_emu`); `abs_symbols.json`
   and `mog.screens.json` name the entries files as `impl`.
2. `combat.cpp` and `screens.cpp` call the primitives' C++ bodies through `rt/prim.hpp` (64 `RT_FN` sites -> 1: `rt_scr_ritual_done`, an address written into the handler
   table; `rtScrCall` and `call()` are gone, so the Amiga code is smaller: Release `.text` 578668 -> 578552).  No unicorn test links these two files, so nothing needed a
   reverse shim; `rtFightAsm` stays for the two arena-table dispatches of combat (`opCallSpawn`, `opCreatureArena`).
3. What other files (arena, combat_ui, overworld, ...) still reach by address -- `RT_FN(rt_x)`, the handler / arena tables, `rtAsmCall`, `rtFightAsm`, `rtOwAsm` -- needs a
   PC definition of every `rt_x`: `src/rt/sdl/reg_entries.cpp` (the mog primitives and the creature tables of arena.cpp, as in part 1) and one
   `src/rt/sdl/<file>_entries.cpp` per file whose Amiga shims moved.  Each is `void rt_x(rt::CallRegs &)`, i.e. the Amiga shim with the registers read from /
   written to the block, around the same C++ body; the call helpers of the cluster (`rtFightAsm`, `rtOwAsm`) convert their own register block to a `CallRegs` and call
   `rtAsmCall` (one detail: `CallRegs` has no D7; the plane count of `rt_mog_cel_init` travels in the unused `d5` slot).
4. **Creature handlers are typed tables on the PC** (`rt/sdl/handlers.hpp`): the three handler-table entries that are not ported fighters (`rt_scr_dice_idle`,
   `rt_scr_ritual_done`, `rt_ow_dragon_handler`) are `void (*)(uint32_t owner, HandlerResult *)` on the PC, `rtCreatureCall` calls them as that, and they end in
   `handlerExit` (what `LAB_02BA` / `rt_mog_pair_load` does with registers).  The Amiga keeps the register entries; both use the same table address.
5. Other platform seams: `fighters.cpp` reads the beam through `platform::timerBeamVhposr()` (was a raw `$DFF006` read); `overworld.cpp` pins `rt_enh_pic_sz` like
   `combat_load.cpp` does; `rtMogEnterMap` / `rt_mog_quit` have PC entries (`sdl/mainloop_entries.cpp`: the map loop runs in place, the quit scene as a call;
   unwinding the scene stack on quit is the flow's job, 11.4).

Counts: `RT_FN` lines in `src/rt/*.cpp` 228 -> 165 (arena 58, combat_ui 53, overworld 51, files / palette_glue / screens 1 each; the 31 `rt_ar_*` and the arena /
handler table addresses are among them), register-block helpers 8 -> 3 live (`rtAsmCall` in arena / combat_ui, `rtOwAsm`, `rtFightAsm`; `rtScrCall` and the old
`rtMainCall` / `rtMainCallD` are gone, `rtCreatureCall` / `rtCombatCall` are trampolines of the entries files), `rt/prim.hpp` +22 primitives.  Release `.text` -116 B,
Debug unchanged in kind.

Left of 11.1d: the cluster's *callers* of those entries -- `arena.cpp` (58 `RT_FN`), `combat_ui.cpp` (53), `overworld.cpp` (51) -- still call by address through `rtAsmCall` /
`rtOwAsm` and the PC adapters above, because their emulator tests (`test_arena_emu`, `test_combat_ui_emu`, `test_boot_map_emu`, `test_mainloop_emu`, `test_places_emu`)
intercept the `rt_*` labels (`RT_LABELS`: the label of the original routine is the oracle).  Turning those calls into `rt/prim.hpp` calls needs a reverse shim per primitive
in the rigs (C arguments -> registers -> the original's label, like `rtCopyRect` in `tests/wipe_emu_support.cpp`) -- about 55 distinct primitives; the arena set-up /
swap / init tables (`rt_ar_*`, 42 `RT_FN`) become typed function-pointer tables then, as the handler entries above already are.  The PC does not need it to run them.
First light (`build-sdl`, ADFs copied to a temp drawer, `--fast --skip-intro`): the title menu comes up with no `STUB` line (`tests/test_pc_firstlight.py`); the menu does not
react to scripted keys on the PC yet (the same on the commit before this step), so the map and a fight were not reached headless: that is 11.4.

* **11.1e** Host `ace/types.h` shim (`UWORD`, `ULONG`, ... on `<stdint.h>`) and a ratchet test that compiles the 18 clean files and the eight platform
  headers on host (the header half exists: `tests/test_platform_headers.py`); `-D__CODE_CHECKER__` is a stopgap, not a design.

## 7. 11.2: pointers held as 32-bit 68k addresses

### 7.1 Where they are

| place | count | what |
|---|---|---|
| owned data objects (`tools/gen_data.py`, `build/gen/owned_data.*`) | **702 pointer cells** in 93 objects (mog 599 in 60, program 103 in 33); 374 `const char *` members in `owned_data.hpp` | link-time constants (`DC.L LABEL`) inside the original's DATA/BSS hunks: string tables, script/frame tables, job handler tables, hook lists. Their layout (offsets, sizes) is the 68k image's, i.e. **4-byte pointers** |
| `Knight` record (`include/game/knight.hpp`, 132 bytes) | **13** pointer fields: `ulHitTarget`, `ulAttacker`, `ulIdleScript`, `ulScript26`, `ulHurtScripts`, `ulActionScripts`, `ulJobParam`, `ulDamageTable`, `ulWalkScripts`, `ulDefenseTable`, `ulInventory`, `ulEngagedWith`, `ulName` | `uint32_t`, so `sizeof(Knight)` and every `offsetof` static_assert are pointer-size independent; the *values* are 68k addresses |
| other typed records (`Job`, `Lair`, `MapNode`, `CombatCells`, `PlaceCells`, roster, ...) | 47 `uint32_t` fields in `include/game` documented as pointer/table/script/address (the Knight's 13 are among them); 95 address-typed `uint32_t` declarations in `include/game`, 31 in `include/engine` | e.g. `Job` handler and frame list, `Lair` loot, `ulRecordsAddr` of `World` |
| `state_bind.hpp` | 8 pointer cells (`mogCurKnight`, `mogFirstFighter`, `mogUiKnight`, `mogUiInventory`, `mogUiKnight2`, `mogUiInventory2[2]`, `mogCurLair`, `mogCreatureHeap`) + `mogHeapTable[25]` | "Knight*" etc. as `uint32_t`; `mogHeapTable` is the heap pointer table (41 uses of it and of `mogArenaPtrs`) |
| `mog*`/`prg*` ULONG cells in rt | ~107 `extern uint32_t mog...` | cursors, screen bases, list heads, hook slots |
| conversion helpers in `src/rt` | 113 `(uintptr_t)` lines in 27 files; `addr()`-style helpers re-defined in 15 files, `knightAt()`/`invAt()` (or `kn()`/`inv()`) in 6, the `rd32/rd16/rd8/st32/st16/st8` family in 3 (`arena`, `combat_ui`, `loot`) | every one is `uint32_t <-> pointer` through `uintptr_t` |
| `jobAddr`/`jobPtr` (`include/engine/jobs.hpp`) | 98 uses | the same, in L1 |
| code addresses | 306 `RT_FN(rt_x)`; the VBL hook list (9 slots), the job handler table, the main-loop step table, `rt_irq_h4`, `fighters.cpp` handler addresses | 32-bit *function* addresses stored in data and called through K2 helpers |
| `rt_screen_work` chip block | `abs.h`: one block of `RT_SCREEN_WORK_SIZE 0x18086` bytes; both screens, the copper stub and the text cursor are aliases at fixed offsets; screen bases are stored as `ULONG` | chip addresses |
| loaded data | `origload` (10.2a) fills the owned objects from the original binaries and applies their relocations using the layout JSON (`owned_layout.json`: pointer offsets per object) | assumes 4-byte pointer cells |

Pure-rule code (`src/game/rules`) is already clean: the layer lint forbids integer-to-pointer casts and the Knight pointer fields there; `World`
(`rt/world_bind.cpp`) is the one place that turns the cells into pointers.

### 7.2 The two options

**A. 32-bit build (`-m32`, quick).** Everything above stays literally valid: 4-byte pointer cells keep the generated struct layout and the
`owned_layout.json` offsets; `uint32_t <-> pointer` casts are lossless; function addresses fit in the 32-bit hook/step tables; the "chip RAM"
of the virtual Amiga can be any `malloc`ed block (screens, sprites, samples and the blitter all take pointers). Costs: (1) one-time work on
the generator (the COFF underscore below) and removal of the m68k `asm` (11.1d/11.4, needed in both options); (2) 32-bit only: fine on
Windows (WOW64) and Linux with multilib, **not on macOS** (no 32-bit user space since Catalina) and not on ARM-only hosts; (3) a 4 GB
address space limits nothing the game needs.

**B. Base-relative offsets / handles (portable).** Pointers in game data become offsets into one emulated 68k address space (chip + fast
as two arrays or one flat 16 MB array), with `ms::Addr` accessors (`rd8/16/32`, `st*`, `ptr<T>(addr)`) replacing the 113 + 98 casts, the
generated structs becoming offset-typed (`uint32_t` instead of `const char *`, 374 members), the hook/step/job tables holding table
indices instead of function addresses, and the register-ABI layer going typed (11.1d, which A needs as well). This also makes saves and
the netplay state hash (ROADMAP 12.3) address-independent, and removes ASLR from the picture. It continues 9.3d (`World` replaces raw cells).
Cost: a large mechanical change across `rt` (27 files), `gen_data.py`, the struct emitter, `origload` and the diff tests, and it must stay
bit-identical on the Amiga.

### 7.3 Recommendation

1. **Do A first** (`-m32`, task 11.2 decision S): it is the only option that reaches first light (11.10) without touching the 702 + 374 + 108 + 98
   sites, and it is what the roadmap already assumed. Define it as a build mode (`MS_PLATFORM=SDL`, `-m32` / `i686-pc-windows-msvc`), add a
   `static_assert(sizeof(void *) == 4)` guard in the SDL target so a 64-bit configure fails loudly, and make 11.3 (endianness) independent of it
   (it is: the slot types and images are about byte order, not pointer size).
2. **Keep B as the continuation of 9.3d, not a PC prerequisite**: route new code through `World`/accessors, shrink the cast sites as scenes move
   to typed APIs, and decide on offsets when the first non-x86 or macOS target is asked for. The accessor helpers (`addr`, `knightAt`, `invAt`, `rd32/st32`,
   re-implemented in 15 files) should be hoisted to one header now (small, behaviour-neutral, and the first step of B).
3. Work items A needs (suggested 11.2a..): (a) `gen_data.py` emits `_`-prefixed names in its top-level `.set`/`.globl` text when the target is
   i686 COFF (3 sites; section 7.4); (b) a `-m32` host test run of `tests/test_game_state.py`-style layout checks on `owned_data.hpp` (sizes of the
   generated structs equal the 4-byte layout); (c) cast helpers hoisted; (d) alignment: the 68000 tolerates nothing odd, x86 tolerates
   everything, so no new risk, but byte order is 11.3.

### 7.4 Experiments (this machine, 2026-10-10)

* Host compiler: Clang 11.0.1, default target `x86_64-pc-windows-msvc`; MSVC 2022 and Windows SDK 10.0.26100 installed; MinGW (`msys64/mingw32`) present.
* `clang++ --target=i686-pc-windows-msvc h.cpp` (and `-m32`) **links and runs**: `hello 4` (`sizeof(void*) == 4`), 116 KB exe.
* The vendored SDL2 2.32.10 (`tools/sdl2`, git-ignored) has `lib/x86`: a program that calls `SDL_Init(SDL_INIT_TIMER)` and `SDL_GetVersion` built with
  `--target=i686-pc-windows-msvc -I.../include -L.../lib/x86 -lSDL2` and run with the x86 `SDL2.dll` prints `SDL 2.32.10 ptr=4`. So an SDL2 32-bit build
  was available without installing anything.  The same holds for SDL3 3.4.18 (`tools/sdl3`, `lib/x86/SDL3.lib`): `SDL_Init(VIDEO | AUDIO | GAMEPAD)` and
  `SDL_OpenAudioDeviceStream` work under the dummy drivers; the game now builds against SDL3 (owner decision 2026-10-10).
* **Symbol naming trap**: `extern "C" uint32_t mogCurKnight;` plus a top-level `asm(".set mogCurKnight, g_state+8")` links on x86_64 but fails on i686
  COFF (`LNK1220`), because i686 COFF prefixes C symbols with `_` and a raw asm name does not get one. With `.globl _mogCurKnight` / `.set _mogCurKnight,
  _g_state+8` it links and prints the right value. `asm("pin_sym")` pins on both the definition and the extern are consistent and work unchanged.
  `gen_data.py` writes 1 such `.set` per C++ cell name (`tools/cell_names.yaml`) and the alias lines for `referenced_aliases`; those are the only
  generated places affected. Linux/ELF has no underscore.

## 8. Reproduce

```sh
# per-file counts and the table of section 3: the script was a scratch file; the regexes are listed in section 2
# host syntax check of one rt file (ace at D:/Amiga/ace):
clang++ -fsyntax-only -std=c++17 -fno-exceptions -fno-rtti -D__CODE_CHECKER__ -Iinclude -Isrc -ID:/Amiga/ace/include src/rt/flow.cpp
# the platform headers:
py -m unittest tests.test_platform_headers
# 32-bit hello / SDL:
clang++ --target=i686-pc-windows-msvc -I tools/sdl3/SDL3-3.4.18/include h.cpp -o h32.exe -L tools/sdl3/SDL3-3.4.18/lib/x86 -lSDL3
```

## Building the PC version (ROADMAP 11.4)

**C++ standard library (owner 2026-10-11):** allowed in PC-only files (`src/rt/sdl/`, which the Amiga build never compiles), never
in shared code (`src/engine`, `src/game`, the rest of `src/rt`, `include/`): `tests/test_pc_stdlib.py` fails on a C++ standard
header or `std::` outside `src/rt/sdl/`. The installed MSVC STL (Visual Studio 18, MSVC 14.51) requires clang 20 or newer;
the PC toolchain's clang 11 was rejected (`STL1000`); since 2026-10-11 the host clang is 23.1.3 and the STL works.

### Modern C++ in PC-only files (owner 2026-10-11)

Everything under `src/rt/sdl/` is written as modern C++ (C++23). Shared code keeps the freestanding Amiga rules.

- **RAII for every resource:** SDL objects (window, renderer, texture, audio stream, gamepad, subsystems), files, OS handles,
  locks. Use `std::unique_ptr<T, Deleter>` for C handles (one deleter per SDL type, e.g. `struct SdlWindowDelete { void
  operator()(SDL_Window *p) const { SDL_DestroyWindow(p); } };`) or a small guard class when acquire/release is not a pointer.
- **Rule of zero** for plain data and for classes whose members already manage themselves (`std::array`, `std::vector`,
  `std::string`, `std::unique_ptr`): no hand-written copy/move/destructor.
- **Rule of five** for a class that owns a resource directly: write all five, or `= delete` copy and move explicitly (the
  `MS_GUARD_PINNED` pattern of `docs/RAII.md` for pinned guards).
- **No raw `new`/`delete`:** `std::make_unique`; no owning raw pointers.
- **Idioms:** `std::span` for buffers, `std::string_view` for read-only text, `std::optional` for maybe-values, `enum class`,
  `constexpr`/`consteval` tables, `[[nodiscard]]` on functions whose result must be checked, structured bindings, range-for,
  `<algorithm>`/`<ranges>` where they read better than a loop, `std::filesystem` for paths, `<chrono>` for time.
- **Off:** exceptions and RTTI (shared code is compiled into the same exe without them; SDL reports errors by return value).
  So no throwing APIs: `std::filesystem` with the `std::error_code` overloads, no `std::stoi` (use `std::from_chars`).
- **Globals:** `constexpr`/`constinit` only; no dynamically initialised globals (static init order across the shared code).
- The boundary to shared code stays the C-style platform interface (`include/rt/platform/*.hpp`): no STL types cross it.

32-bit, clang `i686-pc-windows-msvc`, SDL3 3.4.18 VC kit unpacked in `tools/sdl3/SDL3-3.4.18` (git-ignored; get
`SDL3-devel-3.4.18-VC.zip` from the libsdl-org/SDL GitHub releases; headers are included as `<SDL3/SDL.h>`). No Amiga toolchain needed.

```sh
export PATH="/c/msys64/mingw64/bin:/c/Program Files/CMake/bin:$PATH"     # ninja, cmake
cmake -S . -B build-sdl -G Ninja -DMS_PLATFORM=SDL -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/pc-i686-clang.cmake        # optional: -DMS_SDL3_DIR=<kit> -DMS_CLANG_BIN=<llvm bin>
cmake --build build-sdl                                                  # -> build-sdl/moonstone.exe + SDL3.dll
build-sdl/moonstone.exe --smoke 5                                        # headless, exit 0; window: no args, Esc closes
py -m unittest tests.test_host_rt tests.test_sdl_build                   # ratchet + build/smoke test
```

SDL3 notes (port from SDL2, owner decision 2026-10-10; `src/rt/sdl`, the only place SDL is used):

* Headers `<SDL3/SDL.h>`, library `SDL3.lib` / `SDL3.dll`, `SDL_MAIN_HANDLED` + `<SDL3/SDL_main.h>` for `SDL_SetMainReady`. Calls return `bool` (true = success).
  The dummy drivers are selected with `SDL_VIDEO_DRIVER` / `SDL_AUDIO_DRIVER` (SDL2: `SDL_VIDEODRIVER` / `SDL_AUDIODRIVER`); `--smoke` sets them as hints. `SDL_INIT_TIMER` is gone
  (performance counter and `SDL_Delay` need no init).
* Guards (`sdl_guards.hpp`) stay the only create/destroy pairs: `SdlRuntime`, `SdlSubsystem`, `SdlWindow` (no position, SDL3 centres), `SdlRenderer` (render driver name or
  nullptr, `"software"` for `--smoke`), `SdlTexture`, `SdlAudioStream` (replaces `SdlAudioDevice`).
* Display: streaming ARGB8888 texture with `SDL_SCALEMODE_NEAREST` (per texture; the SDL2 scale-quality hint is gone), drawn with `SDL_RenderTexture` into the `fitRect`
  rectangle. `SDL_SetRenderLogicalPresentation(..., SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)` was evaluated and not used: it scales by whole factors only, so a 320x240
  logical size (the 6:5 pixel shape) letterboxes on every window that is not an exact multiple, while `fitRect` fills the window with the same 6:5 aspect (and the mouse scaling
  of the input module is derived from the same rectangle). The pixel-exact `--dump-frame` path (`engine/dispframe`, `engine/planar`) is untouched.
* CRT effect (optional, off by default; `src/rt/sdl/crt.cpp`): `--crt` starts with it, `--no-crt` forces it off, **F12** toggles it while playing (the Amiga keyboard ends at F10 and
  F11 is fullscreen, so the game never sees F12; the key is consumed in `frame_loop.cpp` before the input module). The choice F12 makes is remembered in `crt.txt` in the user directory
  (`--pref`), read at start unless `--crt` / `--no-crt` is given; headless runs never read it. Implementation, chosen for robustness over looks: no shader (SDL3's GPU API is a second
  device next to the 2D renderer and fails on old drivers), no per-frame CPU work. Three renderer passes inside the pixel-perfect rectangle: the nearest-filtered picture; the same texture
  bilinear-filtered and added at 110/255 (`SDL_BLENDMODE_ADD`, the glow / horizontal bleed, it also makes up most of the brightness the next pass takes away); an overlay texture multiplied over
  it (`SDL_BLENDMODE_MOD`) holding scanlines (beam centre off-centre in each source row so even 2x shows a dark row), an RGB aperture grille (one triad per 3 output columns, only from 3x)
  and a vignette. The overlay is computed on the CPU once per output size (`crtOverlayPixel`; ~20 ms at 1280x800, ~80 ms estimated at 2560x1600, so a hitch when the window or fullscreen
  changes size) and uploaded as a static texture; per frame it costs two extra quads (CPU submit 0.003 ms, no measurable change in present time at 4x with Direct3D). Below 2x (window smaller than
  640x400) the effect is skipped. Not done: barrel curvature (a warped mesh would break the pixel-perfect scale; would need a shader). Tuning constants are at the top of `crt.cpp`.
  Debug: `--dump-output F.png [N]` writes the composed window (scaling, effect, bars; `SDL_RenderReadPixels` before the present) of the N-th presented frame (default 20; with `--fast` only
  every 50th timer frame is presented); together with `SDL_VIDEO_DRIVER=dummy` (software renderer) and `--scale 3|4` (honoured headless too) it gives the 3x / 4x pictures without a window.
* Input: scancodes are still USB HID usages, so `engine/pcinput` tables hold (the `static_assert`s in `input.cpp` check the anchors against the SDL3 enum). Key events carry
  `down` / `repeat` booleans and `mod` (`SDL_KMOD_CTRL | SDL_KMOD_ALT` for the Ctrl+Alt+G chord); the mouse uses `SDL_SetWindowRelativeMouseMode` and float `xrel` / `yrel`;
  game controllers are `SDL_Gamepad` (`SDL_EVENT_GAMEPAD_ADDED` carries the joystick id to open; A / X / right shoulder -> `SDL_GAMEPAD_BUTTON_SOUTH` / `WEST` / `RIGHT_SHOULDER`,
  B / Y -> `EAST` / `NORTH`, same physical buttons as before). Quit is `SDL_EVENT_QUIT`; Escape is `SDLK_ESCAPE` in `key.key`.
* Files: `SDL_GetBasePath` returns a string owned by SDL (not freed), `SDL_GetPrefPath` still needs `SDL_free`.

## Blitter usage (ROADMAP 11.7)

Surveyed 2026-10-10 from every place that programs the blitter or builds an `ms::BlitOp`: `src/engine/blit.cpp` (cel renderer and
`copy_rect`, shared with mog), `src/engine/bgblit.cpp` (arena background tiles), and the three direct writers `src/rt/engine_blit.cpp`
(`hwBlit`, the sink of the first two), `src/rt/arena_bg.cpp` (its own `hwBlit` for the tile planes), `src/rt/mog_display.cpp` and
`src/rt/prg_copy.cpp` (whole-screen copies). ACE's `blitRect` (`src/main.cpp`, `src/rt/text.cpp`) is the boot test pattern and the debug text
overlay, not the game. Nothing else touches `BLT*`.

| user | BLTCON0 | BLTCON1 | channels | masks A first/last | modulos | size | mode |
|---|---|---|---|---|---|---|---|
| cel gather (`planCel` gather[i]) | `09F0` (A to D) | 0 | A D | edge masks of a clipped cel (`firstMask`, `lastMask`) | A = source skip, D = 2 | h<<6 / w | ascending |
| cel mask plane 1 (mask[0]) | `0100` OR `08F0` / `04CC` / `02AA` per present plane 0..2 | 0 | A B C D (any subset enabled by the OR) | `FFFF` / `FFFF` | all 2 | h<<6 / w | ascending, D = OR of the present planes |
| cel mask plane 2 (mask[1]) | `03AA` OR `08F0` / `04CC` for planes 3, 4 | 0 | B C D, A and B optional | `FFFF` / `FFFF` | all 2 | same | ascending, accumulates onto mask[0] (C = D) |
| cel mask plane 6 (mask[2], enhanced) | `0BFA` (A or C to D) | 0 | A C D | `FFFF` / `FFFF` | 2 | same | ascending |
| cel cookie cut (`paint[i]`) | `shift<<12` + `0FF2` (A over C through B) or `0722` (clear under B, no A data) | `shift<<12` (B shift) | A B C D | `FFFF` / `FFFF` | A, B = 0 or 2; C, D = stride - bytes | h<<6 / w+1 or w | ascending, shifts 0..15, C = D in place |
| `copy_rect` (`planCopyRect`, `planCopyPlanes`) | `09F0` | 0 | A D | `FFFF` / `FFFF` | arbitrary even | h<<6 / w | ascending |
| `copy_rect_desc` (the screen wipe, `src/rt/wipe.cpp`) | `09F0` | `0002` | A D | `FFFF` / `FFFF` | arbitrary even | h<<6 / w | **descending**, start at the last word, no shift |
| arena tile (`bgPlanTile`) | `shift<<12` + `0FF2` | `shift<<12` | A B C D | `FFFF` / **`0000`** | A, C, D = 34, B = 0 | h<<6 / 3 | ascending, shift 0..15, third word catches the shifted tail |
| screen copy (`displayBlitScreen`, `prgCopyScreen`) | `09F0` | 0 | A D | `FFFF` / `FFFF` | 0 / 0 | `3214` (200 rows x 20 words) | ascending, **pointers run on from plane to plane** (5 or 6 blits without reloading A / D) |

Features the game uses: all of A, B, C, D; minterms `F0`, `CC`/`AA` combinations (mask builds), `FA`, `F2`, `22`; A shift (BLTCON0 15..12)
and B shift (BLTCON1 15..12) up to 15, ascending only; first and last word masks on A, including `0000` as the last word mask and the
one-word-wide case; modulos of every channel (positive, 0, 2, 34, stride-dependent, any even value for `copy_rect`); BLTSIZE with
height up to 200+ and width up to 40 words; descending mode with shifts zero; the pointers left advanced after a blit.

Not used anywhere (so the software blitter refuses them): line mode (BLTCON1 bit 0), inclusive / exclusive fill and carry-in (bits 4, 3, 2),
shifts in descending mode, BLTCON1 bits 5..11, and cookie cuts that rely on a disabled channel's stale BLTxDAT register (a channel the
minterm ignores is simply not read). BLTSIZE 0 rows / 0 words (meaning 1024 / 64) is implemented although the game never sends it. The
blitter-nasty bit and blit interrupts are not used either (`WaitBlit` polls DMACONR bit 14 only).

`src/engine/softblit.cpp` (`ms::softblit::run`) executes exactly this set, word by word over a big-endian byte array, and returns
`BLIT_UNSUPPORTED` for the rest; `src/rt/sdl/blit.cpp` is `rt::platform::blit` on top of it (`blitRun` runs the whole blit at once,
`blitWait` is empty, `blitBusy` is false).  The direct register writers (`displayBlitScreen`, `prgCopyScreen`, `arena_bg`'s `hwBlit`) go
through `blitRun` on the PC with one `BlitOp` per plane (11.1c); a screen copy that leans on the advanced pointers builds the op of plane
p with `ptA + p * 8000`, `ptD + p * 8000` (`softblit::BlitEnd` returns the register values a real blit would leave).

## Paula usage (ROADMAP 11.8)

Surveyed 2026-10-10 from `src/engine/synth.cpp` + `src/rt/synth.cpp` (mog's synth), `src/rt/audio.cpp` (program's music through ACE ptplayer), `src/rt/irq.cpp`
(the level-4 handler), docs/AUDIO.md and a register-level run of all 167 synth sequences (`ms::synth*` with a recording `SynthHw`: 400 ticks per
sequence on a voice). Nothing else in `src/` touches `AUDx`, `DMACON` audio bits, `ADKCON` or the AUD interrupt bits (the only other `DMACON`
writes are the display's `$8380/$8040/$8020/$8400`, the blitter and sprite bits; `INTENA` master brackets are `rt_irq_disable/enable`).

**Two users, never at the same time** (the music is stopped at mog's entry, `rt_audio_quiesce_mog`):

| user | how it drives Paula | what the PC needs |
|---|---|---|
| mog's synth (all music and every effect after the knight select) | raw register writes through `ms::SynthHw`; sequencer tick from the VBL job list (50 Hz), loop/one-shot logic in the **AUDx interrupt handler** (`synthInt4`, level 4) | the register file, DMA state machine with LC/LEN latching, the per-channel interrupt requests, a handler call at the chip time of the buffer end |
| program's music (intro, prologue, ending) | ACE ptplayer 6.3: module tick from **CIA-B timer A** (125 BPM = 50 Hz), a **CIA-B timer B** one-shot (576 ticks) between "DMA on" and "write the repeat LC/LEN"; no audio interrupts (INTENA AUDx masked, completion polled from INTREQR for sfx only, unused) | a ProTracker replayer ticked at 50 Hz and the same two-phase register protocol (`ms::modTick`, `ms::modLoopPointers`), no CIA |

**Registers and how often**

* `AUDxLC` (long), `AUDxLEN`, `AUDxPER`, `AUDxVOL` for channels 0..3; **no `AUDxDAT` write anywhere** (no manual playback).
* `PER` and `VOL` are written for every voice on every tick (50 Hz): the synth's vibrato/tremolo/envelope make the period and volume change per
  vblank. Volumes seen: 0..63 (synth), 0..64 (module, scaled by the master volume, `rt::musicFade` steps it down by 4). Periods seen in the synth: 1 (a
  parked channel: `restBuffer` writes `VOL 0, LC dummy, LEN 1, PER 1`), 0 (an idle voice's period before its first note, volume 0) and 113..1209 for
  notes (no value in 2..112 is ever written); the module uses the ProTracker range 113..856. Periods below 113 are therefore only ever written to
  silent, parked channels.
* `LEN` 1..20613 words (the longest instrument sample, 41 KB); LEN 1 is the one-word silent buffer (`ulWaveBase`, `ulWaveBase + 168`).
* `DMACON`: `$800F` at init (the master bit DMAEN is the display's business and stays set), per channel `1 << ch` (off) and `$8000 | 1 << ch`
  (on), and one quirk kept verbatim: `synthStart` ends with `DMACON = $0002`, i.e. it switches **channel 1** off whatever channel it started.
* `INTENA`/`INTREQ` AUDx bits (`$0080 << ch`, SETCLR form): enabled only while a channel's loop logic needs the interrupt; `INTREQ` is cleared by
  the handler and by the start code. `INTENAR`/`INTREQR` are read by the handler (loop over the pending bits, acknowledge, check the enable bit).
* `ADKCON`: written once, `$00FF` (SETCLR bit clear = clear the eight modulation bits): **no attach modulation** of volume or period is ever used.
  The CIA-A PRA bit 1 (audio filter / LED) is never touched by the game; ptplayer's `mt_reset` clears it (filter off).

**The DMA tricks the model has to honour** (each one is in `tests/test_paula.py`, against an independent Python model and with the real synth on top)

1. **The DMA start raises the channel interrupt, after the CPU's trailing writes.** `registerPass` writes `LC, LEN, DMACON on, INTREQ clear, INTENA on`
   and relies on the chip's start request arriving afterwards (the interrupt count `uwIrqCount` is 2 for a one-shot, 1 for a loop). In the model a
   DMACON write only marks the channel STARTING; the start (latch LC/LEN, raise the request, first word) happens at the next time advance.
2. **New `LC`/`LEN` take effect at the next reload, never mid-buffer.** The start interrupt handler writes the *next* buffer: for a loop instrument
   `LC = sample + loopOffset, LEN = words - loopOffset / 2` (used when the first full pass ends, then repeated by the plain reload); for a one-shot it parks
   on the one-word silent buffer. The module player does the same without an interrupt: after the DMA start delay it writes the repeat pointers.
3. **One-shot vs loop is decided by the handler, not by the chip**: the chip only reloads. A one-shot ends when the second request (the reload that
   starts the parked silent word) makes the handler switch the channel's DMA off; the channel then holds its last output byte (the silent word is 0).
4. **Re-triggering**: `synthStart` and `noteSilence` stop a channel (DMACON off, INTENA off, INTREQ clear), park it (`VOL 0, LC dummy, LEN 1, PER 1`) and start it
   again; the module player clears the DMACON bit, writes LC/LEN/PER/VOL and sets the bit.
5. **Period and volume are changed freely while a channel plays**: the period is read when a byte starts (a change applies from the next byte), the volume at once.

**Not modelled** (never used): `AUDxDAT`, attach modulation (`ADKCON` bits 0..7 are stored and reported, never applied), the LED filter *as a machine state*
(the game never toggles it; the PC's post filter has LED variants for listening only, below), the sub-sample start-up delay of the real chip, CIA-B timers (the module tick is the vblank), and `LEN 0` / `PER` below 113 beyond the documented rules
(`LEN 0` = 65536 words, `PER 0` = 65536, `PER < 113` plays as 113: the DMA slot limit of PAL).

### The model and the platform (`include/engine/paula.hpp`, `src/rt/sdl/audio.*`)

* `ms::paula` (pure, `src/engine/paula.cpp`): register file + four DMA machines advanced in output frames at any rate (48000 Hz), big-endian sample bytes
  from the `ChipView`, Amiga panning (0,3 left; 1,2 right) with a separation (default 70), and two renderers for the staircase Paula outputs (below): band-limited
  steps (default) or the exact integer box filter. The analogue output stage (`--audio-filter`) is a separate post filter, `engine/outfilter`.
  `ms::modplay` (pure) is the ProTracker replayer for program's music.
* **Threading** (SDL3 push model): the model runs on the *game thread in virtual time* (`audioOnBoundary` after each 50 Hz vblank handler: modTick, 960 output
  frames, `synthInt4` called from inside the render at the exact chip time of the buffer end). The finished frames are handed to SDL with one
  `SDL_PutAudioStreamData` per vblank into the stream of `SDL_OpenAudioDeviceStream` (guard `SdlAudioStream`; SDL converts to the device format and its own
  audio thread pulls from the stream under its own lock). There is no callback, no ring and no atomics of ours; SDL's thread never touches the model or a
  register. The device opens paused and is resumed when 2400 frames (50 ms: 10 ms of silent pad + 2 vblanks) are queued (prefill, "Output robustness" below);
  `SDL_GetAudioStreamQueued` drives the accounting: a chunk that would take the queue past 16384 frames (~340 ms) is dropped (`--fast`: the game outruns the
  device), a stream found empty after playing started counts an underrun, pauses the device and buffers up again with a larger pad. (SDL2 version: a hand-written lock-free SPSC ring and an `SDL_OpenAudioDevice` callback, same prefill and
  capacity; the WAV dump and the model are untouched, `--dump-audio` is byte-identical.)
  Why not a command queue rendered in SDL's thread: the level-4 handler must run between statements of the game thread, and output that depends on audio-thread
  scheduling could not be tested or dumped. Consequence: the samples are a pure function of the register writes and the vblank count (`--dump-audio f.wav`,
  `--fast` reproducible); latency is the queue (~40 ms on average, 50 ms right after the prefill) plus the 512-frame device buffer (`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`, ~11 ms).
* Smoke: `moonstone.exe --smoke N --fast --dump-audio f.wav --audio-render box --audio-filter off --audio-separation 100` plays a four-channel test tone through `platform::paulaWrite16/32` (rt/sdl/smoke.hpp); the test
  compares the WAV sample for sample with the Python model (the options pin the box renderer; the defaults are compared with the BLEP model, below).

### PC audio quality: band-limited synthesis, filters, settings, robustness (2026-10-26, ROADMAP 11.8a)

Options (all PC only; the Amiga build and its sound are untouched, `engine/paula*`, `engine/outfilter` and `rt/sdl/audio*` are not part of it):

| option | values | default | what |
|---|---|---|---|
| `--audio-render` | `blep`, `box` | `blep` | `blep`: every step of Paula's staircase is inserted as a band-limited step (below); `box`: the exact box filter (the old renderer, kept for A/B and as the test reference) |
| `--audio-separation N` | 0..100 | 70 | 100 = Amiga hard panning (channels 0, 3 left; 1, 2 right), 0 = mono; 70 sounds natural on headphones |
| `--audio-filter` | `off`, `a500`, `a500led`, `a1200`, `a1200led` | `a1200` | the analogue output stage of the machine (below) |
| `--audio-save` | | | write the settings in force to `<user directory>/audio.txt` |

**Settings file** `audio.txt` in the user directory (`--pref DIR`, else SDL's pref path): `render=blep`, `separation=70`, `filter=a1200`, one per line, `#`
comments, case-insensitive; a missing file, key or a value that does not parse leaves that setting at its default (the other lines still apply). The command
line overrides the file for that run; `--audio-save` writes the effective settings back (so `moonstone.exe --audio-separation 100 --audio-save` is remembered).
Nothing else writes the file: a plain launch and every headless run leave the user directory alone, and `--smoke` without `--pref` does not read it (tests must
not depend on the developer's file). The CRT toggle of the display branch keeps its own file; the two do not share one.

**Band-limited steps** (`RENDER_BLEP`, `engine/paula.cpp` + `engine/paulablep.cpp`). Paula's output is a staircase: each byte is held for one period (>= 113
ticks of 3.5469 MHz, i.e. a step rate up to 31 kHz) and the box renderer averages it over each 48 kHz frame. That averaging folds everything the staircase has above
24 kHz back into the audible band as aliasing (a 28.6 kHz staircase of a 447 Hz sine puts an image at 19.8 kHz, -42 dB below the tone). BLEP instead keeps the
staircase exact in chip time and adds each step (a byte or volume change of a channel, with its exact position inside the frame) to the output as a band-limited
step: the integral of a half-band Kaiser-windowed sinc (beta 8, 32 taps, -6 dB at 24 kHz, > 80 dB down above ~28 kHz); output = the held level at the sample
instant + the residual `delta * (S(x) - U(x))` of the steps within +-16 samples. That is the staircase low-passed at Nyquist and then sampled: aliasing is gone
(the same test tone: -113 dB), a held level is reproduced exactly once a step's kernel has passed (DC exact, tests), and the true high-frequency content of the
staircase (the "Paula grit" up to 20 kHz, which the box renderer rolls off by 1 to 4 dB there) is kept. The kernel is linear-phase, so the output lags the chip by 16
frames (0.33 ms); the interrupts are unaffected (they run in the chip-time loop, not in the output). The kernel table (32 taps x 1025 phases, Q24, linear
interpolation between phases) is computed in double at start and used with integers only. Gain: with the default separation 70 the output is 1.3 to 2.3 dB quieter than
hard-panned (the channels share both speakers), the renderers themselves agree within 0.1 dB.

**The game hears the same Paula.** Both renderers are fed by one chip-time loop (`stepFrame` / `integrateFrame`: the only place that advances a channel, fetches a
word, raises and delivers a request), the BLEP code only records the level steps that loop produces. `tests/test_paula_blep.py` runs random scenarios in both modes
and requires identical (frame, channel) interrupt logs; headless play (`pc_play.sh`, title music and a lair fight) logged 236 interrupts in 6100 vblanks with the old
build, `--audio-render box`, and BLEP alike, and the box WAVs are byte-identical to the old build's.

**Output filters** (`engine/outfilter`, applied to the model's frames before the device and the dump): the real machines' analogue stage is a 5.2 Hz high-pass (AC
coupling, removes the DC of a held sample); on the **A500** a fixed one-pole low-pass of 360 ohm / 0.1 uF = 4421 Hz; on the **A1200** the fixed low-pass sits at ~34.4 kHz
(a 48 kHz output is hardly touched: about -0.1 dB at 20 kHz); and on both the **LED filter**, a second-order low-pass (3090 Hz, Q 0.66) that the CIA-A PRA bit 1
(power LED) switches in on top. Figures as modelled by the pt2-clone / WinUAE (from memory of those sources, not re-derived from schematics). **The game never
touches that bit** (ptplayer's `mt_reset` clears it, i.e. LED filter off), so the faithful A1200 is `a1200` (the default); `a500` is the dull A500 sound, the `led`
variants exist for listening.

**Output robustness.** The game thread pushes 960 frames per vblank; the device pulls whole buffers. The queue must hold more than one buffer after the vblank-sized
pushes have taken their share, or the stream runs dry (the old 1920-frame prefill with a 1024-frame device buffer left no slack for a vblank later than ~19 ms, and
`SDL_GetAudioStreamQueued` hitting 0 is how an underrun is logged). Now: device buffer 512 frames; prefill 2400 frames (10 ms of silent pad before the first vblank + 2 vblanks =
50 ms; the average latency, queue + device, stays about 50 to 60 ms); each underrun adds 10 ms of pad (up to 20 ms) for the re-prefill; the exit log gives the
fewest frames the queue held before a push (`least queued`), the slack actually used. Clock drift between the 50 Hz timer (performance counter) and the sound card
is steered away: in real-time runs the queue level after each push is averaged over half a second and compared with the level the first seconds settled at; beyond
8 ms the stream's playback ratio is moved by `error / 480000` (at most +-0.4 %, so < 7 cents; SDL resamples only then, with a mild loss above 18 kHz), inside the dead band it is
exactly 1. The WAV dump is taken before all of this. Measuring caveat: SDL's `dummy` audio driver rounds its buffer period to whole milliseconds, so its effective rate
differs from 48 kHz by a few percent for most buffer sizes (512 frames: 10 ms = +6.7 %): underrun and drop counts under `SDL_AUDIO_DRIVER=dummy` say nothing about a real
device; the 1024-frame setting happens to be close to real time there.

## Input, files, log and crash (ROADMAP 11.9)

Code: `include/engine/pcinput.hpp` + `src/engine/pcinput.cpp` (pure, host-tested), `src/rt/sdl/{input,files,gamedata,crash,log}.cpp`. Tests:
`tests/test_pc_input.py`, `tests/test_pc_files.py` (both through `build-sdl/moonstone.exe`, mutation-checked).

### Input contract (what `src/rt/input.cpp` expects)

* **Keys**: the *raw Amiga key code* 0..$7F (the code before the `(key << 1) | released` encoding of `rt::deliverKey`; ACE's `KEY_*`), press and
  release edges, by physical position on a US keyboard (`ms::keymapRaw`, indexed by the USB HID usage = `SDL_Scancode`). The game's own translation
  table (`prgKeyXlat` / `mogKeyXlat`) is applied later by `ms::keyDecode`, so the PC never sends PC scan codes. Left and Right Ctrl are both $63, Left /
  Right Alt $64 / $65, the Windows keys the Amiga keys ($66 / $67), NumLock and ScrollLock the keypad `(` `)`, PageUp / Help = Help.
* **No key repeat**: the Amiga keyboard sends none (input.device repeats for ordinary programs; this game owns the CIA), so SDL's repeat events are
  dropped and a held key is one press and one release. Releases of keys never seen going down are dropped; a lost window focus releases every held key.
* **When**: events are not delivered where SDL queues them but at the frame boundary, from the frame hook (`frame_loop` `pumpEvents`: `inputFrameBegin`,
  every event through `inputHandleEvent`, `inputFrameEnd`), i.e. between two statements of game code, like the level-2 interrupt would be. Escape leaves the
  program only while no key handler is registered (then it is a game key).
* **Mouse**: relative motion (`SDL_SetWindowRelativeMouseMode` on the window that last got the focus; fractional motion from high-DPI devices is carried to the next event; grabbed while the window has focus; Ctrl+Alt+G toggles, `--no-grab` disables) scaled from window pixels to the game's
  320x200 counts with the same fit as the display (`fitRect`, 6:5), kept as 16.16 remainders, rounded toward zero, at most 127 counts per axis per frame
  (the game's `mouseDelta` reads a sample difference in -127..127). `JOY0DAT` = V counter << 8 | H counter, both wrapping bytes; the game's own `255 - d`
  wrap quirk loses one count per wrap, as on the Amiga. Left button = `isFire0` (CIA-A PRA bit 6), right = `isRight0` (POTGOR bit 10).
* **Joystick**: port 1 = cursor keys + Right Ctrl / Space (`--joykeys none` turns the keys off) OR SDL gamepad 1 (`SDL_Gamepad`; d-pad, left stick beyond 12000/32767,
  A / X / right shoulder = fire, B / Y = `isRight1`); port 0 = gamepad 2 while one is connected (it replaces the mouse, as a stick in the mouse
  port does), else the mouse. Direction bits are written as `JOYxDAT` exactly as `ms::joyDirections` reads them (`ms::joyEncode`); opposite directions cancel.
  The keyboard joystick keys also reach the game as keys (the co-op key set A reads them from the key table).
* **Script**: `--keys "@2 Right +Left_Ctrl -Left_Ctrl ~Right mouse=3,-2 lmb+"` (or `--keys-file F`, the same text from a file: a soak script does not fit the command line;
  32768 events at most) (see `inputScriptParse`) injects the same events at chosen frames and

  `--smoke` logs `input: frame N key down 0x4E` / `input: frame N ports joy0=.. joy1=.. fire0=..` lines: the proof that they reach the platform state.

### Local co-op on one PC (ROADMAP M8; 2026-10-25)

The co-op game of the Amiga (title menu Players 2..4 + Mode Coop, one controller per knight on the knight screen) runs unchanged on the PC: the
controllers are the four of `engine/pad.hpp`, fed by this platform layer.

| controller | on the PC | fire |
|---|---|---|
| Joystick 1 | cursor keys, or gamepad 1 (port 1) | Right Ctrl or Space (Space is the status sheet on the map: the pad word is the same, the key is also a game key) |
| Joystick 2 | gamepad 2 only (port 0; without one port 0 is the mouse) | gamepad A / X / right shoulder |
| Keys arrows | cursor keys again, as the Amiga's keyboard set (Ctrl / Right Shift fire): only usable with `--joykeys none`, else it is Joystick 1's keys | Left / Right Ctrl, Right Shift |
| Keys WASD | W A S D, always there (the game's own keyboard set, read from the key table) | Left or Right Alt |

Two players on one keyboard need nothing: knight 1 gets Joystick 1 (arrows), the Control line of knight 2 starts on **Keys WASD** (the
knight screen skips controllers the PC cannot drive, `rt/sdl/input.cpp` `padUsable` through `ms::game::g_pfnPadUsable`, null on the Amiga =
unchanged offer; left / right on the Control line still cycle the free usable ones, and when none is left an unusable one is offered rather than
nothing).  Three or four players: gamepads for Joystick 1 / 2 (`--joykeys none` then frees the arrow keys for "Keys arrows" of knight 3 or 4),
W A S D for the other.  Two keyboard sets at once were ghosting-checked for the Amiga matrix (`tests/test_keyghost.py`); a PC keyboard has its own
rollover, so three keys of two sets in a corner of the matrix (arrows + Right Ctrl + W / A / Alt together) can drop on cheap keyboards.
The sheets and the loot screen are closed by the owner's own fire after moving the cursor onto the pillar with the owner's own keys (the other
players' keys do nothing in that moment: the party layer reads the current knight's controller).  `--keys` drives every set by key name
(`+Left_Alt -Left_Alt`, `+D`, `+Right_Ctrl`), so the whole co-op game is scriptable: `tests/pc_play/pc_play.sh coopmap|coopfight|cooploot|cooptown|coopswitch|coopshop|coopsheet|coop4|coop4lair`.

**One human, computer team-mates** (ROADMAP 8.3b, `docs/MOONSTONE2.md` section 9): the Control line of knights 2..4 has **Computer** as one more stop after the
last controller (one Right from Keys WASD on a PC without gamepads), so one player with the cursor keys plays a whole party.  The human walks the party every round, the
computer knights fight beside it and buy what it buys in town.  Scripts: `pc_play.sh coopaiknight|coopai|coopaifight|coopailoot|coopaisheet|coopaishop|coopaishopsheet|coop4ai|coop4ailair`;
the random-key soak `py tests/pc_play/coop_soak.py --players 4 --frames 90000 --seeds 1 2 3` (1 human + 3 computers; the script is written to a file and run with `--keys-file F`,
the command line is too short for a soak).


### Files

`PROGDIR:` is the directory of the executable for reads and the **user directory** (`SDL_GetPrefPath("MoonstoneACE", "Moonstone")`, e.g.
`%APPDATA%\MoonstoneACE\Moonstone\`; `--pref DIR` overrides) for writes: mod exports, saves, anything the game writes. A read of `PROGDIR:x` looks in the
user directory first, so a mod the player edited shadows the shipped one; the install directory is never written to. `DF0:`..`DF3:` do not exist on the
PC; any other volume is refused; a host path (`C:/x`, relative) is used as it is. If a name does not open as typed, the path is matched component by
component without regard to case (Linux and macOS; Windows already does). Logs stay next to the exe (`moonstone.log`, `crash.log`), not in the user
directory, so they are easy to find.

Game data (`src/rt/sdl/gamedata.*`, the PC twin of `rt/files.cpp` + `rt/adfdisks.cpp`): `PROGDIR:data/<name>`, `PROGDIR:<name>`, `<disks>/<name>` (extracted
files), then the ADF images in `<disks>` (default `PROGDIR:disks`, `--disks DIR`) and `PROGDIR:` read by `engine/ofs` (the reader of ROADMAP 10.2b, host-clean as it
is). An image is any 901,120-byte file that mounts and holds a marker file of disk A (`program`, `mog`), B (`He1.ob`) or C (`be1.c`). A hit of at most 8 bytes
(disk A's 5-byte `kn1.ob`) loses to anything bigger; of several ADF copies the largest wins. `--read a,b,c` logs size, CRC-32 and origin of each.

### Log and crash

`rt::platform::logLine/logFormat` write stderr and `moonstone.log`. `crashReporterInstall` (a `CrashReporterScope` in `main`): an unhandled structured
exception writes `crash.log` (exception code and name, fault address as `module+offset`, access kind, registers, stack words that point into code),
appends it to the log and stderr, shows a message box naming the file and ends the process with the exception code; `abort()` (SIGABRT) the same without
registers. Offsets resolve against the map / PDB of the build that crashed (no symbol lookup in the crash path, no minidump). Ctrl+C, Ctrl+Break, closing the
console, logoff and shutdown post `SDL_EVENT_QUIT` (the frame loop returns, the guards run); a second request while the first is pending ends the process.
Headless and test runs (`--smoke`, `--crash-test`) are *quiet*: no message boxes, "message box suppressed" is logged instead. `rt::fatal` logs `FATAL: ...`
and shows a box unless quiet. `--crash-test access|abort|fatal|quit` provokes each path.

## Copper and sprite usage (ROADMAP 11.6b, 11.6c)

Surveyed 2026-10-10 from `src/rt/amiga/display_ace.cpp`, `src/rt/{display,display_ops,sprites,mog_display,palette_aga,palette_enh,input}.cpp`,
`src/rt/copper_stub.hpp`, `include/engine/display_fx.hpp`, `docs/DISPLAY.md` section 7 and git history (`921694b`: BPLCON2).  Everything here is the
game after ROADMAP 7.1r (all C++, ACE owns the view).

### What the game's copper does

There are exactly two lists and neither contains a palette, a mid-frame register change or a wait other than the first:

| list | contents | written by |
|---|---|---|
| the **stub** `rt_copper_list` (COP1LC of the game, restarted every vblank) | 20 commands: `SPR0PTH..SPR7PTL` (command 2n = SPRnPTH, 2n+1 = SPRnPTL, value = chip address of the sprite data past its height word, the null sprite when unused), `COP2LCH/L := ACE's list`, `COPJMP2`, `$FFFFFFFE` | `displayStubInit` at every overlay entry; the sprite code (`copperStubSetSprite`) edits the value words in place with 16-bit stores |
| **ACE's raw list** (`simpleBuffer`, double buffered, both buffers kept identical) | `WAIT x, line 43` (x = `fetchModeGetCopWaitX`, ~$DC: the end of line 43), `DDFSTOP $D0`, `DDFSTRT $38`, `BPL1MOD 0`, `BPL2MOD 0`, `BPLCON1 0`, then `BPLxPTH/L` for 5 planes (6 with `MS_ENHANCED`), terminated by `WAIT $FF,$FF` (`$FFFF,$FFFE`) | `displayAceActivate` once; `displayShow(base)` rewrites the 10 (12) pointer value words at every screen swap |

* **Palette**: never a copper word.  The colours are CPU writes to `COLOR00..31` (`displayWritePalette`, the fade jobs, the vblank palette hook,
  `displaySetBackdrop`'s error flash); the enhanced mode writes 24-bit values through BPLCON3 banks 0 and 1 with LOCT (`rtPaletteAgaWrite`, bank 0 left
  active afterwards).  On the PC these are latched per frame (11.6a); a copper palette split is *supported* by the interpreter but the game never asks.
* **Per-line changes**: none.  Modulos are 0, BPLCON1 is 0 (no scroll), DDFSTRT/DDFSTOP are `$38/$D0` (20 words = 320 pixels, lores, fetch mode 0),
  bitplane pointers change only at the screen swap, which the game does outside the window (after its own wait for line `$F5`).
* **Window**: `DIWSTRT $2C81`, `DIWSTOP $F4C1` (lines 44..243, low-res positions 129..448 = 320 x 200), written by the CPU: the fight screen shake
  (mog `LAB_0427`, `ms::DiwShake`, `displaySetWindow`) adds the table `{+$0800, 0, -$0800, 0, +$0200, 0, -$0200, 0, +$0100, 0, -$0100}` to both registers,
  every third frame: the window and with it the first fetched row move up/down by 8, 2 and 1 lines (vertical only; the horizontal bytes never change), then
  the normal values return.  The picture is not re-fetched sideways; DIW only clips.
* **Mode**: `BPLCON0 $5200` (5 planes, COLOR, lores) or `$6200` with `BPLCON2 bit 9` (KILLEHB: six real planes, 64 colours) in the enhanced build;
  no hires, no interlace, no HAM, no dual playfield, no EHB, no BPLCON1 scroll.  `FMODE = 0`, `BPLCON3 = 0`, `BPLCON4 = 0` (`displayHandoverToGame`).
  `BPLCON2 = $24` (`$224` at six planes): PF1P = PF2P = 4, i.e. every sprite group is *in front of* the playfield (git `921694b`: with 0 the loot / combat
  cursor was hidden behind the picture).  DMACON: `$8380` (DMAEN BPLEN COPEN), BLTEN, `SPREN`, BLTPRI, set once by `displayInit`.
* **Interlace**: none (non-interlaced PAL, 312 lines).

### What the game does with sprites

* One user: the **mouse / joystick cursor** (`input.cpp` `LAB_0575`, `rt/sprites.cpp`): `spriteDmaOn` (DMACON `$8020`), `spriteInstall(0, data, data2)`,
  `spritePlace(0, x, y)` every tick (`spriteControl` writes VSTART / HSTART / VSTOP / the control bits into the data past the height word: screen x + `$80`,
  y + `$2C`, so the screen's top-left is HSTART `$80`, VSTART `$2C`), `spriteOff(0)` = the null sprite (`mogNoSprite`: height 5, no pixels).
* **Data**, built by `spriteBuild` (`LAB_0572`) from the cursor cels: 16 pixels wide only (FMODE 0; no 32- or 64-wide sprite exists in the game).
  A 4-colour cursor is one sprite (positive height word): words `POS, CTL, (A, B) x height, 0, 0`.  A 16-colour cursor is an **attached pair**: sprite 0
  carries planes 0/1, sprite 1 (the odd one) planes 2/3 and `CTL bit 7` (`$80`, the attach bit) in its control word; `spriteInstall` points SPR1PT at the second
  block (`partnerOf`).  An attached pair on an odd first sprite is skipped (as in the original).
* **Only sprites 0 and 1 are ever used**; sprites 2..7 point at the null sprite (the zero control block: VSTART = VSTOP = 0 never matches a line).
* **Colours**: `BPLCON4 = 0`, so the sprite colour bank is 0: sprite 0 takes palette entries 1..3 (pair 0: 4 * 0 + value), the attached pair 1..15.  OCS would
  use 17..19 / 17..31; on the AGA hardware BPLCON4 ESPRM / OSPRM = 0 selects bank 0 (the `$88` bank of 8_5a's pointer tint was entry 128 = 8 * 16, which is why
  this reading is used).  **Open (11.10)**: compare the cursor colour against the WinUAE shot; if it differs, the bank default becomes `$0011`.
* **Priority**: BPLCON2 `$24` as above: sprite groups 0..3 are all in front.  Group (sprite n >> 1) below PF1P wins over the playfield; the model keeps the
  full rule (PF1P 0..4, behind where the playfield pixel is not colour 0) for the tests.
* **Position**: HSTART is in low-res pixels in the same coordinate system as DIWSTRT (HSTART `$81` = the first picture column, DDFSTRT `$38` -> `2 * $38 + 17 = $81`);
  the game's x + `$80` therefore puts x = 0 one column left of the picture.  Sprites are cut off by the display window (BPLCON3 BRDSPRT is 0).  **Open (11.10)**:
  A/B the sprite x origin against WinUAE (`kFirstPixel` in `rt/sdl/display.cpp`, the HSTART rule in `engine/sprites.hpp`).

### The PC model

* `ms::copper` (`include/engine/copper.hpp`, `src/engine/copper.cpp`, pure): MOVE / WAIT / SKIP over big-endian chip RAM against a beam clock of one PAL frame
  (312 lines x 227 colour clocks, 4 clocks per instruction, the hardware's masked beam compare including the wrap at line 256).  Supported MOVE targets: COP1LC,
  COP2LC, COPJMP1/2, DIWSTRT/DIWSTOP, DDFSTRT/DDFSTOP, DMACON (DMAEN, BPLEN, COPEN, SPREN), BPL1..8PT, BPLCON0..4, BPL1MOD/BPL2MOD, SPR0..7PT, COLOR00..31 (with the
  BPLCON3 bank and LOCT), FMODE, NOOP.  Anything else (protected registers below `$80`, INTREQ, DIWHIGH, manual sprite registers, ...) stops the copper and is reported
  once (`display: unsupported copper register or instruction (addr, words)` in the log); the frame is still drawn with what was applied so far.
* `ms::sprites` (`include/engine/sprites.hpp`, `src/engine/sprites.cpp`, pure): the sprite DMA per line (control block read from line 25, data lines VSTART..VSTOP-1,
  the next control read at VSTOP, pointer writes and DMA off mid-frame, null pointer = no data), 16 / 32 / 64 wide (FMODE bits 3..2, wider modes are implemented and
  tested, the game uses 16), attached pairs, colour banks from BPLCON4 (even sprites ESPRM, odd and attached OSPRM), priority against the
  playfield by BPLCON2 PF1P, BRDSPRT, the playfield XOR of BPLCON4 bits 15..8.
* `ms::dispframe` (`include/engine/dispframe.hpp`, `src/engine/dispframe.cpp`, pure): the 312-line frame builder that `rt/sdl/display.cpp` calls: copper -> sprite DMA ->
  playfield row (`planar::rowIndices`, DDFSTRT..DDFSTOP, first pixel at `2 * DDFSTRT + 17`) -> window -> sprites -> extra-halfbrite -> palette.  The output rectangle is
  the standard window (line 44, position `$81`, 320 x 200); a shaken window moves or clips the picture inside it.
* `rt/sdl/display.cpp` latches what the game says through `platform::display*` (palette, shown screen, window, sprite pointers, sprite DMA on/off) into the register
  image at the start of every frame; an installed copper list (`displaySetCopperList`, SDL-only: the game itself installs none, its stub and ACE's list only carry the
  sprite and bitplane pointers that `displayShow` / `displaySetSprite` already hand over) edits the image per line.  Unsupported features (hires, HAM, dual playfield,
  interlace, BPLCON1 scroll, hires sprites, FMODE bitplane modes) are logged once and drawn as if off.
* Tests: `tests/test_copper.py` (independent Python models of the copper, the sprite DMA and the frame; 67 mutations; the `--smoke-copper` frame of
  `rt/sdl/smoke.hpp`: a palette split, modulo and pointer changes, BPLCON2/BPLCON4 changes, AGA banks with LOCT, a moved window, eight sprites).

## First light (ROADMAP 11.10)

`build-sdl/moonstone.exe` now runs the game: `rtGameRun` (`src/rt/sdl/game.cpp`) reads `program` and `mog` from the ADFs (`origLoad`), allocates the two arenas
from the emulated chip RAM and the heap, and runs the overlay chain with a `setjmp` / `longjmp` pair where the Amiga unwinds the stack (`rt_run_mog`,
`rt_run_program`, and the window being closed: `timerSetLeaveHook`).  program: display init, intro (skippable), then mog: boot loads, the flow's Title scene.

```sh
build-sdl/moonstone.exe                              # the game, ADFs in disks/ next to the exe (or --disks DIR)
build-sdl/moonstone.exe --skip-intro                 # the intro is left at its first beam wait, as if Space had been pressed
build-sdl/moonstone.exe --fast --skip-intro --frames 2500 --dump-frame title.png   # headless-fast: the title screen as a PNG
build-sdl/moonstone.exe --pattern                    # the test pattern of rt/sdl/smoke instead of the game (what --smoke runs headless)
```

Proof: `tests/test_pc_firstlight.py` runs the exe to the title and compares the dump with the WinUAE shot `build/shots/ref/play-options-title.png`
(0.000 % of the pixels differ: the PC picture equals the even columns / rows of the 640x400 WinUAE picture, whose scaler blends the others).

What it took (pattern of 11.1c: neutral core + platform seam; Amiga code moved, not changed):

* **Chip RAM is a COFF section group.**  Every chip object of the generated data (`gen_data.py --target pc32`: `section(".chip$m")`), the 2 MB pool of `rt/sdl/chip`
  and the work block `rt_screen_work` (`rt/sdl/abs_cells.cpp`, the offsets of the generated `abs_stubs.cpp`, a test keeps them equal) lie between the markers
  `.chip$a` / `.chip$z`; the blitter and the display see that range.  Without it the cel renderer's scratch (`prg_SECSTRT_27`) was outside the array.
* **Made platform-neutral (15 files, host-clean 40 -> 55):** `display_ops`, `sprites`, `mog_display`, `prg_ops`, `prg_copy`, `engine_blit`, `input`, `introskip`,
  `soundbank`, `synth`, `enhanced`, `origload`, `mainloop`, `arena`, `combat_ui`.  Their 68k register shims moved verbatim to `src/rt/amiga/*_entries.cpp`;
  the hardware went behind `rt/platform/*` (new Amiga implementations `src/rt/amiga/platform_{display,input,timer,audio}.cpp`, `blit.cpp`),
  `rt/display_hw.hpp` (copper stub / DMACON take-over, Amiga only), `rt/chipalloc.hpp`, `rt/chipmem.hpp` (big-endian words in chip RAM: identical code on the 68k),
  `RT_HOOK` (`rt/stubfn.h`: VBL hook lists hold the C body on the PC).  Colour registers are `platform::displayColorRegs()` (the palette tick writes them one by one).
* **PC twins** (`src/rt/sdl/`, TODO(11.1c) to merge with the neutral files): `game` (overlay chain), `rt_files` (the `rt_file_*` C API over `gamedata`),
  `irq` (`irqLevel3` split into the vblank and blit-done handlers of `platform::timer`), `music` (module music through `platform::musicPlay`),
  `synth_entries`, `modload` (live data = `kDefaults`; the mods folder is not read yet), `display_hw`, `reg_entries`.
* **`reg_entries.cpp`** and `sdl/<file>_entries.cpp`: the mog scene cluster (`arena`, `combat_ui`, `overworld`, ...) still calls routines by address with a register contract
  (`call(RT_FN(rt_x), d0, a0..)` -> `rtAsmCall`).  On the PC `rtAsmCall` calls `rt_x` as `void rt_x(rt::CallRegs &)`; every entry is written since 11.1d part 2 (no `unimplemented` ones left).
* Idle loops (`ms::tickIdle`: wait for a key) pump the frame through `platform::timerIdle()`; on the Amiga the interrupts do it.

Left for the boot beyond the title (the stubs; the exe logs `STUB name` once per stub that a run reaches): `arena_bg` (arena backdrop; the scene files themselves are linked since 11.1d part 2), `knight_colours` (co-op colours), `options_io`, the
mods folder, the art override drawer, the `rt_file_list_dirs` drawer listing, and the enhanced 64-colour mode (11.6d).
