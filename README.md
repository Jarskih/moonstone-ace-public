# moonstone-ace

**Moonstone: A Hard Days Knight** (Mindscape, 1991) rebuilt from its 68k machine code into C++ on
[ACE](https://github.com/AmigaPorts/ACE), running on the Amiga 1200 (68020, AGA, Kickstart 3.x).

It plays like the original, because it was checked against the original at every step. This repository holds
**none of Moonstone's data, art, music or code**. The game reads all of that from your own copy of the three original
disks every time it starts.

---

## How to play

### What you need

- **An Amiga 1200** or **WinUAE** set up as one: 68020, AGA, Kickstart 3.x, 2 MB chip RAM. Fast RAM is recommended but
  not required.
- **The original game**: the three Moonstone floppies (Mindscape, 1991) as ADF images with any file names, as the files
  copied off them, or as real disks.

### Install on a hard disk (real A1200)

1. Make a drawer, for example `DH0:Moonstone`, and copy the `moonstone` executable into it.
2. Put your three ADF files into the same drawer or into a `disks` drawer inside it. The names don't matter; the game
   recognises each disk by its contents.
3. Double-click `moonstone` (give it any Tool icon) or start it from a Shell: `DH0:Moonstone/moonstone`.

If a disk is missing or is a different version, the game stops with a message saying which disk is missing.
Floppies in DF0:-DF3: work too, but the hard disk is the supported way.

### WinUAE

Use an A1200 configuration (68020, 2 MB chip, fast RAM if you like). Add the Moonstone drawer as a directory hard drive,
boot and start `moonstone` as above. `build/aca1232.uae` in a developer checkout is a ready-made configuration of an
A1200 with a 68020 accelerator and 8 MB fast RAM. Map a joystick, or the arrow keys plus a fire key, to the joystick port.

### Playing

- **Up to four knights** play in turns. Pick your knights and names on the start screens; knights nobody picks are
  played by the computer. **Practice** on the title menu is a single training fight.
- **Joystick:** player one uses the joystick port (port 1); a second human player uses the mouse port (port 0).
  Knights take turns, so more players can share the sticks. The joystick moves your knight on the map, picks buttons in
  towns and on the loot screens, and fights in the arenas: direction plus fire gives the different strikes and blocks.
- **On the map:** move to a place and press fire to go in. Towns have the smith, the market, the healer and more;
  lairs hold monsters and treasure; Stonehenge, the wizard and the Valley of the Gods are where the story is.
  **Space** opens the status screen, **E** ends your turn.
- **The goal:** fight through the lairs, the black knights and the dragon, win the keys to the Valley of the Gods, and
  complete the quest at Stonehenge with the Moonstones, before the other knights do.
- There is no quit-to-Workbench yet; leave with a reset (Ctrl-Amiga-Amiga).

### Mods

Rule tweaks need no rebuild: put an `.ini` file into the `mods` drawer next to the game.
`mods/defaults/rules.ini` lists every setting with its default value. A broken file is skipped with a message in
`mods.log`, and the game plays on with the original values. See [docs/MODDING.md](docs/MODDING.md) and
[docs/MOD_KEYS.md](docs/MOD_KEYS.md).

---

## How it was made

The whole conversion took three days (5-7 October 2026) and about 100 commits. It was done by AI agents (Claude),
working in parallel git worktrees, with one integrator merging their branches through a fixed gate. The owner
playtested on a real A1200 and made the design decisions. The plan, with every task and its proof, is
[ROADMAP.md](ROADMAP.md).

### 0. Ground truth

- Extract the three floppies with our own OFS reader (`tools/adfx.py`): 34, 88 and 91 files, every block chain and size
  checked.
- The game is three programs:
  - `nb`, the 15 KB boot loader;
  - `program`, 60 KB: the intro, menus and knight selection;
  - `mog`, 172 KB: the game itself.
  They overlay each other and never return.
- Disassemble them with IRA into 494, 1,499 and 4,247 labels, and **reassemble** them with vasm until the result is
  *load-equivalent*: same hunks, memory flags, every data byte, same relocations. A few places where IRA had decoded
  data as code had to be rewritten so they encode the same bytes again.
- One surprise: **99 of program's 223 routines are instruction-identical to routines in mog.** These are the disk
  driver, interrupts, the file cache, the picture decoder and the custom-chip set-up. They became one shared engine.

### 1. The original running on ACE

- Link the reassembled code with a small ACE runtime instead of the original's hardware takeover. ACE owns the CPU
  vectors, the keyboard and memory. The game still owns the copper, blitter, sound and disk.
- Absolute RAM addresses in the original were turned back into symbols, so the code could be relinked anywhere.
- Milestones:
  - the first visible frame, the Mindscape logo;
  - the intro playing through to "Select a Knight";
  - a full boot into the game, all still 100% original code.

### 2. Lifting, one routine at a time

- **`tools/lift.py`** turns a routine into *literal* C++: every 68000 instruction becomes a line with exact flag
  semantics, and branches become `goto`. It refuses anything it doesn't understand.
- **A differential harness** runs the original routine and the C++ side by side on the PC: 500 random cases per routine
  (seed 1), comparing registers, flags and memory. A routine counts as *lifted* only when every case passes. Larger
  pieces were checked in **unicorn** (a 68k emulator) against the reassembled original image.
- **Thunks** let a proven C++ routine replace its asm in the running game behind a register-marshalling wrapper. Every
  swap could be bisected: the same body could go in and out.
- About 220 routines went through this pipeline before the work moved from literal lifting to writing idiomatic C++
  directly against the proven behaviour.

### 3. Idiomatic C++, scene by scene

- **`src/engine`:** the pure parts: blitter-style cel drawing, RLE/RNC/packbits decoders, palettes, jobs, the music
  synth.
- **`src/game`:** the rules and scenes: the map, towns, places, fights with per-creature AI, loot, the ending.
- **`src/rt`:** the Amiga-specific runtime.
- Game data that lived in the original's data sections became **typed C++ data**. A generator names all 851 data cells,
  so the code reads `mogFightTotal` instead of `LAB_05EC`.
- On 7 October the last asm left the link: **the game is entirely C++** (milestone 7.1r). The Release build is about
  470 KB with link-time optimisation, against 406 KB for the original-code build.

### 4. Proving it plays the same

- **A headless boot harness** (`tools/integrate.py --boot`, [docs/AUTOPLAY.md](docs/AUTOPLAY.md)) runs WinUAE from a
  script: keys, joystick pulses, `wait` for a file, a log line or a game variable, pokes into game state, and screenshots.
  Each screenshot is compared with a reference within a stated tolerance.
- **17 play scripts** cover the game:
  - the map, both towns and the shops, the wizard, the castle, Stonehenge and the ritual, the Valley of the Gods and its
    guardian;
  - lair fights against five different creature arenas, the knight duel, the dragon;
  - two players, death and the ending.
- **Every merge goes through the same gate:** build Release and Debug, about 85 test modules, the boot regression on both
  builds and the play scripts.
- **The checks caught real bugs.** One example: in the dragon fight the knight left a trail of copies and the dragon was
  drawn as stripes. The arena was re-using the dragon's map-flight record: the original resets 15 fields when the dragon
  lands, and the C++ had reset 4. A test of every one of those writes now guards it.

### 5. Readable, moddable code

- **Names and constants:** fields and cells got real names, magic numbers became `enum class`es (actor types, actions,
  items, moon phases, scenes, script opcodes). The renamed build was byte-identical to the build before.
- **A game API** (`include/game/api/`): party, items and gold, clock and dice, fights, lairs. Rules call named functions
  instead of poking memory. A layer lint test keeps hardware code out of game logic.
- **Scenes:** every screen is its own scene with a list of the files it loads, run by one scene manager (switch / push
  / pop) with mark-and-release memory. Adding a scene is one file and one table line
  ([docs/GAME_FLOW.md](docs/GAME_FLOW.md)).
- **Data-file mods:** an INI reader, a schema, and generated defaults. Bad files are skipped with a line-numbered
  message.
- **Monster kit** ([docs/MONSTER_KIT.md](docs/MONSTER_KIT.md)):
  - a fight-script assembler and disassembler that round-trips all original creatures byte for byte;
  - a catalog of the eight creature AIs and the animations each one needs;
  - a browser editor (`py tools/monsterkit.py serve`) to clone a monster, repaint it in the game palette, move its
    attack points, pick its AI and build it back into the game.

### 6. Shipping without the original

The repository holds no original bytes:

- **The build needs no original files.** A table of facts (where each data cell lives, what it points to, the checksum
  of the known version) replaces the original data.
- **The game fills its data at start-up** from the original executables on your disks. A memory dump of a booted game
  matches the old built-in data exactly: 131,392 data bytes and 7,724 synth bytes.
- **The game reads ADF images directly**, identifying each disk by its files.
- **A leak scan** in every gate compares every published file with the original disks: byte runs, number tables and
  texts.

### What's next

- **Mods without rebuilding:** creatures, items, lairs, shops and prices become data files, then the rules become
  readable C++ against the API.
- **Moonstone 2** ([docs/MOONSTONE2.md](docs/MOONSTONE2.md)): two to four knights as a co-op party, new weapons
  including bows, playable monsters, and maybe Golden-Axe-style scrolling lairs.

---

## Building

- **Needs:** the Amiga C/C++ toolchain (Bartman's "Amiga C/C++ Compile, Debug & Profile" VS Code extension), CMake,
  Ninja, Python 3 with PyYAML, and clang++ for the host tests.
- **ACE** is the `ace/` git submodule (`git clone --recursive`); a sibling `../ace` checkout also works.
- **Local tools**, git-ignored: `tools/toolchain/`, `tools/AmigaCMakeCrossToolchains/` and `tools/bartman_gcc_support/`.
- Commands, configure lines and build variants: [BUILD.md](BUILD.md).
- `py tools/setup.py A.adf B.adf C.adf` extracts your disks for the tests that compare against the original, builds
  and installs into `build/hd`. The build itself does not need the disks.

**Docs:**

| Doc | What it covers |
|---|---|
| [ROADMAP.md](ROADMAP.md) | status and every task |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | layers and the modding design |
| [docs/MODULES.md](docs/MODULES.md), [docs/LABEL_INDEX.md](docs/LABEL_INDEX.md) | which file holds which original routine |
| [docs/BOOT_CHAIN.md](docs/BOOT_CHAIN.md), [docs/DISPLAY.md](docs/DISPLAY.md), [docs/FILES.md](docs/FILES.md), [docs/MEMORY.md](docs/MEMORY.md), [docs/PERF.md](docs/PERF.md) | how the Amiga side works |
| [docs/PUBLISHING.md](docs/PUBLISHING.md) | what is and isn't in this repository |
