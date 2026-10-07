# Headless boot test: autoplay + serial log

A build with `MS_AUTOPLAY` reads `PROGDIR:autoplay.txt` at startup and injects keys / joystick values itself, so a boot
test needs no desktop focus, no host keystrokes and no mouse. `uaeshot.ps1 -Autoplay` runs it, captures screenshots when
the game says so, and `tools/shotcmp.py` compares them with references.

## Build

CMake option `MS_AUTOPLAY` (default OFF; nothing is compiled in without it, every hook is an inline no-op). It adds `-DMS_AUTOPLAY=1`
and, for C++, `-include src/rt/serlog_logwrite.hpp` (the game's own `logWrite()` calls then go to the serial log too; ACE_DEBUG
cannot be used for that, it checks the stack against ACE's bounds and the game runs on its own). Use a separate build dir:

```sh
cmake -S . -B build-autoplay -G Ninja $TC -DCMAKE_BUILD_TYPE=Debug -DMS_AUTOPLAY=ON
cmake --build build-autoplay && cmake --build build-autoplay --target hdinstall
```

`py tools/integrate.py --boot` does exactly this (configure flags copied from `build-game-debug`'s cache; plus the Release twin,
see "Release leg") and then runs the regression below. A config for it is generated as `build-autoplay/autoplay.uae` (`moonstone-ace-hd.uae` with DH0 on
`build-autoplay/hd`).

## Script format (`include/engine/autoplay.hpp`, parser `src/engine/autoplay.cpp`)

One command per line, `frame <N> <command>` (or a bare `wait ...` / `sync`, below); `#` / `;` comments; case-insensitive; CRLF ok; at
most 480 events, 16 KB; shot / log / wait names at most 27 characters (`[A-Za-z0-9_.-]`), `type` text at most 19.

| command | effect |
|---|---|
| `frame 600 key SPACE down` / `up` | raw Amiga key press / release, through the keyboard ISR's own delivery path |
| `frame 600 key SPACE tap` | down at N, up at N+15 |
| `frame 900 joy1 down` | port 1 state := the directions (`up down left right fire`, `+` joined, `none` = release all); `joy0` for port 0 |
| `frame 900 joy1 fire pulse [F]` | like that, but the harness releases it itself after F frames (default 3) or at the next file open (see "Pulses") |
| `frame 1200 type ACE` | one tap per character (A-Z, 0-9, `_` = space), key-downs 30 frames apart, held 15 |
| `frame 700 shot menu` | logs `AUTOPLAY shot menu`, then **freezes the guest for 8 s** (400 VBLs) while the host captures the window to `build/shots/menu.png` (see "Shots") |
| `frame 710 log some_text` | logs `AUTOPLAY log some_text` |
| `frame 3000 quit` | logs `AUTOPLAY quit`; the host script ends that emulator |
| `wait file Re.a [max 3000]` | **barrier**: block the script until the game opened that data file (`files: open ...Re.a`, case-insensitive, last path component) |
| `wait log some_text [max F]` | barrier: until a serial-log line (the game's own `logWrite` too) contains the text (`_` matches a space) |
| `wait input [K] [gap G] [max F]` | barrier: until the game has polled the joystick on K (default 5) frames in a row *after a break* (a file open, or no poll for more than G ticks, default 0), i.e. a new input loop is running after a screen change |
| `sync` | barrier that never blocks: forget the hits seen so far (the next `wait` needs a fresh one) |
| `wait var scene 2 [max F]` | barrier: until the game variable (`scene` `turn` `day` `spent` `players` `defeat`, or a field of the knight picked with `poke who N`, see "Poke / warp") equals the value |
| `frame 50 poke mapx 120` | test hook on the game state (MS_AUTOPLAY), see "Poke / warp" |
| `frame 50 mash joy1 10` / `mash joy1 off` | fight autopilot: a new pseudo-random stick+fire pattern every 10 frames (deterministic seed) |

Key names: A-Z 0-9, SPACE RETURN ENTER ESC TAB BACKSPACE DEL HELP, UP DOWN LEFT RIGHT, F1-F10, LSHIFT RSHIFT CTRL LALT RALT
LAMIGA RAMIGA CAPSLOCK, NUM0-NUM9, COMMA PERIOD MINUS EQUALS SLASH ... (full table in `src/engine/autoplay.cpp`).
Bad lines are skipped and counted (`AUTOPLAY script: N events, M bad lines (first L)` in the log).

Frame = the level-3 VERTB count since the autoplay was armed (`rt::autoplayTick` from `irqLevel3`, PAL 50 per second), so it
does not advance while the OS owns the machine (file loads inside `systemUse`) and the game's own frame counter
(`LAB_0379`) is a different clock. Events fire in the VBL interrupt; a key held for fewer than ~2 frames can be missed by game
code that polls it.

### Barriers: frames relative to events

A `wait ...` / `sync` line is a **barrier**: it ends the *segment* of the lines before it. The parser sorts events by frame only inside a
segment, the barrier goes behind everything listed before it (an optional `frame N` prefix is a minimum delay after the previous
barrier), and in the segment after a barrier `frame N` means **N frames after the barrier was released**. So a script that only
uses `frame` lines (every older script) is unchanged, and one with waits is a chain of "do X, wait until the game got there, do Y
N frames later". An optional trailing `max F` gives up after F frames and goes on (logs `AUTOPLAY wait-timeout`); without it a
barrier waits for ever (the host `-Timeout` ends the run).

`wait file` / `wait log` match **hits**: every serial-log line that matches the pattern of some wait in the script is remembered when it
happens (a ring of 64), so a wait that is reached after its event already occurred does not hang. A wait consumes the oldest
unconsumed hit of its pattern; the next wait of the same text needs a later one (`sync` forgets all of them). The log shows
`AUTOPLAY waited <what> frame N` for every released barrier.

`wait input` is for screens that load no file (the menu, the knight select, the name entry and the map keep their data in
memory: after the menu **no** file is opened until a place or fight is entered). The game polls the joystick (`rt/input` `joyPoll`)
in every input loop (menus: every frame; the map: every 2nd or 3rd, use `gap 4` there); when the polling stops for a redraw or a
load and then starts again, that is a new input loop. It cannot see a transition that is faster than G ticks and has no load: a
screen already polling when the barrier is reached is not "new". Use it where a real gap exists (the end of the intro, a fight
starting) with a `max`, and plain `frame` offsets elsewhere: the autoplay frame is emulated time, it does not depend on the host
speed, and it stands still in loads, so a `frame` delay after a pulse is exact.

### Pulses

`frame N joyP <dirs> pulse [F]` sets the port state like a plain `joyP` line and clears it by itself after F frames (default 3; the map
polls only every 2nd-3rd frame, the menus every frame) or when the game opens a file, whichever comes first; the log says
`AUTOPLAY pulse joyP done, R reads`, and `MISSED` when R = 0, i.e. the game never read the port while it was up (a script
bug: wrong screen, or the screen was not ready). A press can therefore never outlive its screen: plain `joyP fire` ... `joyP none`
pairs kept the fire down across the screen change when the frame counter stood still in a load, and the name entry takes fire as
OK ("SIR GODBER" was accepted before a key was typed). Use a pulse for every menu step / fire press; hold only for walking
(`joy1 right` ... `joy1 none`) or the fight controls. Independently of that, **every file open releases all held input**
(joysticks at once, keys at the next tick; log `AUTOPLAY release-on-load`), so a held direction never survives a load.
(An earlier "consumed by the first read" variant lost presses: something else reads the port too, e.g. the cursor hook that runs
in the VBL interrupt on the town screens, so the pulse is time-based.)

### Poke / warp (test hooks, `src/rt/autoplay_game.cpp`)

Walking to every place and winning every fight by hand would make the play scripts hours long, so the harness can change and read
game variables. They exist only in a build with `MS_AUTOPLAY`, run from the VBL
interrupt (use them on a settled screen: the map, a shop), and are not part of any original behaviour. A failed poke is logged
(`AUTOPLAY poke <name> <v> FAILED`; `tools/integrate.py --boot` prints it as a note).

`frame N poke <name> <value>` (decimal or `0xHEX`):

| name | effect |
|---|---|
| `who N` | the knight the following names act on (0..4; default 0 = the human of a one-player game; 1 = the second human or the first black knight) |
| `mapx` `mapy` `gold` `hp` `hpmax` `lives` `progress` `strength` `constitution` `endurance` `daggers` `frog` | field of that knight (`frog` = days it skips its turn: 100 parks a black knight) |
| `keys` `moonstones` | the knight's inventory words (keys 15 = all four; moonstones 15 = every phase: Stonehenge ends the game) |
| `day N` | the day counter (ROADMAP 8.4a): the dragon spawns whenever the map scene is (re-)entered on day 2 or later, so `poke day 2` + the status screen (Space, exit pillar) puts it on the map; `tests/boot/perf_dragon.txt` |
| `warp_node N` | put the knight on map node N (`poke dump 1` lists them: 0..3 the home villages, 4 town A `$19`, 5 town B `$1A`, 6 the valley `$1C`, 7 Stonehenge `$1B`, 8 Math `$1E`) |
| `warp_lair N` / `warp_knight N` | on lair N (0..23) / on top of knight N (a duel); then `joy1 fire pulse 8` opens the arrival menu / the place |
| `cursorx` `cursory` | the joystick cursor of the town / shop screens (lowres 0..314 x 0..194): a click is `poke cursorx`, `poke cursory`, 25 frames, `joy1 fire pulse 8` |
| `dump 1` | log the state: players, turn, day, the knights, the nodes, the 24 lairs |
| `regions 1` | log the click regions of the current shop / loot screen (position, size, button id, type, slot; repeats collapsed) |

`wait var <name> <value>` reads the same names plus `scene` (the screen state machine: 1 two knights meet, 2 creature loot, 3 wizard
item screen, 5 smith, 6 market, 9 status / temple / "you lost", 10 dragon loot, 11 exchange), `turn` (0..3: whose turn), `day`, `spent`,
`players`, `defeat`. Event-driven waits: `wait var keys 0` = the valley fight was won, `wait var scene 2` = the lair is cleared,
`wait var lives 0` = the knight is dead. A `scene` stays at its last value after the screen is left: wait for it only when it cannot
hold already.

Cursor screens (towns, shops, healer, dice, temple, loot): the cursor moves 2 px per frame while a direction is held (`@hold` in a
script: `joy1 up` for N frames then `joy1 none` = 2N px); it starts at the town menu at 290,100 (town A) / 30,100 (town B); a click
needs fire held about 8 frames (`pulse 8`); exit of a shop screen is a right-hand pillar (308,140), of the status / temple / wizard
screens the pillar at 236,150, of the two-knight meeting screen the middle pillar at 155,110; the healer / mystic have arrows
(153,174) (168,174), Ok (140,189), Exit (190,189). The map polls the stick only every 2nd-3rd frame: use `pulse 8` there too.

Do not poke the stats high: the stat sheet registers one click region per stat point and the screen loop has a pool of 100 regions
(`poolClear`: 0x960 bytes of 24). With strength 40 / constitution 60 / endurance 40 the pool overflowed and the loot icons / gold of the
creature loot screen were never registered, so clicking them did nothing (looked like a bug, `poke regions 1` showed 64+ identical stat
regions). Use strength 9, constitution 30, endurance 9 (hp 310) in the fight scripts.

`mash joy1 F` plays a fight: every F frames the port gets the next of 14 patterns (the eight directions and neutral, mostly with fire)
from a fixed seed. The fights are random (the enemies), so the scripts that use it take loose shot tolerances and wait on game
variables, not on frames. A fight reads the stick rarely, a short pulse is missed: hold, or `mash`.

### Shots

Why the old shots flaked: the host script sees the `AUTOPLAY shot` log line, waits 0.3 s and captures the emulator window, but the
log reaches it through a console copy (`conread.ps1`) and a polling loop, so on a loaded host the capture was taken many
seconds late and showed the *following* screens (the name screen "already accepted", the map...). The game never knew. Now the
guest **stands still for 400 VBLs inside the tick** after logging `shot` (busy-polling INTREQR, so the picture, the colour cycling
and every game timer freeze), then logs `AUTOPLAY shot-end <name>`. The host captures in that window and checks the log
afterwards: if `shot-end` was already there it prints `SHOT-LATE <name>` and `tools/integrate.py --boot` fails the run
(rerun on a quieter host; do not relax the freeze). A shot costs ~8 s of wall time.

Example (skip the intro, look at the main menu):

```
frame 300 key SPACE tap
frame 1500 shot menu
frame 1600 quit
```

## Which port the game reads (joystick injection)

Every joystick read of the running game ends in `joyPoll` (`src/rt/input.cpp`: mog `LAB_00EE`/`LAB_00EC`/`LAB_00EA` through the
`rt_mog_joy_read` / `rt_mog_wait_fire` / `rt_mog_joy_port` shims, `ms::joyRead` on JOY0DAT/JOY1DAT/CIAA PRA). `autoplayJoy` overwrites its
result (`LAB_062F` port 0, `LAB_0630` port 1), so a `joyN` event is seen frame-exactly by every caller. The remaining raw
`JOY1DAT`/`CIAA_PRA` reads in the asm (mog after `LAB_0B6B`, program `LAB_0328`..) are unreachable dead code; the program overlay (intro)
reads no joystick at all, only `rt_intro_skip_poll`, which also treats an autoplay fire as a button (`autoplayFireHeld`).

Which port matters (verified by boot, `tests/boot/`; the map walks with port 1 too):

- main menu, Select Knight, name entry: **port 1** (`joy1`). The menu cursor moves one step per `joy1 down` press; fire (`joy1 fire`) activates.
- Practice (the fight): the knight reads **port 0** (`joy0 right` walks it to the right edge; `joy1 right` does nothing).
- Fire and menu steps are `pulse`s (see above); a held press that was still down when the next screen started its input loop
  typed "WHE" into the name entry and accepted it.

## Run

```powershell
powershell -File uaeshot.ps1 -Config build\autoplay-hd.uae -Instance 2 -Autoplay build\script.txt [-Timeout 180] [-Dir shotdir] [-Exe path\to\moonstone]
py tools/shotcmp.py menu --promote      # first time: this shot becomes the reference (build/shots/ref/menu.png)
py tools/shotcmp.py menu                # later: PASS/FAIL, build/shots/diff/menu-diff.png
```

`-Config` must have a `filesystem2=rw,DH0:hd:<dir>,0` line; `<dir>` gets `autoplay.txt`. `-Instance N` uses its own
`build/uaeshot-N.uae` and `build/uaeshot-N.log` and kills only its own WinUAE (without it the script keeps its old behaviour and
kills every winuae64). With `-Autoplay` the window is put at the bottom of the z-order without activation, nothing is typed and
`SetForegroundWindow` is not called. The wait ends at the `quit` line or after `-Timeout` seconds (then `autoplay-end.png`).

## Boot regression (`tests/boot/`, `tools/integrate.py --boot`)

```sh
py tools/integrate.py --boot               # resource + verify + builds + build-autoplay (+hdinstall) + tests, then the boot regression
py tools/integrate.py --boot --only-boot   # just the boot (build-autoplay/ already built); ~4 min
py tools/shotcmp.py reg-menu --promote     # (re)create a reference after an intended visual change
```

Every `tests/boot/regression*.txt` is one boot (instance 3: `build/uaeshot-3.*`, or `MS_BOOT_INSTANCE=N`: own config, own copy of the HD
tree `build-autoplay/hd-N`, so two instances can run side by side), then each of its `shot` names is compared with
`build/shots/ref/<name>.png` (`tools/shotcmp.py` crop and tolerance, PASS/FAIL per screen). The comparison is independent of the host's display scaling and window decorations: `uaeshot.ps1` saves the window at its logical size (the drawn part of the capture bitmap is kept, found with a sentinel fill per shot, so a DPI-unaware WinUAE window on a 150 % monitor, or one dragged between monitors mid-run, is no longer saved inside a larger physical-size bitmap), and `shotcmp` locates the 640x400 Amiga display inside the picture (the window's 1 px frame lines give the client origin and scale; fixed crop 70,77 if no window is recognised) and resamples it to 640x400 before comparing, so references captured at other title bar heights or scales stay valid (no re-promotion; new references can be promoted from any host). A `SHOT-LATE` line from the host script fails
the run. Options of `py tools/integrate.py --boot --only-boot`: `--boot-script FILE` (repeatable; any script, e.g. `tests/boot/play_*.txt`),
`--boot-repeat N` (every script N times: the stability check; each run's shots are kept as `<shot>.runK.png` and its serial log as
`<script>-runK.log` in the shot directory), `--boot-play commit|nightly` (see "Play script tiers"), `--no-play`, `--boot-shots DIR`, `--boot-refs DIR`. Directives in comment lines
(`tools/bootcheck.py`): `# shotcmp-tol <shot> <pct>` per-shot tolerance (default 0.5 %), `# shotdiff <a> <b> <pct>` the two shots must differ
by at least that much (proves an injected input changed the screen, no reference involved). Stale shots are deleted before a run.

- `regression.txt`: intro skip -> `reg-menu` -> down x3 -> `reg-menu-knight` -> fire -> `reg-knight-select` -> fire -> `reg-name-entry`
  ("SIR GODBER_") -> 16 backspaces, type ACE -> `reg-name-typed` ("ACE_") -> Return -> `reg-map`.
- `regression_practice.txt`: menu -> down x2 -> `reg-menu-practice` -> fire -> `reg-practice-start` -> `joy0 right` ->
  `reg-practice-right` (knight at the right edge). The map never returns to the menu, hence the second boot.

Limits: the Practice fight is not deterministic (random enemy moves, load timing), so its references use 12 % tolerance and the real
assertion is the `shotdiff` (the knight moved). The knight screen's name line may or may not have been drawn at the shot (1.8 %
tolerance). Frame gaps assume the Debug build; the host timeout is `frames / 50 + 150 s + 10 s per shot` (`tools/bootcheck.py`,
the frames of a script with waits are summed over its segments, a wait without `max` counts 1500). Do not run two boots on one
instance at once.

