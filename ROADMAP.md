# moonstone-ace roadmap

Goal: Moonstone decompiled to C++ on ACE, cross-built on Windows, running on
the A1200 (68020, AGA, KS3.1). Every milestone ends with a game that builds
and runs. The original routines in `../moonshard/moonstone-main/amiga_asm/`
are the reference.

Size: **S** ≤ 1 session · **M** a few sessions · **L** split further when started.
Status: `[x]` done · `[~]` in progress · `[ ]` todo.

---

## Where we are (2026-10-07)

- **The game is entirely C++ and links no asm** (7.1r, `db7dd9f`): `py tools/asm_remaining.py` reports 0 live units for program and mog,
  no vasm step, no `asm/*.o` in the link map. Entries are `rtGameRun` -> `rt_prg_main` / `rt_mog_entry`. Release LTO exe **470,524 B**
  (732,764 B at the first C++ link, 406 KB in the asm era). **75 test modules** in `tests/`; `integrate.py --boot` (headless
  regression: intro skip, menu, knight select, name entry, map; Practice fight) passes.
- **M0, M1, M3, M5 done; M2, M4, M6, M7 are down to boot/playtest checks and a few real tasks.** Everything written for the
  asm + thunk era (M2 runtime shims, the 3.x lifting pipeline and thunks, 4.x shims, the 5.x layouts, the 6.x scene ports) is done or
  superseded by 7.1; `src/lifted`, `tools/lift*.py` and the diffharness stay as the replay oracle only. Each item below says which.
- **Real work left:**
  1. **Boot checks** the regression does not cover (items marked "boot check pending"): lair/dragon/knight fights (6.4, 6.4a, 7.1j),
     map movement and AI (6.3), town/places/loot screens (6.7-6.9, 7.1k), ending (7.4), audio listening A/B (2.6, 4.6).
  2. **2.1** exit path (C7), **2.11** memory budget/arena failure on screen, **2.10** parity screenshots against the original disks.
  3. **7.1s** alias renames and patch-machinery removal, **7.2** 50 Hz per scene on stock and owner config, **7.3** Release/ADF/HD
     install, **7.4** full playthrough, **H4** floppy config.
  4. Backlog: enhanced display 4.8a/4.8c-e. After M7: **M8** (Moonstone 2).
- Docs for modders: `docs/MODULES.md`, `docs/LABEL_INDEX.md` (generated), `docs/MODDING.md` (7.5).
- Measured earlier and still the basis of the engine layout: 99 of program's 223 routines (15.5 KB) were instruction-identical to mog
  routines (trackdisk, IRQ, file cache, IMAGEXCEL, custom-chip init); they now exist once in `src/engine`.

### Routine status ladder (one owner: `tools/symbols.yaml`; history since 7.1r: every routine is C++, the ladder only records how it got there)

| status | meaning | proof |
|---|---|---|
| `asm` | runs from `asm/<bin>.s` | `resource.py --verify` |
| `lifted` | literal `Regs&` C++ body exists and is host-proven | replay cases PASS |
| `idiomatic` | rewritten with real types/names; asm body deleted; callers may still be asm | replay cases PASS, screenshot parity for its scene |
| `native` | all callers are C++; thunk deleted | no `MS_SYM` reference left |

`lift.py` and the thunk generator write the status; humans write only names and notes.
Whether a lifted routine is *linked* in place of its asm is a build fact (the 3.2b swap
list in CMake), not a status: the same body must be able to go in and out for bisecting.

### Housekeeping (before any new M2 work)

- [x] **H1** Commit the baseline (`331a914` on branch `moonstone-ace`, 2026-10-05): `git add moonstone-ace` (the repo-root `.gitignore` already excludes `build*/`, `*.log`, `shots/`; the project `.gitignore` excludes `tests/diff/*/*.json`). Verify the generated `asm/*.s`, `include/ms/gen/*` and `src/rt/{abs_stubs,image_tab}.cpp` are tracked and current (`tests/test_resource.py::test_generated_files_in_repo_are_current`) — S
- [x] **H2** `tools/check.py`: one command that runs `py -m unittest discover tests`, `resource.py --verify`, `routines.py` (symbols validation), `diffharness/run_host.py` and `lift.py --survey`, and exits non-zero on any failure. Every milestone gate below means "check.py is green" — S
- [x] **H3** Make `symbols.yaml` status match reality: `lift.py --prove` marks a routine `lifted` only when its case file PASSes (today 242 files in `src/lifted/`, 278 PASS, 0 entries marked). Add `native` to the validator's allowed statuses — S
  > Done (H2+H3): `py tools/check.py [--quick|--regen|--builds]` runs unittest → `resource --verify` → symbols validation → run_host (regenerates only missing cases, n=500 seed=1) → status drift (4b) → lift survey, and prints a table (full run about 4 min).
  > `tools/status.py` is the only writer of `status:` (line-based, keeps human fields). 216 routines are `lifted`; 25 lifted files have no fuzz cases and stay `asm` (`build/diff/host/nocases.json`, → 3.4a). `native` is allowed.
- [x] **H4** (dropped 2026-10-07 by the owner: HD only, no floppy target) Floppy (ADF) config: the ADF cannot hold the arenas plus the 470 KB exe in 1 MB chip, and needs the original disks in `DF1:`-`DF3:` (the `rt/files` search order). HD config is done (`chipmem_size=4` in both configs). Decide: drop the floppy target or document it as data-less; shares the install work of 7.3 - S
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.

- [x] **H5** Release game link: under `-flto -fwhole-program` every top-level `asm()` block is merged into one assembler file, so the `.weak` stubs in the generated `src/rt/abs_stubs.cpp` collide with real definitions (`rt_irq_disable`/`rt_irq_enable` in `src/rt/irq.cpp`): "symbol already defined". `.weak` only resolves at link time, which the Debug build (no LTO) reaches. Fix in the generator: `abs_symbols.json` marks each implemented function (e.g. `"impl": "src/rt/irq.cpp"`) and `resource.py` emits no stub for it. A test then builds both Release and Debug with `MS_LINK_GAME_ASM=ON` — S
  > Done: `funcs` items may be `{"name", "impl"}`; the four `rt_irq_*` functions are marked implemented in `src/rt/irq.cpp`; `test_implemented_funcs_have_no_stub`. All four builds link (rechecked 2026-10-05).

## M0: Ground truth  `[x]`
Done when we have proof that the listings *are* the game, and know what every label is.

- [x] **0.1** OFS extractor with full chain and size checks; SHA-1 checksums of all three disks (`tools/adfx.py`) — S
- [x] **0.2** All three binaries reassemble to load-equivalent output (`tools/reassemble.py`) — S
- [x] **0.3** Verified label → hunk/offset/line map for nb/program/mog (`build/reasm/*.symbols.json`) — S
- [x] **0.4** Call graph from the listing: JSR/BSR/Bcc/JMP edges, entry points, `JSR (An)` jump-table targets, 18 indirect sites in mog resolved by hand — M
- [x] **0.5** Code/data classification: reachable code vs. data; the 6 data-as-code regions found in 0.2 marked as data — M
  > Result (`tools/callgraph.py`, `build/inventory/SUMMARY.md`): nb/program/mog have 65/223/678 routines and 142/740/2298 edges.
  > Indirect sites: mog has 15 resolved, 1 bounded and 5 unresolved (lines 7784, 17459, 17508, 17511, 28811). LAB_0646 dispatches to 18 handlers.
  > All six IRA mis-decodes are classified as data. mog hunk 9 is a CPU-detect/anti-debug stub (MOVEC, illegal-instruction and trace handlers), not graphics, so it is dropped in M1.
  > Unreached bytes (unknown, not proven dead): 2.5k/7.6k/9.0k.
- [x] **0.6** `tools/symbols.yaml` routine table, the single owner of: label, name, binary, start/end, callers, register inputs/outputs, flags used, hardware/`EXT_` touches, status. Names seeded from the ~100 labels moonshard already cites and from `DOC_*.md` — M
- [x] **0.7** Ghidra headless import (`tools/ghidra/import.sh`, `export_decomp.sh <bin> LABEL…`): hunks packed from 0x100000, relocations applied, our labels applied, functions created at JSR/BSR targets; about 48 s for all three. Follow-up after 0.4/0.5: add jump-table entries as functions and stop disassembling data regions as code. The decompiler loses register-only outputs (e.g. the D5 increment in `LAB_03CA`), so it is a reading aid only — S
- [x] **0.8** Inventory report: routine count per binary/section; leaves vs. callers; hardware-touching vs. pure; CPU delay loops listed (needed for 68020 timing) — S
  > Result for 0.6+0.8: `tools/routines.py` writes the generated facts to `build/inventory/routines.json`; hand-kept names and status live only in `tools/symbols.yaml`, which is validated. `REPORT.md` holds the lift order.
  > Counts (nb/program/mog): pure routines 13/90/337; named 18/64/60, each name with a file:line source. Treat unverified DOC_* names with care (`LAB_025F` is a palette fade, not a VBL wait).
  > CPU delay loop: only program `LAB_006E` (two `DBF` waits around DMACON) → task 2.8.
  > Self-modifying code: nb `SECSTRT_0`; mog `LAB_0F76`, `LAB_0F7C`, `LAB_0FC2` → flag for M1/M4.
  > Overall: M0 complete.
- [x] **0.9** Twin table: `routines.py` emits, for every program routine, the mog routine with the identical normalised instruction stream (labels replaced by a placeholder, comments stripped), and vice versa, into `routines.json` and `REPORT.md`. Basis for 3.7 and the M4 sizing; today the 99/223 figure comes from a one-off script — S
  > Reconciled 2026-10-07: superseded by 7.1r: the shared engine exists once in C++ (`src/engine`, called by both overlays); the twin table only sized the lifting work, which is finished. The 99/223 figure stays as history.

## M1: Linkable asm  `[x]`
Done when program+mog asm assembles to ELF objects that link into one executable with an ACE `main`, with no absolute addresses left.

- [x] **1.1** Project skeleton: `CMakeLists.txt` (C++17, `../ace`, elf → hunk → adf, empty `MS_ASM_SOURCES` hook for vasm `-Felf`), `src/main.cpp` + `src/rt/{system,display,text}`, `moonstone-ace.uae` (A1200/AGA/KS3.1), `BUILD.md`. Release and Debug both build; the Release ADF boots in WinUAE (49.9 fps). Gotchas fixed: Debug needs `-fno-lto -fno-whole-program` on the C flags too, and the startup-sequence is written by Python so it stays LF — S
- [x] **1.2** `tools/resource.py`: generates `asm/*.s` from the IRA listings plus small checked-in patch files (no hand-editing of generated output) — M
- [x] **1.3** Replace absolute RAM addresses with symbols: `$6BEFA` screen, `$7F682+`/`$7F6AE` copper, `$3E0–$400` loader cells, `$7FFxx` trackdisk state; table in `asm/patches/abs_symbols.json` — M
- [x] **1.4** Turn hardware takeover sites (vector pokes `$64–$78`/`$BC`, INTENA/DMACON writes, CIA setup) into calls to `rt_*` stubs — M
- [x] **1.4a** Hunk-9 protection stub: find out which exit a genuine original disk takes. The stub is called every main-loop pass via `LAB_00B4` (mog.asm:167), decrypts itself under TRACE, and exits on a D0 magic value: `RTS`, a tail call into `LAB_0328`, or the `$FF`-fill sabotage at `LAB_03A7`. It is currently patched to `RTS` (unverified) — S
  > Done: the `RTS` stand-in skipped the main menu (Players/Gore/Practice/Select Knight), which is the unlabelled code at `LAB_00B4+$c` that the stub's return chain lands in. `src/rt/hunk9.cpp` replays that chain (`LAB_02CE`, `LAB_00EE`, `LAB_03A7`, then the menu). The menu now matches the original disks (`build/shots/C3-menu-*.png`).
  > Partial result (`docs/HUNK9_STUB.md`): the stub runs on every main-loop pass with no once-flag. It disables interrupts (INTENA $4000), runs a CPU-detect MOVEC, then decrypts and re-encrypts itself under TRACE, with a key that depends on CCR/D1/D2.
  > D0 is a constant ($3d742cf1) planted by the decrypted code, not a disk check or code checksum. In emulation it took the RTS exit, but that RTS popped a synthesized chain into the `LAB_03A7` sabotage, possibly an emulation artefact. Keeping `RTS` is provisional, low-medium confidence.
  > **The ADFs are a Crystal crack, not pristine originals** (cracktro plus a `Crystal` file on disk A), so "genuine" behaviour cannot be proven from them.
  > **Closing procedure (do it once 2.4a shows a picture, so the result can be compared):** boot the cracked original in `build/hunk9/a500.uae`, reach the map, take a `.uss` savestate, read it with `BBHack/read_states.py`: (1) `LAB_064D` must not hold 0x2D0 bytes of $FF (sabotage did not run); (2) vectors $8/$C/$10/$24 and INTENA state; (3) does `LAB_0328` run once or twice per main-loop pass (count with a WinUAE debugger breakpoint). Whichever exit matches is the patch. Also confirm the main loop re-enables interrupts (`LAB_0D7C`) without the stub's INTENA write.
- [x] **1.5** Drop the overlay chain: program's hunk loader (S_4) jumps to "Mog" → a direct call into the linked mog entry; `nb` is not linked at all — M
- [x] **1.6** vasm `-Felf` per binary, symbol prefixes `prg_`/`mog_` to avoid `LAB_` clashes; cross-binary references resolved by name — S
  > Result for 1.2–1.4+1.6: `tools/resource.py` generates `asm/{program,mog}.s` with `prg_`/`mog_` prefixes and 22+24 absolute addresses turned into `rt_*` symbols (`asm/patches/abs_symbols.json`). It applies 11+16 checked patches (loader, IRQ install, INTENA, vectors, TRAP15, hunk 9), and `--verify` shows every difference is inside a patch.
  > The two binaries share no symbols. Both ELF objects assemble; with `-DMS_LINK_GAME_ASM=ON` the link has 0 undefined symbols (484 KB exe, link check only).
  > Hardware access lists for M2: `asm/*.hw.txt`.
- [x] **1.7** Link check: `.elf` links with ACE and stub `rt`, `elf2hunk` produces an exe, map file reviewed for chip-section placement — S
  > Result for 1.5+1.7 (`docs/BOOT_CHAIN.md`): the chain is nb → program → mog → program (ending/diag via `$3E0` bit 7) → mog. Each entry takes only A0/D0 (fast arena) and A1/D1 (chip arena). The loader jumps are patched to `rt_run_*`; `rtGameRun()` loops over the overlays with a flat stack.
  > DATA is restored and BSS zeroed on overlay re-entry, using the generated `src/rt/image_tab.cpp`. Self-modifying CODE is not reset (→ 2.12).
  > Builds with `-DMS_LINK_GAME_ASM=ON` in `build-game`/`build-game-debug`: 406 KB exe, 86 hunks; every CHIP section lands in a CHIP hunk; static chip about 181 KB plus a 376 KB arena.
  > First WinUAE boot: black screen while the original trackdisk code reads the ADF directly (→ 2.7, done). `rt_*` stubs log the first 64 hits via `logWrite`.
  > `MS_LINK_LIFTED` (default OFF) keeps `src/lifted` out of the Amiga build until the thunks (3.2) exist.
  > M1 is complete apart from 1.4a.

## M2: Boots on the A1200 (asm game, ACE runtime)  `[~]` (the asm era is over; what is left is the exit path 2.1, audio parity 2.6, parity shots 2.10, budget 2.11 and the gate 2.13)
Done when the A1200 config boots from ADF and HD; intro → knight select → map → combat → shop is playable; screenshots match the original (WinUAE A500 config, the same cracked disks) at the same scenes; `check.py` green.

