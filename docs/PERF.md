# Performance (ROADMAP 7.2)

## How it is measured

`MS_AUTOPLAY` builds (docs/AUTOPLAY.md) carry `src/rt/perf.{hpp,cpp}`; use a **Release** autoplay build (`-DCMAKE_BUILD_TYPE=Release
-DMS_AUTOPLAY=ON`, own build dir), Debug numbers mean nothing. Every 250 VBLs (5 s of game time; the counter does not advance while
the OS owns the machine, so file loads are excluded) the serial log gets one line:

```
PERF vbls=250 frames=50 fps100=1000 budget=6 work_avg=790 work_max=1096 late=0 beam=199 slip=51 maxdelta=3
```

- `frames`: passes of the map or fight loop (`rtMogFrameStart` / `rtMogFrameWait`, mog `LAB_031D` / `LAB_031F`); `fps100` = fps x 100.
- `budget`: `LAB_05BA`, the loop's tick budget (map 2 = 25 fps cap, fight 6 = 8.3 fps cap).
- `work_*`: stamp to the start of the wait in beam lines (312 per VBL, 64 us each); `late` = frames whose work exceeded the budget.
- `beam/slip/maxdelta`: `rt::displayWaitBeam` calls and VBL gaps between consecutive calls, for the beam-synchronous loops (menus, fades,
  scenes); inside the map/fight loops it counts the loop's own wait and means nothing. `beam=0` windows are idle screens (the menu
  polls from the VBL interrupt).

Scenes are driven by `autoplay.txt` scripts with `log scene_x` markers (the frame numbers are in the log); see `tests/boot/*.txt`.
Windows are 250 VBLs, so a scene change inside a window mixes two scenes.

WinUAE 6.0.3, A1200 chipset, KS 3.1, 68020 at 14.19 MHz (`cpu_speed=real`), **cycle-exact**: `cpu_compatible=true`,
`cpu_cycle_exact=true`, `cpu_memory_cycle_exact=true`, `blitter_cycle_exact=true` (the log says `CPU=68020 ... ~cycle-exact`), 2 MB chip;
"owner" adds 8 MB 32-bit fast RAM (`z3mem_size=8`, `cpu_24bit_addressing=false`), "stock" has none. "approx" is the same machine with
the regression config's non-cycle-exact CPU (`cpu_compatible=false`), much faster than a real one. The owner's accelerator clock is not
known here: 14.19 MHz is the conservative case.

## Results (Release build, serial `PERF` lines)

| scene | owner exact | stock exact | owner approx | note |
|---|---|---|---|---|
| menu (title, Practice/Select Knight) | idle, no loop | idle, no loop | same | pointer and cursor run from the VBL interrupt |
| map, idle | 25.0 fps (work 623 / 624 lines) | 25.0 fps (621) | 33.4 fps (404) | budget 2 |
| map, token moving | 24.0 fps (late 2 of 120) | 23.2-23.4 fps (late 0-2) | 31.6 / 23.4 | |
| practice fight | 10.0 fps (work 695, max 698 of 1872) | 10.0 fps (790, max 1096 of 1872) | 10.0 fps (374, max 377) | budget 6 |
| creature fight (map encounter) | not hit in that run | 10.0 fps (work 774-1206, max 1547 of 1872, late 0) | not run | |
| town / shop screen | not reached | not reached | not reached | event driven; needs a map walk script |
| floppy boot (ADF + original disks in DF1:-DF3:, stock exact, turbo floppy) | - | fight 10.0 fps (784, max 859) | - | |

Nothing is late in a fight on either machine; the busiest creature-fight window used 83 % of the budget on the stock machine. The map
loop sits at its 25 fps budget (work ends at the budget boundary), moving the token costs it up to 7 % (23.2 fps, a few late frames).
Fast RAM saves only 12 % of the fight loop's `work` (695 vs 790 lines) - but `work` contains the idle display flip; the split below (ROADMAP 8.4a) shows 20-45 % of the real pre-flip work and that the loops are blit bound.

## Pacing quantisation (not a regression, worth knowing)