### Play script tiers

A plain `py tools/integrate.py --boot` runs, after `regression*.txt`, the **per-commit tier** `PLAY_COMMIT` in `tools/integrate.py`
(`play_castle`, `play_wizard`, `play_stonehenge`, `play_duel`: ~9.5 min on the Debug leg; the Release leg stays regression-only).
`--no-play` drops it, `--boot-play nightly` runs every `tests/boot/play_*.txt` (~45 min serial, ~25 min split over two
instances with `MS_BOOT_INSTANCE`), `--boot-play commit` selects the tier explicitly; an explicit `--boot-script` list runs exactly that.

### Play scripts (`tests/boot/play_*.txt`, whole-game coverage)

The scripts are generated: `tests/boot/src/play_*.mk` hold them with relative delays (`+120 shot x`, `@click X Y`, `@hold joy1 up 32`,
`@PRE` = the boot prefix) and `py tests/boot/src/mkplay.py` expands them to `tests/boot/play_*.txt` (a unit test keeps the two in
step). Edit the `.mk`, not the `.txt`.

Run with `py tools/integrate.py --boot --only-boot --boot-script tests/boot/play_<x>.txt [--boot-script ...] [--boot-repeat N]`
(use `MS_BOOT_INSTANCE=N` and `--boot-shots` / `--boot-refs` for a worktree). Every script boots to the map with the same prefix
(intro skip, Select Knight, first knight, fire = keep the name), then uses the hooks above. The scripts' headers say what they cover.

