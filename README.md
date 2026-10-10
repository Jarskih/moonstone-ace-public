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

<img width="1280" height="457" alt="image" src="https://github.com/user-attachments/assets/6fd8bce6-9111-44d4-91fe-d7ef816a9b1b" />

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