The loops wait `budget - elapsed_ticks` beam passes (`LAB_0D74`, line $F5) where `elapsed_ticks` is an integer VBL difference, so
the frame period is not `max(budget, work)` but depends on where the work ends:
a fight with 1.1 ticks of work runs 6 VBLs per frame (8.3 fps, the budget), with 2.5 ticks of work 5 VBLs (10 fps), and the non cycle-exact
model's 1.2-tick map frames run at 1.5 VBLs. So the game's speed depends on the CPU: on this port fights run 8.3-10 fps and the map 25-33 fps.
The A500 original did the same arithmetic with more work per frame (likely 3+ ticks for a fight, i.e. the full 6-tick period): expect
fights up to 20 % and the map up to 33 % faster than on an A500, never slower. An exact budget would make `rtMogFrameWait` wait until
`tick >= start + budget` (a behaviour change, not done here). The original's own rate could not be measured with the tools at hand
(no probe in the original binary; its frame loop is the same asm as the port's `LAB_031D/031F`).

## Found while measuring

The first Release fight crashed (Line-F exception in DATA at the first mirrored cel). Cause: `-fipa-icf` folded
`rtCelMirror_mog` into `rtCelMirror_prg` as a 6-byte `bra.l` whose PC32 field `elf2hunk` relocated like an absolute address.
Fixed in `CMakeLists.txt` with `-fno-ipa-icf` (compile and link); the Debug and map-only Release runs never hit it.

## Fight frame split (ROADMAP 8.4a)

Why: sizing a 4-knights-plus-enemies fight (docs/MOONSTONE2.md 4.6). `PERF` only knows the whole frame; the split below says where the beam lines go and how they scale with the
number of fighters. Release + `MS_AUTOPLAY` build, WinUAE cycle-exact as above ("stock" = 2 MB chip, no fast; "owner" = + 8 MB 32-bit fast; `cpu_speed=real`, `cpu_compatible=true`,
`cpu_cycle_exact=true`, `cpu_memory_cycle_exact=true`, `blitter_cycle_exact=true`, `chipset=aga`).

### How it is measured

* `rt/perf` has a **colour-clock timer** (`vposr:vhposr` read as one long, 227 colour clocks per line, 312 lines, the 312-line wrap counted by whoever stamps; `perfPoll` runs from the VBL
  interrupt, the main-context stamp masks interrupts for ~20 cycles). The old tick-based `work` could only resolve a beam line (64 us) and a cel draw is 1-3 lines.
* The fight frame is cut into **sections** by `perfEnter(section)` / `perfLeave(previous)` hooks (inline no-ops without `MS_AUTOPLAY`): `src/rt/combat.cpp` (`opJobPass`, `opTick`,
  `opFlip`, `opContactPass`, `opDrawPass`), `src/rt/creatures.cpp` (`cbHandler`: one handler = one fighter's update) and `src/rt/combat_script.cpp` (the four cel callbacks: prepare, target,
  wait-blitter, draw). A frame is `OTHER` (frame start) -> `JOBS` (the dispatch loop without the handlers) -> `HANDLER`s -> `SCRIPT` (the combat tick: z-sort, scripts, hit lists, dirty
  rects, without the draws) -> `DRAW` (cel blits) -> **`FLIP`** (`displayWaitBeam`: it waits for beam line `$F5`) -> `POST` (low-HP warnings, key handling) -> `CONTACT` -> `RESTORE`
  (background restore of the dirty rectangles) -> the frame wait, which is not counted. Handler and draw calls, the live fighters (job slots of every type but the effect `$28`, the dragon
  part `$2C` and the dagger `$34`), the used job slots and the dirty rectangles (`mogRectCount`, counted past the cap of 45) are read once per frame between the tick and the flip.
* Per **bin** (fighters f, job slots j) and heartbeat window the serial log gets three lines; the console copy cuts a line at ~120 characters, hence three short ones:

```
PERFX f=5 j=6 n=105 work=2049.0 flip=164.2 post=10.1 oth=4.3
PERFY f=5 j=6 hand=15.1 jobs=7.6 scr=130.0 draw=1415.2 con=28.0 res=262.0
PERFZ f=5 j=6 hc=27 dr=157 rc=150 rmax=21 rov=0 ty=608
```

  all in beam lines per frame (`hc`, `dr`, `rc` are x 10: handler calls, cel draws, dirty rectangles per frame; `rmax` = the most rectangles in one frame, `rov` frames over 45,
  `ty` = the job type bits seen, bit = type / 4). The frame that was in progress when the heartbeat printed is dropped (the serial output stalls the interrupt), as is a frame over 10 ticks.
  `PERFC frames=N clk_bad=M` counts the frames where this timer and the old tick-based one disagree by more than 4 lines: ~1-4 of 50 in a fight (the tick-based one can be a VBL off), but
  **all of them in the map loop**: in the map the new timer reads ~305 where `PERF work=` says ~624 (one whole VBL less). The fight numbers agree with `PERF work=` to 1-2 %
  (e.g. duel 1082 vs `work_avg` 1147-1206 of the same windows, which include the frame the heartbeat print stalls); the map frame is not used here, so the discrepancy is left open (cause not investigated; check it before trusting either map number for anything finer than "25 fps").
* `py tools/perfx.py build/perf-*.log` prints the bins as a table, `py tools/perfx.py --model` fits and predicts (see 4.6 of MOONSTONE2.md for the numbers).
* The scenarios are `tests/boot/perf_duel.txt` (warp onto a black knight), `perf_lair{0,1,7,11,15,16,19}.txt` (one lair per creature arena of the lair table: `LAB_07BD` offsets $24, $18,
  $30, $1C, $40, $04, $20; lairs 18, 21, 23 have handler $00 and are not fights), `perf_dragon.txt` (the dragon: `poke day 2`, status screen to re-enter the map, other knights taken out
  of the draw, `wait file Dragon1.cel`). A strong knight (strength 9, 310 HP, hp re-poked every 200 frames) plays 2000 frames with `mash joy1 10`; no shots (a shot freezes the guest).
  `poke day N` is new in `src/rt/autoplay_game.cpp`. Run: `powershell -File uaeshot.ps1 -Config build/perf-stock.uae -Instance 71 -Autoplay tests/boot/perf_lair0.txt`
  with the perf configs (cycle-exact lines above, own `hd-71` / `hd-72` copies of `build-autoplay-rel/hd`), then `py tools/perfx.py ... build/uaeshot-71.log`.

### Findings

* `work` (above) includes the idle **flip**: after the draws the loop waits for beam line `$F5` (`rt::displaySwap`), 140-200 lines on average whatever the load. Real work is `work - flip`;
  the old "fast RAM saves 12 % (695 vs 790)" compared the two `work`s; the real saving is **20-45 % of the pre-flip work and 50-60 % of the restore** (MOONSTONE2.md 4.6).
* The frame is `W = 312 * ceil(B1 / 312) + B2` with `B1` = job pass + handlers + script + draws before the flip and `B2` = contact scan + restore + warnings after it.
* **Pacing, derived and checked**: the loop starts just after line `$F5`, so the frame wait is `budget - floor((246 + W) / 312)` further `$F5` crossings (zero when that is <= 0).
  For `W < 1626` the period is 5 VBLs (10 fps), except for `W` in the 66 lines after a multiple of 312, which gives 6 VBLs (8.3 fps); for `W >= 1626` there is no wait and the period is `W`
  itself, so **fps = 15600 / W** (6.5 fps at W = 2400, measured lair 0 on stock 6.4-6.6). The budget of 6 ticks is therefore not a cap on how slow a fight can get.
* Cel draws are **85-90 % of the pre-flip work**; handlers are 0.3-2 % of a frame; one knight or creature is ~390-460 beam lines on stock, ~250-275 with fast RAM.
* Dirty rectangles ~= cel draws ~= 3.5-4 per fighter; the busiest bin (knight + 5 ratmen) used 10 of 10 job slots and 25 of 45 rectangles; no frame ever listed more than 45.

### Results (stock exact / owner exact, beam lines per frame, bins of >= 8 frames; the largest two bins of each scenario)

The full table is the output of `py tools/perfx.py --model` (raw logs of this run: `build/perf-logs/`). Summary rows (more in MOONSTONE2.md 4.6):

| scenario | cfg | f / j | frames | W | B1 | B2 | handler | jobs | script | draw | contact | restore | flip (idle) | draws | rects (max) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| duel | stock | 2 / 2 | 402 | 1082 | 717 | 202 | 5 | 4 | 63 | 642 | 23 | 166 | 163 | 7.6 | 7.0 (11) |
| duel | owner | 2 / 2 | 196 | 693 | 411 | 83 | 3 | 2 | 53 | 348 | 13 | 66 | 199 | 7.3 | 6.7 (11) |
| lair 7 (drake) | stock | 2 / 2 | 466 | 1224 | 809 | 240 | 3 | 3 | 71 | 728 | 21 | 208 | 175 | 8.7 | 8.4 (12) |
| lair 7 | owner | 2 / 2 | 449 | 750 | 485 | 104 | 1 | 2 | 45 | 426 | 12 | 87 | 160 | 8.4 | 8.4 (11) |
| lair 1 (3 brawlers) | stock | 4 / 4 | 393 | 1328 | 963 | 199 | 24 | 7 | 103 | 820 | 27 | 160 | 166 | 13.2 | 12.7 (17) |
| lair 1 | owner | 4 / 4 | 453 | 1067 | 784 | 113 | 10 | 6 | 99 | 663 | 18 | 87 | 170 | 13.5 | 12.7 (19) |
| lair 11 (3 brawlers) | stock | 4 / 4 | 324 | 1755 | 1279 | 310 | 29 | 8 | 105 | 1131 | 27 | 270 | 166 | 12.8 | 12.3 (18) |
| lair 19 | stock | 4 / 4 | 436 | 1395 | 1015 | 215 | 27 | 7 | 114 | 862 | 24 | 178 | 165 | 15.8 | 15.1 (20) |
| lair 0 (4 ratmen) | stock | 5 / 6 | 105 | 2049 | 1584 | 301 | 15 | 8 | 130 | 1415 | 28 | 262 | 164 | 15.7 | 15.0 (21) |
| lair 0 | owner | 5 / 6 | 125 | 1331 | 969 | 180 | 6 | 11 | 95 | 856 | 17 | 158 | 182 | 15.7 | 15.1 (20) |
| lair 0 (5 ratmen) | stock | 6 / 10 | 11 | 2542 | 2041 | 374 | 28 | 11 | 187 | 1808 | 41 | 323 | 127 | 23.1 | 18.9 (23) |
| lair 0 (5 ratmen) | owner | 6 / 10 | 18 | 1738 | 1326 | 218 | 14 | 37 | 171 | 1101 | 40 | 173 | 194 | 22.8 | 19.1 (22) |
| dragon + parts, 1 knight | stock | 1 / 4 | 528 | 1372 | 1123 | 93 | 14 | 7 | 84 | 997 | 20 | 63 | 157 | 9.2 | 9.2 (14) |
| dragon + parts, 1 knight | owner | 1 / 5 | 273 | 1155 | 826 | 137 | 9 | 29 | 62 | 722 | 14 | 27 | 192 | 10.4 | 10.4 (13) |

(owner "jobs" column: 29-45 lines in some bins is the dispatch loop spawning scripts, not a steady cost. The dragon job has type `$28`, so the "fighters" column counts its knight only.
The owner dragon rows have a larger `post` (90-125) than stock: the repeated hp poke triggers the low-HP warning ramps.)

### Co-op fights (ROADMAP 8.4b): measured 2026-10-10 (branch wt-coop_pass)

Release `MS_AUTOPLAY` build (`build-autoplay-rel`), WinUAE 6 cycle-exact A1200 as in "How it is measured": **stock** = 2 MB chip, no fast RAM; **owner** = + 8 MB 32-bit fast RAM (`z3mem_size=8`, `cpu_24bit_addressing=false`; 68020 at 14.19 MHz, the real accelerator clock is not known).
Scripts `tests/boot/perf_coop_lair.txt` (two knights, `mash joy1` + `mash joy0`, lair 0, wave doubled) and `perf_coop_bk.txt` (the party warps onto black knight record 2; arrival menu key 1); both logs checked: `waves: scaling=0 coop=1 total=36 alive=5` (lair) and `f=4` in `PERFX` (2 + 2 fighters, BK fight) - the boot part ran into the co-op game, not the practice duel.  Classic row = `perf_lair0.txt` (one knight vs the same lair) on the same build.  fps = the `PERF fps100` of the 250-VBL windows with at least 15 frames in a fight (budget 6); the PERFX bins give where the beam lines go.

| fight | fighters f / jobs j | stock exact | owner exact | busiest bins W = work in beam lines (stock / owner) |
|---|---|---|---|---|
| classic practice duel (earlier table) | 2 / 2 | 10.0 fps | 10.0 fps | 1082 / 693 |
| classic 1 knight vs lair 0 (ratmen), same build | 5-6 / 6-8 | 6.5 fps mean (6.0-8.4) | 9.4 fps mean (5.4-10.0) | 2229-2523 / 1060-1335 |
| **co-op 2 knights vs lair 0 (wave doubled, +1 alive)** | 7-9 / 8-10 | **4.9 fps** mean (4.6-6.2; the windows at W 2900-3300) | **8.2 fps** mean (6.0-9.0) | 2204-2894 / 1762-2083 |
| **co-op 2 knights vs 2 black knights** | 4 / 4-8 | **6.5 fps** mean (6.2-7.6, one 3.8 window at the arena load) | **9.4 fps** mean (7.6-10.0) | 1760-2639 / 1229-1586 |

Reading: a fight frame is cel-draw bound (draw 1067-1674 of 1760-2639 lines for the 2 + 2 fight on stock); the frame is `W` lines when `W >= 1626` (fps = 15600 / W), so the co-op lair on stock sits at 4.6-5.4 fps while its owner-machine twin keeps 8+ fps.  The 2 + 2 black-knight fight is as fast as a classic ratmen lair on stock and at 10 fps on the owner config; the doubled ratmen wave of 5-6 creatures is the expensive one (7-9 live fighters).  Job slots used 8-10 of 16, dirty rects at most 34 of 64, no frame over the cap (`>45` column 0).

Limits and chip memory as written in the preparation notes below; the colour copy of the second knight costs ~87 KB of chip while a creature fight lasts.

#### What was prepared before the run (kept for reference)

* **Measured 2026-10-10 (table above); the preparation notes were:**  The scripts are in place: `tests/boot/perf_coop_lair.txt` (two knights, `mash joy1` + `mash joy0`, against lair 0 with the wave doubled, +1 alive) and
  `tests/boot/perf_coop_bk.txt` (two knights against the two black knights); run them on the Release `MS_AUTOPLAY` build with the cycle-exact configs of "How it is measured" (stock = no fast RAM, owner = `z3mem_size=8`) and read `PERFX` / `PERF` lines
  where `frames > 0`.  The boot part of both scripts uses long fixed delays: under the cycle-exact CPU the menus come up later than in the debug config, an early press is `MISSED` and the game then starts the two-player practice duel instead of the co-op
  game (`f=2 j=2`, `ty=8`, no creatures): check that `waves: ... coop=1` / `AUTOPLAY fight roster on=1` are in the log before trusting a window.
* **The PERF counters work in party fights** (`frames`, `fps100` > 0 in every fight window of `build/shots/play_coop_fight-run1.log`, debug config 3.4-7 fps).  Windows with `frames=0` are the arena set-up: the file loads and, until 2026-10-09, the colour copy of the
  second knight (a per-pixel loop, 16 s on the debug build; now byte-wise bit logic, `engine/celremap.cpp`).
* **Limits in a party fight**: 16 job slots and 64 dirty rects (`game::g_combatLimits`, `rt/combat_pool.cpp`); the arrays are static (16 x 50 + 16 x 36 + 2 x 16 x 80 bytes and two 528-byte rect lists, chip or fast as the exe's BSS).  Two knights and a wave of five used 8-9 of the 10 classic slots and left no room for
  daggers; the classic fight is untouched (10 / 45).
* Chip memory: the second knight's colour copy takes ~87 KB while a creature fight lasts (stock A1200 spare: ~258 KB).