**Ownership rule for M2:** while the original code runs, *it* owns the copper
list, bitplanes, blitter, Paula and the CIA-B disk bits exactly as on the
A500; ACE owns only the CPU vectors, the keyboard CIA handshake and memory.
ACE's view/blit manager is used before `rtGameRun()` and after it returns,
never concurrently. Hosting the original copper list in a `copBlock` and
routing blits through ACE is M4 work (4.4/4.8), not M2.

Checkpoints, in order (each is a screenshot in `build/shots/` plus a line here):
**C1** first non-black frame · **C2** intro plays with sound · **C3** knight
select reacts to keyboard and joystick · **C4** map scene (mog) · **C5** one
combat ends and returns to the map · **C6** shop · **C7** ESC exits to
Workbench cleanly and the game can be started again.

- [~] **2.1** Exit path (C7): the original never exits, and `rtGameRun` only leaves when `rt_game_next = OVERLAY_EXIT` (nothing sets it from input). Still to do: a hotkey in `rt/input` that sets EXIT, `irqRemove`, ACE `systemUse` restores DMA/INTENA/copper, arenas freed. Boot check pending: ESC returns to Workbench and the game starts again - S
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
- [x] **2.2** `rt/irq`: VBlank/CIA callbacks via `systemSetInt` driving the original INT handlers (S_15) — M
  > Reconciled 2026-10-07: superseded by 7.1d: the original INT handlers are gone; the IRQ layer is C++ (`src/rt/irq.cpp`, `docs/IRQ.md`, `tests/test_irq.py`). The 50 Hz proof stands (500 VBLs = 500 beam frames) and the boot regression runs through it.
  > Implemented (`docs/IRQ.md`, `src/rt/irq.cpp`, `tests/test_irq.py`): levels 3 and 4 run the original handlers behind faked 68020 format-0 frames on private 2 KB stacks; level 2 is replaced by `rt/input`; levels 1/5/6 (trackdisk) are not installed. `rt_irq_disable/enable` are the original master-bit writes. Serial debug trace every 50th VBL in Debug builds.
  > Proven by a compile-time self-test in WinUAE (`build/shots/irq_selftest.log`): the game's own VBL handler counted 500 frames against 500 beam frames, so it runs at 50 Hz. Not yet proven on screen: that `SECSTRT_15`'s tick advances the intro. Closes with C2.
  > `systemUse`/`systemUnuse` re-enable all registered INTENA bits on exit, which differs from the original masking (documented in IRQ.md; recheck at C2).
- [x] **2.3** `rt/input`: keyboard and joystick through ACE, feeding the original's input variables/scancode buffer — S
  > Reconciled 2026-10-07: superseded by 7.1d: keyboard/joystick are C++ (`src/engine/input.cpp`, `src/rt/input.cpp`); C3/C4 reached 2026-10-05 and `integrate.py --boot` (`tests/boot/regression.txt`: menu, knight select, name entry, map) covers it. The caption screen after the intro is C++ since 7.1i.
  > **C3 reached** (2026-10-05): menu and knight select react to the joystick, and name entry takes keyboard input correctly (`build/shots/C3-name-*.png`); confirming the name loads the map (**C4 reached**, `C3-name-2-return.png`). Before the 1.4a fix, keys arrived garbled or late; this did not reproduce afterwards, root cause unconfirmed. Open: the caption screen after the intro ("The druids sent their best knights…") is black in the port but shows text over Stonehenge on the original.
  > Implemented (`src/rt/input.cpp`): ACE's CIA-A serial callback is replaced by `inputKeyIsr`, which does ACE's handshake and then writes the overlay's `LAB_036C/036D/0362` (`LAB_0B90/0B91/0B86` in mog) exactly as the original INT2 tail did. Joystick/mouse are plain hardware reads and stay in the asm. Proven: A, Space and Return reach the game's tables with its PC-style codes (same self-test log). Closes with C3.
- [x] **2.3a** Intro skip (a feature the original lacks): Space, Return or a fire button during the intro fades to black and enters mog ("Select a Knight"). The ending re-entry is not skippable — S
  > `src/rt/introskip.cpp` + `asm/patches/program.introskip.json`: `intro-skip-arm` (program.asm:131, the first store past the bit-7 ending branch) arms it; `intro-skip-poll` (program.asm:10311, `LAB_0552`'s beam wait, main context only) polls it and replays the intro's own tail (`LAB_025F` fade, `LAB_005B` VBL unhook, then `rt_run_mog`). Keys are latched in `rt/input`'s ISR (`inputSkipPressCount`), because frame waits can be seconds apart during loads.
  > Proven: `build/shots/introskip-{0-before,1-fade,2-select}.png` (one Space tap at 12 s reaches the select screen; without input the select screen comes at about 150 s) and `introskip-ctl-60.png` (no input, intro unchanged). Fire buttons are not tested yet. `uaeshot.ps1 -Steps "wait:12,key:Space,..."` drives WinUAE; it boots a copy of the config with the Windows-message keyboard, since injected keys miss WinUAE's raw-input one.
- [x] **2.4** `rt/display` — split:
  > Reconciled 2026-10-07: all split items closed (2.4a done; 2.4b/2.4c/2.4d superseded by 7.1e/7.1c): ACE owns display and blitter in C++ (`MS_ACE_DISPLAY` default ON).
  - [x] **2.4a** First frame (C1). The game already points `COP1LCH` at `rt_copper_list` (patch `copper-ptr`) and fills `rt_screen_work`; `SECSTRT_29`'s DMACON/BPLCON/DIW/DDF writes are live (the loader's `DMACON $0380` is dead code since 1.5). Find which of these is wrong under ACE: dump `COP1LC`, `DMACONR`, `BPLCON0` and the first 16 copper words after `SECSTRT_29` over the serial trace. Likely culprits: ACE's `viewLoad`ed splash copper still active (the game's `COPJMP1` must win after `systemUnuse`), bitplane DMA left off, or the copper list's bitplane pointer slots (`$7F6AA..$7F6D4` → `rt_copper_bpl*`) still holding the original absolute addresses inside the S_30 DATA image — S
    > **C1 reached** (`build/shots/C1.png`, Mindscape logo, 49.3 fps; `docs/DISPLAY.md`). Root cause was not a hardware wait: the two screen-pointer cells at the start of S_30 (program.asm:10437-10440, mog.asm:24615-24618) still held the A500 addresses `$75A3C`/`$6BDFA`, and `LAB_054D` (program.asm:10242) cleared 40000 bytes at each, wiping the exe/ACE heap.
    > Fix: patch tables `asm/patches/{program,mog}.display.json`. The `rt_screen_work` block now starts at `$6BDFA` (82438 bytes, still one block for 2.4d) and gains `rt_screen_clear`/`rt_screen_b`. `displayHandoverToGame()` zeroes FMODE and BPLCON1–4.
    > **Next blocker (2.4e):** after `bg1a.piv` opens, the screen goes black, the CPU idles, and a Guru `8000 0003` (address error) appeared once.
  - [x] **2.4b** AGA sanity (FMODE/BPLCON1–4 zeroed in `displayHandoverToGame`; palette bank still to verify on a colour scene): once the frame shows, reset `BPLCON3` (palette bank 0, no dual-playfield), `BPLCON4` = 0, `FMODE` = 0 before handing over, and confirm the copper palette writes do not land in a shifted bank. ACE's `systemUnuse` does part of this; verify rather than assume — S
    > Reconciled 2026-10-07: superseded by 7.1e: AGA registers (FMODE, BPLCON1-4, BPLCON2 `$24`) are set in `src/rt/display_ace.cpp`; the colour scenes of the boot regression (menu, knights, map) compare clean against `build/shots/ref/`.
  - [x] **2.4e** Address error after `bg1a.piv` (C1 → C2). Suspects, in order:
    - another absolute address left in DATA, found by scanning S_30 and the other DATA hunks for longwords in `$60000–$80000` that are not relocated;
    - the `bg1a.piv` decode path;
    - the IRQ trampoline's private stack or frame.

    Read the Guru's PC/address from WinUAE's debugger log and map it back to a label with `build/reasm/*.symbols.json` — S
    > Result: the crash was in dos.library's `Close()`, not in game code. The overlays use memory *above* their carve-out as scratch: `LAB_0044` (program.asm:920-955) bumps the free-memory cells, then `LAB_0185` loads `bg1a.piv` into fast and `LAB_0496` loads into chip. `rtGameRun` had allocated only the bump, so loads ran into the dos heap.
    > Fix (`src/rt/game.cpp`): the arena is the bump plus scratch (chip +64 KB, fast +160 KB, a guess with headroom; measured so far +1.3 KB / +28 KB; `arena-high-*` is logged per open, sizes settled in 2.11).
    > `tools/absscan.py` found no other live A500 pointers. Crash tooling: `src/rt/crash.cpp` (vectors 2-7, 9-11 → `PROGDIR:crash.log`) and `tools/crashmap.py`.
    > **The intro plays and "Select a Knight" is reached** (`build/shots/C2-1..3.png`, `C2-4-knight-select.png`, 49.9 fps).
  - [x] **2.4c** Blitter: the game waits for blit-done via the level-3 BLIT interrupt (`LAB_0367`) and polls `DMACONR`; confirm the trampoline latency on a 68020 does not stall `LAB_0324`'s wait, and that no ACE blit runs while the game runs — S
    > Reconciled 2026-10-07: superseded by 7.1c: blitter code and the BLIT wait are C++ (`src/engine/blit.cpp`); no asm waits left. Verified by the boot regression and the 7.1c unicorn tests.
  - [x] **2.4d** `rt_screen_work` is 82 KB of CHIP BSS (`$6BEFA..$80000`) that the original cleared as one block (`SECSTRT_29`/`SECSTRT_34`); keep it one block until M4 so the sub-symbol offsets stay valid. No work, written down so nobody "fixes" it — S
    > Reconciled 2026-10-07: obsolete: `rt_screen_work` and its sub-symbol offsets no longer exist; screens are ACE bitmaps (4.8/7.1e) and CHIP data is C++-owned (7.1n/7.1r).
- [x] **2.5** `rt/palette`: fades and cycling hooks (S_31). Under the ownership rule nothing is needed for M2 (S_31 writes the copper palette words directly); the task becomes "verify the fade-in on C2" and the real work moves to 4.5 — S
  > Reconciled 2026-10-07: superseded by 4.5/7.1e: fades, cycling and ramps are C++ (`src/engine/palette.cpp`, `src/rt/engine_palette.cpp`); the fade-in shows in the regression shots.
- [~] **2.6** Audio parity on hardware: intro/ending modules play through ptplayer and mog's sfx/tunes through the C++ synth (`src/engine/synth.cpp`, `src/rt/audio.cpp`); no asm player is left. Boot check pending: listening A/B of the intro music and the synth against the original (docs/AUDIO.md section 8), and `AUD0..3` INTENA re-masked after the overlay switch - S
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
- [x] **2.7** `rt/files`: trackdisk (S_13) and file cache (S_18) replaced by dos/`diskFile` reads of the original data files; works from ADF and HD — M
- [x] **2.8** `rt/timing`: the one CPU delay loop from 0.8 (program `LAB_006E`, two `DBF` waits around DMACON) converted to a beam or CIA wait. Also list every `VHPOSR` poll (2 in program, 4 in mog) and check each still terminates on a 68020 — S
  > The loop is the tail of `LAB_006D` (two `MOVE.W #$012c,D0`/`DBF` waits at program.asm 1323 and 1329, around the DMACON set; `LAB_006E` is the per-channel setup it calls): 3014 cycles = ~425 us on a 7.09 MHz 68000. `rt_audio_wait` (`src/rt/timing.cpp`, `asm/patches/program.timing.json`) waits 8 VHPOSR line changes (448-512 us) on any CPU. No mog twin.
