# Memory budget (ROADMAP 2.11)

Numbers of the Release game (`build/`, LTO, no asm since 7.1r; HEAD db7dd9f plus the 7.2/7.3 changes) measured in WinUAE
(Kickstart 3.1, A1200, 68020, 2 MB chip). The game logs them itself: `PROGDIR:boot.log` on an HD install (rewritten at every
start; not written on a floppy boot) and the serial log of `MS_AUTOPLAY` builds, one `mem ...` line per checkpoint
(`rt::bootLogMemory`: `memGetFreeChipSize`, `AvailMem(MEMF_FAST)` and the largest free blocks).

## What the game allocates

| item | bytes | where | note |
|---|---:|---|---|
| exe CODE hunk | 249,340 | fast if present, else chip | |
| exe RODATA hunk | 24,448 | same | |
| exe DATA hunk (+20 B `text`) | 58,936 | same | game DATA images (`ms::owned::*`, tables) |
| exe BSS hunk | 112,540 | same | includes the 32 KB big stack (`s_pBigStack`, `src/main.cpp`) |
| exe CHIP data hunk | 2,728 | chip | |
| exe CHIP BSS hunk | 144,468 | chip | `rt_screen_work` 82,438 (docs/DISPLAY.md 2.4d), copper list, sprite data, ... |
| **exe total** | **592,460** | chip 147,196 + any 445,264 | file size 475,536 (BSS is not in the file) |
| chip arena (`BUMP_CHIP + SCRATCH_CHIP`) | 442,136 | chip | 376,600 carve + 65,536 scratch (`src/rt/game.cpp`) |
| fast arena (`BUMP_FAST + SCRATCH_FAST`) | 524,566 | fast, else chip | 360,726 carve + 163,840 scratch |
| DATA snapshots (re-entry restore, `imageEnter`) | 61,592 | fast, else chip | program 15,488, mog 46,104 |
| ACE view / splash screens, copper lists, OS | see below | chip | made before `rtGameRun` |

Arena sizes are the original's carve plus a scratch tail (loads and decoders write above the bump). Measured high-water over the
scenes played so far (intro, menu, knight select, map, practice, a creature fight): chip arena 376,556 (the tail is unused),
fast arena 388,848 (28,122 of the 163,840 tail used). The tails could shrink by about 145 KB, but only a full playthrough (7.4,
dragon, every creature and loader) can prove it, so they stay as they are.

## Stock A1200 (68020, 2 MB chip, no fast RAM): fits

Everything is chip then: exe 592,460 + arenas 966,702 + snapshots 61,592 = 1,620,754 bytes, plus the splash/ACE display and the OS.
Measured (WinUAE, KS 3.1, boot straight into `s/startup-sequence` = `moonstone`, dir-mounted HD):

| checkpoint | free chip | largest block |
|---|---:|---:|
| `rtGameRun` start (OS + exe + splash display in place) | 1,293,024 | 1,293,024 |
| after the arenas | 326,320 | 326,320 |
| entering program (display handover + DATA snapshot) | 310,688 | 310,688 |
| entering mog (+ its DATA snapshot): the low point | **264,584** | 264,584 |

So a stock machine needs about 1.03 MB of free chip after the exe is loaded and the splash is up, and has 258 KB to spare in that
setup. The game was played on that configuration (menu, map, practice fight, a creature fight; frame rates in ROADMAP 7.2). With
Workbench booted (screen, drawers, handlers) the OS takes a few hundred KB more, which can eat the margin: start from the
startup-sequence, close Workbench windows/screens, or add fast RAM (then the 445 KB `any` hunks, the fast arena and the
snapshots all move out of chip: free chip at `rtGameRun` start is 1,870,944, after the arenas 1,428,808).

A floppy boot (docs/INSTALL.md) leaves 53 KB less (four drives' DOS buffers): 208,840 at the low point.

Owner machine (68020 + 8 MB fast, 2 MB chip): chip 1,870,944 free at start, 1,428,664 after everything; fast 7,810,512 at
start, 7,224,168 at the low point (586 KB used). Memory is not a constraint there.

## Failing loudly

`rtGameRun` no longer just logs a failed allocation: `rt::fatal` (`src/rt/system.cpp`) writes the reason to the log,
prints it to the shell/boot console (`Output()`), and, when there is no console (icon start), shows a recoverable Intuition
alert. Checked with 1 MB chip: `moonstone-ace: not enough chip memory for the game arena (needs about 442 KB; docs/MEMORY.md)`,
the program returns to the OS. The same path covers the fast arena and the DATA snapshots.

## S_4/S_5 loader hunks and `--gc-sections`

The dead loader hunks left with the asm (7.1r): there is no asm in the link any more (Release exe 732,764 -> 475,536 bytes).
What `--gc-sections` could still drop is small: with `-Wl,--gc-sections` instead of `--no-gc-sections` in `CMakeLists.txt` the file
is 462,592 bytes (-12,944: .text -8.2 KB, .rodata -0.6 KB, .data -0.1 KB), BSS unchanged. Not enabled: the link map and the
undefined-symbol check are the reason for `--no-gc-sections`, and the gc'd exe has not been booted. If memory gets tight on a
stock machine this is a 2.7 % saving to try first, after the arena tails above (about 145 KB).
