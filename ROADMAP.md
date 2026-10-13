# moonstone-ace roadmap

Goal: Moonstone decompiled to C++ on ACE, cross-built on Windows, running on
the A1200 (68020, AGA, KS3.1). Every milestone ends with a game that builds
and runs. The original routines in `../moonshard/moonstone-main/amiga_asm/`
are the reference.

Size: **S** ≤ 1 session · **M** a few sessions · **L** split further when started.
Status: `[x]` done · `[~]` in progress · `[ ]` todo.

---

## Where we are (2026-10-07)

### v0.2.0 acceptance follow-up (2026-10-13)

- Saved batch evidence is ahead of the summary: `build/batch6_direct.log` (71 common hashes, direct UDP + rejoin),
  `build/batch6_tcp.log` (69, TCP + rejoin), `build/batch6_quad.log` (38 equal across four nodes),
  `build/batch7_classic.log` (59) and `build/batch8_relay.log` (relay + UDP, NETPAIR PASS).
  These are recorded runs, not a blanket closure of all M12 acceptance criteria.
- The live public service passes `py tools/netserver/selftest.py --server moonstone.shatteredallegiance.com --port 6510`:
  direct, relay, UDP relay, rejoin-flagged Playing game, and wrong-build rejection. Local and live-service selftests both pass.
- Fresh classic protocol run **passed**: `build/accept-classic-diag.log`, real speed, both quit markers,
  62 equal common hashes through tick 3100, 17 matching tick-pokes, UDP active on both with no fallback. Map turns, next day,
  guest-controlled lair fight and return to map were exercised. On rerun with the loot-ready barrier and the lobby input fix,
  NETPAIR PASS again: 58 matching hashes through tick 2900, both quit, 15 matching tick-pokes, UDP active; screenshot
  `build/shots/netpair-cj-loot.png` confirms the completed loot screen. `screen ready 2` is logged after redraw/palette, not at the early scene-ID store.
- A rerun with only a 240 s autoplay timeout ended early during the second map turn (tick 1272), then the runner closed the
  host; this was an undersized test timeout, not a diagnosed network freeze. Two subsequent runs with the documented 600 s
  allowance passed: one had 60 common hashes through tick 3000, the latest 62 common hashes through tick 3100, both quit
  markers, 17 matching tick-pokes, and UDP active without fallback. `screen_ready_2` appeared in both windows; map, fight and
  loot screenshots are valid. Logs `build/uaeshot-114.log` through `build/uaeshot-117.log` (summary in tool output for runs 114/115
  and 116/117).
  The first repeat reached both quit markers with all 60 common hashes equal,
  but `uaeshot.ps1` missed the host's quit because WinUAE split its serial capture into 12-character chunks. The runner
  now reconstructs those lines for shot / shot-end / quit detection; verified against the captured log (13 shots, one quit).
  A repeat with that reader recovered the markers normally but stopped progressing on both nodes after tick ~580,
  with 11 matching hashes through tick 550, no desync, and VBL/autoplay still running. The hash barrier and subsequent
  turn wait timed out. Latest diagnostic logs: `build/uaeshot-94.log`, `build/uaeshot-95.log`; screenshot `build/shots/autoplay-end.png`.
   Repeats also froze in the lobby, and a guest froze at tick 5 inside the node pump with no socket call active; a later run
   stopped progressing in its lockstep wait at tick 2045. The extended two-window town visit exposed and fixed deterministic
   tick starvation in the cursor-polled town loop (no beam wait). Two full-duration post-fix town runs passed; keep monitoring
   the older failure signature during the broader network matrix. Latest town run: `build/uaeshot-138.log` / `uaeshot-139.log`,
   NETPAIR PASS through tick 3950, 79 common hashes, 21 tick-pokes; Town A and Next Day screenshots checked.
- The on-screen ping is application PING/PONG RTT, including socket-service scheduling, measured with the emulated clock,
  not an OS/network-only ping. Debug autoplay fight shot `build/shots/netpair-cj-fight.png` showed 94 ms / 0% loss on localhost;
  map PERF lines in the repeat show roughly 8-9 updates/s and 62-120 ms RTT. Screenshot capture freezes a guest for 400 VBLs.
  More frequent transport servicing during frame waits is a candidate improvement; it has not been implemented or measured here.
- `check.py --quick`: **GREEN**, 151 modules / 1358 tests (35 mutation tests skipped); resource verification, symbols,
  status ladder and survey passed. The four m68k test rigs now define `BARTMAN_GCC` as the real build does; the SDL smoke test
  builds `build-sdl-test` so a running preview cannot lock its link output. All five previously failing modules passed (55 tests).
  The receive diagnostics passed the focused network/structural checks (38 tests), and the loot readiness change passed
  `test_bootcheck`, `test_combat`, `test_loot`, `test_netsession` (42 tests). Amiga Release and autoplay builds link.
  Logs: `build/testrun/<module>.log`, `build/accept-classic-diag.log`.

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
  4. Backlog: enhanced display 4.8a/4.8c-e. After M7: **M8** (Moonstone 2); **M11** PC version on SDL3 (owner 2026-10-09; SDL3 since 2026-10-10).
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


- [ ] **9.9** Options screen for mods (owner 2026-10-10): a title-menu **Options** item opens a screen that lists the mod packs
  (folders in `PROGDIR:mods_examples/` and the player's own packs in a `packs/` drawer) with On / Off, and the main settings of
  the enabled packs and the base rules (e.g. Dexterity on/off, wave scaling) as left/right choices; the choice is saved in
  `PROGDIR:mods/options.ini`, and on leaving the screen the data is rebuilt (defaults + enabled packs in order, the same loader
  as at start-up, `rt::modsLoad`, under its own OS guard) before the next game starts. Changes apply to the next game only, never
  inside a running one; in a network game the host's mod set is the one played (the data checksum and rule bits in HELLO
  already refuse a mismatch, so clients are told which mods the host uses). Conflicting packs (the same key set twice) are
  shown on the screen, the later pack wins. Files set up by hand before launch keep working as today - M
  > Built: pack rows in the small font, 8 per page; DEXTERITY OFF/ON and MONSTER SCALING NONE/LIMITED/FULL show the value in
  > effect; setting-only packs are hidden and an old `options.ini` converts them to settings; switching a pack on turns a
  > conflicting pack off ("X TURNED OFF") instead of the CONFLICT note. Examples cheap_shops and cave_troll removed (owner).
  > Open: the `play_options` boot run with screenshot.
  > 2026-10-11 (owner): the Options screen is the SETTINGS row of the new main menu, not a title-menu item (`poke menuact 2`, `play_options`); leaving it returns to the main menu.

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
  > Knight, input routed per current knight on map/fights/loot; `tests/test_coop.py`, `play_coop.txt`. Open: per-fighter pads in
  > fights (8.4b). The 4-player adapter moved to 8.2a (backlog, owner 2026-10-09).
  > 2026-10-09 (owner: finish co-op for 2 to 4 knights): title menu Players 4 -> "Coop 2" -> "Coop 3" -> "Coop 4" (`PARTY_COOP_MAX` 4); the knight screen offers each knight the first free controller in the order Joystick 1, Joystick 2, Keys arrows, Keys WASD and cycles only the free ones, so no two knights share a pad; both keyboard sets held at once checked against a keyboard-matrix ghosting model (`tests/test_keyghost.py`, `docs/MOONSTONE2.md` 3.2: no phantom is E / Space / Esc / Return; the real A1200 matrix is for the owner to check, 8.7). `tests/test_coop.py` (Coop 2..4 menu steps, four-knight and three-knight pad binding), boot `tests/boot/play_coop4.txt` (`build/shots/play-coop4-*`).
  > Emulator pass 2026-10-10 (branch wt-coop_pass, Debug `build-autoplay`, WinUAE instance 90): `play_coop.txt` runs to quit; Players -> "Coop 2", both knights' Control lines (Joystick 1 / Joystick 2 / Keys arrows), the leader's own pad steers (joy1 ignored while knight 2 leads; the arrow keys move it), the dumps show `leader` / `pad` rotating (`build/shots/play-coop-players.png`, `play-coop-pad2.png`, `play-coop-walk2.png`). The MISSED `joy1` pulses of the earlier report did not repeat in `play_coop`; they come from a boot where the title menu is not ready when the script presses (seen once in `play_coop_valley`: the run then started a practice fight and its numbers were discarded; rerun was clean): a host-load flake of the fixed delays, not a game bug. `play_coop4.txt` (4 knights, four controllers) passes: leader 1, 2, 3, 4 each steering with its own pad, the others ignored; the script no longer waits for a black-knight turn (a party of 4 has no black knight on the map).
  > 2026-10-11 (owner, title menu rework): Players is the count only (1..4, `menuSetPlayers`, the original), a separate **Mode** row (directly under Players, then an empty line, then Practice / Select Knight) is Classic / Coop with no number; the "Coop 2..4" steps and
  > `pfnDrawPartyText` are gone. Coop with one player makes two, in co-op Players stays within 2..`PARTY_COOP_MAX`, back to Classic keeps the count. The Gore row of the title menu is removed
  > (gore always on, see 12.3). The title menu is the LOCAL row of the new main menu (LOCAL / MULTIPLAYER / SETTINGS, `SceneMainMenu`), with a Back row and Esc; `tests/test_main_menu.py`,
  > `tests/test_coop.py`; scripts `play_coop*.mk`, `play_coop_mode.txt` and the `@PRE` prefix navigate the new rows (Players, Mode, Practice, Select Knight: down x3 to Select Knight).
- [ ] **8.2a** *(backlog 2026-10-09: owner moved it out of the co-op work; 2 knights use ports 0/1 and the keyboard sets)* Parallel-port
  4-player adapter (ports 3/4, as Dyna Blaster used): pure PRB/PRA decoder with host tests, guarded parallel-port DDR use only while a
  knight picks Joystick 3/4, "Joystick 3/4" on the knight Control line, a raw-bits diagnostics view for the owner's pinout check on the
  A1200, autoplay joy3/joy4 injection (MOONSTONE2.md 3.2, open question 11) - M