| script | covers |
|---|---|
| `play_map` | map, walking (port 1), the status screen (Space), end of turn (E), the black knights' turns, "Next Day", the arrival menu (village / lair list) |
| `play_town_a` | town A: smith (buying), dice, healer (donation), market, temple (Space), exit |
| `play_town_b` | town B: Mythral the mystic, the healer, exit |
| `play_wizard` | Math the wizard: three text pages, the status screen |
| `play_castle` | the home village (a life) |
| `play_stonehenge` | Stonehenge without the moonstone: Danu's offer, the item screen, the ritual, the extra life |
| `play_end` | Stonehenge with the moonstones: the quest is completed, the ceremony, the whole ending, the title menu |
| `play_valley` | the Valley of the Gods without keys, then with keys: the guardian fight (autopilot), the moonstone, the win text |
| `play_lair` | a lair fight (ratmen), the creature loot screen, back to the map |
| `play_duel` | a duel with a black knight, the meeting screen |
| `play_2p` | two players: Players = 2, two Select Knight rounds, the second human's turn |
| `play_coop` | co-op for two (ROADMAP 8.2): Players right x4 = "Coop 2", a controller per knight (joystick 1; keys arrows picked with the cursor key and Ctrl), the map: the second knight moves with its keys, not with joy1 |
| `play_death` | a lost fight with one life left, the status screen without lives, game over, the title menu |

