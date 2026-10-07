# Installing moonstone-ace (ROADMAP 7.3)

The repository contains no original data. You need your own copy of the three Moonstone (Mindscape, 1991) floppies
as `.adf` images; the build extracts the game files from them. Nothing extracted or staged is ever committed (`build/` is git-ignored).

Target: Amiga 1200 (68020, AGA, Kickstart 3.x, 2 MB chip RAM). Fast RAM is optional but recommended (see below).

## 1. Build the install tree (on the PC)

```sh
# toolchain PATH of AGENTS.md / BUILD.md, then from moonstone-ace/
py tools/adfx.py build/disks "path/to/Moonstone (Mindscape) A.adf" "path/to/Moonstone (Mindscape) B.adf" "path/to/Moonstone (Mindscape) C.adf"
cmake -S . -B build -G Ninja <toolchain flags of BUILD.md> -DCMAKE_BUILD_TYPE=Release
cmake --build build --target hdinstall
```

`build/hd/` is the complete install, about 2.7 MB (exe 475,536 bytes, 150 data files from the three disks merged by
`tools/hdstage.py`; the one name clash, `kn1.ob`, resolves to disk B's real file):

```
hd/moonstone              the game (an ordinary AmigaOS hunk executable)
hd/data/                  the game files from disks A, B and C
hd/s/startup-sequence     one line, `moonstone` (LF line ends), only used when the folder is booted as a volume
```

## 2. Hard disk install on a real A1200 (the supported way)

1. Copy the `hd` folder to the Amiga (CF card in a PC card reader, Gotek/USB, serial or network) and rename it, for example
   `DH0:Moonstone`. Keep `moonstone` and `data/` together in the same drawer; the game finds its files through `PROGDIR:`
   (the directory the executable was started from), so the drawer can be anywhere and can be started from any current directory.
   `s/startup-sequence` is not needed on a normal install (it exists so the same tree can be mounted as a bootable volume in WinUAE).
2. Start it from a Shell: `DH0:Moonstone/moonstone` (or `cd DH0:Moonstone` then `moonstone`), or give it a Workbench icon:
   copy any Project/Tool icon to `Moonstone/moonstone.info` (default tool, no tool types needed), then double-click it.
   No `stack` setting is needed: the game runs on its own 32 KB stack.
3. There is no exit from the game yet (ROADMAP 2.1): leave with a reset (Ctrl-Amiga-Amiga).
4. Fast RAM: with fast RAM (an accelerator's 8 MB, say) the exe's code and data hunks, the second arena and the snapshots go there and
   the game keeps about 0.6 MB of chip RAM (chip hunks 147 KB + chip arena 442 KB). Without fast RAM everything goes to chip and the
   machine needs about 1.03 MB of free chip RAM once the exe is loaded (docs/MEMORY.md: 264,584 bytes were left on a bare
   Kickstart 3.1 boot of a 2 MB machine). On a stock 2 MB machine start the game from the startup-sequence (or a Shell with no
   Workbench screens open) rather than from a crowded Workbench. If memory is short the game stops with an explanatory message on
   the console (or an Intuition alert when started from an icon) instead of crashing; `PROGDIR:boot.log` records the free chip/fast
   memory at each stage.
5. Optional output: the game writes `files.log` and `boot.log` into its drawer on an HD install (a few KB; only when a `data/`
   drawer exists next to the executable) and `crash.log` if the CPU takes an exception.

Controls: a joystick (menus and the map: the joystick port, JOY1; the fight reads the knights' own ports, see docs/AUTOPLAY.md
"Which port the game reads"). Space, Return or a fire button skips the intro.

## 3. Floppy install (limited)

`cmake --build build --target adf` writes `build/moonstone.adf`: an OFS boot disk with `moonstone` and an LF-only
`s/startup-sequence` (about 475 KB of 880 KB used). It cannot hold the game's data (2.2 MB), so it is not a standalone floppy
version. It boots, shows the menu and plays when the three original floppies are inserted in DF1:, DF2: and DF3: (the game searches
`DF1:` `DF2:` `DF0:` `DF3:` for each file, and takes disk B's real `kn1.ob` over disk A's 5-byte stub). That needs three extra
drives, so on a real machine use the HD install. A floppy boot does not write log files. A self-contained multi-disk set (data
spread over several ADFs with disk-change prompts) is the open part of H4 and is not planned for now.

Tested in WinUAE: `floppy0 = build/moonstone.adf`, `floppy1..3` = the original A/B/C images, turbo floppy: boots, menu, Practice fight.

## 4. WinUAE

`moonstone-ace-hd.uae` (A1200, 2 MB chip, no fast RAM, `DH0` = a build's `hd` folder) and `build/aca1232.uae` (68020 + 8 MB fast,
the owner machine) are ready-made; point `filesystem2=rw,DH0:hd:<path>` at `build/hd`. The Kickstart ROM path inside is the
developer's: set your own `kickstart_rom_file` (A1200 3.1 recommended).