- [x] **8.3** Party on the map: one party token (or knights in turn, per 8.1), encounters and place visits take the whole party - M
  > Map part done 2026-10-09 (merged onto 8.2 + RAII 9.2b): the party is ONE token at the first player's home (`rt/party.cpp`, `rt/party_start.cpp`, `game/rules/party`), the leader rotates each round and steers with its own pad, Space shows one status sheet per member (`statusParty`, `ms::scopeExit` gives back LAB_0633 / ActiveKnights +0), each sheet closes with its owner's fire (TURN focus reads the member's pad). Screens: `tests/boot/play_coop.txt` (menu-driven, `build/shots/play-coop-status1/status2/end`) and `play_coop_map.txt` (poke-driven, `play-coop-map-status1/status2/end`). Open: encounters / place visits with the whole party (8.4, 8.5).
  > 2026-10-09 N = 2..4: the leader rotates through every member (`play_coop4`: leader 1, 2, 3, 4, each steering the token with its own controller, the other three held and ignored), `statusParty` shows N sheets (each closed with its owner's pad), (the roster strip `mapDrawRoster` of that day was removed on the owner's request 2026-10-09: ONE player-coloured token on the map).
  > Verified 2026-10-10: `play_coop.txt`, `play_coop_map.txt` (one token, leader rotation, status sheets per member in the member's colour: `play-coop-status1/2.png`, `play-coop-map-leader2.png`) and `play_coop4.txt` pass. **Owner change 2026-10-09 (done 2026-10-10): the roster strip (`drawRoster`, the helmets along the top edge) is gone, the map shows ONE player-coloured token (the leader's)**; the black knights are the original AI knights as tokens of their own (see 8.3a). Unit tests `tests/test_party.py` rewritten for it (`draw_no_roster`).
- [x] **8.3a** Knight record pool and turn slots for a party of 4 plus black knights (records after `mogKnights[4]`, the dragon, which is hard-wired) - M
  > Done 2026-10-09: party records 0..n-1, black-knight group records 5.. (`poke dump 1` logs `party size/leader/bk/port/pad`), turn slots and leader rotation per round; `tests/test_party.py`, `play_coop.txt`, `play_coop_map.txt` (fixed for the 8.2 controller lines of the knight screen).
  > 2026-10-09 N = 3, 4: the pool already held four black-knight records (`REC_BK_POOL` 4 after the dragon, `mogKnights[0..4]` unmoved); `tests/test_party.py` `n3_*` / `n4_*` (start, token, leader 0..N-1 twice, BK upkeep, budget minimum, dead skipped, frogs, mirrors, scan, BK engagement follows the leader); the fixed-4 loops are audited in `docs/MOONSTONE2.md` 6.2 (loot's `lootNextKnight` is left to 8.5, fights' `knightsRecalcAll` after a settled fight covers records 0..3 only: the black-knight pool records need their own recalc in 8.4b).
  > Owner change 2026-10-09, done 2026-10-10 (replaces the single BK group token): the knight records the party does not use (records 2, 3 for two knights; none for four) stay the ORIGINAL AI black knights (kind 4, original lives / stats / names, normal `LAB_01AE` start spots 160,20 and 160,180, each its own token and own turn slot, moving with the original AI: `partySlotCount` = 1 + (4 - party size), `partySlotRecord`; `partyAiPickOpponent` picks the party token for each; `partyRotateLeader` re-engages them with the new leader). The pool records 5.. are now FIGHT EXTRAS only (never drawn / scheduled / scanned): in a duel the engaged map knight is foe 0 (`FightRoster::ulDuelFoe`, set by `fightMeet`; `fight_seed.cpp`) and extras fill up to the party size; after a won fight the extras hand their belongings to the engaged knight (`partyBkPotFill(rs, iPot)`). Verified `play_coop_map.txt` (the two black knights wander on their own: dumps `knight2/3` move, `play-coop-map-leader2.png` shows one party token + two dark tokens) and `play_coop_bk.txt` (below).
- [~] **8.4** Co-op fights: up to four player knights on one side of the arena against the enemies; fighter slots, AI targeting,
  hit tests and the UI panels for four; when the party meets black knights there are as many black knights as player knights - L
  > 2 knights done 2026-10-09 with 8.4b (lair creatures and the 2-vs-2 black knights, scaling, end / settle rules, a colour of its own for the second knight: `src/rt/knight_colours.cpp`, the knight cels remapped once at fight start into free palette entries of the arena picture, `engine/celremap.cpp`, `tests/test_celremap.py`; the log line `knight colours: knight 2 uses palette entries ...`). Left: 3/4 knights tuning.
  > 2026-10-09 (branch wt-8_4h, code written, **emulator pass pending**, so [~] not [x]): the valley guardian and the dragon are party fights (`PartyFightScope` + `PlaceOps::pfnPartyFightOpen/Close/ValleySettle` in `game/placevisit.cpp`, `rt/party_fight.cpp`; `game/roster_scope.hpp` is the one roster guard; the guardian grabs keep their knight: `heldOf`; the dragon's walk keeps the handler's target). Valley: when the whole party LOSES every fallen knight pays `valleyDefeat` (2 lives + a stat point, `partyValleyPenalty`, loss-only since 2026-10-10), a won fight costs a fallen knight only the normal life, win keeps the 8.5 victory, the whole party down = temple screen. Dragon: `fightDragon` with `RosterScope`, per-knight settle, a lost party is robbed knight by knight, progress for the knights still standing. Colours: the second knight's own colour now also in the duel ($0C) and dragon ($14) scenes (KN5 29..31 counts as used in the dragon / balok / troll arenas; a dragon picture with only two free entries, bg2 / bg5a, falls back to the shared colour; the valley picture may too). HUD: the only per-fighter indicator is the low-hp palette pulse; knight 2 pulses its own three entries (`rosterWarnEntries`; the 6 ramp slots are then full, so the duel's opponent warning gives way), knights 3/4 have none (no colour of their own). Black knights: `rosterFoeSlot` spreads N of them (x staggered, depth $4B..$80) through `FightOps::spawnAtDepth`. Host tests: `tests/test_coop_fights.py`. Emulator pass: `tests/boot/play_coop_valley.txt` (`play-cvalley-*`; dump of both knights' lives / hp after the fight), `play_coop_dragon.txt` (`play-cdragon-*`), `play_coop_bk.txt` (placement), `play_coop_fight.txt`; look for the log lines `knight colours: ...`.
  > Emulator pass 2026-10-10 (Debug autoplay): **lair** (`play_coop_fight.txt`, `play_coop_town.txt`): both knights drawn, own colours (`knight colours: knight 2 uses palette entries 31 30 29`), ratmen enter and fight, the lair is cleared, scene 2 loot (`build/shots/play-coop-fight-mash.png`, `play-coop-town-arena.png`). The `wait-timeout var scene=other` of the earlier report was the fixed boot delays (host load) plus, in `play_coop_town`, the old town-menu timing: the run now ends with `AUTOPLAY quit`. **Black knights** (`play_coop_bk.txt`): 2 knights vs the engaged AI knight record + 1 extra, `fight roster on=1 party=2 foes=2`, defeat bits 0x30 (both foes down), the loser keeps the stats and loses a life, the pot's gold 30 lands on the defeated record, meeting screen `play-coop-bk-fight-meet.png` shows `SIR BALAIN` (the original name). **Valley** (`play_coop_valley.txt`, first run): `roster on=1 party=2`, knight 2 (hp 12) fell and the party won: keys 0, moonstone 1 each, knight 2 lost the normal 1 life only (5 -> 4): **the guardian's penalty is now loss-only** (`partyValleyPenalty` returns 0 unless the whole party lost; `tests/test_coop_fights.py` `valley_won_pays_nothing` / `valley_lost_pays`). **Dragon** (`play_coop_dragon.txt`, first run, script fixed: a party's Space shows two status sheets, the second is closed with joy0): both knights vs the dragon (`roster on=1 party=2`); the dragon picture has no free palette entries, so `knight colours: only 0 free entries, the knights share a colour` (both knights blue, expected, MOONSTONE2 6.3); the autopilot party LOST (both down, lives 3, temple screen): the dragon is deadly for mash play, as in the classic `play_dragon` (`play-dragon-lost`). Still open: 3/4 knights tuning, the dragon win path (loot screen with two knights) was not reached by the autopilot.
- [x] **8.4a** Perf probe first: draw/update cost per fighter (beam lines) to size a 4+4 fight on stock and fast-RAM machines (linear estimate ~2040 of 1872 lines on stock) - S
  > Done 2026-10-07 (`docs/PERF.md` "Fight frame split", `tools/perfx.py --model`, `tests/boot/perf_*.txt`): cel draws are 85-90% of a fight frame; per fighter ~460 beam lines (knight) / ~388 (creature) on stock, ~250-275 with fast RAM. Predicted 4 knights + 4 creatures: 4.4 fps stock, 7.0 fps owner; 2 knights + 4 creatures fits better. Levers: halve cel draws + skip unchanged sprites -> 10 fps; job table 10 -> 16, rect cap 45 -> 64. Owner 2026-10-07: 2-knight co-op is an acceptable scope if 4 is too much.
- [x] **8.4b** Fight-engine generalisation: N fighters, per-fighter pad (Knight+11 port pick), creature targeting beyond the first fighter, team-aware contact, more job slots/dirty rects, per-knight script/damage table copies - L
  > Done 2026-10-09 (branch wt-8_4b; 2 knights first, the engine is N-generic up to 4): `include/game/roster.hpp` / `src/game/roster.cpp` (`FightRoster`: party fighters, foe knights, teams, sticky nearest-living targeting, settle bits; pure, host test `tests/test_roster.py`), opened for the length of a fight by `RosterScope` in `src/game/combat.cpp` (`fightCreature` for the lair, `fightMeet` for the black knights; seeded from the co-op party by `src/rt/fight_seed.cpp`). Per-knight pad: every member is port 1 and `rt/input.cpp` picks the pad of the running handler's record (focus TURN), keyboard pads stay held across the fight frame (`opKeyReset` keeps the key-down flags in a party fight). Creature targeting: the ten handler heads call `targetFor` (nearest living knight, sticky, the knight it just hit / that hit it; grabs keep their knight), the `*e.pFirst` reads become `firstOf` / `heldOf`, finishing blows call `knockOut` / `endIfPartyDown`; classic = the original line (`FighterEnv::pRoster` null / inactive). Team-aware contact + movement: `contactScan` skips allies (no friendly fire, daggers carry `Knight::ubTeam`), `blockedMask` lets allies pass. The script ops LAB_0005 / 0006 / 000A / 000D of a party knight's script settle that knight (`fightOpPartyRun`: the creatures are released when the LAST knight has entered, the fight ends when the whole party / the whole foe side is down). Job table and dirty rects as parameters: `game::g_combatLimits` 10 / 45 classic, 16 jobs / 64 rects in a party fight, the arrays switched by `src/rt/combat_pool.cpp` (docs/PERF.md). `poke fightdump 1` logs roster, fighters, jobs, creatures; `poke lair_left N`, `warp_knight 5..8`. The lair creatures were frozen at first (the enter script of EACH knight toggles the creature pause: two knights = paused again) - fixed, see above. Screens: `tests/boot/play_coop_fight.txt` (`build/shots/play-coop-fight-{arena,moved,mash,loot,end}.png`, 2 knights vs ratmen: both drawn, each moved by its own pad, wave total x2 / alive +1, loot screen after the clear).
  > Black knights (slice 3, 2026-10-09): `tests/boot/play_coop_bk.txt` (`play-coop-bk-fight-*.png`): the party token meets the BK group, N knights fight N black knights (AI handler, own target slot, sticky), the fight ends when a side is down, losers lose a life (`rosterSettle`), a lost party is robbed by BK 0 (`settleFight` + the BK record is recalculated: `settleFight` covers records 0..3 only), a won fight ends on the meeting screen; loot per knight is 8.5.
  > Open: the valley guardian and the dragon stay one knight; low-HP warnings only for the first fighter; no HUD per knight; 3/4 knights: placement / pools / scaling (`coopTotalFactor` ...) are N-generic and `tests/boot/play_coop_fight4.txt` runs four (ratmen wave x4: 3.4-4 fps on the debug build, not measured exact), but nothing beyond that is tuned.
  > 2026-10-10 hurt frames (branch kcol_hurt, host-tested, **emulator shot pending**): knight 2 turned into knight 1's colour during the ratmen's struck / cursed reactions (LAB_085E / 085F switch the job to the creature cel list and draw the knight out of RATMEN2) and the Be's grip, KN4 / KN5 and creature jobs holding the knight were not copied either. Every cel set of the knight and creature lists with class-colour pixels is now copied (`celSkinCandidates`, `rosterDrawSkin`, `rt/knight_colours.cpp`; docs/MOONSTONE2.md 4.5), tests `tests/test_skin_cover.py`. Shot: `tests/boot/play_coop_fight.txt` (lair 0 ratmen), look at knight 2 when a ratman casts the curse / strike 8 on it.
  > 2026-10-11 death of one party knight (owner report from a network co-op game: when one knight was killed the other disappeared, the dead one took the other's colour and the fight ended; branch worktree-agent-a50a994d8fb1ffbbb): the finishing sequences of the creatures (the ratmen's squeeze `LAB_0869`, spear `081C`, flyer `0849` / `084E`, Be's carry `0895` / `0896`, snatcher `08A1` / `08A2`, dragon part `0884`) run on the CREATURE's job and end with `LAB_000D` (the current knight is dead) and `LAB_0006` (the fight is over): in a party fight `LAB_000D` marked the LEADER dead whoever was struck and `LAB_0006` ended the fight unconditionally, so one dead knight took the survivor with it and the fight ended with a knight standing. Now: `rosterScriptVictim` / `rosterScriptEndsFight` (game/roster.hpp, used by `fightOpPartyRun`): a creature's `LAB_000D` marks the knight it holds; its `LAB_0006` ends the fight only when every party knight is down (a creature that is itself down, a boss's death script, still ends it); the ratmen's squeeze lets go of the dead knight while another stands (`casterDropVictim`: the knight's own job plays the fall in its own colours). Second bug found by the emulator pass: after its death script (the corpse is drawn into the background, then `done`) the handler stood the fallen knight up again on its pad's input (a ghost beside its corpse, `fighterKnight` / `partyKnightStaysDown`: the job now ends). Co-op death rule (docs/MOONSTONE2.md 2.4 / 6.3): a fallen knight is out of the fight, the others fight on, the fight ends when the creatures are gone or every party knight is down, the fallen knight loses its life at the end as before. Tests: `tests/test_coop_script_ops.py` (mutation-proven), `tests/test_roster.py`; boot `tests/boot/play_coop_death.txt` (`build/shots/play-coop-death-*`, the `fightop` log lines). The colour: in the runs the dead knight's corpse and its own death animation were drawn in its own colours; a held / carried victim is drawn by the CREATURE's job from the creature's copy of the cel set (`rosterDrawSkin`), which is a best-effort chip copy (`knight colours: no N bytes ... keeps the shared colour` in the log): with too little chip memory that figure takes the shared colour; the squeeze now ends with the knight's own job. Not run in the emulator: spear / flyer / snatcher / drake / dragon-part kills with a surviving knight (their victims' jobs are removed: no corpse). Open: the squeeze is broken by fire + down on port 1 (`casterSlam` reads the raw second joystick), i.e. by knight 1's pad whoever is held, not by the held knight's own pad.
  > Owner 2026-10-09: optimise **without removing detail** (no dropped shadow/part cels, no enemy cap): 8.4c-8.4f, plus the
  > extra-half-brite shadows 8.4g. Estimate for 4 + 4 (8.4a model, rough): 8.4c-e give ~6.5 fps stock / ~10 fps owner (8.4f, frame
  > skip, cut by the owner). Each task proves pixel identity (frame hashes on/off over the perf scripts) and reports PERFX before/after.
  > Emulator pass 2026-10-10: see 8.4. fps (docs/PERF.md "Co-op fights", cycle-exact WinUAE): 2 knights vs a ratmen wave (lair 0, 7-9 fighters) 4.6-5.0 fps stock / 8-9 fps owner; 2 knights vs 2 black knights 6.2-7.6 fps stock / 9.6-10 fps owner (classic fights 10.0 fps).