### Release leg (`build-autoplay-rel/`, `[boot rel ]`)

Release-only bugs (LTO / `-fipa-icf` folding a function into a 6-byte `bra.l` that elf2hunk relocated wrongly: Line-F crash in the first
mirrored fight cel, ROADMAP 7.2) never show on the Debug build. `integrate.py --boot` therefore builds and `hdinstall`s a second dir,
`build-autoplay-rel/` (Release + LTO + `MS_AUTOPLAY=ON`, flags copied like `build-autoplay/`), after the Debug one, and runs every
`tests/boot/regression*.txt` on it too (same instance, after the Debug leg). Its report lines are labelled `[boot rel ]`; its log is
`build/integrate/boot_rel_<script>.log`; its config is `build-autoplay-rel/autoplay.uae`, which keeps the stock A1200 RAM of
`moonstone-ace-hd.uae` (the Debug leg gives itself 4 MB fast RAM) so a memory-budget regression fails here as well.

Release timing differs from Debug (faster loop, different load phase), so the Release leg compares against its own references,
`build/shots/ref-rel/` (the Debug ones stay in `build/shots/ref/`; `shotdiff` assertions are the same). Create or refresh them after an
intended visual change or a first run on a new checkout, from a known-good Release build:

```sh
py tools/integrate.py --boot --update-refs-rel     # builds the Release autoplay dir, runs its boots, copies the shots to ref-rel/
```