- [x] **2.9** Disk build: `adf` target (exe + startup-sequence, LF endings) and HD install layout; user supplies data from their own disks via `adfx.py` — S
  > Result for 2.7+2.9 (`docs/FILES.md`): the original OFS-reader entry points (prg `LAB_0390/03B2/03C5/03DA`, mog `LAB_0BB5/0BD7/0BEA/0BFF`) now jump to `rt_*_file_*`, which use dos.library with one static 32 KB buffer, wrapped in `systemUse`.
  > Disk prompts (`LAB_0100`) and the drive scan are disabled for HD installs. Search order: `PROGDIR:data/` → `PROGDIR:` → `DF*:`. Name clash: only `kn1.ob` (A's 5-byte stub loses to B's real file).
  > `--target hdinstall` + `moonstone-ace-hd.uae`. Proven: real files load (`bold.f`, `message.piv`, `mindscape`) in a build that skips SECSTRT_29.
  > TODO: when the game owns the copper list, the `systemUse` guard inside `rt_file_*` must save and restore `COP1LC`/`DMACON` around the DOS call (ACE's `systemUse` reloads its own view), or files must be read before handover / from a VBL-safe window. The floppy boot variant is untested.
- [~] **2.10** Parity check against the original. Boot check pending: `uaeshot.ps1` screenshots of title/map/combat/shop vs the cracked disks in `build/hunk9/a500.uae` (the `build/shots/ref/` references are port-vs-port regression shots, not original parity); `.uss` savestates in `build/states/` as reference RAM for the unknown fields in `docs/GAME_STATE.md` - S
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
- [x] **2.11** Memory budget on a stock A1200 (2 MB chip, no fast RAM): the Release LTO exe is 470,524 B (was 732,764 B at the first C++ link); chip arena + fast arena + ACE + stack still to be summed into `BUILD.md`. Open: a boot-time `memGetFreeChipSize` log line, and `rtGameRun` only logs `ERR: arena alloc failed` where it should fail loudly on screen. Boot check pending: Release exe on a stock 68020/2 MB config (shares 7.2) - S
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > Target note (2026-10-06): the owner's real machine is an A1200 with an accelerator (68020, 8 MB 32-bit fast RAM + 2 MB chip), so "A1200 + 68020 + 8 MB fast" is the primary hardware config (also what the boot regression runs: fastmem 4). The stock no-fast budget stays a Release-build goal, not a blocker.
  > 2026-10-07: Release fits a stock 68020 / 2 MB chip / no-fast A1200 (WinUAE, bare KS 3.1 boot): 264,584 B chip free at the low point; owner machine 1.4 MB chip + 7.2 MB fast free. `rt::bootLogMemory` logs, `rt::fatal` fails loudly (verified with 1 MB chip). Budget in `docs/MEMORY.md`. Open: arena tails could shrink ~145 KB after 7.4; a Workbench-started run on real hardware.
- [x] **2.12** Overlay re-entry: the ending/diag pass re-enters program and then mog (`$3E0` bit 7). `image_tab` restores DATA/BSS but not mog's self-modifying CODE (`LAB_0F76`, `LAB_0F7C`, `LAB_0FC2`, all in S_44, the section that also holds the INT4 audio handler `LAB_0F69`). Either snapshot those CODE sections too or make the three sites write a DATA cell (patch in `mog.json`). Needed before 7.4, cheap now — S
  > Reconciled 2026-10-07: superseded by 7.1g/7.1r: the self-modifying synth code is C++ (`ms::Synth`, cleared on each mog entry) and the overlay reset runs from `rt::g_imageProgram/g_imageMog`; no self-modifying CODE is left to snapshot. The ending/diag re-entry itself is exercised by 7.4.
- [~] **2.13** M2 gate: `check.py` green (about 4 min) and both `.uae` configs boot; C1-C4 reached. Boot check pending: C5 (lair fight returns to the map), C6 (shop) and C7 (exit, needs 2.1) screenshots in `build/shots/`, and C2 sound parity (2.6)
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.

## M3: Lifting pipeline  `[x]` (finished its job; `LAB_03CA` and everything else is C++ and linked, the pipeline now only serves as oracle)
Done when one real routine (`LAB_03CA`, contact test) is in C++, verified, linked into the A1200 build, and the game still runs with it.

- [x] **3.1** `include/ms/regs.hpp`: D0–D7/A0–A6 + CCR model and big-endian memory accessors; host and Amiga builds — S
  > Result for 3.1+3.3: `regs.hpp`, the unicorn harness (`tools/diffharness/harness.py`: gen/fuzz, hardware access is a fault), the host replay runner (`run_host.py`) and 62 one-instruction self-tests checking every flag helper against unicorn.
  > The LAB_03CA pilot passes 2000/2000 cases and catches a deliberate GE→GT bug.
  > Unicorn quirks: `reg_read(SR)` returns stale flags, so the CCR is read via a `MOVE SR` stub; call `ctl_flush_tb()` after writing memory; write SR before A7.
- [x] **3.2** Thunk generator (critical path to M4–M6) — M, split:
  > Reconciled 2026-10-07: superseded by 7.1r: the thunk generator did its job and the swap lists are gone (`MS_SWAP_LIST`, `MS_LINK_GAME_ASM` removed); the game links no asm. `tools/thunks.py` and `tests/test_thunks.py` remain as tooling.
  - [x] **3.2a** Contract per routine from `routines.json`: `reads_before_write` (inputs), `writes` (outputs), `ccr_live_out`, `uses_sp_tricks`, `approx`. Routines with `approx`, sp tricks or `ccr_live_out` get a hand-written contract in `symbols.yaml` (`regs_in/regs_out/ccr_out`) before they may be thunked; the generator refuses otherwise.
  - [x] **3.2b** asm → C++ thunk: `<bin>_<LABEL>:` saves all registers into a static `Regs`, calls `lab_<LABEL>(Regs&)`, writes the outputs and the CCR back, `RTS`. The thunk *replaces* the original label in `asm/<bin>.s`; `resource.py` emits the original body under `<LABEL>__asm` so `--verify` still holds and any routine can be switched back to asm through a CMake list, for bisecting.
  - [x] **3.2c** C++ → asm: `asmCall(sym, Regs&)` for lifted routines that still call asm (`--allow-calls`); same register marshalling in reverse.
  - [x] **3.2d** Gates: `-DMS_LINK_LIFTED=ON` with zero routines swapped boots and matches C1–C4; then with `LAB_03CA` swapped; then with every `pure` replay-PASS routine swapped. A failing swap set is bisected with the CMake list.
    > Reconciled 2026-10-07: superseded by 7.1r: no thunk gate is needed any more; the asm-free link is gated by `integrate.py --boot` (regression PASS) instead of the swap-list bisect.
  > Result for 3.2a–c + the 3.2d build gate (`tools/thunks.py`, `tools/contracts.yaml`, `tools/swap/{none,pilot,pure}.txt`, `src/rt/thunks.cpp`, `include/ms/{linked,thunk_run}.hpp`):
  > - Thunks are appended in the routine's own hunk; the original label becomes `<prefix>LABEL__asm`, with the body kept for bisecting.
  > - Regs live in a 1096-byte stack frame rather than a static, so nesting and recursion are safe.
  > - Only references that can encode it are redirected: JSR/JMP, DC.L, #label, LEA/PEA, in-range `.W` branches (pure list: 549+103 redirected, 86+12 stay on `__asm`).
  > - Contracts: 238 of 241 lifted routines qualify automatically; 3 have hand contracts (`ccr_out`, `sp_delta`).
  > - `ms_call_asm` implements 3.2c.
  > - `-DMS_LINK_GAME_ASM=ON -DMS_LINK_LIFTED=ON -DMS_SWAP_LIST=tools/swap/<list>.txt` links in Debug and Release with none, pilot and pure (216 routines).
  > - Lifted sources and rt glue are built `-fno-lto` so Release keeps them.
  > - Marshalling is tested by running the generated thunk in unicorn.
  > - **Boot gate still open:** boot C1→ each list in turn, then bisect a failing list by deleting lines. First suspects: the SECSTRT_* entries in pure.txt, then `ms_thunk_run`/the frame.
- [x] **3.3** Differential harness (`tools/diffharness/`): unicorn loads the reassembled hunks, seeds registers and memory (fuzzing plus savestate RAM via `BBHack/read_states.py`), runs label → RTS, records registers, flags and dirty memory — M
- [x] **3.4** Host replay: the same cases run against the C++ build (clang); runner modelled on moonshard's `tools/test_host.py`; one command checks every lifted routine — M
- [x] **3.5** `tools/lift.py`: literal asm → `Regs&` C++ transliteration per routine; the Ghidra decompile goes next to it as a reading aid — M
  > Result for 3.4+3.5: `tools/lift.py` (+ `lift_parse/emit/ops/fuzz.py`) emits `lab_<LABEL>` (mog) / `lab_<bin>_<LABEL>` C++ with labels as `MS_SYM()` from generated `include/ms/gen/*_syms.hpp`.
  > `--survey`: about 743 of 840 routines lift completely. Blockers: DC.W in code 45, indirect JSR/JMP 38, RTE 26, TRAP 6, SR moves 4.
  > Proven so far: 278 replay files PASS (`build/run_host.log`), 242 lifted files (180 mog + 62 program: pure leaves plus `--closure`), all compiling with m68k g++ -Werror. `lift_prove3.log`: ≥500 cases for 210 routines, fewer for 7, none for 24 (unproven).
  > DIV overflow flags follow unicorn (N/Z undefined on a real 68000). Case files are git-ignored and regenerated with fixed seeds.
- [x] **3.4a** Harness follow-ups — S:
  > Reconciled 2026-10-07: closed as obsolete: lifting stopped when the C++ ports replaced the asm (7.1); `src/lifted` and the diffharness stay untouched as the replay oracle, and the remaining lifter blockers (`DC.W` in code, `RTE`, indirect jumps) have no consumer.
  - `run_host` registry should key on binary+label, and its regex currently mistakes declarations for definitions.
  - Raise `replay_main`'s 64-write-run cap.
  - Treat unicorn's DIVS INT_MIN/-1 crash as a fault inside `harness.py`.
  - Pointer-structure seeding from the 2.10 savestates to prove the 50 unproven routines.
  - Lifter blockers, in order of routines unblocked: `DC.W` inside code (14 left, all IRA data-as-code `LAB_0FEC`–`LAB_108E` plus program `LAB_0003`/`LAB_005C`), `RTE` (15: interrupt handlers and the dead loaders, never lifted; mark `status: hw` and exclude), indirect `JSR/JMP` (23: a `switch` over the resolved candidate sets from 0.4).
  > Partial result (2026-10-05): `lift_parse` keeps only what control flow reaches from the entry, so unreachable data in a routine's span (padding after `BRA`, CODE cells such as mog `LAB_01EB`, inline strings) is dropped and reachable data still fails. Closure selection admits tail-`BRA`/`JMP` and fall-through routines. The harness faults (`selfmod`) on fetching an instruction the run wrote; this check is armed only for runs that wrote into the image, because with a Python code hook unicorn's DIVS overflow kills the process. Survey: 733/779 routines lift (was 695). On disk: 312 lifted files (was 242), 261 `lifted` in `symbols.yaml`, 50 without cases. `check.py` green, `run_host --m68k-check` OK. None of the new routines are in a swap list yet.
- [x] **3.6** End-to-end pilot on `LAB_03CA`: the literal lift already PASSes; now (1) link it through the 3.2 thunk and boot to C5 (combat uses it), (2) idiomatic rewrite (`contactTest(const Fighter&, …)`), replay PASS again, boot again, (3) mark `idiomatic` — S
  > Reconciled 2026-10-07: superseded by 6.6/7.1m: the contact test `LAB_03CA` is idiomatic C++ in `src/game/creatures.cpp` (`tests/test_creatures.py` vs the lifted 68k) and the Practice fight boots through it.
- [x] **3.7** Shared engine, lift once: from the 0.9 twin table, every program routine with a mog twin gets one C++ body and two thunks (`prg_LABx`, `mog_LABy` → `lab_<name>`). The body is parameterised by a per-binary symbol table (template parameter or a `const Syms&`), because the twins are identical only after label substitution: they touch their own binary's DATA/BSS. Replay cases are generated for *both* binaries against the one body. Cuts M4 to the ~124 program-only routines plus the shared set once — M
  > Reconciled 2026-10-07: superseded by 7.1o/7.1r: shared engine routines are written once in C++ (`src/engine`) and both overlays call them directly; no twin thunks exist.
- [x] **3.8** Verification for hardware-touching routines (today the harness faults on any `$DFFxxx`/CIA access, so S_23 IMAGEXCEL, S_29, S_31 and the sound player cannot be diffed): decide between (a) a minimal blitter/copper model in `harness.py` (minterm blits with A/B/C/D, masks, modulos; enough for IMAGEXCEL's few patterns) and (b) bitmap parity from 2.10 savestates (compare `rt_screen_work`/S_30 planes after one frame). Recommendation: (b) first (cheap, covers the whole frame), (a) only if 4.4 needs per-call diffs — S (decision) / M (if a)
  > Reconciled 2026-10-07: superseded by the boot regression: decision (b), bitmap parity: `tools/shotcmp.py` compares `integrate.py --boot` shots with `build/shots/ref/`; the hardware routines were also compared in unicorn (7.1c, `tests/test_sfx.py` pattern).

## M4: Engine in C++ (shared engine + program-only code)  `[~]` (code done; open: 4.6 listening, 4.8a enhanced playthrough, 4.8c-e backlog)
Done when program's non-hardware code and the shared engine routines are C++, every routine passes the differential tests (or the 3.8 bitmap check), and the game runs C1–C7.

- [x] **4.1** Decoders: RNC `LAB_0190`, LZSS `LAB_049C`, RLE `LAB_0448`, PackBits `LAB_0434`; cross-check against `libmoon_assets` (`rnc1.c`, `lzss_cel.c`, `rle_stile.c`, `packbits_piv.c`, host-tested in `moonstone-main/tests/test_decompressors.c`) by decoding every file in `build/disks` both ways — M
  > LZSS done: `src/engine/lzss.cpp` (`ms::lzssDecode`, pure, host-built) replaces both asm copies (program `LAB_049C`, mog `LAB_0CC2`) through `rt_lzss_decode` (`src/rt/engine.cpp`, patch tables `asm/patches/*.engine.json`). `tests/test_engine.py` decodes all 45 `.cel`/`.ob` files identically to libmoon_assets; the game renders through it (`build/shots/lzss-*.png`). Distance 0 follows the asm (bytes left as they are), not libmoon's fill.
  > RNC: `ms::rncDecodeInPlace` (`src/engine/rnc.cpp`, shim `src/rt/engine_rnc.cpp`) replaces program `LAB_0190` (no mog twin); it decodes `music.cmp`/`vmusic.cmp`, the only RNC files. libmoon `rnc1.c` assumes a different format (18-byte header, Huffman), so `tests/test_engine_rnc.py` cross-checks against a Python decoder written from the asm.
  > RLE: `ms::rleDecode` (`src/engine/rle.cpp`, shim `src/rt/engine_rle.cpp`) replaces program `LAB_0448` and mog `LAB_0C6D` (patched one loop-label in, the entry MOVEQs stay). Bit-level, opcode table from the asm; libmoon `rle_stile.c` has a different table. No file on the disks goes through it (`.stile` files are raw tiles), so the test uses random streams vs a per-bit model. Divergence: the first output byte is cleared (the asm ORs into it).
  > PackBits: `ms::packBitsDecode` (`src/engine/packbits.cpp`) ports the BODY decode of the IFF ILBM loader at `LAB_0434` (mog `LAB_0C59`). Not patched in: nothing calls the loader and every `.piv` on the disks is LZSS. Kept for mods that load IFF pictures; tested on synthetic ILBMs vs libmoon.
  > Boot with all of 4.1/4.2 linked reaches the map: `build/shots/t-{8,16,menu,map}.png`. Debug builds start the intro a few seconds later (the -O0 RNC decode), so scripted skips need `wait:16`.
- [x] **4.2** Math, RNG and text/number helpers; one shared util, no duplicates — S
  > `src/engine/util.cpp` (shims `src/rt/engine_util.cpp`): `ms::rngNext` (mog `LAB_04A1`, 27 callers; state stays the game long `LAB_0973`), `ms::rngPercent` (`LAB_04A3`, 0..100), `ms::formatNumber3` (`LAB_0442`, 3-char field). program has no RNG and its number formatter is dead code, so all three are mog-only. No string copy/length helpers worth porting were found. `tests/test_engine_util.py` checks them against a literal model of the asm (ROXR with X).
- [x] **4.3** Job/event/animation manager (S_10, 47 routines; only 10 have mog twins, so program and mog carry *different* managers: program's here, mog's in 6.1) → fixed pool of 40 + dispatch table — L
  > `src/engine/jobs.cpp` (shim `src/rt/engine_jobs.cpp`, patches `asm/patches/program.jobs.json`): pool reset `LAB_01E4`, alloc `LAB_01DA`, spawn-queued `LAB_01E8` and the per-frame tick/dispatch `LAB_01EC` in C++ over the game's own 40-slot pool (`struct Job`, 42 bytes, static_assert layout). Handlers and the script interpreter `LAB_01F1` stay asm, called through trampolines. `tests/test_engine_jobs.py`.
- [x] **4.4** IMAGEXCEL renderer (S_23/S_25; S_23 is 9/9 shared with mog) → C++ driving the blitter directly first (pixel-equal via 3.8), then ACE `blit` where the minterm/mask pattern maps 1:1 — L
  > `src/engine/blit.cpp` (pure: plans every blitter register per plane) + `src/rt/engine_blit.cpp` (WaitBlit and register writes in asm order): draw_cel `LAB_04B5`/mog `LAB_0CDB`, copy_rect `LAB_04E1`/mog `LAB_0D07`, descending copy_rect `LAB_04E2`. `tests/test_engine_blit.py` compares 2500+ cases register-for-register with a transcription of the asm; boot to the map renders through it (`build/shots/k-*.png`). Pixel compare via 3.8 still open; ACE `blit` not used yet.
- [x] **4.5** Palette/fade/volume ramps (S_31: 6 of 25 shared) folded into `rt/palette`; fade timing checked against a 2.10 screenshot sequence — S
  > `src/engine/palette.cpp` (shims `src/rt/engine_palette.cpp`, patches `{program,mog}.palette.json`): fade target, colour cycles, ramps, the per-VBL tick (`LAB_057D`/mog `LAB_0E5D`) and program's volume fade. Timing and the original quirks (LAB_0592 channel compare) kept. mog's self-patching `LAB_0FC2` stays asm. Intro fades checked: `build/shots/g-*.png`. Note for scripted runs: fronting the WinUAE window can register a left click (port-0 fire), which skips the intro.
- [~] **4.6** Music: decided ptplayer (intro/ending modules) plus the C++ synth for mog; the asm player is gone. Boot check pending: the owner's listening A/B (docs/AUDIO.md section 8; `-DMS_SYNTH_ASM=ON` builds the original synth for the comparison) - S
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > Decided 2026-10-05: ACE ptplayer. Only program has a module player (intro `music.cmp`, ending `vmusic.cmp`, both FLT4 31-sample modules); mog has its own voice/sequence synth for in-game tunes and all sfx, which stays asm (`docs/AUDIO.md`). `src/rt/audio.cpp` + `program.audio.json`/`mog.audio.json` redirect start/stop to ptplayer under `MS_MUSIC_PTPLAYER` (default ON; OFF = original player). F00/D01 fixed up on load to match the original. IRQ stacks raised to 4 KB for INT6 nesting. Intro runs identically (`build/shots/p-*.png`); open: listening A/B by the user.
- [x] **4.7** Remove `rt` shims made dead by 4.x; program's hardware layer fully replaced; the dead S_4/S_5 loaders dropped and `--verify` scoped to the remaining asm — S
  > Reconciled 2026-10-07: superseded by 7.1r: dead loaders, TRAP #15 helpers and the `--verify` layout went with the asm link; what is left of the patch tables is 7.1s.
  > `docs/DEAD_RT.md` has the reachability analysis. Removed: the S_4/S_5 loader patches (the loaders are now inert data, `ldr-dead` `as_data` patches), the TRAP #15 helpers, program `files-skip`, program `palette-cycle-add-cpp`, both `rle-cpp` patches and `src/rt/engine_rle.cpp` (no live caller; `ms::rleDecode` stays for tools/mods), and their abs_symbols entries. Open: dropping the loader hunks needs `--verify`/`image_tab` keyed by section name; dead trackdisk/OFS bodies remain; the live hardware layer (music, copper, IRQ) is 4.6/4.8.
- [x] **4.8** Display handover to ACE: the original copper list becomes a `copBlock`, screen = `simpleBuffer` 320×200×5 double-buffered, AGA set up by ACE. Only now does ACE own the display while the game runs — M
  > Step 1 behind `MS_ACE_DISPLAY` (default OFF; `docs/DISPLAY.md` section 7): screens A/B wrapped as ACE bitmaps of a 320x200x5 simpleBuffer view, the game's copper list becomes a stub that jumps into ACE's list, plane-pointer writes and the screen swap go through `rt_display_show`, palette stays CPU writes (`src/rt/display_ace.cpp`, `{program,mog}.display_ace.json`). Booted ON 2026-10-06: logo, menu, loading caption, Practice render (`build/shots/on2-*.png`); OFF unchanged (`off-*.png`). The fight trail (6.0a) is the same in both modes. Next: make ON the default after a full playthrough, then 4.8a.
- [~] **4.8a** Enhanced display (`MS_ENHANCED`, 6 planes/64 colours, default OFF): works in menu, knights and the Practice arena with the redrawn art. Boot check pending: play the rest of the game in enhanced mode on the owner machine (68020 + fast RAM) and the 50 Hz check (7.2); 4.8c-e extend it (backlog) - M
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > `MS_ENHANCED` (needs `MS_ACE_DISPLAY`; default OFF): runtime plane count `rt_enh_planes` 5/6, screens grown to 6 planes (region base `$67F7A`), enlarged arenas (needs fast RAM; falls back to 5 planes), 6-plane cel drawing, picture loaders read 64 palette words + `.pal` sidecars, 24-bit palette follower with fades (`src/rt/enhanced.cpp`, `palette_enh.cpp`, `src/engine/enhcarve.cpp`/`enhpal.cpp`, `{program,mog}.enhanced.json`). Booted ON 2026-10-06 with the redrawn art (`build/enh.uae`, fastmem 8): logo, intro, menu OK (`build/shots/e-*.png`); Practice arena (the `Test` 9-picture pack) decodes as garbage. Dropped: the mirror loop patch (`MOVEQ #5,D7` at mog 23042/program twin) - original cels use bit 5 of that byte for something else, so it halted the 5-plane game; mirrored sprites in enhanced mode lack plane 5 until a runtime-checked shim replaces it. OFF build unchanged (`f-*.png`).
  > 2026-10-06 later: the mirror loop is back as a runtime-checked shim (`rt_*_mirror_init`, D7 = planes-1; OFF Practice runs, `build/shots/x-off.png`). The `Test` arena pack is read with a bigger count and walked by `rtEnhPackDone` (pointers + per-picture sizes). The nine copy-count patches (`asm/patches/mog.enhanced_packsz.json.parked`) hang the game at mog load when enabled (`y-on*.png`); suspect the 46,927-byte redrawn pictures overflow `LAB_05C2` or the decode target. Parked, so enhanced reaches the menu and the Practice arena is still garbage (`z-on1.png`). Enhanced diagnostics are buffered into `files.log` at the next file open.
  > Resolved the same day: the "hang" was a slower load. With all nine copy-count patches, enhanced mode (`build/enh.uae`, redrawn art) reaches the menu and renders the Practice arena and knights in 64 colours (`build/shots/t9-*.png`); OFF Practice unchanged (`fin-off.png`). `-DMS_ENH_TRACE` adds COLOR00 breadcrumbs. Open: play through the rest of the game in enhanced mode, 50 Hz check (7.2).
- [x] **4.8b** Roomier enhanced buffers for more detailed art (decided 2026-10-06; target A1200 + ACA-1232 with fast RAM): raise the enhanced caps (`Test` pack 320 KB -> 512 KB, `message.piv`/`ch.piv` raw regions 24 KB -> 32 KB, cel read buffer 41244 -> 64 KB, cel decode stretch with headroom), arenas follow; plain-mode layout unchanged; `artMaxSize`/`artClassify` follow the new caps - S
  > Enhanced only: `kEnhPackBytes` 512 KB (pack increments stretched 8/3), `kEnhRawPictureBytes` 32 KB (still fits the DBF counts), cel read buffer 64 KB in fast RAM appended to each overlay's fast carve (`celRead`, `rt::enhCelReadBuf`; plain mode keeps the 41244 BSS buffer), `ms::celReadBufferBytes`; decode stretch stays 3/2. Arenas chip 639204 (unchanged) / fast 904591 -> 1119012; plain byte-identical (`tests/test_enh_carve.py`). Per-picture cap inside the pack stays 64 KB (DBF). Booted enhanced on the owner-machine config (`build/aca1232.uae`): menu + Practice OK (`build/shots/enh48b/`). Found there: 4.8e.
- [ ] **4.8c** *(backlog 2026-10-06: owner chose C++ conversion first; plain 32-colour build is the focus; worktrees D:/Amiga/wt/4_8c, 4_8e hold partial work)* Copper gradients in enhanced mode (decided 2026-10-06): per-picture gradient sidecars (`<name>.grad`: colour index, line range, 24-bit stops) rewrite chosen palette entries per scanline through ACE's copper list, so skies/ground shade without spending palette entries; artconv/authoring support; off in plain mode; 50 Hz cost measured (7.2) - M
- [ ] **4.8d** *(backlog 2026-10-06: owner chose C++ conversion first; plain 32-colour build is the focus; worktrees D:/Amiga/wt/4_8c, 4_8e hold partial work)* 256 colours (8 planes) in enhanced mode (decided 2026-10-06): runtime plane count 8, screens/carve/arenas/cel drawing/loaders/palette writer (all 8 AGA banks) for 8 planes, artconv `--planes 8` and a palette split for 64..255; 6-plane art keeps working. After 4.8b (shares the carve). Art redraw is the owner's - L
  > Art side done 2026-10-06: `tools/artenhance256.py` (0..63 = today's 64-colour palette bit-exact, 64..159 shared sprite/cel/font shades, 160..255 picture-local; split from measurements, `--measure`, `--shared N`), output `build/art/export256`, `artconv --planes 8`, previews `build/shots/art256-{outdoor,arena,sprites}.png`, `tests/test_artenhance256.py`, docs/ART.md. Open: the 8-plane runtime, and 4.8e recipes for 64..255.
- [ ] **4.8e** *(backlog 2026-10-06: owner chose C++ conversion first; plain 32-colour build is the focus; worktrees D:/Amiga/wt/4_8c, 4_8e hold partial work)* Palette effects must drive the derived shades (found 2026-10-06, enhanced boot: the Mindscape logo flash "static"): colour cycles/flashes/ramps change entries 0..31 only, so pixels shaded with 32..63 (artenhance blends) keep their colour and the picture speckles. Record per shade slot its parent colours + weight in the sidecar (artenhance), recompute shades from the live 0..31 whenever an effect writes them (fades already scale all 64) - M
- [x] **4.4a** Renderer depth parameter: `ms::planCel`/`runCel` and the copy_rect paths take the plane count (5 or 6), masks stay one plane; host test for 6 planes - S
  > `CelView::depth` (5 default, 6), mask in temp plane `[depth]`, a third mask blit ORs plane 5 in at depth 6; `planCopyPlanes`/`planCopyPlanesDesc` for N-plane copy_rect. Depth 5 is op-for-op identical to before (`tests/test_engine_blit.py`, 1500 random draws vs the N-plane asm model). Game side wiring is 4.8a.
- [x] **4.5a** AGA palette path: 24-bit colour tables, fades with 256 levels per gun in enhanced mode (`src/engine/palette.cpp`), 12-bit path unchanged for parity - S
  > 24-bit twins in `src/engine/palette.cpp` (`colorStep24`, fades/cycles/ramps over 64 colours, one level per gun per step) and the AGA writer `src/rt/palette_aga.cpp` (BPLCON3 LOCT hi/lo nibbles per 32-colour bank); not called until 4.8a. 12-bit path untouched.
- [x] **4.9** Intro and ending orchestration (program S_0 scenes `LAB_001A`–`LAB_003B`; S_0 has 35 routines, 2 shared) as ACE states — M
  > `src/engine/intro.cpp` (script tables + stepper `ms::introRun`) and `src/rt/engine_intro.cpp` (patches `program.intro.json`): both `SECSTRT_0` chains, the intro (lines 132-152) and the ending (157-169), run from C++; the scene bodies `LAB_001A`..`LAB_003B` stay asm (their timing and music sync live there; porting them is 6.10). The intro skip is unchanged. Timeline identical to the asm build at 9/15/23/38/53 s (`build/shots/h-*.png` vs `o-*.png`). `uaeshot.ps1` now unplugs port 0: a captured host mouse click is port-0 fire and skipped the intro in earlier runs.

## M5: Game data model and rules (mog.asm)  `[x]`
Done when mog's BSS/DATA is typed structs, core rules are pure C with host tests, and the game runs.

- [x] **5.1** Struct layouts from BSS usage: knight records (`LAB_01C4`), dragon, encounter, scheduler, inventory (24-byte counted), map state; `static_assert` offsets match the original; field names cross-checked against DOC_TECHNIQUE §10.18 and the 2.10 savestates — M
  > Reconciled 2026-10-07: superseded by 7.1n3: the typed structs (`include/game/{knight,world,state,state_bind}.hpp`, `docs/GAME_STATE.md`, `tests/test_game_state.py`) are now the owned C++ data, not an overlay on asm BSS. The few unknown fields are listed in GAME_STATE.md and can be confirmed with the 2.10 savestates.
  > `include/game/{knight,world,state,state_bind}.hpp`, `docs/GAME_STATE.md`, `tests/test_game_state.py`: `Knight` (132 B x5 at `LAB_0613`; the 5th is the dragon, also the heap creature layout), `Inventory` (24 B x5), `ActiveKnights`, `Lair`, `MapNode`, mog `Job`; scheduler state is scalars typed in `state_bind.hpp`. Every offset static_asserted and every binding size checked against mog.asm DS/DC. Several DOC_TECHNIQUE 10.18 names are wrong per the asm (see the doc). Open: unknown fields to confirm against 2.10 savestates.
- [x] **5.2** Asm DATA tables → generated `static const` tables (generator script, output git-ignored; the ripped art stays personal-use and out of git) — M
  > `tools/gen_tables.py` + curated `tools/tables.yaml` -> `build/gen/game_tables.{hpp,cpp}` (git-ignored): 21 tables (map sites `LAB_069F`, creature nodes `LAB_07BD`-`LAB_07C0`, encounter bands, trade prices `LAB_0F63`, combat offsets, knight names, menu text lists, keymaps); pointers become symbols. `tests/test_gen_tables.py`. Skipped (layout not established): `LAB_08C9`-`LAB_08CC`, movement paths `LAB_07BA`-`LAB_07BC`; combat script blobs belong to 6.5. Not yet in the CMake build: nothing consumes the tables until M6.
- [x] **5.2a** Asset override loader: `rt/files` looks for a replacement in `art/` on HD before the original disk file, so redrawn art drops in file by file - S
  > `rt_file_open` tries `PROGDIR:art/<name>` first (same OsGuard; plain names only); `ms::artClassify` (`src/engine/artcheck.cpp`) refuses 6-plane pictures/cels until 4.8a and falls back to the original. `hdinstall` copies `build/art/import/{A,B,C}` into `hd/art/` (re-run cmake after creating it). Logged in `files.log` as `art override <name> <size>`. `tests/test_art_override.py`. Not yet booted with an override.
- [x] **5.2b** `tools/artconv.py`: export every original `.cel`/`.ob`/`.piv`/`.stile` to indexed PNG templates (frames, masks, palette; output git-ignored, personal use) and import redrawn 64-colour indexed PNGs back to the game formats at 6 planes (LZSS-packed, masks generated from colour 0); round-trip test on the originals at 5 planes - M
  > `tools/artconv.py export|import`, `docs/ART.md` (artist guide), `tests/test_artconv.py`: 77 files (51 cel-family incl. fonts, 23 piv, a 9-piv pack, 2 stile index maps) export to indexed PNG + JSON sidecars in `build/art/export/` and re-import byte-identical in decoded content at 5 planes. 6-plane output: pictures `BE16 6` + 64 palette words + 48000-byte body, 24-bit palette in a `.pal` sidecar; sprites use plane bit 5. Sprites carry no palette and no stored mask (mask = OR of planes, index 0 transparent). Code-side palettes (`LAB_08D1..08D5`) are only dumped.
- [x] **5.3** Pure rules in `src/pure` (C11, host-tested): stats/HP (`LAB_0013`/`LAB_0019`, already lifted and PASS), scheduler (`LAB_0DBA`), daily upkeep (`LAB_0029`), lunar clock, settlement. Reuse moonshard `mechanics.c` only where its function passes the same replay cases as the lifted routine — M
  > C++ (not C11) in `src/game/rules.cpp` / `include/game/rules.hpp` on the 5.1 structs: `LAB_0013`/`0019`/`0011` recalcs, settlement `LAB_0021`/`001C`/`000E`/`000D`, daily upkeep `LAB_002B`-`0030`, lunar clock `LAB_0029`, scheduler `LAB_0DB9`-`0DBE` (one pass per call). `tests/test_game_rules.py` checks each against a Python model of the asm, and `0011`/`0013`/`0019`/`000E`/`0029` also against the lifted C++. Wired in-game: `LAB_0013`/`LAB_0019` (`mog.rules.json`, `src/rt/game_rules.cpp`; do not combine with swap list `pure.txt`). mechanics.c differs (4 records not 5, no loot transfer, lives saturate) and is not reused. Boot to the map: `build/shots/n-*.png`.
- [x] **5.4** RNG semantics confirmed bit-exact (the same seed gives the same sequence as the original) — S
  > `ms::rngNext` (4.2) runs on the game's own seed `LAB_0973` and `tests/test_engine_util.py` checks 200-step sequences from 7 seeds (both entry X values) against a literal model of `LAB_04A1` (ROR/EOR/ROXR with X).
- [x] **5.5** The unresolved indirect sites in reachable code (0.5: mog lines 7784 and 17459, program lines 2821, 4590 and 4600; the other five are in dead loader code) resolved by tracing in WinUAE at C4–C6 (log the target of each `JSR (A0)`/`JMP (A3)`); without this the S_0 lifts stay partial — S
  > Reconciled 2026-10-07: superseded by 7.1h/7.1p: the unresolved indirect sites became C++ function pointers/tags in owned tables and the asm that held them is gone, so nothing is left to trace.

## M6: Game scenes in C++ (mog.asm)  `[~]` (all scenes are C++; open: boot checks of 6.3, 6.4, 6.4a, 6.7, 6.8, 6.9)
Each scene is one state module (enter/update/draw/exit) on ACE `stateManager`, ported behind the 3.2 thunks while the other scenes stayed asm (history: no asm is left since 7.1r). Done per scene: no asm in that scene, differential tests pass, screenshot parity against the 2.10 references. Scene ids follow `LAB_068F` (DOC_TECHNIQUE §10.19).

- [x] **6.0a** Fight background restore bug (found 2026-10-06 booting Practice): the moving knight leaves a trail of old frames (`build/shots/t2-fight*.png`). Bisect: present with every mog C++ port patch removed, so it is in the M2 runtime/relocation layer (display buffers), not the ports. Analysis in `docs/DISPLAY.md` section 6 - M
  > 2026-10-06 after 7.1f: Practice shows no trail in `build/shots/fix-practice2.png`; the hit sets now register (7.1f1), which may be related. Re-check with a longer walk before closing.
  > Fixed by 7.1f1 (2026-10-06): a 2.5 s walk each way leaves no trail and the knights now take fight poses (`build/shots/tr-*.png`). Cause: no hit sets were registered (the old collide.hit loader read a size cell only the original disk reader wrote), so the fight scripts ran without contact data. Not a display bug.
- [x] **6.1** Main loop and scene dispatch (`SECSTRT_0`, `LAB_0001` loop, `LAB_04CF` entry, mog's job manager) — M
  > Reconciled 2026-10-07: boot checked: Practice, campaign and map run through `src/game/mainloop.cpp`/`mogjobs.cpp`; `integrate.py --boot` PASS. The note that the loop skeleton stays asm is obsolete (7.1l).
  > `src/game/mainloop.cpp` (boot after `LAB_04A5`, scene table TITLE/PRACTICE/CAMPAIGN/QUIT/MAP) and `src/game/mogjobs.cpp` (mog job reset/find/toggle/kill/restart, frame pacer `LAB_031D`/`031F`), `mog.mainloop.json` (10 patches). Booted 2026-10-06: menu, Gore toggle, Practice and campaign to the map run through it (`build/shots/m2-*.png`, `t1-*.png`). Overworld loop `LAB_0DAB` and the boot set-up routines stay asm.
- [x] **6.2** Knight select / menu (Players, Gore, Practice, Select Knight) — M
  > Reconciled 2026-10-07: boot checked: `tests/boot/regression.txt` drives menu, cursor, Select Knight, portraits and name entry with keyboard and joystick (PASS); the asm primitives were ported by 7.1k.
  > `src/game/scene_menu.cpp` (pure `SceneMenu`/`SceneKnights`) + `src/rt/scene_menu.cpp` (trampolines to the asm draw/input primitives), `mog.scene_menu.json`: menu input/cursor/toggles and knight select with name editor in C++; host test vs asm model over 600 scripts. Open: keyboard-driven boot check (menu, portraits, name entry).
- [x] **6.3** Overworld map in C++ (`src/game/overworld.cpp`, map drawing 7.1l): reaching the map is in the regression (`reg-map`). Boot check pending: movement between nodes, node arrival menu, turn scheduler, AI knight turns, dragon attack/flight - L
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > `src/game/overworld.cpp` (`mog.overworld.json`, 28 patches): movement `LAB_0DA6`/`0E07`/`0E22`/`0DD8`, node scan + arrival menu `LAB_0069`/`0E3D`/`0E45`, the turn scheduler now runs through rules.hpp `advanceTurn`/`setupTurn`, AI knight walk/roam/opponent/potion/scroll/shop, dragon attack/spawn/flight `LAB_0DB6`/`0DCB`/`0DCF`. Map drawing and the loop skeleton stay asm. `LAB_07BA`-`07BC` are lair creature spawn lists, not map paths. 30 tests vs lifted 68k. Open: boot check on the map; shim glue not run in unicorn.
  > booted 2026-10-07 on the all-C++ build (34db0bc): `integrate.py --boot-play nightly`, all 12 `tests/boot/play_*.txt` PASS: map walk, status, turn end, black knights, Next Day, arrival menus (play_map, play_2p).
- [~] **6.4** Combat FSM in C++ (`src/game/combat.cpp`, per-fighter handlers 6.4a): Practice boots (`regression_practice.txt`). Boot check pending: knight-vs-knight, lair, dragon fight and the post-fight meeting screen - L
  > 2026-10-07 dragon fight fixed (owner report: knight trail + striped garbage): `dragonCommon` (src/game/combat.cpp) now does all of
  > LAB_0195 (type $14, hurt/walk/idle scripts LAB_0604/060F/0882, reach $3C/$14/5, hp $78, facing, input port); before, the arena dragon
  > kept the map-flight record (type $28) and ran the flight handler/cels. `tests/test_combat.py` model fixed too. Boot: `play_dragon`
  > (loss path, 9 shots PASS), `play_lair1/6/10/19` (four more creature arenas). Open: the dragon win path + its loot screen.
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > `LAB_04CF`/`04D0` is the post-fight meeting screen loop, not the fight loop; the fight loop is `LAB_0036` (entries `LAB_004F` knight, `LAB_005B` lair, `LAB_0083` dragon). Both ported in `src/game/combat.cpp` with arena set-ups and settlement (`mog.combat.json`, 12 patches). Not done: per-fighter handlers `LAB_01CA`-`0300` (input-to-action via `LAB_07D9`/`07DA`, AI knight) - see 6.4a. Open: boot check (Practice, meeting, lair, dragon).
  > booted 2026-10-07 on the all-C++ build (34db0bc): `integrate.py --boot-play nightly`, all 12 `tests/boot/play_*.txt` PASS: duel PvP, lair (ratmen), valley guardian, death/game over PASS; open: dragon fight.
- [~] **6.4a** Per-fighter handlers are all C++ (`fighters.cpp`, `fight_creatures.cpp`, 7.1h). Boot check pending: lair creatures and the dragon in a boot (Practice only so far) - M
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > `src/game/fighters.cpp`: knight `LAB_01CA` (input to action, AI), flyer, brawler, caster, dragon, dragon part, drake, dagger, idle handlers, hurt reactions `LAB_01E1`-`0206`, movement helpers, sound pickers (`mog.fighters.json`); dispatched from `creatureDispatch`. Tested vs lifted 68k and in unicorn. Open: boot check (Practice, lair, dragon).
  > booted 2026-10-07 on the all-C++ build (34db0bc): `integrate.py --boot-play nightly`, all 12 `tests/boot/play_*.txt` PASS: knight/creature/guardian handlers PASS in play_duel/lair/valley; open: dragon.
- [x] **6.5** Combat script engine: interpreter `LAB_032E`–`034D` and handlers; moonshard's `combat_script.c` decoder reused after diffing — L
  > Reconciled 2026-10-07: boot checked through the Practice fight (`regression_practice.txt` PASS, `m6-*.png`); the script engine and the 31 script-called routines (7.1h) are C++, host-tested tick by tick.
  > `src/game/combat_script.cpp`: driver `LAB_0328` (`combatTick`), z-sort `LAB_0351`, interpreter `LAB_032E`-`0330` and handlers `LAB_0358`-`039C`, motion step, draw/hurt/attack lists; sprite draw, sound and spawn stay asm behind callbacks (`src/rt/combat_script.cpp`, one patch `combat-tick-cpp`). Host test steps the real scripts from `LAB_07DB` plus random scripts tick by tick vs an asm model. moonshard `combat_script.c` is a decoder only; its `$FD`/`$FE` sizes are wrong. Open: boot check via Practice.
- [x] **6.6** Creatures, daggers, hit/contact (`LAB_03DB` family; `planar_contact.c` reused after diffing) — M
  > Reconciled 2026-10-07: creature movement/hurt handlers are done (7.1h); contact and daggers run in the Practice fight. Lair/dragon creatures are covered by the 6.4 boot check.
  > `src/game/creatures.cpp`: contact `LAB_03CA`/`03DB`/`03BE`, blocked mask `03A9`, damage `021B`/`0204`, daggers `02CA`-`02FD`, job/record/spawn plumbing `0310`/`0171`/`02D0`, dispatch `0322` (`mog.creatures.json`, 10 patches). `tests/test_creatures.py` (vs lifted 68k + asm model) and `tests/test_creatures_emu.py` (shims vs original in unicorn). planar_contact.c agrees on 1500 cases; its depth test is wrong at 0x8000. Not done: creature movement/hurt handlers (6.4a).
- [x] **6.7** Town screens in C++ (`scene_town.cpp` transactions + `rt/screens.cpp` UI, 7.1k). Boot check pending: each screen (smith, market, knight exchange, temple, castle rest, healer, dice house) in a boot - M
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > Roadmap names were wrong: state 5 = smith, 6 = market, 8/11 (and 1/2/10) = knight-to-knight exchange, 9 = temple stat purchase; the "tavern" is the dice house; `LAB_0F63` is the dice payout table. Transactions in `src/game/scene_town.cpp` (shop, market buy/sell, exchanges, temple, castle, rest, healer, dice), 15 patches in `mog.scene_town.json`, host test vs asm model. UI stays asm. Kept bug-compatible: market buys call `LAB_0013` on the inventory record (one line in `src/rt/scene_town.cpp` to fix). Open: boot check of each screen.
  > booted 2026-10-07 on the all-C++ build (34db0bc): `integrate.py --boot-play nightly`, all 12 `tests/boot/play_*.txt` PASS: merchant/smith buys, tavern dice, healer, high temple market, temple screen (play_town_a/b).
- [x] **6.8** Places in C++ (`scene_places.cpp`, `rt/screens.cpp`). Boot check pending: mystic, wizard, village, Stonehenge (win test + ending), Valley of the Gods, per node - M
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > `src/game/scene_places.cpp`: node dispatcher `LAB_007B`, town buttons, mystic gamble `LAB_047C`, Math the wizard `LAB_045E` (also the AI knights' daily step), gifts `LAB_046C`/`0471`, Stonehenge win test + ending code + Danu, Valley of the Gods (`mog.scene_places.json`, 17 patches). Town $1A Space opens a bogus scene in the original (left asm). Open: boot check per node.
  > booted 2026-10-07 on the all-C++ build (34db0bc): `integrate.py --boot-play nightly`, all 12 `tests/boot/play_*.txt` PASS: mystic, wizard pages, home village, Stonehenge ritual + extra life, Valley of the Gods with/without keys (play_town_b, play_wizard, play_castle, play_stonehenge, play_valley).
- [x] **6.9** Loot and post-combat screens in C++ (`loot.cpp`, `rt/combat_ui.cpp`). Boot check pending: loot screen buttons, item use/drop/move after a fight - M
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > `src/game/loot.cpp`: button dispatch `LAB_052C`, next knight `LAB_0527`, loot move `LAB_053F`, drop `LAB_053D`, use-item `LAB_052D`-`053A`, loot screen setup `LAB_058A`-`059F` (`mog.loot.json`, 6 patches). Quirks kept (wizard double decrement through the sound channel in D1, unchecked counts). The town exchange-sword note "LAB_0551 stays asm" no longer applies. Open: boot check of the loot screens.
  > booted 2026-10-07 on the all-C++ build (34db0bc): `integrate.py --boot-play nightly`, all 12 `tests/boot/play_*.txt` PASS: lair loot screen takes gold (play_lair); note: >100 click regions with huge poked stats overflow the pool, same as the original.
- [x] **6.10** Animations shared by intro/ending/scenes (`LAB_0030` family) — M
  > Reconciled 2026-10-07: the animation interpreter, handlers and all intro/ending scene bodies are C++ (`src/engine/anim.cpp`, `scenes.cpp`, 7.1i); the intro boots unattended. The ending sequence is checked by 7.4.
  > `src/engine/anim.cpp`: program's animation script interpreter `LAB_01F1` and all handlers (now called by `jobsTick`), spawn helpers `LAB_0015`/`0016`, credit overlay `LAB_003C`, scenes `LAB_002C`-`0031` as step lists (`program.anim.json`). Intro identical (`build/shots/p-*.png`). Remaining scene bodies `LAB_001A`/`001B`/`001C`/`0036`-`003B` stay asm.

## M7: Asm-free release  `[~]` (asm-free link done 2026-10-07; open: 7.1j/7.1k boot checks, 7.1s, 7.2, 7.3, 7.4)
Done when no asm remains in the build and a full playthrough on the A1200 config works.

- [x] **7.1** Delete the remaining asm, thunks, `regs.hpp` and the vasm step; `MS_LINK_GAME_ASM`/`MS_LINK_LIFTED` options removed — S
  > Reconciled 2026-10-07: done by 7.1a-7.1r: no game asm in the link, no thunks, `MS_LINK_GAME_ASM`/`MS_LINK_LIFTED` options removed. `regs.hpp` stays only for `src/lifted` (oracle, cross-compile check); the asm reference and patch tables are 7.1s.
  > Remaining live asm (2026-10-06, `docs/ASM_REMAINING.md`, `tools/asm_remaining.py`): 817 routines / ~13,800 instructions (program 206, mog 611) and 171 KB live DATA/BSS. Split into lettered tasks below; batches in the doc section 5.
- [x] **7.1a** Text/font printing (program S_12 `028F`/`0291`/`0297`/`02A0`, mog `0430`-`044B`) - S
  > `src/engine/gtext.cpp` (pure layout) + `src/rt/gtext.cpp`; program `LAB_028F` and mog `LAB_0431`/`0432`/`0448` (+ folded helpers) patched (`*.text.json`); scene_menu calls C++ directly. Booted: menu, knight select, name entry (`build/shots/b1-*.png`).
- [x] **7.1b** Audio front end: ptplayer unconditional, mog S_16 sound request (`LAB_0AA2`) - S
  > ptplayer is the only music path (option `MS_MUSIC_PTPLAYER` removed). mog sound requests (`LAB_0AA2`, releases `LAB_0A9E`-`0AA1`, stop-all `LAB_0AA9`: busy mask + round-robin cursor) in `src/engine/sfx.cpp` / `src/rt/sfx.cpp` (`mog.sfx.json`); combat/fighters/loot call it directly. Synth start `LAB_0F8C` still asm (7.1g). Tested vs asm model and in unicorn. Open: listening check.
- [x] **7.1c** Blitter remnants S_23/25 + S_28/30 (finishes 4.4, folds in the enhanced mirror/temp shims, 3.8 pixel compare) - M
  > Plane-count init, plane bases, clip, cel mirror (now any plane count; the enhanced mirror/temp shims are gone), IMAGEXCEL scratch carve, all WaitBlits -> `rt_blit_wait` (`*.blit2.json`). 3.8 pixel compare: `tests/test_engine_blit.py` PixelCompare draws real cels through the planner + software blitter vs a CPU render (depth 5/6, clipped, flipped). Booted: Practice knights mirror correctly (`b1-fight*.png`).
- [x] **7.1d** IRQ, keyboard/joystick, trackdisk residue (S_15/S_20, S_13/S_18) - M
  > Level-3 handler (frame counter, mouse, POTGO, hook list), INT4 default, key decode (`ms::keyDecode`), mouse, joystick reads, cursor on/off and key reset/wait in C++ (`src/rt/irq.cpp`, `src/rt/input.cpp`, `src/engine/input.cpp`; `*.irq2.json`). INT1/5/6 and alt-INT2 deleted; trackdisk residue dead (program) or two stubs (mog). `tests/test_input.py` incl. unicorn vs the original handlers. Booted 2026-10-06 (`build/shots/fix-*.png`).
- [x] **7.1e** Display/copper/palette/sprites; finishes 4.8 (`MS_ACE_DISPLAY` default ON); needs 6.0a understood - M
  > ACE is the only display path (`MS_ACE_DISPLAY` removed): init/swap/clear/frame and beam waits/palette writes/sprites/DIW shake/fades/wipe in C++ (`src/rt/{display_ops,sprites,mog_display,palette_glue,wipe}.cpp`, `src/engine/display_fx.cpp`; `*.display2.json`), unicorn-tested against the originals. Booted 2026-10-06: full intro, menu, knight select, name entry, map, Practice (`build/shots/m3..m6-*.png`). Open: enhanced-mode boot, dead `enh-*`/`copper-ptr` patch entries (docs/DISPLAY.md 8.3).
- [x] **7.1f** Asset loaders and file layer (f1 loaders, f2 combat_load + stub deletion) - L
  > Reconciled 2026-10-07: done: loaders, combat data loading and the S_8 scene loaders are C++ (7.1i/7.1n); the dead `enh-*`/`files-drive-*` patches went with the patch cleanup (8a92b70, 7.1r).
  > f1: picture (file/memory, incl. 6-plane), cel/ob/font, LZSS blob and collide.hit loaders in `src/engine/loaders.cpp` / `src/rt/loaders.cpp` (`*.loaders.json`); the enhanced picture shims are folded in. Hit sets now register: the old loader read a size cell only the original disk reader wrote, so this build had none before. f2: combat data loading `LAB_00F8`-`0155` in `src/game/combat_load.cpp` (`mog.combat_load.json`, 26 patches); dead rt code and the logging-stub mechanism removed (`tools/resource.py` rejects bare funcs). Boot fix: `rt_cl_select` must leave through `rt_mog_hunk9_exit`, not the raw `SECSTRT_9` stub. Open: the S_8 intro-sequence loaders (with 7.1i), the dead `enh-raw-*`/`enh-pack-*` and `files-drive-*` patches.
- [x] **7.1g** mog synth S_44/S_45 (needs 2.12; acceptance by listening A/B) - L
  > Done 2026-10-06: synth in `src/engine/synth.cpp` (`synthInit/Relocate/Start/Tick/Int4/Fade`, state `ms::Synth` cleared per mog entry, closes the synth part of 2.12) and `src/rt/synth.cpp` (shims), tables generated by `tools/gen_synth_tables.py`, `mog.synth.json`; proven against the original in unicorn: per-tick Paula write logs for all 168 sequences x 4 channels, random multi-voice/INT4/fade/lock runs, compiled m68k shims (`tests/test_synth.py`). A/B via `-DMS_SYNTH_ASM=ON`. Open: the owner's listening check (docs/AUDIO.md section 8); drop `LAB_0F69` + the `int4-vector` patch with the A/B switch. Boot: regression_practice PASS; regression.txt name-entry flaked (the 6-frame fire held across a load lands in name entry and accepts it: harness race, also seen before this merge; fix the harness to release input when the frame counter freezes).
- [x] **7.1h** Fighter handler tables and script ops: trace the live entries, port or delete the rest - L
  > Fighter handler tables, hurt reactions, S_40 creatures (snatcher, demon, AI knight, stalker in `src/game/fight_creatures.cpp`) and the 31 script-called routines (`src/game/fight_ops.cpp`, `$B0` operands tagged) in C++; 118 routines -> 4 one-instruction stubs (`mog.fight_ops.json`). Practice boots (`m6-*.png`). Open: lair and dragon fights in a boot.
- [x] **7.1i** Intro/ending scene bodies and anim handlers (program S_0, S_10) - M
  > Intro/ending scene bodies, frame loop, S_8 scene loaders, caption screen, job sort in C++ (`src/engine/scenes.cpp`, `src/rt/engine_scenes.cpp`, `program.scenes.json`); program live asm ~2030 -> ~950 instructions. Traces match the original in unicorn. Full unattended intro boots (`m3-*.png`). Open: the ending sequence (needs a Stonehenge win).
- [~] **7.1j** Combat set-up and combat/loot UI are C++ (`rt/arena.cpp`, `rt/combat_ui.cpp`; the last asm bodies went in 7.1o-7.1r). Boot check pending: lair and dragon arenas and the loot screens (with 6.4/6.9) - L
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > Arena set-ups, creature arenas/initialisers, lair loot (`src/rt/arena.cpp`) and the combat/loot UI: stat sheet, inventory icons, loot and shop screens, hit test, cursor, wizard screen (`src/rt/combat_ui.cpp`), `mog.combat2.json` (40 entries). Unicorn tests vs the originals; `integrate.py --boot` all PASS. Still asm: `LAB_0164`-`0167`/`0192`/`01A3` (patched earlier), dead bodies of already-C++ routines.
  > booted 2026-10-07 on the all-C++ build (34db0bc): `integrate.py --boot-play nightly`, all 12 `tests/boot/play_*.txt` PASS: lair + duel arenas and loot PASS; open: dragon arena.
- [x] **7.1k** Menu, places and town screens are C++ (`rt/screens.cpp`; the `rt_town_dice_*`/`rt_places_*` shims went with 7.1o). Boot check pending: town/mystic/healer/dice/ritual screens (with 6.7/6.8) - M
  > Reconciled 2026-10-07: text rewritten to what is left today; the notes below are history.
  > Mystic, healer, donation, dice house, Danu ritual screens, line printer, knight colour ramps and the two script handlers in `src/rt/screens.cpp` (`mog.screens.json`); mog.places 295 -> 7 instr, mog.town_ui 367 -> 9. Boots to the menu (`build/shots/k-menu.png`). Open: boot the town/mystic/healer/dice/ritual screens; retire the now-dead `rt_town_dice_*`, `rt_town_healer_apply`, `rt_places_donate_*`, `rt_places_mystic` shims and their patches.
  > booted 2026-10-07 on the all-C++ build (34db0bc): `integrate.py --boot-play nightly`, all 12 `tests/boot/play_*.txt` PASS: town, ritual and place screens PASS (play_town_a/b, play_stonehenge, play_wizard).
- [x] **7.1l** Map drawing, boot skeleton, rules remnants - M
  > Map loop/drawing/colour jobs/menu text (`src/game/overworld.cpp`), place visits `LAB_007B`-`00B3` (`src/game/placevisit.cpp`), boot steps, program `SECSTRT_0` (`src/game/progmain.cpp`), rng seed, `LAB_000A`/`000D` shims; group 871 -> ~15 stub instructions (`*.boot_map.json`). Unicorn vs originals; `integrate.py --boot` all PASS (regression machine now has 4 MB fast RAM: the Debug exe no longer fits a stock A1200, see 2.11). Open: boot town/Stonehenge/valley visits; point combat.cpp/loot.cpp at the `rt_ow_*` entries so the last map stubs go.
- [x] **7.1m** Contact, job/frame primitives, arena background blit - M
  > Restore pass (mog `LAB_039E`, program `LAB_0242`; one body, enhanced sixth plane folded in), arena backdrop compositor S_12 + loader + obstacle probe (`src/engine/bgblit.cpp`, `src/game/arena.cpp`, `src/rt/arena_bg.cpp`), small primitives (`src/game/prims.cpp`, `src/rt/prims.cpp`); 573 -> 18 instructions of `JMP rt_*` stubs. Unicorn vs originals incl. blitter register snapshots; Practice pixel-identical to the original run; `integrate.py --boot` all PASS.
- [x] **7.1n** DATA/BSS to C++ data / generated tables (n1 program + twins, n2 mog S_4 tables, n3 mog S_1 state, n4 CHIP data) - L
  > n1 done 2026-10-06: `tools/gen_data.py` generates `build/gen/owned_data.{hpp,cpp}` (packed `ms::owned::*` structs initialised from the asm DC data, pointer cells as link-time constants, no ctors) for program S_2/3/9/11/16/17/22/24/26/32/33 and mog S_21/22/27/29/31/39 (`asm/patches/{program,mog}.data.json` `extern_data`; mog S_22/27/29 alias program's objects, S_21/31 own instances); `.set` aliases keep every `prg_/mog_LAB_*` name; resource.py drops owned hunks from the elf asm (XREF), `--verify` keeps them; image_tab lists all-zero owned DATA as BSS. `owned_data.cpp` is `-fno-lto`. `tests/test_gen_data.py` (bytes, relocs, labels vs the original hunks). Also fixed: `prg_LAB_0262` declared as data in progmain.cpp (LTO clash). Next: n2 (mog S_4 tables), n3 (mog S_1 + small mog hunks), n4 (CHIP hunks prg S_27/S_30, mog S_32/S_35/S_15/S_43).
  > n3 done 2026-10-06 (boot check pending, owner paused WinUAE testing): mog S_1 is `ms::owned::GameState g_mogState` (typed members from state_bind.hpp, offsets static_asserted), S_2/10/11/13/14/17/19/41/42 owned with default members (`asm/patches/mog.data_state.json`); gen_data reads every `<bin>.data*.json`, `"typed"`/`"includes"` keys. The agent saw reg-name-entry fail once with it and pass without; the same failure flaked at HEAD before it (harness race), so bisect only after the harness fix.
  > n2 done 2026-10-06 (boot check pending): mog S_4 is `ms::owned::MogTables g_mogTables` (`asm/patches/mog.data_tables.json`, curated regions in `tools/data_types.yaml`: menu text rows, `knightNames`, `creatureTypeDef[24]`, `creatureNodePos/Aux/Script[24]`, `encounterBands[4]`, combat action offsets, menu Y, player counts, moon frames; NUL-terminated texts as writable char arrays; scripts/palette/other tables stay label byte runs). gen_data applies same-length line patches inside owned hunks (the 79 `mog.fight_ops.json` operand patches); `.set` aliases keep every name. Left: n4.
  > n4 done 2026-10-06 (boot check pending): CHIP hunks owned (`asm/patches/{program,mog}.data_chip.json`): `g_prgScratch` shared by program S_27 and mog S_32 (one 45,744 B chip copy), `g_prgScreen`/`g_mogScreen` (S_30/S_35: screen cells keep the display patches, the copper template is only read by dead init code), `g_mogTileMask` (S_15), `g_mogCursorSprite` (S_43). Chip placement: `.chipdata.MEMF_CHIP` / `.chipbss.MEMF_CHIP` (nobits via asm), proven by a hunk-header test. Still asm-owned: program S_5/6/7/14/19/28, mog S_3/6/7/8/24/33/38/45 -> n5.
  > n5 done 2026-10-07 (boot check pending): the last DATA/BSS hunks are owned (`*.data_rest.json`): loader config/state/sector, trackdisk, work buffer, fonts typed `Glyph8 font[128]`, mog disk prompt `TextItem[7]`, `g_mogNoSprite`, `g_mogSynthData` (one chip object for both synths; `ms::kSynthWave` aliases it in game builds). `test_every_data_and_bss_hunk_is_owned`: asm/program.s and asm/mog.s hold CODE sections only. nb is not linked.
- [x] **7.1o** Stub cutover: ~205 one-instruction `JMP rt_x` stubs (program 44, mog 160) die when their C++ callers (`call(mog_LAB_x)`, `owCall`, `rtMainCall`, trampolines in src/rt) call the C++ behind rt_x directly; per group with its unicorn test: combat_load/combat_setup, job_frame, palette, display/irq/input, text/places/town_ui, then the program twins - M
  > Done 2026-10-07: part B 5edca11 (34 stubs, `tools/stub_cutover.py`, `RT_FN`), part A e9d76a8 (66 fight-load/arena/combat-UI stubs, callers call `rtCl*`/`rtAr*`/`rtCui*` directly), the rest with 7.1q. Boot regression PASS.
- [x] **7.1p** Address tables: script/handler/jump tables holding asm code addresses (fight_* LAB_0005..000D, sfx release LAB_0A9E/0A9F, creature handlers LAB_08C7, dragon scripts LAB_08FC, hunk-9 return chain, synth tick LAB_0F73) become C++ function pointers or tags in the owned data - M
  > Done 2026-10-07 (mog side; program's three script stubs LAB_0032/003F/0040 remain for 7.1r): arena table LAB_08C8 / swap LAB_05F0 / init LAB_05F1 hold rt entry addresses, fight handler identities are `rtFighterTag` bytes, fight_*/sfx-release/ritual-dice script operands are `$F000xxxx` tags (`mog.fight_tags.json`) dispatched by `rtFightOpRun` (LAB_04BA plays $99 on channel 3: the original forces index 0), VBL hooks hold rt entries, hunk-9 return chain is a direct call.
- [x] **7.1q** Last real asm code: mog sound-bank loaders LAB_0AA7/0AAA..0AB5, loot/town click chain LAB_052A/052F, display_ops LAB_0416/0419/041F/0422 (+ enhanced clear loops), program intro text-page scenes LAB_059E..05AC, anim_jobs LAB_025F..026C/0258/0264, carve LAB_0044 called directly, dragon flight LAB_0DCF; retire the dead ST/NT player (program LAB_0061/0083), the S_44/45 synth after the listening sign-off, the ldr-dead hunks - M
  > Done 2026-10-07: program 24d5ff9 (intro progress/text pages, anim jobs, screen ops, dead ST/NT player and cells retired; booted the full intro), mog (sound banks `src/rt/soundbank.cpp`, loot/town click chain `rtLootClick`, display ops, dragon handler, title step `rtTitleStep`). Live asm left: program SECSTRT_0 + 3 script stubs, mog SECSTRT_0 entry glue + the S_44 synth for the MS_SYNTH_ASM A/B build, data cells in code hunks (7.1r). Boot regression PASS; fights per creature, loot/town clicks, dragon flight still to boot (playtest).
- [x] **7.1r** Drop the asm from the link: overlay-entry reset from gen_data's object images instead of image_tab/hunk sections, CHIP objects owned by C++, `.set` aliases gone, `--verify`/test_resource/abs_symbols/vasm step retired (asm/*.s kept only as reference or deleted), MS_LINK_GAME_ASM/MS_LINK_LIFTED removed and the `#if` guards made unconditional - L
  > Done 2026-10-07: the default game build links NO game asm (no vasm step, no asm/*.o in the link map; `asm_remaining` 0 live units). Entries: rtGameRun calls `rt_prg_main` / `rt_mog_entry`. Code-hunk data cells are gen_data `extern_cells` (`asm/patches/*.data_cells.json`, bytes+relocs from the original binary), identity labels map to C++ functions (`link_names`), overlay-entry reset tables `rt::g_imageProgram/g_imageMog` replace image_tab.cpp. MS_LINK_GAME_ASM/MS_SWAP_LIST/linked-lifted removed (MS_LINK_LIFTED = cross-compile check only); `build/` is the Release game. `MS_SYNTH_ASM` assembles only `asm/synth.s` (mog S_44) for the A/B listening. Kept: the `.set prg_/mog_LAB_*` names as aliases of C++-owned cells (renaming ~730 externs is follow-up 7.1s), asm/*.s + patch tables as the `--verify` reference. Release LTO exe 732,764 -> 470,524 B. Boot: regression PASS (Debug autoplay), Release exe plays the intro on the owner-machine config (`build/shots/rel/`).
- [x] **7.1s** Follow-ups of the asm-free link: rename the `prg_/mog_LAB_*` alias names to C++ names (typed members of the owned structs; about 730 externs), prune `rt_*` asm-contract trampolines that only asm called, delete the patch machinery (`asm/patches`, `tools/resource.py`, `--verify`) and `asm/*.s` once the reference check is no longer wanted; `MS_SYNTH_ASM` and `asm/synth.s` go when the owner accepts the synth listening A/B (4.6) - M
  > 2026-10-07: 31 dead `rt_*` asm-contract trampolines removed (12 moved into test support, `-DMS_TEST_ENTRIES`), 309 cells renamed to C++ names over 2,023 uses (`tools/cell_names.yaml`, `tools/rename_cells.py --left` lists the rest: 597 names / 1,408 uses, mostly arena/combat-UI script tables), `.set` aliases only for still-spelled labels. Release exe 475,536 -> 464,284 B. Open: the remaining names; MS_SYNTH_ASM/asm/synth.s after the listening sign-off.
  > Remainder done 2026-10-07: 851 cells named (creature script tables per creature, animation scripts, texts/file names by content, button tables, palettes, carve buffers); 55 legacy names left (77 uses), all code-entry labels used by the A/B synth asm block or by tests that compare with the original. MS_SYNTH_ASM/asm/synth.s go after the owner's listening sign-off (2.6/4.6).
- [~] **7.2** Performance on a stock A1200 (68020/14 MHz, 2 MB chip, no fast RAM): 50 Hz where the original was; measure with the VBL counter from 2.2 per scene — M
  > Measure on two configs: owner-machine-like (68020 + 8 MB fast; build/aca1232.uae) first, then stock 68020/14 MHz.
  > 2026-10-07 (cycle-exact 68020/14 MHz, `docs/PERF.md`, MS_AUTOPLAY `PERF` lines): fights 10.0 fps on owner and stock configs (the 6-tick budget cap; busiest creature-fight window 83% of the budget on stock, none late), map 23-25 fps. Found and fixed: Release LTO folded identical functions (`-fipa-icf`) into a `bra.l` that elf2hunk relocates wrongly -> every Release fight crashed; `-fno-ipa-icf`. Open: town screen fps, owner-config creature fight.
- [~] **7.3** Release install: the Release exe (470,524 B) builds from `build/`; open: `hdinstall` layout and startup-sequence checked on a clean HD, the ADF target (cannot hold the arenas, see H4), `BUILD.md` updated by the integrator. Boot check pending: Release exe from a clean HD folder - S
  > 2026-10-07: `cmake --build build --target hdinstall` -> self-contained `build/hd` (exe 475,536 B, LF startup-sequence, data/ from the owner's disks), `docs/INSTALL.md` for a real A1200; the ADF boots with the original disks in DF1:-DF3: (not standalone: 2.2 MB data vs 880 KB). Fixed: a 5-byte kn1.ob stub on disk A won the DF search; log writes on a floppy hung the boot (logs only on HD installs now). Open: real-hardware check by the owner.
- [~] **7.4** Full playthrough checklist (all locations, every Moonstone, ending via the 2.12 re-entry) vs. the original — M
  > booted 2026-10-07 on the all-C++ build (34db0bc): `integrate.py --boot-play nightly`, all 12 `tests/boot/play_*.txt` PASS: whole game scripted end to end incl. the ending (play_end) and game over (play_death); open: dragon, every Moonstone location, comparison with the original (2.10).
- [x] **7.5** Docs: module map, label → function index generated from `symbols.yaml` — S
  > Reconciled 2026-10-07: `docs/MODULES.md` (module map) and `docs/LABEL_INDEX.md` (label -> C++ function, generated from the `LAB_xxxx` comments by `tools/label_index.py`, not from `symbols.yaml`, which another session owns), host test `tests/test_label_index.py`, plus `docs/MODDING.md`. Regenerate with `py tools/label_index.py`.

---

## M9: Modding layer (readable game API + data files)  `[ ]`

Decided 2026-10-07 by the owner: "abstract away all low level stuff and let the player change the game logic easily";
method chosen: **data files + C++ rules**. Layers: platform (ACE, hardware, files, display, audio, input devices) ->
engine services (drawing, sound, screens, the fight bytecode, entity pools) -> a high-level **game API** (knights, stats,
inventory, gold, items, creatures, fights, places, text, sound cues, RNG; no addresses or raw cells) -> **rules** (short
readable C++ functions against the API) + **data** (text files in `PROGDIR:mods/` read at startup; missing file = built-in
defaults). The built-in defaults reproduce the original exactly (parity tests); M8 co-op and new weapons build on this layer.

- [x] **9.1** Architecture note `docs/ARCHITECTURE.md`: layers, the game API surface, data file format and schema, how rules
  and data are found/overridden, how the readability passes (names, game programming patterns) map onto it - S
  > Done 2026-10-07: `docs/ARCHITECTURE.md` (layers L0 platform / L1 engine / L2 game API / L2b scenes / L3 rules + data, crossing rules with a lint, API surface, INI data format + schema `tools/mod_schema/*.yaml` + generator `tools/gen_moddata.py`, loader `src/rt/modload.cpp`, parity at four levels, migration steps 9.3a-e, 9.4a-d, 9.5a-e3, 9.6a-h, 9.7). Owner decisions: one `mods/` folder; a bad file is skipped with a warning; no-scaling becomes `[waves] scaling = none`; scripts by name only until 8.8.
- [~] **9.2** Readability phase 1: shared names, typed fields, named constants/enums (no behaviour change) - M
  > Phase 1 done 2026-10-07: Knight/Lair/Job `Unk`/`Flag` fields named (uwHeight, ubInputPort, uwReachX/uwTooCloseX/uwDepthReach,
  > ubCooldown, ubBehaviourFlags, ulActive; never-touched bytes are `Spare<offset>`), 30 cells renamed in `tools/cell_names.yaml`
  > (mogFightTotal/mogMaxAliveAtOnce/mogAliveNow, mogEncounterKind, palettes, colour cycle, AI goal...), new `include/game/constants.hpp`
  > (enum classes KnightKind, ActorType, InputPort, Action, SwordItem, ArmourItem, MoonFrame, SceneId, EncounterKind, PlaceId, ScriptOp;
  > pool sizes; `raw()`), ~150 literal sites swapped. Proof: the Debug autoplay binary is byte-identical before and after. Open: phase 2
  > (dispatch tables by ActorType, script-op table, scene state table, item/stat tables, pool vocabulary, input commands).
- [x] **9.2a** Game flow as a state machine (owner request 2026-10-07): every screen of the game (title/menu, map, map turn steps,
  towns/cities and their shops, home villages, places: wizard, Stonehenge, valley, castle; lair approach, lair fight, loot, duel, dragon,
  status, next day, death, ending) is a State object (enter / update / exit, explicit transitions in one table) driven by one state
  machine, instead of overlay-era call chains, step counters and flag latches. Behaviour bit-identical (boot regression + all play
  scripts); `docs/GAME_FLOW.md` with the state diagram; transitions are the hook points for mods and Moonstone 2 co-op - L
  > Owner 2026-10-07: they are separate scenes, each scene loads its assets on enter and unloads them on exit (an asset table per
  > scene: files, kinds, buffers; shared fixed buffers are acquired/released, same order and timing as the original).
  > Done 2026-10-07: `src/game/flow/` scene manager (SceneDef enter/run/exit/resume + asset rows; one stack; one transition table
  > Switch/Push/Pop/Halt in `registry.cpp`), 35 scenes in `src/game/scenes/` (title .. ending, towns, places, shop/loot screens, lair,
  > duel, dragon, fight), `src/engine/memstack.cpp` mark/release + shared slots with balance checks, `docs/GAME_FLOW.md` (diagram,
  > assets, how to add a scene), example scene behind `-DMS_EXAMPLE_SCENE=ON`, `tests/test_flow.py`. Nightly: all play scripts PASS.
  > Open (phase 2): move the shop/fight loads into enter(), a real MemStack block, the file cache in fast RAM.
- [x] **9.2b** RAII for resources (owner 2026-10-07): every acquire/release pair becomes a stack guard (`include/engine/guard.hpp` +
  rt guards): file handles, MemStack marks and scene slots, systemUse/Unuse, IRQ/DMA/blitter ownership, display state saves; a lint test
  flags new raw pairs; rule in CLAUDE.md - M
  > Done 2026-10-07: `include/engine/guard.hpp` (ScopeExit, MemMark, guard traits), `src/rt/guards.hpp` (SystemAccess, OsAccess,
  > NoRequesters, DosHandle, DosLock, FileHandle (the only movable one), MemBlock, IrqOff, FileSession); ~45 pair sites converted, 14
  > exempt rows in `docs/RAII.md` (rule of zero for data, rule of five by deletion for guards); lint `tests/test_raii.py` (check.py 0b).
- [x] **9.3** Game API header(s) and the platform/engine boundary: game logic stops touching cells, offsets, hardware - L
  > 9.3a/b done 2026-10-07: `include/game/api/` (World, party/stats, items/gold, clock/rng, fight pool, lairs, cue queue; typed ids)
  > with `tests/test_game_api.py`; `rt::worldRebind()` binds the live cells at every imageEnter, `World::pData` = `g_gameData`.
  > 9.3c + 9.3e done 2026-10-07: `tests/test_layers.py` layer lint (L0/L1/L1g/L2/L3/DATA/L2b include + pointer rules; allow-list
  > `tests/layers_allow.json` 12 entries with reasons, stale entries fail too; step 0 of check.py); `src/game/rules.cpp` split into
  > `src/game/rules/{stats,settle,clock,turns}.cpp` + headers (umbrella `game/rules.hpp` kept), World overloads for recalc/settle.
  > 9.3d done 2026-10-07: knight pointer fields go through API accessors (knightEngagedWith/Engage/Disengage, knightInventoryAddr,
  > knightWalkScripts); `tests/layers_allow.json` is empty. Def types came with 9.5.
- [~] **9.4** Data files: format, loader with validation and error messages, defaults generated from the original data,
  a parity test that the defaults equal the original tables - M
  > 9.4a + 9.4b done 2026-10-07: INI tokenizer `src/engine/inifile.cpp` (sections, comments, CRLF/BOM, 120-byte lines, int/`$hex`/string/list
  > values), generic applier `src/game/data/modparse.cpp` (field descriptors, `base =` rows, `mods/<file>:<line>: ... (file ignored)` errors),
  > checker `src/game/data/modcheck.cpp` (hook interface for 9.4c topic checks), schema `tools/mod_schema/rules.yaml` (`[waves] scaling`,
  > `[coop]` alive_bonus/total_factor/writeback_divisor, `[limits]` gold_cap 150 / dagger_cap 10 from scene_town.cpp), generator
  > `tools/gen_moddata.py` → `build/gen/{mod_defaults,mod_schema}.cpp` + `defaults/rules.ini`, key reference `docs/MOD_KEYS.md`.
  > Tests `test_modparse` (every error reason has a case, mutation-checked), `test_gen_moddata` (reference ini parses back to `kDefaults`
  > byte for byte). ~7 KB when linked (`-Os`).
  > 9.4c done 2026-10-07: `src/rt/modload.cpp` + pure `src/game/data/modload.cpp`: `g_gameData = kDefaults`, then `PROGDIR:mods/rules.ini`
  > (bad file skipped whole, `mods/<file>:<line>: ...` to the log and `PROGDIR:mods.log`, red splash 3 s, unknown *.ini listed), values
  > logged; `rt_file_open_exact/write_exact/list`; hdinstall stages `mods/defaults/`. Proof: `tools/modsboot.py bad|good` PASS, `test_modload`.
  > Release +13.6 KB. Values not read by the game yet (9.5a). Open: 9.4d parity leg.
- [ ] **9.5** Tables to data (type objects): creatures (stats, scripts, sounds), items/weapons/armour, lairs, shops and
  prices, encounters, places, scaling rules (MS_MOD_NO_SCALING becomes a data switch) - L
  > Owner 2026-10-07: `[waves] scaling = original|limited|none`; limited = the monster count still grows with the knight, the number
  > alive at once does not.
  > 9.5d done 2026-10-07: WeaponDef/ArmourDef/ItemDef rows in GameData (`tools/mod_schema/items.yaml`, `mods/items.ini`), table sections
  > in gen_moddata, rules/stats.cpp and `src/game/damage.cpp` (contactDamage) read the defs; `tests/test_items_data.py`. Open: shop/loot
  > code still uses literal codes/prices (9.5b/c files).
  > 9.5a + 9.4d done 2026-10-07: `src/game/rules/waves.cpp` (waveScale/waveWriteback) driven by `[waves] scaling = original|limited|none`
  > and `[coop] enabled/alive_bonus/total_factor/writeback_divisor` (co-op inactive by default); CMake MS_MOD_NO_SCALING removed;
  > `waves:` log line per fight; `tests/test_waves.py` (3000 cases per path vs the old code), `tests/test_mod_parity.py`,
  > `tools/modsboot.py plain|waves|defaults` PASS.
  > 9.5b + 9.5c done 2026-10-07: `shops.yaml` (market prices/sell shift/stock, dice rows, temple stat cost, healer, dagger price; smith
  > prices = the items.ini WeaponDef/ArmourDef price), `places.yaml` (place rows, map nodes), `lairs.yaml` (24 lairs, loot odds),
  > `encounters.yaml` (dragon day, AI engage odds); scene_town/overworld/placevisit/resetGame read GameData; topic checks
  > `src/game/data/modtopics.cpp`; `tests/test_shops_data.py`; `modsboot.py smith` PASS. Lair/loot/node keys default to -1 (= keep the
  > original from the disk data). Open: shop screen price text is picture text; wizard/mystic/gift bands and key placement.
- [x] **9.6** Rules as readable C++ against the API (State/Type Object/Bytecode/Object Pool/Command patterns where they fit):
  damage and contact, wave scaling, leveling and stats, healing, shops, dice, rituals, AI choices - L
  > 9.6a-f done 2026-10-07: `src/game/rules/{stats,levelling,healing,shops,dice,rituals,ai_map}.cpp` (+ doc blocks in turns/settle):
  > each header opens with "what a modder can change here"; scenes keep the UI and ask the rules; `tests/test_rules_topics.py`;
  > MODDING.md section 4c.
  > 9.6g/h done 2026-10-07: `rules/damage.cpp` (contact damage, block decision, one hurt row per ActorType, named flat values),
  > `rules/ai_fight*.cpp` (one decision file per AI, `kFightAis` table mapping catalog AI names 1:1, handler table by AiKind);
  > `tests/test_rules_fight.py`. The catalog calls Be the lion (`Lion (Be)`, AI `flyer` = a leaper).
  > 9.5e1-e3 done 2026-10-07: `creatures.ini` / `arenas.ini` rows over the built-in records (override-only, -1 = original), one generic
  > arena runner, cel sets and sound banks by name; a new monster = creature + arena + lair row, no C++ (MODDING 4b). Emu: identical.
  > Web editor: `tools/monsterkit/editor_web/monster_editor.html` (standalone, no game art; built by build_web.py). 10.4: export_public.py,
  > dev_setup.py, docs/MIGRATION.md (dry run green).
- [x] **9.7** Modding guide + example mods (no scaling, cheaper shops, tougher dragon) that need no rebuild - S
  > Done 2026-10-07: MODDING.md rewritten as a player guide (what a mod is, quick start, one section per file, three-row new monster);
  > `mods_examples/{no_scaling,limited_scaling,cheap_shops,tough_dragon,cave_troll}` (staged by hdinstall, inactive), each boot-checked
  > with `tools/modsboot.py`; the dragon's creatures.ini row now applies (`dragonRowApply`); `tests/test_mod_examples.py`.
- [ ] **9.8** Monster/asset creation tool (owner request 2026-10-07; chosen: visual editor in the browser): a local web page loads a PNG sprite sheet, snaps colours to the game palette, sets frames, anchor points and hit boxes with the mouse, previews animations at Amiga speed and exports a "monster kit"; a command-line converter turns the kit into game files (cels, hit set, animation scripts, `creatures.ini` row, sound bank pick) - L
  > Split: 9.8a kit format + formats research (cel/.ob/.c/hit sets/script opcodes) + converter (needs 8.8 editable scripts for new animations); 9.8b the browser editor; 9.8c an example new monster end to end (boot + play script). Owner 2026-10-07: per behaviour type the tool knows every animation the enemy needs (required checklist, export blocked if one is missing), and any existing monster can be cloned (frames, animations, hit boxes, stats, sounds) as the start of a new one. The AI is choosable per monster (`ai:` = one of the engine's fighter behaviours: flyer, snatcher, demon, brawler, caster, drake, stalker, dragon, AI knight; fight_ops.cpp fightTablesInit); the checklist follows the chosen AI; brand-new AIs are C++ rule handlers registered by name.
  > 9.8a1 done 2026-10-07: `tools/fightscript.py` (fight-script disassembler/assembler, exact byte+relocation round trip over all 8
  > creatures, the demon and 290 script roots; oracle-tested against a host build of combat_script.cpp), `tools/monsterkit/engine_ports.py`
  > (hitParse, contactTest, overlap, depth, cel size; random-vector tested against host builds), `ai_catalog.json` (8 AIs + demon + AI
  > knight: roles, required hurt indices 1/2/3/5/6/8, tunables, damage, sounds), `kit.schema.json`; every [guess] in MONSTER_KIT.md
  > verified or corrected (section 8). Tests `test_fightscript`, `test_monsterkit`. Next: 9.8a2 (clone/check/build T0), 9.8b editor.
  > 9.8a2 done 2026-10-07: `tools/monsterkit.py clone|check|build --tier T0|install|catalog`; all 8 creatures clone and rebuild
  > byte-identical (decode-equal with `--reencode`); a recoloured ratmen T0 boots through the art override (play_lair shot). Next: 9.8b
  > browser editor + `serve`, T1/T2, 9.8c.
  > 9.8b done 2026-10-07: `py tools/monsterkit.py serve` (127.0.0.1 only, guarded JSON API) + `tools/monsterkit/editor/` (plain JS,
  > offline): clone from game, sheets/frames, anchor + attack points by mouse, pencil/fill/eyedropper/undo, onion skin, PNG import snapped
  > to the 32-colour palette, AI picker with the live required-animation checklist (Build blocked while red), stats/tunables/damage, 50 Hz
  > preview, Build T0 + Install. `tests/test_monsterkit_serve.py` (23). Open: step editing (T2), contact/walk preview, rect editing.

- [ ] **9.8d** *(backlog 2026-10-07)* Beholder enemy (owner request, reference art kept out of git): a floating eye that only fires a laser.
  Owner design: no melee hits; a long visible charge (~1.5 s) before each shot, then the beam, then a fade; slow rate (~3-4 s
  per shot); smooth animation (a new pose every tick: waving stalks, 1 px bob, ramping glow). Base AI: spearman (trogg spear:
  attacks from range, one attack role, built-in cool-down), not Be (= the lion) and not the troll. Needs: T1 (new frame sizes for
  the beam), a minimal T2 (script tick counts), own cel set + own scripts for a separate creature (9.8c), damage per hit as data
  (9.6g); later a dedicated "beamer" AI with a line hit test. Original art only (name it e.g. "Gazer" in any public example).
  WIP: branch wt-beholder, kit build/kits/beholder.

## M8: Moonstone 2 (co-op party of four knights)  `[ ]`

Decided 2026-10-06 by the owner, to start after M7 (all C++ first). Aim: up to four human knights play together as a party.
Must-have is co-op through the existing game; the scrolling lairs are a stretch goal. Built on the C++ game (`src/game`),
behind a build/game option so the original single-player rules stay intact for parity tests.

- [x] **8.1** Design note `docs/MOONSTONE2.md`: what the original already has (1-4 human knights taking turns on the map, 1v1
  knight duels, knight-vs-creatures fights), what changes per screen (map, towns, lairs, fights, loot, end), save-state and
  option layout. Open owner decisions are listed there (party moves together or each knight in turn; shared or own gold/loot) - S
  > Owner decisions 2026-10-06: the party moves as one on the map (one token, one move per turn); loot is not split - every knight gets the same loot (each receives a full copy of the gold/items found).
  > Done 2026-10-07: `docs/MOONSTONE2.md` (engine facts with file:line, per-screen changes, input plan, limits, phased plan P0..P11, 12 owner questions in section 7). Key limits: fights have two named fighters and every creature targets the "first fighter"; 10 job slots, 45 dirty rects/frame, team-blind contact, 2 knight colour triplets in 5-plane mode, 100-record click pool; black knights are the unused knight slots (none with 4 humans) -> a new record pool after the dragon slot.
  > Owner decision 2026-10-07: 2-knight co-op is an acceptable scope (fits the frame budget far better than 4). In co-op mode (2 knights) the "alive at once" count gets +1 (scaleWave's LAB_05ED) and the fight's total monster count is doubled (LAB_05EC, incl. the lair count) and the remaining count written back to the lair after the fight (mog.asm 636-639) is halved (rounded up), so the lair population stays consistent; with MS_MOD_NO_SCALING these are the only scaling.
  > Owner 2026-10-07: co-op is now THE PRIORITY. Decisions: 2 knights first (two joysticks; engine built so 4 can follow); the party
  > token's leader rotates each round; a knight knocked down in a fight the party wins still loses a life (as the original); friendly
  > fire off. Defaults taken from MOONSTONE2.md section 7 for the rest: one black-knight group token spawning N fighters, N = party
  > size, move budget = the party minimum, home villages: own village rule kept, gold copies kept (decision 3).
  > Owner 2026-10-07: the party's starting location is the FIRST player's home (village).
- [~] **8.2** Input for four players: joystick ports 0/1, the parallel-port 4-player adapter (ports 3/4, as Dyna Blaster used)
  and keyboard sets (arrows+RCtrl, WASD+LAlt); a per-knight control choice on the knight screen - M
  > 2 knights done 2026-10-07: `include/game/party.hpp` (`g_party`, classic when inactive), `include/engine/pad.hpp` (Joystick 1, Joystick 2
  > = mouse port, Keys arrows + Ctrl/RShift, Keys WASD + Alt), title menu Players -> "Coop 2", a Control line per knight on Select
  > Knight, input routed per current knight on map/fights/loot; `tests/test_coop.py`, `play_coop.txt`. Open: 4-player adapter (pinout),
  > per-fighter pads in fights (8.4b).
- [ ] **8.3** Party on the map: one party token (or knights in turn, per 8.1), encounters and place visits take the whole party - M
- [ ] **8.3a** Knight record pool and turn slots for a party of 4 plus black knights (records after `mogKnights[4]`, the dragon, which is hard-wired) - M
- [ ] **8.4** Co-op fights: up to four player knights on one side of the arena against the enemies; fighter slots, AI targeting,
  hit tests and the UI panels for four; when the party meets black knights there are as many black knights as player knights - L
- [x] **8.4a** Perf probe first: draw/update cost per fighter (beam lines) to size a 4+4 fight on stock and fast-RAM machines (linear estimate ~2040 of 1872 lines on stock) - S
  > Done 2026-10-07 (`docs/PERF.md` "Fight frame split", `tools/perfx.py --model`, `tests/boot/perf_*.txt`): cel draws are 85-90% of a fight frame; per fighter ~460 beam lines (knight) / ~388 (creature) on stock, ~250-275 with fast RAM. Predicted 4 knights + 4 creatures: 4.4 fps stock, 7.0 fps owner; 2 knights + 4 creatures fits better. Levers: halve cel draws + skip unchanged sprites -> 10 fps; job table 10 -> 16, rect cap 45 -> 64. Owner 2026-10-07: 2-knight co-op is an acceptable scope if 4 is too much.
- [ ] **8.4b** Fight-engine generalisation: N fighters, per-fighter pad (Knight+11 port pick), creature targeting beyond the first fighter, team-aware contact, more job slots/dirty rects, per-knight script/damage table copies - L
- [ ] **8.5** Co-op lairs, loot and towns: lair fights with the party, loot shared/split per 8.1, town screens per knight - M
- [ ] **8.6** (stretch) Golden-Axe style lairs: after a wave is killed the knights walk right, the arena scrolls to the next
  section and the next wave enters; ends with the lair's guardian. Needs a scrolling arena (ACE scroll buffer) and wave tables - L
- [ ] **8.7** Balance and playtest of the co-op mode on the owner's A1200 (68020 + 8 MB fast) - M
- [ ] **8.8** New weapon types (owner request 2026-10-06): weapons become data (reach, damage, speed, the attack moves they allow,
  the knight cel set they use) instead of the original's fixed sword/item cases; then melee weapons with longer reach - poleaxe,
  spear - each with its own attack animation frames (new knight cels, drawn by the owner or derived from existing frames), hit
  boxes in the hit sets (`collide.hit` format) and shop/loot entries - L
- [ ] **8.9** Ranged weapons - bows (and maybe crossbows/throwing axes): a projectile object in the fight engine (spawn, flight,
  hit test against fighters, ammo), aim/draw animation, enemy AI that closes in or keeps distance - L
  > Owner note 2026-10-06: the game already throws daggers (fighters.cpp: `daggerAim` / `daggerStart`, `DaggerBlock`; the caster states LAB_0255/0256/0259/025E), so bows reuse that projectile path (aim, flight, landing, hold-then-throw-again) with an arrow cel and bow stats instead of a new projectile system; the work is making it data-driven and available to player knights.
  > Correction (8.1 note): the player/AI knight dagger is the scripted type-$34 projectile (`daggerRelease` creatures.cpp, `fighterDagger` fighters.cpp); `daggerAim`/`daggerStart` is the caster/drake/dragon ballistic path. Bows extend the $34 path.
- [ ] **8.10** Playable monsters (owner idea 2026-10-06): a player picks a creature (troll, rat man, demon, ...) instead of a
  knight. In fights the creature's AI controller (`src/game/fighters.cpp`, `fight_creatures.cpp`) is replaced by joystick input mapped onto
  its existing moves (walk, its attacks, dagger throw where it has one); its stats/hit points come from `creatureTypeDef` (7.1n2).
  Outside fights: a party member slot with a creature portrait, what it can buy/use in towns, how it levels up. Balance per creature - L
  > Correction (8.1 note): `creatureTypeDef` is the 24-row lair table; creature stats live in the per-creature `init0169`..`init019F` (arena.cpp).

## M10: Publishable repository (no original data)  `[ ]`

Owner 2026-10-07: the project will be published on GitHub without any original art, data or code; everything original is
extracted from the user's own Amiga disks (the three ADFs) at setup/build time.

- [x] **10.1** Audit: list every tracked file derived from the original (disassembly, reassembled `asm/*.s` + patches, `src/lifted`,
  embedded byte tables in src/tests, shots, fixtures) and every build input that comes from it (`build/reasm`, `build/disks`); doc
  `docs/PUBLISHING.md` - S
- [ ] **10.2** Build from the ADFs only: `py tools/setup.py <A.adf> <B.adf> <C.adf>` extracts the disks, produces whatever the build
  needs from the original binaries (hunks, labels by offset from our own facts, not the IRA listing), generates the data, builds and
  hdinstalls; `asm/*.s` + patches and the reference listing become generated from the disks; tests needing originals skip cleanly - L
  > Owner 2026-10-07 (target changed): the GAME loads the original data at start-up instead of compiling it in, so the build and the
  > released executable contain nothing original: 10.2a the data hunks of `program`/`mog` (~61 KB) become empty same-size arrays filled
  > at start from the original executables on the disk/HD/ADF (hunk parse + relocation, checksum of the known version, clear error
  > naming the wrong disk); 10.2b an OFS reader in `src/rt/files.cpp` so `PROGDIR:disks/*.adf` work as well as DF0-3:/HD files;
  > 10.2c the build needs no original file (gen_data emits layouts/sizes only). The "build from the ADFs" pipeline above is CUT
  > (owner 2026-10-07): setup.py is only a developer helper that extracts the ADFs for the tests that compare against the originals.
  > Proof: the start-up-filled hunks equal today's compiled-in data byte for byte; boot regression + play scripts green.
- [x] **10.3** Leak scan test: every tracked file is compared against the extracted original files (long byte runs, decoded pictures,
  text strings); runs in `integrate.py` so nothing original slips back in - M
- [~] **10.4** Clean export: `tools/export_public.py` writes `moonstone-ace` (+ `ace` as a submodule) into a fresh repository with
  no history, runs the leak scan and a from-ADF build there; README for players (where to get the disks, setup, WinUAE/real A1200) - M
  > Owner 2026-10-07: the public repo is `D:\moonstone-ace-public` (created, `ace/` a git submodule pinned at 9e6ce06 from
  > AmigaPorts/ACE; layout = moonstone-ace contents at the root + `ace/`). Once it builds from the ADFs only, development MIGRATES
  > there (worktrees, agents, integrate); D:\Amiga stays as the private archive (moonshard, IRA listing, history with originals).
- [ ] **10.6** Ready-to-play Amiga releases (owner 2026-10-07: easy play), built by one target, no original data inside (needs 10.2a/b):
  (a) bootable "Moonstone ACE" ADF (exe + startup-sequence) that plays from the original disks: one-drive A1200s get an in-game
  "insert Moonstone disk B" prompt (volume-name lookup, no hidden DOS requesters while the game owns the display), extra drives
  are used without swapping (closes H4 for 2 MB chip); (b) HD archive (.lha) with icon: drop the ADFs or the disk files into its
  `disks/` drawer and double-click; (c) a WinUAE-ready zip (A1200 config + the game; the user adds the ADFs). Checked on the
  owner's A1200 + ACA-1232 and on a stock A1200 config - M
  > Owner 2026-10-07: HD only ("nobody uses floppy"): (b) the HD archive, then (c); (a) the boot floppy is dropped, and so is H4.
- [ ] **10.5** Owner decision before pushing: licence, and the legal question of publishing a translation of the original program - S

## Risks and open questions
- **Cracked reference.** All parity targets are the Crystal crack, not a retail disk; hunk 9 (1.4a) and any crack-side patches are unknowable beyond what the listings show. Record this in the README's status table.
- **68000 → 68020 semantics.** Undefined flags after DIV, `MOVE SR` privilege, trace/illegal behaviour (hunk 9), instruction timing (`VHPOSR` and `DBF` loops). The harness runs a 68000 model; the target is a 68020. Any routine whose replay depends on undefined flags must be marked in `symbols.yaml`.
- **ACE `systemUse` inside the game** (file reads) reloads ACE's view and interrupts; under the M2 ownership rule every such call must save/restore the game's copper and DMA state (2.9 TODO).
- **Thunk cost.** A register-marshalling thunk per call is fine for scene-level routines but not for inner loops (IMAGEXCEL per-sprite calls, contact tests per frame). Lift callers and callees together (`--closure`) so hot paths stay inside C++.
- **Unresolved indirect sites** (five in reachable code, see 5.5) can hide code the call graph never saw.

## How to check
```sh
py -m unittest discover tests            # tools, patch tables, IRQ/files contracts
py tools/resource.py --verify            # asm/*.s still load-equivalent modulo patches
py tools/routines.py                     # symbols.yaml validates, inventory regenerates
py tools/diffharness/run_host.py         # every lifted routine replays its cases
py tools/lift.py --survey                # lifter coverage / blockers
cmake --build build-game-debug --target hdinstall   # then moonstone-ace-hd.uae, screenshot via uaeshot.ps1
```
(H2 folds these into `tools/check.py`.)

## Rules that apply to every task
- The original asm is the reference; the C ports (moonstone-main, moonshard) are only cross-checks.
- A routine counts as lifted only after its differential tests pass; screenshots check scenes, not routines.
- Every task that changes behaviour ends with a `build/shots/` screenshot or a replay log named in its result note.
- AGENTS.md engine patterns: small modules, state tables, fixed pools/no heap, update/draw/input separated, pure rules host-tested.
- No original binaries or data in git: extracted/generated files go to `build/`.
- Task numbers are referenced from code comments and docs (`ROADMAP 2.7`, `2.9`, `3.2`); never renumber. Add `Na` suffixes or new trailing numbers instead.