- [ ] **8.4c** Blitter nasty (`BLTPRI` in `DMACON`) during the cel draw pass only, restored after; quick measurement first (perf_duel,
  perf_lair0 on stock and owner configs), keep it only if it gains - S
- [ ] **8.4d** Precomputed cel masks: build each cel's mask once at cel load (chip, ~+1/5 of the cel data) instead of the OR blits per
  draw (`src/engine/blit.cpp:171-181`); chip budget check on stock (docs/MEMORY.md), fall back to per-draw masks when chip is short - M
- [ ] **8.4e** Skip unchanged fighters: no restore and no draw for a job whose cel frame, position and z are the same as on BOTH buffers'
  last draw and whose rectangle overlaps no changed one; same pixels by construction - M
- [x] **8.4f** ~~Frame skip for co-op fights~~ CUT (owner 2026-10-09: "no frame skips, will make game harder"): a big fight plays slower
  rather than dropping frames, as the original did; speed comes from 8.4c-e only
- [ ] **8.4g** Extra-half-brite shadows (owner 2026-10-09: "sounds good"). First confirm which cels are shadows (MOONSTONE2.md 4.6
  only says "body, shadow / weapon parts"; a per-cel draw log of perf_duel settles it). Then: a shadow becomes "set the dark plane under
  the shadow mask" (1 plane, real see-through darkening) instead of a full cookie-cut cel; every other cel and the background restore also
  clear/restore the dark plane, so it is a looks gain, roughly cost-neutral (to be measured). Plain mode: 6 planes with EHB (32 colours +
  their automatic half-bright copies; art unchanged). Enhanced mode already uses the 6th plane for 64 real colours (KILLEHB,
  `src/rt/display_ace.cpp:181`): there a 7th plane with colours 64-127 programmed as darkened copies of 0-63 (palette fades must write
  both banks). Chip: +1 plane per screen buffer (~8 KB each). Shots before/after in `build/shots/` - M