Flags: `--no-rel` skips the Release leg (Debug-only boot as before), `--rel-only` skips the Debug one, `--rel-practice-only` runs only `regression_practice.txt` on it.
If the Release exe crashes, the script never reaches `quit`: the leg reports `NO quit line (timeout)` and the shots after the crash
fail (`shotdiff` = the screen did not change), so a Release-only crash FAILS the regression.

## Log sink

`rt/serlog` (`src/rt/serlog.cpp`) writes Paula serial output (`SERDAT`, 115200 baud set via `SERPER`, TBE polled with a timeout) from
any context, no DOS call. WinUAE started with `-log -serlog` prints the Amiga's serial output into its console window;
`tools/conread.ps1` (started hidden by `uaeshot.ps1`) attaches to that console (`AttachConsole`), grows its buffer to 9000 rows
and appends the lines to `build/uaeshot-N.log`, which `uaeshot.ps1` tails for `AUTOPLAY shot` / `AUTOPLAY quit`. The log also
carries `files:`, `rt irq:` and `rtGameRun:` lines (every `logWrite` of the game with the force-include above), and
`AUTOPLAY frame N` every 250 frames as a heartbeat.

Things that were tried and do not work on this WinUAE 6.0.3: `-conlogfile`, config `logfile=`, `serial_port=TCP://...` (no
listener appears), so the console is the only sink. Do not set `ACE_DEBUG`.

`tools/conread.ps1` fixes (boot regression made them visible): it appends through a shared-write `FileStream` with retries (a plain
`Add-Content` collided with `uaeshot.ps1` reading the same log, threw, and the script died, so the run froze at a few hundred lines and
no `AUTOPLAY shot` was ever seen), retries `AttachConsole` for ~15 s, and keeps the 9000-row buffer. `uaeshot.ps1` now derives its root and
shot directory from its own location, so it also works from a git worktree.

## Perf log (ROADMAP 7.2)

`rt/perf` adds one `PERF ...` line to the serial log every 250 VBLs (frames of the map/fight loop, work time against the loop's tick
budget, beam-wait gaps). Method, line format and results: docs/PERF.md. Use a Release + `MS_AUTOPLAY` build for numbers.
The fight frame split (`PERFX/PERFY/PERFZ` lines per fighter/job-slot bin, `tests/boot/perf_*.txt`, `py tools/perfx.py`) is described under "Fight frame split" in docs/PERF.md.