- [~] **8.5** Co-op lairs, loot and towns: lair fights with the party, loot shared/split per 8.1, town screens per knight - M
  > Done 2026-10-09 (the non-fight part, branch wt-8_5; the fights are 8.4/8.4b), for the 2-knight party (owner 2026-10-09: focus on 2 knights; Coop 3/4 stays in the menu, the code is N-generic and the host test also runs N = 3, 4): **loot is copied, not split** (`game/party_loot.hpp`, `rt/party_loot.cpp`): when the screen's knight takes an item, a key / moonstone, the gold pile, an armour or a sword from a lair / the dragon / the black knights' pot, every other living member gets the same take once ("replay on a snapshot of the pile": the original `lootMove` / `exchangeGold` / `exchangeArmour` / `exchangeSword` run on each member and a scratch copy, so caps and quirks apply per knight; gold copies kept, no price scaling; no click record is added, the 100-record pool is untouched). The black knights' pot: the BK records 6.. hand their belongings to record 5 before the meeting screen (`partyBkPotFill`). Exchange screens' "next knight" cycles through the party's members only (`lootNextMember`). **Shop queue**: a town (A / B), a home village (castle blessing + temple sheet) and the wizard run once per living member, leader first then record order, each with its OWN pad / gold / inventory (`rt::ShopVisit` guard gives `LAB_0633` / `ActiveKnights +0` back; `PlaceOps::pfnNextShopper`); the turn is spent once. **Valley**: the victory (+3 progress, keys taken) and the guardian's moonstone go to every living member; **Stonehenge / ending**: the first member (leader first) holding the stone of the moon phase is the visiting knight, its ending plays (owner decision 2026-10-09: the stone owner's ending only; MOONSTONE2 section 7 Q8). **Lives**: a member with no lives is out (no shop turn, no copies, never the leader, no budget); the party is over when all are (`partyAllDown`, N-generic). Tests: `tests/test_coop_loot.py` (N = 2, 3, 4: copies, gold caps, ring/sword, pot, queue order for every leader, dead members, next-member, stone owner, lives); boot `tests/boot/play_coop_town.txt` (`build/shots/play-coop-town-*`: knight 1's town screen, then knight 2's own town screen answering only joystick 2 (`dump` shows `cur=1`), the lair loot screen of knight 2 with both knights' gold +25 in the dump: 50 -> 75 and 120 -> 145). Open: lair / valley / dragon FIGHTS with the party (8.4), the valley defeat applies to the leader only until the fight reports who is down (8.4b), the loot a black-knight win takes from a LOSING party (`settleFight` in a BK fight, 8.4) and a boot script for the valley / Stonehenge party paths (unit-tested only).
  > Emulator pass 2026-10-10: `play_coop_town.txt` runs to quit: the party visits town A (each member's own turn with its own pad: joy1 does nothing on knight 2's screen), the round ends, knight 2 leads the lair fight, the creature loot screen: one click on the gold pile gives BOTH knights the pile (dump: knight 1 50 -> 75, knight 2 120 -> 145), a knight that went down in the fight lost its normal life (5 -> 4). The valley victory / Stonehenge ending were not run by an emulator script.
  > Owner change 2026-10-10 (branch wt-8_5b): the fixed per-knight turn queue is gone.  A town, a home village (blessing + temple sheet) and the wizard run ONCE with the leader active; players switch between the living members with the arrows in the corner box (see 8.5a) and ONE Exit leaves for the whole party (the turn is spent once).  `PlaceOps::pfnNextShopper` became `pfnTurnSwitch(iDir)` (`rt::shopSwitch`), `shopQueueAdvance` became the circular `shopQueueStep(q, +1 / -1)`; the village blesses every living member (`pvCastle` over `rt::shopMembers`), the wizard shows Math once.  The switch sets LAB_0633, ActiveKnights +0 (so the pad choice) and LAB_068B (the sheet's knight); gold / inventory / shops follow.  `play_coop_town.mk` shows it: a click on `>` makes knight 2 active, joy1 does nothing, joy0 moves the cursor, its Exit ends the visit.  The other co-op scripts that walk a village / wizard visit were not re-run.
- [~] **8.5a** Co-op: whose turn it is on the place screens (owner 2026-10-09: the town screen looked the same for every knight) - S
  > Written 2026-10-09 (branch wt-8_5a), host-tested, build-autoplay compiles; the boot run is left to the merged batch.  Three cues inside the shop queue, co-op only (classic byte-identical: every hook is null or inert without a `rt::ShopVisit` of a co-op game): (1) banner `SIR <NAME>'S TURN` (`ms::game::turnBannerText`, `include/game/party_turn.hpp`) in a parchment box at the top of the picture, shown once per member turn after the town fades in (`PlaceOps::pfnTurnMark` MARK_BANNER) and at the first draw of a shop / temple sheet when the town did not show it (village, wizard: `ScreenOps::turnBanner`), until the first pad input (a press still held does not count) or 75 frames, then the picture is restored (`rt_mog_blit_both` / `rtScreenRedraw`); (2) the knight's name (without `SIR `, 8 chars) on the town menu between `Visit` and `Merchant`, drawn into the clean background (MARK_NAME) so every redraw keeps it; (3) pointer colour: sprite colours 128..143 (BPLCON4 sprite bank 0x88, BPLCON3 bank 4) filled with the class colour (`turnPointerRgb`) at the brightness of the picture palette's 16..31; `rt::PointerTint` inside `ShopVisit` gives BPLCON4 back.  Code: `src/rt/party_turn.cpp`.  Tests: `tests/test_party_turn.py`.  Boot: `tests/boot/play_coop_town.txt` shots `play-coop-town-k1-banner`, `-k1`, `-k2-banner`, `-k2` (name + pointer afterwards; the pointer was seen blue for knight 1 and red for knight 2 in a first run; the banner was first printed in the large title font, fixed by selecting the small menu font, NOT yet re-verified).  Open: verify shots, the wizard's Math speech and the dice / healer / mystic screens print no name.
  > Owner changes 2026-10-09, done 2026-10-10: (a) the town menu name is gone; the active knight's TOKEN (its bright map sprite) and NAME (without `SIR `) stand in the UPPER-LEFT corner for the whole turn: town pictures in the BIG title font on a parchment box (colour read from the menu), drawn into the clean background; the black shop / temple / wizard sheets get the same corner in the small white font on a black box after every redraw (`ScreenOps::turnCorner`); (b) the banner `SIR <NAME>'S TURN` is the big font on a parchment box over the picture (small centred text on the sheets); (c) the mouse pointer keeps its normal colours: `rt::PointerTint`, the BPLCON4 / bank-4 registers and `turnPointerRgb` are removed. Bug found by the pass: the name buffers store a space as `_` (`SIR_GODBER`), so the banner printed `SIR SIR GODBER` and the corner name did not drop `SIR`; both now accept `_`. The first (small-font) banner / name versions overflowed their box / overlapped `Visit` / `Merchant`. Verified (`play_coop_town.txt`: `play-coop-town-k1-banner.png`, `-k1.png`, `-k2-banner.png`, `-k2.png`): banner legible, corner fits, no overlap, restored on redraws; the temple sheet shows the small corner (`play-cdragon-end.png`). Not shown: the creature loot screens (the map cel table is not guaranteed there), smith / market / dice / healer sheets were not looked at in a shot.
  > Owner changes 2026-10-10 (branch wt-8_5b): (a) the corner NAME is plain black text in the town menu's own small font (the font glyphs are drawn, then every non-parchment pixel of the text rectangle becomes colour 0; `blackenText`), no knight colour, no big title font; only the TOKEN keeps the knight's colour; the parchment box shrank to fit (126 x 20); on the black sheets the name keeps the sheet's own small white font; (b) the turn banner is the same plain black small text on its parchment box (banner box 20 high); (c) `<` and `>` arrows (5 x 9 px triangles, hit rectangles 12 px wide, full box height) in the corner box switch to the previous / next living member (only drawn with two or more): town screens through synthetic hit records with button ids 6 / 7 (`TB_PREV` / `TB_NEXT`, `rt::turnArrowHit` called first by the click hit test) and the sheets (smith / market / temple / wizard) through `sClick` -> `rt::turnArrowClick` (switch, `rtScreenRedraw`, the new knight's banner).  The arrows are live from the drawing of a corner until `MARK_LEAVE` (the town is left for a shop / Exit) or the end of the visit, so the healer / dice / mystic screens never see them.  Verified: `play_coop_town.txt` (instance 93): `play-coop-town-k1.png`, `-k1-banner.png`, `-k2.png`, `-k2-banner.png`.
- [ ] **8.6** *(backlog 2026-10-09: owner, after the 2-4 knight co-op)* Golden-Axe style lairs: after a wave is killed the knights walk
  right, the arena scrolls to the next section and the next wave enters; ends with the lair's guardian. Needs a scrolling arena (ACE
  scroll buffer) and wave tables - L
  > Owner 2026-10-09: the lair guardian starts as a RECOLOURED, stronger version of the lair's toughest creature: same cel set, its
  > own colour triplet (a boss palette per creature type, as data), more hit points (same damage as the creature: owner, no harder
  > hits), a name for the boss bar (8.11a).
  > Scaled-up cels were rejected (blocky, 4x chip memory and blit time per frame); new boss art (like 9.8d) may follow for a few lairs.
- [ ] **8.6b** Lair guardians in today's (non-scrolling) lairs, before 8.6 (owner 2026-10-09): when a lair's last creature of
  its population falls, its guardian enters as the final enemy - a recoloured version of the lair's creature (same cel set, a boss
  colour triplet per creature type as data), more hit points (default x3, tunable in `creatures.ini`), the same damage (owner: "they
  are already deadly"), a name for the boss bar (8.11a). Works in classic and co-op behind a mod switch (classic parity untouched
  when off); the lair's existing treasure stays the reward. Starts after the co-op fights (8.4b) are merged, since it touches
  the lair fight start/end - M
- [ ] **8.6a** *(backlog 2026-10-09: owner)* Parallax background for the scrolling lairs of 8.6: the far layers (sky, distant trees,
  hills) scroll slower than the arena floor, e.g. AGA dual playfield or copper-split bands per layer; layer art split from the
  arena pictures (or redrawn by the owner); cost measured on the owner's A1200 and stock (docs/PERF.md) - L
- [ ] **8.7** Balance and playtest of the co-op mode on the owner's A1200 (68020 + 8 MB fast) - M
- [x] **8.2b** Explicit title-menu Mode selector, imported from the public checkout: Classic / Co-op 2, left/right or fire;
  the Players shortcut retains Co-op 3/4. Mode and Multiplayer share a compact six-row layout when both are enabled.
- [x] **8.8a** Optional Dexterity stat, imported from the public checkout: 1-5, trainable on the knight sheet, configured by
  `[dexterity]` in `rules.ini`; shortens terminal counted melee recovery holds. Byte +87 stores Dexterity; sticky enemy
  targets use +131 (shared with dagger-only team metadata), retaining the 132-byte record layout.
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
- [ ] **8.11** *(backlog 2026-10-09: owner)* Enemy health bars in fights: a small bar above (or below) each enemy fighter showing its
  hit points against its starting maximum, drawn with the fighter (dirty-rect aware, no extra palette entries beyond one or two free
  ones), hidden when the enemy is down; an option (mod/settings switch) so the classic look stays available - S
- [ ] **8.11a** *(backlog 2026-10-09: owner)* Boss health bar: one wide bar with the boss's name along the top or bottom edge of the
  arena for the big single enemies (the dragon in the Valley of the Gods, the lair guardians of 8.6, other bosses), shown while the
  boss fight runs, sharing the drawing and the on/off switch of 8.11 - S

## M10: Publishable repository (no original data)  `[ ]`

Owner 2026-10-07: the project will be published on GitHub without any original art, data or code; everything original is
extracted from the user's own Amiga disks (the three ADFs) at setup/build time.

- [x] **10.1** Audit: list every tracked file derived from the original (disassembly, reassembled `asm/*.s` + patches, `src/lifted`,
  embedded byte tables in src/tests, shots, fixtures) and every build input that comes from it (`build/reasm`, `build/disks`); doc
  `docs/PUBLISHING.md` - S
- [x] **10.2** Build from the ADFs only: `py tools/setup.py <A.adf> <B.adf> <C.adf>` extracts the disks, produces whatever the build
  needs from the original binaries (hunks, labels by offset from our own facts, not the IRA listing), generates the data, builds and
  hdinstalls; `asm/*.s` + patches and the reference listing become generated from the disks; tests needing originals skip cleanly - L
  > Owner 2026-10-07 (target changed): the GAME loads the original data at start-up instead of compiling it in, so the build and the
  > released executable contain nothing original: 10.2a the data hunks of `program`/`mog` (~61 KB) become empty same-size arrays filled
  > at start from the original executables on the disk/HD/ADF (hunk parse + relocation, checksum of the known version, clear error
  > naming the wrong disk); 10.2b an OFS reader in `src/rt/files.cpp` so `PROGDIR:disks/*.adf` work as well as DF0-3:/HD files;
  > 10.2c the build needs no original file (gen_data emits layouts/sizes only). The "build from the ADFs" pipeline above is CUT
  > (owner 2026-10-07): setup.py is only a developer helper that extracts the ADFs for the tests that compare against the originals.
  > Proof: the start-up-filled hunks equal today's compiled-in data byte for byte; boot regression + play scripts green.
  > Done 2026-10-12 as the one command for a PUBLIC clone: `py tools/setup.py <A.adf B.adf C.adf | folder>` (images found by content, any names
  > and order; wrong/duplicate/missing/bad-dump disks stop with the file, the SHA-1 found and the expected one from `KNOWN_DUMPS` / `tools/facts`;
  > `--allow-unknown-dump`) -> toolchain check that lists each missing piece with how to get it (`--fetch-deps` clones the pinned
  > bartman_gcc_support) -> `build/adf`, `build/disks`, `build/reasm` stand-ins -> configure + build Release (gen_data/gen_moddata run in the
  > build) + `hdinstall` -> `build/hd` + `build/moonstone-ace-hd.uae` and the WinUAE command; `--dry-run` lists the steps and writes nothing.
  > `tests/test_setup.py` (synthetic OFS images). Proof: `export_public.py` into a temp folder (leak scan clean), `setup.py <ADF folder>
  > --fetch-deps` there built the Release exe (859,064 bytes, same size as the checkout's build of the same commit; 29 bytes differ: 27 in
  > HUNK_SYMBOL names, 2 in an 8-byte tail data hunk of linker padding) and staged `build/hd` (152 data files). Open: the ace/ submodule
  > and the cross-toolchain clone of the real public repository; the Amiga compiler itself still comes from the VS Code extension.
- [x] **10.3** Leak scan test: every tracked file is compared against the extracted original files (long byte runs, decoded pictures,
  text strings); runs in `integrate.py` so nothing original slips back in - M
- [~] **10.4** Clean export: `tools/export_public.py` writes `moonstone-ace` (+ `ace` as a submodule) into a fresh repository with
  no history, runs the leak scan and a from-ADF build there; README for players (where to get the disks, setup, WinUAE/real A1200) - M
  > Owner 2026-10-07: the public repo is `D:\moonstone-ace-public` (created, `ace/` a git submodule pinned at 9e6ce06 from
  > AmigaPorts/ACE; layout = moonstone-ace contents at the root + `ace/`). Once it builds from the ADFs only, development MIGRATES
  > there (worktrees, agents, integrate); D:\Amiga stays as the private archive (moonshard, IRA listing, history with originals).
- [~] **10.6** Ready-to-play Amiga releases (owner 2026-10-07: easy play), built by one target, no original data inside (needs 10.2a/b):
  (a) bootable "Moonstone ACE" ADF (exe + startup-sequence) that plays from the original disks: one-drive A1200s get an in-game
  "insert Moonstone disk B" prompt (volume-name lookup, no hidden DOS requesters while the game owns the display), extra drives
  are used without swapping (closes H4 for 2 MB chip); (b) HD archive (.lha) with icon: drop the ADFs or the disk files into its
  `disks/` drawer and double-click; (c) a WinUAE-ready zip (A1200 config + the game; the user adds the ADFs). Checked on the
  owner's A1200 + ACA-1232 and on a stock A1200 config - M
  > Owner 2026-10-07: HD only ("nobody uses floppy"): (b) the HD archive, then (c); (a) the boot floppy is dropped, and so is H4.
  > Done in code 2026-10-12 (open: an owner trial on the A1200 / ACA-1232 and a stock A1200, a Workbench icon, an `.lha`): `py tools/package.py [--amiga]`
  > writes `build/release/moonstone-ace-<ver>-amiga.zip` (a .zip: no lha tool on the build machine): the `Moonstone/` drawer (Release exe, `s/startup-sequence`,
  > `disks/` for the ADFs, `mods/defaults`, `mods_examples`), README.txt, `moonstone-ace-a1200.uae` + `start-winuae.bat` (WinUAE: 68020, 2 MB chip + 8 MB fast,
  > bsdsocket on), docs, SHA256SUMS.txt. Version from `include/engine/version.hpp`; a guard (names, types, hashes, floppy magic, `leakscan`) fails the packaging
  > when anything original is in it; `tests/test_package.py`. No boot .adf (dropped above; H4). BUILD.md "Release packages". Release notes:
  > `docs/RELEASE_NOTES_TEMPLATE.md`, draft `docs/RELEASE_NOTES_0.2.0_DRAFT.md`.
- [ ] **10.7** THEA1200 support (owner 2026-10-10; Retro Games Ltd's full-size A1200, ARM Linux running a modified Amiberry,
  Workbench 3.1, AGA, adjustable CPU speed, USB sticks as drives, USB gamepad, LAN + WiFi on the box): the HD archive of 10.6 installs
  from a USB stick through Workbench, the gamepad works as joystick 1, co-op with USB pads / keyboard sets, and network play
  (M12) when its Amiberry exposes bsdsocket.library [unknown]. **WinUAE only for now (owner: no device yet)**: `thea1200.uae`
  approximates it (68020 fastest, 2 MB chip + 8 MB fast, bsdsocket emulation); its numbers are guesses until the device's A1200
  profile is known - M
- [ ] **10.5** Owner decision before pushing: licence, and the legal question of publishing a translation of the original program - S

## M11: PC version (SDL3)  `[ ]`

Owner 2026-10-09: a PC build of the same game on SDL3 (SDL2 at first; SDL3 3.4.18 since owner 2026-10-10), next to the Amiga build (which stays the primary target and is not
changed in behaviour). Survey 2026-10-09: `src/engine` (6.6k lines) and `src/game` (12k lines) have no ACE, custom-chip or
AmigaOS dependency and already compile with host `clang++` for the tests; the port is the L0 platform layer `src/rt` (59 files
include ACE, 21 write custom-chip registers, ~45 inline-asm sites, 18 of them in `engine_blit.cpp`).
Approach: a **"virtual Amiga" backend** behind the existing rt interfaces (chip RAM as a byte array, planar framebuffer, software
blitter, copper-to-palette, Paula mixer), so the code above L0 runs unchanged and PC frames can be compared with WinUAE frames
from the same autoplay/play scripts. A native renderer (indexed SDL textures, no planar memory) is a later option, not this milestone.
Windows first (`build-sdl/`, `-DMS_PLATFORM=SDL`); Linux/macOS come cheaply afterwards. Data as on the Amiga: read from the user's
ADFs (10.2a/b), nothing original in the PC build either.

- [x] **11.1** Platform split: list every `src/rt` file as platform-neutral / Amiga-only / needs both, and move Amiga-only code
  behind `src/rt/amiga/` + a small platform interface (`include/rt/platform/*.hpp`: display, blit, audio, timer, input, files,
  log); Amiga build unchanged (boot regression + Release size checked) - M
  > Done 2026-10-10: inventory in `docs/PC_PORT.md` (`src/rt`: 14 neutral, 31 neutral behind 68k register-ABI shims, 19 both,
  > 11 Amiga-only, 1 retired); ACE-free interfaces `include/rt/platform/{display,blit,audio,timer,input,files,log,system}.hpp`
  > (54 functions, `tests/test_platform_headers.py`); `crash`, `serlog`, `display_ace` moved to `src/rt/amiga/`; Amiga Debug and
  > Release equal in symbols and sizes. The real blocker is the register-ABI layer (64 asm blocks with 213 entry labels in 47
  > files, 19 `jsr (An)` helpers, 306 `RT_FN`), not the Amiga-only files; true inline asm is only 9 sites.
- [ ] **11.1a** Move the remaining Amiga-only files to `src/rt/amiga/` (`audio`, `irq` Amiga half, `palette_aga`, `prg_copy`, `text`,
  `system`, `display`, `asmcall`, `guards.hpp`, `copper_stub.hpp`, `serlog_logwrite.hpp` with its CMake `-include`) - S
- [ ] **11.1b** Amiga implementations of the 8 platform interfaces as thin wrappers; route callers through them, one interface per
  merge, Release size checked - M
- [ ] **11.1c** Split the 19 "both" files into neutral core + platform seam (`display_ops`, `engine_blit`, `files`, `input`, `irq`,
  `game`, `sprites`, `synth`, `enhanced`, `perf`, `autoplay`, `mog_display`, `arena_bg`, `palette_glue`, `prg_ops`, `modload`,
  `origload`, `knight_colours`, `adfdisks`) - M
- [~] **11.1d** Replace register-ABI shims and `jsr (An)` helpers with typed C call tables; the Amiga shims generated from the same
  table so the Amiga image is unchanged. Largest item, unblocks 11.4 - L
  > Part 1 done 2026-10-10 (`docs/PC_PORT.md` 6.1): shim asm blocks moved unchanged to `src/rt/amiga/<file>_entries.cpp`, C++
  > callers call bodies through `src/rt/prim.hpp` instead of `RT_FN` + `jsr (An)` helpers, trampolines/inline `jsr` became C++.
  > Host-clean rt files 17 -> 38, 11.1d stubs 26 -> 8, `RT_FN` 306 -> 236; Amiga Release `.text` -336 B, Debug -1118 B; unicorn
  > tests prove the typed hosts (`test_engine_scenes`, `test_prog_main_emu`, `test_wipe`, `test_combat_load`). Open (part 2): the
  > mog scene cluster (`arena`, `combat`, `combat_ui`, `creatures`, `fighters`, `screens`, `overworld`, `loot`, `mainloop`,
  > `combat_script`: 8 stubs, 236 `RT_FN`, 8 register-block helpers, handler tables like `LAB_08C8` need typed tables). Its
  > emulator tests (`test_places_emu`, `test_boot_map_emu`, `test_mainloop_emu`, `test_creatures_emu`, `test_fighters_emu`, ...)
  > skip or fail to set up in worktrees: repair them first as the safety net.
- [x] **11.1e** Host `ace/types.h` shim + ratchet test compiling the host-clean rt files with host clang - S
- [x] **11.2** Pointers held as 32-bit 68k addresses (`state_bind.hpp`, `Knight::ulInventory`, `mogHeapTable`, ...): decide
  between a 32-bit PC build (`-m32`, quick) and base-relative offsets/handles (portable, continues 9.3d); first PC builds may use
  `-m32` - S (decision) + M (if offsets)
  > Decided 2026-10-10: **`-m32` first** (i686, clang `--target=i686-pc-windows-msvc`; hello world and SDL2 2.32.10 `lib/x86`
  > link and run here). Survey (`docs/PC_PORT.md`): 702 pointer cells in 93 generated data objects, 374 `const char *` in
  > `owned_data.hpp`, 13 pointer fields in `Knight`, 47 pointer-like `uint32_t` in `include/game`, `state_bind.hpp` 8 cells +
  > `mogHeapTable[25]`, 113 `(uintptr_t)` lines, 98 `jobAddr`/`jobPtr`. Known cost: i686 COFF prefixes C symbols with `_`, so
  > `gen_data.py`'s `.set` aliases need the prefix on that target. Base-relative offsets stay the later 64-bit path (9.3d).
- [x] **11.3** Byte order: owned data images and the slot types (`ByteSlot` "in the HIGH byte", `WordSlot`) assume big-endian;
  big-endian accessors or byte-swapped images from `gen_data.py`/the 10.2a start-up loader; proof: unicorn/oracle tests and the
  pure rule host tests pass on little-endian with the same results - M
  > Done 2026-10-10 (`docs/PC_ENDIAN.md`): `include/engine/endian.hpp` (`be::ld16/ld32/st16/st32`, shared by the 15 files that had
  > local helpers), `include/game/record.hpp` (68k byte offsets into a `Knight` mapped to native fields), slot value views in
  > `world.hpp`; real LE bugs fixed in `game/arena.cpp` (obstacle table) and `engine/bgblit.cpp` (tile script); data-image fill
  > swaps 16/32-bit units on LE (`OrigFillRun::ubUnit`, `tools/data_units.yaml` from `tools/endian_scan.py`; a separate LE fill
  > table, the Amiga table unchanged). Proof: `tests/test_endian.py` (9 mutations caught), `gen_data.endian_fill_check()` against
  > the real executables. Open: `curated:` units for native tables reached only by address (palettes `LAB_08D1..08D5` etc., check
  > at 11.10), `src/rt` wide-pointer casts into chip RAM (~150 in 39 files, with 11.1c/11.6), state-hash byte order (12.x).
- [x] **11.4** CMake SDL target `build-sdl/` (vendored SDL, Windows host toolchain), links engine + game + an empty
  platform; inline asm replaced by the C paths or `#ifdef`ed - S
  > Done 2026-10-10 (with 11.1e): `-DMS_PLATFORM=SDL` (`cmake/sdl.cmake`, `cmake/toolchains/pc-i686-clang.cmake`: clang i686 +
  > MSVC link.exe, SDL2 2.32.10 x86 from git-ignored `tools/sdl2/`) builds `build-sdl/moonstone.exe` (~390 KB + SDL2.dll) from
  > `src/engine`, `src/game`, `gen_data.py --target pc32` (LE fill, COFF `_` aliases), the 17 host-clean rt files
  > (`src/rt/host_clean.txt`, ratchet `tests/test_host_rt.py` with the `include/host/ace/types.h` shim) and `src/rt/sdl/` (guards
  > for SDL init/window/renderer, 50 Hz loop, stderr + `moonstone.log`, `--smoke N`; `tests/test_sdl_build.py`). Everything else
  > is an `MS_STUB` with `TODO(11.x)` in `src/rt/sdl/`: 68 sites (11.1d 26, 11.9 15, 11.6 10, 11.5 7, 11.8 7, 11.7 2, 11.1c 1)
  > plus the 11.1c data objects and the 11.6 chip block. `rtGameRun` is not entered yet. Workarounds: link.exe (lld-link 11 vs
  > the new CRT), `/MANIFEST:NO` (no rc.exe on PATH).
  > 2026-10-10 (owner): SDL2 replaced by **SDL3 3.4.18** (`tools/sdl3`, git-ignored; `MS_SDL3_DIR`, `SDL3.dll`); `src/rt/sdl`
  > ported (guards, `SDL_Gamepad`, per-window relative mouse, `SDL_RenderTexture` nearest, `SDL_EVENT_*`, `bool` returns; dummy
  > drivers `SDL_VIDEO_DRIVER`/`SDL_AUDIO_DRIVER`). Audio uses SDL3's push model (`SDL_OpenAudioDeviceStream` +
  > `SDL_PutAudioStreamData` per vblank from the game thread); the hand-written SPSC ring and callback are gone, prefill and caps
  > kept. `--dump-audio` and `--dump-frame` byte-identical to the SDL2 build; `fitRect` kept over SDL's integer-scale presentation
  > (that would letterbox the 6:5 picture). No SDL2 left in the build.
- [x] **11.5** Timing/IRQ: 50 Hz vblank tick and CIA timers driven by SDL (`irq.cpp` contract from `docs/IRQ.md`), frame pacing - S
  > Done 2026-10-10: `rt/sdl/timer` (single-threaded model documented in `timer.hpp`): the 50 Hz vblank is delivered by
  > `timerPump` at the game's wait points (`timerWaitBeam`, `timerWaitFrame`) on SDL performance counters, resync after >3 late
  > frames; blitter-done raised synchronously at the end of each blit; `MasterIrqOff` holds requests pending (collapsed like
  > INTREQ) until the next pump; beam from time since the boundary, a virtual counter under `--fast`. No CIA timers (ptplayer is
  > 11.8). Proof: `timerSelfTest` under `--smoke`. Open: `irqLevel3` needs its VERTB/BLIT work split on PC; game quit polling.
- [ ] **11.6** Display: planar chip-RAM framebuffer -> RGB each frame, AGA palette, copper lists interpreted as per-line palette and
  bitplane-pointer changes, hardware sprites, wipes; window scaling (integer + aspect) - L (split when started: 11.6a framebuffer +
  palette, 11.6b copper, 11.6c sprites, 11.6d wipes/enhanced 64-colour mode)
  - [x] **11.6a** framebuffer + palette. Done 2026-10-10: `engine/planar` (1..8 planes, modulos, any width, bounds-checked;
    OCS 12-bit, AGA hi/lo, EHB; `fitRect` integer scale with 6:5 aspect); `rt/sdl/chip` (2 MB chip array + allocator, softblit
    bound to it); `rt/sdl/display` converts the shown screen per frame into a streaming texture; `--fast`, `--dump-frame`.
    `tests/test_planar.py`: independent model, 26 mutations, pixel-exact smoke frame. Open: `rt_screen_work` still outside the
    chip array (`stubs.cpp` alias), `displayWaitFrames` stub
  - [x] **11.6b** copper: per-line palette and bitplane-pointer changes (`displaySetWindow`). Done 2026-10-10: `engine/copper`
    (MOVE/WAIT/SKIP over big-endian chip RAM on a PAL beam clock, surveyed register subset, the rest refused with a status) and
    `engine/dispframe` (per-line frame: copper, sprite DMA, `planar::rowIndices`, DIW window, modulos, EHB, AGA banks);
    `rt/sdl/display` renders through it. The game itself changes nothing mid-frame (survey: `docs/PC_PORT.md` "Copper and
    sprite usage"). `tests/test_copper.py`: independent models, 67 mutations, `--smoke-copper` frame pixel-exact. Open
    (11.10): the 4-clock instruction model and effective-line rule are assumptions, compare with WinUAE
  - [x] **11.6c** hardware sprites (`displaySetSprite`, `displaySpritesEnable`). Done 2026-10-10: `engine/sprites` (sprite
    DMA per line, 16/32/64 wide, attached pairs, BPLCON4 banks, BPLCON2 priority). The game uses only sprites 0/1 (cursor, 16
    wide, BPLCON2 $24). Open (11.10): with BPLCON4 = 0 the cursor takes palette 1..3 (bank 0); verify the HSTART origin and
    attached-pair bank against a WinUAE cursor shot
  - [ ] **11.6d** wipes, enhanced 64-colour mode (`rtEnhPlanesStub`)
- [x] **11.7** Software blitter for the minterm/shift/mask/fill cases the game uses (`engine_blit.cpp`, IMAGEXCEL, text), with host
  tests comparing against recorded blits from the Amiga build - M
  > Done 2026-10-10: `engine/softblit` (`ms::softblit::run`), word-exact software blitter over big-endian chip bytes for the
  > surveyed set (`docs/PC_PORT.md` "Blitter usage": A/B/C/D, all minterms, A/B shift 0..15 ascending, first/last A masks, modulos,
  > descending without shift, BLTSIZE 0 = max, pointer run-on); line/fill/descending-shift refused loudly. `rt/sdl/blit.cpp`
  > runs on it (synchronous). `tests/test_softblit.py`: independent Python model, 1,320 random ops, 140 planner-derived blits,
  > real `.cel` pixel compare, 25 mutations caught. Open: 11.6 binds `chipView()` to the chip RAM array; the 11.1c/11.9 direct
  > blitter writers (`displayBlitScreen`, `prgCopyScreen`, arena `hwBlit`) must build `BlitOp`s and go through `blitRun`.
- [x] **11.8** Audio: 4-channel Paula mixer (period, volume, loops, the synth's DMA tricks) into an SDL audio callback; A/B listening
  against WinUAE (as 2.6/4.6) - M
  > Done 2026-10-10 (code; A/B listening open): `engine/paula` (pure: DMA states, LC/LEN latched at reload, start and reload
  > raise the channel interrupt, hold on DMA off, 0,3 left / 1,2 right + separation, exact box-filter resampling, optional
  > low-pass) and `engine/modplay` (ProTracker replayer replacing ptplayer on PC; excluded from the Amiga image). `rt/sdl/audio`:
  > the model runs on the game thread in virtual time (960 frames per vblank, AUDx handler at chip time), finished samples
  > reach the SDL callback through a lock-free SPSC ring; `--dump-audio` writes the deterministic WAV. Usage survey in
  > `docs/PC_PORT.md` "Paula usage". `tests/test_paula.py`: independent Python models, real synth's 167 sequences, exe WAV
  > sample-exact, 38 mutations. Open: owner A/B listening vs WinUAE (music, mog banks, filter) once 11.1d wires the callers.
- [x] **11.9** Input (SDL keyboard + joystick/gamepad mapped to the Amiga keys/ports), files (stdio + the 10.2b ADF reader),
  crash/serlog to stderr and a log file - S
  > Done 2026-10-10: `engine/pcinput` (pure: SDL scancode -> Amiga raw key, no repeat as on the Amiga, focus loss releases
  > keys, keyboard joystick, `joyEncode`, pad sticks, JOY0DAT mouse counters scaled to 320x200) and `rt/sdl/{input,files,
  > gamedata,crash}`. Port 1 = cursor keys + RCtrl/Space or pad 1; pad 2 replaces the mouse in port 0. `PROGDIR:` reads the user
  > dir (SDL pref path) then the exe dir, writes only the user dir; game data searched as on the Amiga (`data/`, exe dir, disks
  > drawer, then ADFs through `engine/ofs`). Unhandled-exception filter writes `crash.log` + message box; clean exit on Ctrl+C.
  > Proof: `tests/test_pc_input.py` (26 mutations), `tests/test_pc_files.py` (22 mutations, real ADFs vs `adfx`). Open: the 4
  > input stubs in `stubs.cpp` go with the `rt/input.cpp` split (11.1c/d); owner trial of pads and mouse feel.
- [x] **11.10** First light: boots to the title screen in an SDL window; `build/shots/` PC shot next to the WinUAE shot - S
  > Done 2026-10-11: `build-sdl/moonstone.exe` boots the real game from the user's ADFs (`disks/` next to the exe or `--disks DIR`;
  > the owner's copies in git-ignored `moonstone-ace/disks/`) to the title menu (program -> mog -> Title); `--skip-intro`,
  > `--pattern`, clean exit on window close. Title dump equals the WinUAE shot `play-options-title.png` (0.000 % of the pixels at
  > even columns/rows; WinUAE's scaler blends the others); `tests/test_pc_firstlight.py`. Chip RAM is a COFF section group
  > (generated chip objects, pool, work block). 15 more rt files host-clean (display_ops, sprites, mog_display, prg_ops, prg_copy,
  > engine_blit, input, introskip, soundbank, synth, enhanced, origload, mainloop, arena, combat_ui) with Amiga platform halves
  > in `src/rt/amiga/`; PC twins `sdl/{game,rt_files,irq,music,modload,reg_entries}` (TODO 11.1c). Stubs 71 -> 20 (+13 unwired
  > register entries of the mog cluster). Amiga Release `.text` +394 B. Open: the game beyond the title needs the rest of the mog
  > cluster (11.1d part 2: overworld, combat, screens, loot, creatures, fighters, combat_script, arena_bg); 11.6c cursor bank;
  > audio A/B listening; the disk scan does not find the ADFs in a folder with many other files (e.g. `moonshard/`).
- [ ] **11.11** Parity: `integrate.py --boot` and the play scripts run headless on the PC build (autoplay, no window); frame hashes
  or shots compared with the WinUAE run at checkpoints - M
- [~] **11.12** Release: Windows zip (exe + SDL3.dll, user adds the ADFs to `disks/`), README section; fits M10's no-originals
  rule and the leak scan - S
  > Done 2026-10-12 as a **preview** (`py tools/package.py --pc`): `moonstone-ace-<ver>-windows-preview.zip` = moonstone.exe, SDL3.dll + licence, README, `disks/`,
  > mods_examples, SHA256SUMS.txt. The PC build only reaches the title menu (11.1d), so the file name and README say "preview"; flip `PC_PREVIEW` in
  > `tools/package.py` (or `--pc-final`) when it plays a whole game. Needs the 32-bit VC++ runtime on the player's PC (stated in the README). Open: the final zip.
- [ ] **11.13** Modern C++ PC layer (owner 2026-10-11): rewrite `src/rt/sdl/` as C++23 with the standard library per
  `docs/PC_PORT.md` "Modern C++ in PC-only files" (`std::unique_ptr` deleters for SDL handles, rule of zero/five, `std::span`,
  `std::filesystem`, no raw new/delete), `-std=c++23` for PC-only files only; toolchain cleanup now that clang is 23.1.3 (drop
  the forced MSVC link.exe and `/MANIFEST:NO` if lld-link works). Behaviour unchanged: dumps byte-identical, all PC tests pass - M

## M12: Network play (bsdsocket.library)  `[ ]`

Owner 2026-10-10: multiplayer over TCP/IP through `bsdsocket.library`, in normal (classic: hot-seat knights, human duels,
Practice) and co-op mode (M8). Design note `docs/NETPLAY.md`. Model: **deterministic lockstep on the input**: every machine
runs the whole game, only input frames travel (a few bytes per machine per poll); the tick is the game's input poll; star
topology over TCP (`TCP_NODELAY`), the host relays. Classic single-machine play stays bit-identical (every hook inert without
a session). Main risks: the game runs with the OS off (ACE `systemUnuse`), and the game is not yet deterministic across
machines (beam-position seed, VBL tick counters, interrupt-time input).
Owner decisions 2026-10-10 (NETPLAY.md section 6): LAN **and** internet; 2 machines first, code N-generic up to 4; local and
remote players mixed; drops as recommended (classic knight -> AI knight, co-op member -> host's controller); cross-play with the
PC build (M11). The owner has no network card (players asked for the feature): target the generic `bsdsocket.library` API (Roadshow,
AmiTCP, Genesis, Miami), develop on WinUAE's bsdsocket emulation, real hardware from players.
Release plan (owner 2026-10-11): **v0.2.0** = network play as built (lobby, direct + relay, co-op and classic, drops, rejoin
12.15a, the ending) plus UDP input with history (NETPLAY.md 2.2), after a 4-player co-op test in WinUAE. **v0.3.0** = the PC
build as a network node (12.14), the LAN game browser (12.16) and late join of a new player (12.15); backlog until v0.2.0 is out.
When v0.2.0 is done: branch `release/0.2` from `moonstone-ace` (v0.2 fixes go there and are merged back; v0.3.0 continues on
`moonstone-ace`), and v0.2.0 goes to the published repository github.com/Jarskih/moonstone-ace-public through `tools/export_public.py`
(10.4: no original data) with the tag `v0.2.0` and the GitHub release there (owner 2026-10-11).
**v0.4.0** (owner 2026-10-12: "Moonstone 2 gameplay + easy to play everywhere"): lair guardians 8.6b with the boss health bar
8.11a and enemy health bars 8.11; weapons as data 8.8 and bows 8.9; tables to data 9.5 with the monster editor 9.8; ready-to-play
releases 10.6 incl. the PC release zip 11.12; THEA1200 10.7 / 12.20 (WinUAE); scrolling Golden-Axe lairs 8.6 with parallax 8.6a
(owner 2026-10-12: moved into v0.4.0). Extras if they fit: playable monsters 8.10,
extra-half-brite shadows 8.4g, the 64-colour art option 4.8a, the 4-player adapter 8.2a, fight speed-ups 8.4c-8.4e.
Later (v0.5+): the Beholder 9.8d.

- [x] **12.1** Design note `docs/NETPLAY.md`: lockstep model, determinism sources with file:line, transport and the OS
  problem, who controls what per screen in both modes, session/protocol, code layout, owner questions (section 6) - S
- [x] **12.2** Spike: open `bsdsocket.library` from the game and echo TCP frames with a host peer, (A) inside a `systemUse`
  window per tick vs (B) OS alive for the session; measure the window cost per tick on stock and owner configs (WinUAE
  bsdsocket emulation; no owner hardware, so the real-card cost comes later from 12.13); decide A or B - M
  > Spike 2026-10-10 (branch wt-12_2, not merged): Roadshow SDK LVOs as inline-asm stubs (`src/rt/bsdsocket_calls.hpp`), `rt/net`
  > guards (`SocketLib`, `Socket`, `NetWindow`), `ReadArgs` in `main()` (default port 6502), `tools/netpeer.py`, `tools/netspike.py` (the spike scripts were removed 2026-10-11, replaced by `tools/netpair.py`).
  > Host and join both work against a PC peer under `bsdsocket_emu=true` (also on `thea1200.uae`; without it the game says "no
  > TCP/IP stack running" and plays on). **Option A costs ~2 frames (40 ms) per window on every config**: ACE's `systemUse` and
  > `systemOsDisable` each wait for a VBL. **Option B** (OS kept alive) boots to the map with identical shots, no per-tick cost.
  > Menus busy-poll (~14 polls per frame), so at most one window per frame (12.4's `netTick`). Next: a lean window without
  > ACE's VBL waits (A2) and B in a fight, then decide.
  > **Decided 2026-10-10 (merged, wt-12_2): A2 is the default.** `LeanNetWindow` (no `systemUse`): swaps the OS's level 1-6
  > vectors (read at session start) in over ACE's, enables SOFTINT / PORTS / EXTER, restores vectors and INTENA exactly on exit.
  > Cost 3-8 beam lines per window (ACE window: ~66 lines and a VBL wait); fights stay at 10.0 fps (ACE window 8.2-8.4 fps).
  > Levels 1, 2, 3, 6 only hung in 3 of ~7 runs (cause unknown), all six levels never did. B (OS held for the session) also keeps
  > 10.0 fps and is the fallback if a real card loses interrupts between windows (12.13: untested, WinUAE only). `netFrame()`
  > sits at the end of `displayWaitBeamRaw` for now; 12.8b moves it onto the `netTick` frame hook.
- [x] **12.3** Determinism audit: every non-deterministic input to game state (RNG seed from `VHPOSR`, `cbBeam`, the VBL
  tick counters `LAB_0379` / `LAB_0B9D`, VBL hooks, keyboard ISR, mouse counters, wall-time waits) and every input read site
  (stick, keys, last-key cell, mouse), each with file:line, added to NETPLAY.md 1.1 - S
  > Done 2026-10-10 (NETPLAY.md 1.1a-c, 1.2; code reading only): logic advances per loop iteration everywhere EXCEPT the
  > joystick-cursor screens (loot, shops, temple, stat sheets, donation, wizard): `cursorTick` moves the cursor in the VBL
  > interrupt (`src/rt/combat_ui.cpp:1029-1065`). Beam reads into state: the RNG seed and the flyer's lane wobble
  > (`src/game/fighters.cpp:495-496`). "Tick = poll" does not hold (0..n polls per frame, key loops without polls): the tick
  > becomes a frame boundary (`netTick()` in the frame wait). The only seeding is at `mog` entry, so a session needs its own
  > SEED at start. Rule variants (gore, mods, enhanced fallback) go into the handshake. Open: `$88 00` scan of knight / effect /
  > map scripts, key loss during `systemUse` windows.
  > 2026-10-11 (owner): the title menu has no Gore option any more, gore is always on (`LAB_06DA` = 0 = the cell's start value, `MENU_GORE_ON`). `NET_RULE_GORE` (HELLO rules bit 1) stays and is
  > now always set (it used to be set when the cell was non-zero, i.e. gore OFF, which was never the default); the HELLO format is unchanged.
- [x] **12.4** Deterministic mode (runtime flag, off = bit-identical): **the host sends the random seed to the clients**
  (only the host's `rtMogRngSeed` reads `VHPOSR`; a client's blocks at that tick for the host's `SEED` message and writes it
  into `mogRandomSeed`, at session start and at every seeding event, NETPLAY.md 4.1), `netTick()` at the frame boundary latching
  all input (polls, keys, cursor) for the tick, the cursor hook run per tick from the main thread (owner: cursor speed), the
  flyer wobble (`cbBeam`) from the tick, the logical tick counter advanced at the boundary, a `$88 00` scan test of every
  script set - M
  > Written 2026-10-10 (branch wt-12_4, merged; host-tested, Debug links; emulator proof is part of the batch run): NETPLAY.md 1.3.
  > `engine/nettick` (key queue, local frame, latch, `cursorStep`, `tickIdle`), `rt/net_tick` (flag, `netTick`, frame hook seam
  > `netSetFrameHook`, seed API `netSeedProvide` / `netSetSeedHook` / `netSeedAtTick`, pacing counter). `netTick` runs in
  > `displayWaitBeam` (1 VBL) and at the end of `rtMogFrameWait` (the loop's remaining logical budget); spin loops call
  > `ms::tickIdle()`. In the mode the VBL no longer touches `mogFrame`; the cursor steps 2 px x VBLs per tick (`kCursorPixelsPerVbl`);
  > `cbBeam` uses the logical clock. Only the mog overlay (not intro / ending). `fightscript.py loops`: no `loop random` anywhere.
  > Autoplay: `poke netdet 1`, `play_net_lair`, `play_net_coop`. Open for the batch run: flag OFF boot regression; play_lair x3
  > vs play_net_lair x3 (the agent saw a hung post-victory fight and stray key events on its instance, cause unknown).
- [x] **12.5** State hash (`engine/statehash`, addresses normalised) logged every 50 polls under `MS_AUTOPLAY`. **Gate**: the
  same play scripts (classic + co-op) on cycle-exact stock A1200 and on the owner config give identical hash logs - M
- [x] **12.6** Lockstep core `engine/netplay` (pure): input frame, tick ring, input delay, the merge (game state -> owner of
  port 1 / port 0 / keyboard / mouse; co-op member -> `(node, pad)`); host tests incl. simulated 2- and 4-node runs with
  random latency - M
- [x] **12.7** Protocol `engine/netproto` (pure, versioned, host-tested) + transport `rt/net` (RAII guards `SocketLib`,
  `Socket`, `NetWindow`; non-blocking TCP behind `netOpen/Send/Recv/Close`) + `tools/netpeer.py` (a Python node) - M
  > 12.6 done and the pure half of 12.7, 2026-10-10 (branch wt-12_6): `include/engine/netplay.hpp` / `src/engine/netplay.cpp`
  > (11-byte input frame, 16-tick ring per node, delay 0..8, drops, the merge of NETPLAY.md 3.1 / 3.2), `include/engine/netproto.hpp` /
  > `src/engine/netproto.cpp` (game + lobby messages incl. the new DROP, stream framer), `include/engine/statehash.hpp` (CRC-32);
  > `tests/test_netplay.py` (every merge row, 120 simulated 2-4 node star runs with random latency and a drop), `tests/test_netproto.py`
  > (round trips, prefixes, corruption, fuzz, chunked framer, checked against a Python encoder); three mutations caught. Message
  > table in NETPLAY.md 4. Open in 12.7: the transport `rt/net` (12.2 spike) and `tools/netpeer.py`.
- [x] **12.8** Session (the handshake also compares rule options: gore, mods, enhanced art in use, 12.3): one Amiga hosts, the other joins with the host's IP address (player request 2026-10-10). CLI through
  `ReadArgs()` in `main()` before `rt::systemCreate()` (template `HOST/S,JOIN/K,PORT/K/N,NAME/K,DELAY/K/N`:
  `moonstone HOST`, `moonstone JOIN 192.168.1.20`), the same keywords as icon tool types for Workbench starts, `PROGDIR:net.ini`
  for defaults; handshake (protocol version, exe build hash, data checksum, the host's first seed, mode, knight-to-node table),
  "waiting for players" / "connecting" screens - M
- [x] **12.8a** Multiplayer screen (owner 2026-10-10: a new menu screen with Join and Host buttons): a "Multiplayer" item on the
  title menu opens it; buttons **Host game** (listen, then a lobby list of the joined players' names; the host's fire starts the
  session), **Join game** (address editor: digits and `.`, the port on its own line, since bold.f has no `:` glyph; Connect),
  Name, Back; later **Find game** (12.16 / 12.18). Joystick moves and fires, the keyboard types. The typed address and name are
  saved to `net.ini`. After the start every node returns to the title menu in lockstep (the host's menu, NETPLAY.md 3.1).
  Pure state machine (host-tested) + drawing in the game's font; behind a seam until the transport (12.2 / 12.7) is merged - M
  > Written 2026-10-10 (branch wt-12_8a, host-tested, Debug + Release link; the end-of-batch emulator run is pending): `game/net_menu`
  > (pure state machine: pages main / join / lobby, field filters, net.ini load/save in place), `rt/net_stub` (the `NetSeam`
  > the transport replaces: host / join / poll / start / close; autoplay pokes `netfake` `netpeers` `netjoin` `netstarted`), the
  > title list drawn from a RAM copy with Multiplayer as the 5th, last item (Practice / Select Knight 14 lines higher; the
  > original data untouched); `tests/test_net_menu.py` (170 checks), `tests/boot/play_netmenu.txt`. Font limits: no '/' (status
  > "NO TCP IP STACK") and no '-' (host names with a hyphen cannot be typed). At the batch run: re-promote `reg-menu`,
  > `reg-menu-knight`, `reg-menu-practice`; check the typing fix (the last run lost keys) and two "pulse MISSED" notes.
  > Lobby = knight screen (owner 2026-10-10, NETPLAY.md 4.2b, merged fa8d64f): HOST GAME goes straight into the lobby; PLAYERS n OF 4,
  > MODE line (host only, CO-OP default), one coloured selector per seat, each player picks a portrait, names it and (co-op) a
  > controller; START (host, greyed until everyone has picked) goes to the map on every machine at the same tick, joined players
  > see WAITING FOR HOST. Protocol 4: LOBBY_PICK (12), LOBBY_STATE (13), one portrait per node in CONFIG; the lobby server was
  > redeployed with protocol 4 (selftest PASS). `engine/netroom`, `game/scene_lobby`, `game/net_knights`, `rt/lobby_draw`.
  > Open: the net_pair and play_netmenu lobby shots blessed on the batch run; SceneKnights' network branches are dead now.
  > 2026-10-11 (owner): the Multiplayer screen is no longer an item of the title menu but the MULTIPLAYER row of the new main menu (LOCAL / MULTIPLAYER / SETTINGS, first screen after the intro and
  > after a game over); leaving the screen returns to the main menu. `runNetScreen` is unchanged. `poke menuact 1` still opens it (docs/AUTOPLAY.md). A running network game shows the title menu alone.
- [x] **12.8b** First light (owner 2026-10-10: two players on this PC): two WinUAE windows (bsdsocket emulation) play one game,
  `moonstone HOST` / `moonstone JOIN 127.0.0.1`; co-op with 2 knights (one per window) through the title menu, the map and a lair
  fight with matching state hashes; needs 12.4, the 12.2 window decision and the rt wiring (`netTick` -> transport -> `netplay`
  merge, the host's SEED / CONFIG); shots of both windows in `build/shots/` - M
  > Done 2026-10-11: `py tools/netpair.py --lobby 199.247.20.74` NETPAIR PASS: lobby (HOST -> knight screen -> START), co-op map,
  > leader rotation, lair fight, loot, 68 of 68 state hashes equal, the drop check (WAITING FOR GUEST, CONTINUE WITHOUT) and the
  > dashboard listing Co-op -> Playing. Shots `build/shots/netpair-h-*`, `netpair-j-*`.
- [x] **12.9** Normal mode over the network: each node picks its own knights on the knight screen; map turns, towns and loot
  steered by the current knight's node while the others watch; human duel and Practice across two nodes (port 1 / port 0
  words from the two nodes); a "waiting for <name>" cue - M
  > Accepted 2026-10-13: classic two-window NETPAIR PASS through tick 3950, 79 common hashes, both quit, 21 matching tick-pokes,
  > UDP active without fallback. Own knight selection, host/guest map turns, next day, guest-only lair fight/loot, guest-owned Town A
  > entry and exit, next day and return to the map exercised. Screenshots: `netpair-cj-map.png`, `netpair-cj-fight.png`,
  > `netpair-cj-loot.png`, `netpair-cj-town.png`, `netpair-cj-town-nextday.png`, `netpair-cj-town-map.png`.
  > `test_netplay` verifies the human-duel and Practice port 1 / port 0 merge from distinct nodes (`tests/netplay_driver.cpp`);
  > town loop tick starvation is fixed by `PlaceOps::pfnIdle`, with `test_places_emu` and `test_overworld` passing. Earlier
  > tick-2045 / lobby stalls were not reproduced in two full-duration post-fix town runs; retain monitoring in release matrix.
- [x] **12.10** Co-op over the network: `PartyConfig` binding `(node, pad)` (`aubNode[]` next to `aubPad[]`), remote members
  on the knight screen, per-fighter pads from their nodes in fights, mixed local + remote players, the shop queue - M
- [x] **12.11** Drops and desync: stall cue, timeout, drop applied on every node at one tick (classic: the knight becomes an AI
  knight, co-op: its member goes to the host's controller, owner 2026-10-10), hash mismatch stops with a state dump on each node - M
- [x] **12.12** Test harness: two WinUAE instances with bsdsocket emulation on localhost, one autoplay script per node, an
  `integrate.py --net` leg; `netpeer.py` replaying a recorded input log against one instance - M
- [ ] **12.13** *(owner 2026-10-10: WinUAE only for now, real Amigas later)* Real hardware and internet: players' A1200s (PCMCIA Ethernet / WiFi, Plipbox or serial PPP; the owner has no card)
  against WinUAE, then two Amigas; input delay adapting to the measured round trip; LAN and internet sessions (host's router
  forwards one TCP port); fps in fights; UDP with redundant frames only if TCP stalls - M
- [x] **12.13a** Relay for internet play without port forwarding: a small relay (`tools/netserver/`, with the 12.17 lobby; later
  the PC build) that every node connects out to; the host role stays with an Amiga or PC node, the relay only forwards - S
  > Game side written 2026-10-11 (branch worktree-agent-aa0f1b8dd731e9100, host-tested, Debug + Release + PC builds; the two-window result is below):
  > `engine/netrelay` (the host's idle relay connection: RELAY_HELLO, PING, and the hand-over of a paired client with its first bytes to a free game
  > link, so the node cannot tell the pipe from a direct TCP link; up to three clients, any mix of direct and relayed), `rt/net_relay`, `NetTransport::
  > relayHandOver` + byte pushback, `netSessionJoinRelay` (connect to the lobby, RELAY_HELLO(client) first, then HELLO), a lobby keepalive PING every 30 s
  > (the server drops a quiet pipe after 120 s). No wire change, protocol stays 4, the deployed server needs no redeploy (new `--no-connect-back` flag is
  > for tests only). `tests/test_netrelay.py`: fake connections + mutations, and end to end against the real server (host + two clients through the
  > relay, 60 lockstep ticks with equal frames, direct listing keeps the relay out, re-listing moves the idle connection, ten quiet minutes).
  > `py tools/netpair.py --relay` (local lobby server without connect-back, guest uses FIND GAME) runs `tests/boot/net_relay_{host,join}.txt`. NETPLAY.md 4.4.
  > Two-window runs 2026-10-11 (`py tools/netpair.py --relay`, shared machine at 1000-1800% CPU load): run 1 was killed by another session's `uaeshot.ps1` (instance 0
  > kills every WinUAE); run 2: host listed via relay (`lobby status 3`, `relay state 4`, local dashboard "Relay"), the guest's FIND GAME listed the game
  > (`find status 2 games 1`), `join via relay`, the lobby opened on both, both picked, START, 33 of 33 state hashes equal (tick 1600) through the relay, then a
  > desync at tick 1650: the logical clock differed by 4 (the two nodes' `netTick` ring shows different wait sites from tick 1637 on, a timing-dependent wait in
  > the fight loop, NETPLAY.md 1.1a: the same class as the earlier loaded-machine desyncs of net_pair, not a transport difference - the bytes are identical);
  > run 3: the emulated clock ran so slow that the host's REFRESH (every 30 emulated seconds) missed the server's 75 real seconds, the listing was re-registered
  > with a new id and the guest's JOIN_REQ for the old id answered GAME NOT FOUND (shown correctly on screen; `netpair --relay` now starts its local server with
  > `--refresh-timeout 600`). **No clean NETPAIR PASS for --relay yet and the direct run was not made** (run budget used, machine saturated): re-run both on a
  > quiet machine: `py tools/netpair.py --relay --build build-autoplay` and `py tools/netpair.py --build build-autoplay --lobby 199.247.20.74 --timeout 1500`.
  > Shots `build/shots/netpair-j-find.png` (the browser), `netpair-h-listed.png`.
- [ ] **12.14** *(after M11; owner 2026-10-10: wanted)* PC build (M11) as a node (player, host or relay): BSD sockets /
  Winsock behind the same transport; needs logic parity on little-endian (11.3) and the 12.5 hash gate across Amiga and PC - M
- [ ] **12.16** LAN game browser: hosts announce by UDP broadcast (name, mode, players, build hash, port), a "Find game" title
  entry lists them and joins with fire; no server needed (NETPLAY.md 5a) - M
- [x] **12.17** Lobby server for public games (`tools/netserver/`, Python; one process with the 12.13a relay): register /
  refresh / list / unlist; the lobby takes the host's public IP + port from its connection and hands it to a client that joins;
  same build hash only, connect-back test for open ports, abuse limits; host tests with fake nodes - M
  > Done 2026-10-10 (branch wt-12_17) with the server half of 12.13a and the files of 12.19: `tools/netserver/` (asyncio, standard
  > library only, port 6510; `server.py`, Python codec `netproto.py` mirroring the C++ check for check, systemd unit, Dockerfile,
  > README with the owner's deployment steps), `tests/test_netserver.py` (34 tests, fake nodes and clock). Protocol v2: a readable
  > game version (`include/engine/version.hpp`, u16 major.minor, 1.0 for now) in HELLO, BYE, the listing, JOIN_REQ / JOIN_REPLY;
  > new REGISTER_REPLY (41) and RELAY_HELLO (40). Open: the game side of the relay (12.13a), the deployment and host name (12.19).
- [x] **12.18** Internet game browser: "Public: yes / no" for hosts, the "Find game" screen's internet list (name, mode, players,
  ping), join direct (the lobby passes the host's IP to the joiner only) or through the relay; lobby address built in,
  overridable in `net.ini` - M
  > Host half done 2026-10-10 (above). Client half written 2026-10-11: a FIND GAME row on the Multiplayer screen opens the list (`game/net_find`,
  > `engine/netbrowse`, `rt/net_find`): LIST_REQ to the lobby of net.ini / the built-in server, one row per open game (name, COOP / CLASSIC, "n OF 4",
  > OPEN / FULL, another build or version dimmed), fire joins (JOIN_REQ; JOIN_REPLY direct = the typed-address path, relay = `netSessionJoinRelay`),
  > REFRESH / BACK, status line (NO TCP IP STACK, SERVER NOT FOUND, SERVER NOT ANSWERING, NO OPEN GAMES FOUND, GAME FULL, GAME NOT FOUND, OTHER VERSION
  > with "HOST RUNS 1.2 YOU HAVE 1.3", NO ANSWER ...). The server lists open games only, so there is no PLAYING row (a ping column is not built: no
  > wire field for it). `tests/test_net_find.py` (322 checks + mutations), `tests/test_netrelay.py`. NETPLAY.md 4.4. Open: the dimmed-row colour
  > (palette entry 3 of the title picture) and the layout are unchecked on screen beyond `netpair-j-find.png`; `play_netmenu.txt` was re-timed for the
  > extra row (its shots need re-blessing, `reg-` references for the Multiplayer main page changed).
- [x] **12.19** Server operation: `tools/netserver/` (lobby 12.17 + relay 12.13a in one Python process, standard library only,
  one TCP port) packaged as a service (systemd unit / container), counts-only logging, a host name built into the game,
  bandwidth measured per relayed game; **the owner runs it on their own server** (2026-10-10; the host name to build in is
  still open, NETPLAY.md Q8). Direct internet play by IP needs no server; the server is for the public browser and for players
  who cannot forward a port - S
- [ ] **12.20** THEA1200 as a network node (10.7): a session between two `thea1200.uae` WinUAE instances and against the
  owner config, through WinUAE's bsdsocket emulation; on the device itself once one is available - S
- [ ] **12.15** *(later)* Resync and late join via a full-state snapshot (a serializer of the RAM state; shares work with a
  save game) - L
- [~] **12.15a** Rejoin (owner 2026-10-11; the part of 12.15 that is needed first: a dropped player comes back, no general late join). A dropped
  player reconnects at any time - also after the host chose CONTINUE WITHOUT and plays on alone - and ENTERS at the next map turn, never mid-fight: the host
  freezes the lockstep at the turn, sends the world (`game/net_world`: the `net_hash` state plus an audit of the cross-turn state it does not cover - market,
  gift cycles, dragon, terrain, map modes, spawn counters, black-knight pool, party map), chunked with a CRC, both sides compare a hash of the loaded world,
  and the knight goes back to its player with the host's hp / gold / items / position (the rejoiner's local game is discarded); a mismatch aborts cleanly
  and the host continues alone.  Every node gets a random token in CONFIG; a reconnect with that token reclaims exactly that seat; a wrong token or no
  dropped seat is refused with a reason the Multiplayer screen shows (new REJOIN GAME row).  Protocol 5 (`NET_PROTO_VERSION` 5, relay range 1..20, server
  `_join` answers a Playing game for the rejoin flag): **the public server must be redeployed**.  Classic and co-op, 2..4 nodes, several rejoins per game,
  a second drop of the same player; a host that drops itself still ends the clients' session as before (no rejoin to a client).  `docs/NETPLAY.md` 4.5.
   > Code and host tests 2026-10-11 (branch worktree-agent-ad2aeacc797b2e6b5): `engine/netrejoin` + `engine/netsnap` + `game/net_world` + `game/net_rejoin`
   > (pure), `rt/net_rejoin` + `rt/net_world` (sequence and cells), `tests/test_netsession.py` (real nodes over byte pipes, ~20 new mutations),
   > `test_networld`, `test_netproto`, `test_netserver`, `test_net_menu`; Debug and Release builds link. Emulator runs pass: `build/batch6_direct.log`
   > passes direct rejoin, `build/batch8_relay.log` passes relay rejoin, and `build/batch6_quad.log` passes three different guests rejoining in a 4-node game.
    > `batch6_direct` also drops that guest a second time after its return. Host-self-drop and a second return of the same seat remain to verify.
    > The live service's Playing/rejoin selftest passes (2026-10-13).
- [x] **12.21** UDP input with history (owner 2026-10-10 "udp sending history sounds good", 2026-10-11 "do it now"; `docs/NETPLAY.md` 2.2 / 2.2a): the per-tick traffic (the client's
  INPUT, the host's merged TICK) travels as datagrams that repeat every tick the peer has not acknowledged (up to 8), so a lost datagram costs nothing instead of a TCP
  retransmit hitch; TCP stays for setup and the rare messages and takes over silently: no two-way probe handshake in 3 s -> TCP, acks stalled 250 ms -> the unacked ticks also go
  over TCP once, 5 s -> the link reverts to TCP for the session; a rejoiner is TCP.  Same port numbers as TCP (game port, relay 6510); the relay server forwards the datagrams by
  relay session + token.  Protocol 6 (`NET_PROTO_VERSION` 6, INPUT_UDP 21 / TICK_UDP 22): **the public server must be redeployed and UDP 6510 opened**.  `TCP` on the command line /
  `net.ini` `udp = no` force TCP.  Log lines `net: udp: probe sent / ok / fallback to tcp (reason)`, the session counters and a `PERFU` line.
   > Code and host tests 2026-10-11 (branch worktree-agent-a4e1e3e031736ce77): `engine/netudp` (pure) + `engine/netnode` + `engine/netproto`, `rt/net` (UDP socket) + `rt/net_session`
   > (glue, logs), `tools/netserver` (UDP relay, selftest leg, README), `tests/test_netudp.py` (planner, handshake, lossy channel, 24 mutations), `tests/test_netsession.py` section
   > `nudp` (real nodes over a lossy fake datagram network: every 2nd datagram lost, bursts, 30% loss + duplicates + reordering, outages, black holes, one direction dead, strays,
   > conflicts, drop and rejoin scenarios under UDP; 14 mutations), `test_netproto`, `test_netserver`; Debug, Release and autoplay builds link. Emulator runs pass:
    > `build/batch6_direct.log` (71 hashes, UDP + rejoin), `build/batch6_tcp.log` (69, forced TCP A/B), `build/batch8_relay.log` (67, UDP relay + rejoin),
    > and `build/batch6_quad.log` (38 equal hashes across four nodes). The live endpoint's `selftest.py --server moonstone.shatteredallegiance.com`
    > passes direct, relay, UDP relay, Playing/rejoin, and other-build rejection. Lean windows cost roughly 3-8 beam lines in WinUAE (12.2 / NETPLAY 2.1);
    > real hardware remains unmeasured under 12.13.

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
