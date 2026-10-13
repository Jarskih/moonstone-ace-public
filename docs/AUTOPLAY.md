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
| `tick 7000 poke gold 5` / `tick +20 poke ...` / `tick hash+10 poke ...` | network games: the poke is queued and applied by the lockstep tick itself at the start of tick N (absolute, K after the current tick, K after the last consumed hash-barrier tick): the same tick on both windows. See "Tick-aligned pokes"; limit 16 queued |
| `frame 50 poke mapx 120` | test hook on the game state (MS_AUTOPLAY), see "Poke / warp" |
| `frame 50 mash joy1 10` / `mash joy1 off` | fight autopilot: a new pseudo-random stick+fire pattern every 10 frames (deterministic seed); `mash wasd F` plays the same patterns on the keyboard set W A S D + left Alt (not used by a script yet: see the net_pair note in NETPLAY work) |

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
unconsumed hit of its pattern; the next wait of the same text needs a later one (`sync` forgets all of them). A wait that consumes a hit also drops every older unconsumed hit of the other patterns, so a `wait` for an event that can
happen while the script sits in another barrier chain must come before that chain's next `wait` (and a `sync` in between forgets it):
`net_pair_*.txt` put `wait log knight_1_kind` right behind the fire that causes it, before the next `sync` + `wait log hash_tick`.
A pulse is MISSED while the game does not poll the port; in a network game that includes every tick it waits for the peer (the peer's
8 s `shot` freeze), so lockstep scripts step on a shared barrier (the state-hash log line every 50 ticks) instead of frame counts.
The log shows
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
bug: wrong screen, or the screen was not ready). A pulse the game has not read yet when F runs out is held one more frame at a time,
up to 50 frames, like a player keeping the stick pushed (2026-10-11: the title/LOCAL menu polls once per redraw, i.e. every ~12 frames,
and a short pulse during its wait was lost); only then is it MISSED. A press can therefore never outlive its screen: plain `joyP fire` ... `joyP none`
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
| `mapx` `mapy` `gold` `hp` `hpmax` `lives` `progress` `strength` `constitution` `endurance` `dexterity` `daggers` `frog` | field of that knight (`frog` = days it skips its turn: 100 parks a black knight) |
| `keys` `moonstones` | the knight's inventory words (keys 15 = all four; moonstones 15 = every phase: Stonehenge ends the game) |
| `party N` | Moonstone 2 co-op (ROADMAP 8.3): poked before the knight select (title / players menu), the new game is a co-op game of N knights (0 = classic): one party token at the first player's home, the black-knight group, turn slots, the leader rotating each round; `poke dump 1` adds the party lines (size, leader, the leader's port, records 5..), `wait var leader N` / the peek `leader` give the leader (255 outside co-op); `tests/boot/play_coop.txt` |
| `day N` | the day counter (ROADMAP 8.4a): the dragon spawns whenever the map scene is (re-)entered on day 2 or later, so `poke day 2` + the status screen (Space, exit pillar) puts it on the map; `tests/boot/perf_dragon.txt` |
| `warp_node N` | put the knight on map node N (`poke dump 1` lists them: 0..3 the home villages, 4 town A `$19`, 5 town B `$1A`, 6 the valley `$1C`, 7 Stonehenge `$1B`, 8 Math `$1E`) |
| `warp_lair N` / `warp_knight N` | on lair N (0..23) / on top of knight N (a duel); then `joy1 fire pulse 8` opens the arrival menu / the place |
| `cursorx` `cursory` | the joystick cursor of the town / shop screens (lowres 0..314 x 0..194): a click is `poke cursorx`, `poke cursory`, 25 frames, `joy1 fire pulse 8` |
| `fightdump 1` | Moonstone 2 (ROADMAP 8.4b): log the fight roster (`on / party / foes / defeat`), every party fighter (position, pad port, hp, lives), the black knights, the job limits and the job table, every live creature with its sticky target slot and team |
| `netfake 1` / `netpeers N` / `netjoin N` / `netseated 1` / `netstarted 1` | the Multiplayer screen's and the lobby's fake network (ROADMAP 12.8a, `src/rt/net_stub.cpp`; without them the stub reports "no TCP/IP stack"): `netfake 1` = a stack is present (HOST GAME works and opens the lobby, a join gets an answer), `netpeers 0..3` = players in the fake host's lobby, each already picked (BOB portrait 3, CAROL 2, DAVE 1), `netjoin` = the join's outcome (0 connected, 1 no answer, 2 wrong version, 3 stays connecting), `netseated 1` = the fake host seats the joining player (the lobby opens), `netstarted 1` = the fake host's start message (the client's lobby leaves); `tests/boot/play_netmenu.txt` |
| `lair_left N` | the creature count of the lair the last `warp_lair` named (short fights for the play scripts) |
| `warp_knight 5..8` | co-op: on the black-knight group (records 5..8); the arrival menu then needs the key `1` ("Battle with black knights") |
| `dump 1` | log the state: players, turn, day, the knights, the nodes, the 24 lairs |
| `regions 1` | log the click regions of the current shop / loot screen (position, size, button id, type, slot; repeats collapsed) |
| `netdet N` | ROADMAP 12.4: 1 = the deterministic mode for network play (`rt/net_tick`, NETPLAY.md 1.3) on, 0 = off. Only the mog overlay runs in it (the intro and the ending stay classic), so set it at the title menu: `play_net_lair`, `play_net_coop`. `wait var netdet` / `netclock` / `nettick` read the flag, the logical frame counter and the tick count |
| `netaction N` | ROADMAP 12.8b: presses a button of the Multiplayer screen through the same code path as fire on its row: 1 = HOST GAME, 2 = CONNECT (from the title screen's main page it first walks to the join page; the address and port are the ones pre-filled by `HOST` / `JOIN <addr> PORT <p>` on the command line, so the script types nothing), 3 = the lobby's START (only on the host's lobby screen with another player joined and everybody picked), 4 = FIND GAME (ROADMAP 12.18: opens the game browser from the Multiplayer screen's main page; the page asks the lobby named in net.ini `lobby` / `lobby_port` and logs `net: find screen open` and, each time its status changes, `net: find status N games M` - status 2 = the list is in, so `wait log find_status_2_games_1` waits for exactly one listed game; a fire on the first game then joins it, directly or through the relay; the page has no poke of its own). The action waits in a cell until the screen's loop takes it (poke it after `wait log screen_open`, the game logs that line when the screen opens); on the wrong page or while a connect / listen is under way it is ignored, so a repeated poke is a safe retry. Real session and fake stack alike. `tests/boot/net_pair_host.txt` / `net_pair_join.txt` drive both windows with it plus the lobby log lines (`wait log lobby_open`, `peers_1`, `lobby_seat_0_done`, `lobby_seat_1_done`: one line per seat that joins, claims, finishes or leaves; `lockstep_begins`).  `tests/boot/net_relay_host.txt` / `net_relay_join.txt` (generated by the same `mknetpair.py`, run by `py tools/netpair.py --relay`, ROADMAP 12.13a / 12.18) are the same session with a LOCAL lobby server that cannot connect back: the host is listed via relay (`wait log relay_state_4`: the log line `net: relay state 4 ...` says its idle relay connection to the server is up; `net: relay client arrived on link N` when a guest is paired) and the guest uses FIND GAME (`poke netaction 4`, `wait log find_screen_open`, `wait log find_status_2_games_1`, fire) instead of CONNECT.  **5 = REJOIN GAME** (ROADMAP 12.15a, NETPLAY.md 4.5): the Multiplayer screen's row that appears while this machine remembers a game it was dropped from (the log's `net: rejoin memory` line comes when a lockstep begins as a client); it connects to that game and says REJOIN with the saved token. The screen then shows ASKING THE HOST / WAITING FOR THE NEXT TURN and leaves for the game when the host's map turn has sent the world. Logs: host `net: rejoin: node N (NAME) will enter at the next map turn` (the note is up), `map turn at tick`, `is back at tick`; rejoiner `the world of tick N decoded`, `loaded; my hash ... the host's ...: ok`, `lockstep running as node N`; trouble shows as `net: rejoin: failed`, `... differs`, `... could not be resumed` (tools/netpair.py counts those as errors). The dropped window reaches the title menu with Q on the map (the farewell screen, fire), then `poke menuact 1`, `poke netaction 5` (the last part of `tests/boot/net_pair_*.txt` / `net_relay_*.txt`).
  A `wait log` pattern may not contain `:` (the parser rejects it), hence the colon-free log lines |
| *(log lines, ROADMAP 12.21, UDP input with history)* | `net: udp: probe sent (node N)` (the first probe datagram of a link), `net: udp: ok (node N)` (the two-way handshake worked: the ticks travel in datagrams from here), `net: udp: fallback to tcp (REASON) node N` (reasons: `disabled` = command line `TCP` / net.ini `udp = no`, `no udp socket`, `no answer` = no handshake in 3 s of pumping, `loss streak` = the peer's acks stopped for 5 s, `rejoin`), `net: udp: session: sent N received M covered C escalated E bad B probes P, links active A tcp T` (at the end of a session), and `PERFU udp tx= rx= covered= esc= bad= active= tcp=` after each PERF line. `wait log udp_ok` waits for the handshake; `tools/netpair.py` requires `udp: ok` on both windows (and no fallback), `--tcp` starts both windows with `TCP` and requires `fallback to tcp (disabled)` instead |
| `netsilent 1` / `0` | the lockstep node sends nothing (`rt/net_session` `holdWhileSilent`: its frame hook only serves the sockets), so the peer's tick stalls and its WAITING FOR box comes up (the game logs `net: tick N: box waiting` / `box confirm` / `box gone` when the box changes). It stays silent until the peer drops it (BYE, the log's `session ended`), the session ends, `netsilent 0` or a new lockstep begins (the rejoin after the drop check). The drop check of `net_pair_*.txt`: the join goes silent, the host waits for `box_waiting`, presses fire, waits for `box_confirm`, presses again |
| `netstats 1` / `0` | the ping / loss line of a network game (docs/NETPLAY.md 4.7) on / off, as `net.ini` `stats` and the F10 key do (default on in a session). While a session runs the log shows `PERFN PING 38MS  LOSS 2 PCT` (or `... TCP`, with ` (hidden)` appended when it is off) after each PERF line |
| `menuact N` | the main menu / the title menu takes a row as if fire were pressed on it, no cursor counting (owner 2026-10-11: the main menu LOCAL / MULTIPLAYER / SETTINGS comes first; LOCAL opens the title menu Players / Practice / Select Knight): 1 = MULTIPLAYER, 2 = SETTINGS (the Options screen), 3 = LOCAL + Select Knight, 4 = LOCAL + Practice. 1 and 2 are taken by the main menu (from the title menu they go back to it first), 3 and 4 open LOCAL and are taken there at once (`src/rt/scene_menu.cpp`; `tests/boot/net_pair_*.txt`, `play_options`) |
| `netseed V` | the next seeding event (`rtMogRngSeed`, every `mog` entry) uses V instead of the beam |
| `netstate 1` | log `AUTOPLAY netstate tick=.. clock=.. pace=.. seed=..` (tick count, logical clock, real VBL pacing counter, RNG state) |

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

### Tick-aligned pokes (network games)

A plain `poke` runs from the VBL interrupt at autoplay frame N: in a two-window network game the windows are on different logical ticks at that moment,
so the pokes land on different ticks and the state hashes differ for a while (or for good: a poke between two merged frames changes what a tick does).
A **tick-poke** is applied by the lockstep tick itself, in `rt::netTick` (the frame boundary of the main thread, `autoplayTickPokes`), at the *start*
of logical tick N, before that tick's input is latched and before its game logic: the same point on every node. Only in the deterministic mode
(`rt/net_tick`): without ticks nothing is applied.

```
tick 7000 poke moonstones 15          absolute: at the start of tick 7000
tick +20 poke gold 100                relative: 20 ticks after the tick the game is on when the line is reached (not the same tick on two windows)
tick hash+10 poke warp_node 7         10 ticks after the state-hash tick the LAST BARRIER consumed (the line `net: hash tick H crc ...` that released
                                      the preceding `wait log hash_tick`): H + 10 on both windows, whatever tick each was on when it reached the line
frame 5 tick hash+10 poke who 1       an optional `frame F` prefix works like the other lines (F frames after the previous barrier, default 0)
```

The line is **queued when the script reaches it** (it does not wait): a `tick` line right after the barrier is queued at the barrier's release. Pokes
of the same tick apply in script order (`poke who 1` then `poke moonstones 15` act on knight 1; mix tick-pokes only with tick-pokes when they depend
on each other). The log has `AUTOPLAY tick-poke <var> <value> at tick N` (`... FAILED` appended when `autoplayPoke` refused the name or value).
The **tick** comes from `hash+K` for the two-window scripts: both scripts have passed the same `sync` + `wait log hash_tick` barrier, so both know
the same H; K must be larger than the ticks a window can be ahead of the barrier when its script reaches the line (the barrier fires when the
node logs the hash of tick H; the line is reached at the next VBL, one tick per VBL at most; the peer is up to DELAY ticks behind): 10 is safe,
and K below 50 keeps the poke before the next hash bar, which then covers the poked state. A poke that arrives too late (its tick has begun or
passed when the line is reached, or it is still queued when a later tick starts) is *not* applied and logs
`AUTOPLAY ERROR tick-poke <var> <value> ... late` (also: not queued because the queue (16) is full, or `hash+K` with no hash line consumed yet, or `+0`):
`tools/netpair.py` counts every `AUTOPLAY ERROR` line as BAD, and compares the two windows' `tick-poke` lines (same variable, value and tick, same order).
**`sync` behind a `wait var`:** a window notices the variable at its own VBL, a few ticks apart from the peer, so a plain `sync` + `wait log hash_tick` could consume
hash line H on one window and H + 50 on the other, and every later `tick hash+K` would land on different ticks (ROADMAP M12, the classic run). The lockstep tick
therefore records the tick at whose start the variable first held (the same on every node); the `sync` that directly follows the `wait var` keeps the hits and the next
`wait log hash_tick` takes the first hash line after that tick on every window (`AUTOPLAY wait var NAME held at tick N` in the log). If the script reaches the wait
when the variable already holds, the recorded tick is the first tick start after that and may differ per window: put the `wait var` before the event can happen.
That wait is released by the first hash line at or after the *floor* (the first hash tick after the variable's tick) and counts as exactly the floor line, even when
the ring of 64 hits no longer holds it or the window is a bar late; the floor is one shot (the next `sync`, a timeout or the next `wait var` drops it).
**The base of `tick hash+K` is the line the last barrier consumed.** A wait that gave up (`max` frames, logged `wait-timeout`) consumed nothing, and a `wait var` /
`wait input` consumes no hash line: the base is then gone and the `tick hash+K` line logs `AUTOPLAY ERROR tick-poke ...: no tick (no hash barrier consumed yet ...)`.
(The classic run kept the base of a barrier 5000 ticks old - hash 1350 - after a timed-out wait and queued the pokes for tick 1360 while the game was on 9202.)
Pure logic (ring, floor, base): `ApHitLog` in `include/engine/tickpoke.hpp`; `rt/autoplay.cpp` only feeds it log lines and asks it at the barriers.
**State pokes in a two-window script are tick-pokes, and no passing run has a `wait-timeout`** (batch5). A plain `poke warp_lair` landed on a different logical tick per window
(the relay run: the host's map saw the warp at tick 1457 and opened the lair menu, the join's never did, desync at 1600); a timed-out `wait var` ends after the same number of
*frames* on both windows, which is not the same tick, so the next `sync` + `wait log hash_tick` took hash 6400 on one and 6450 on the other and every `tick hash+10` landed 50
ticks apart (the classic run: the "Next Day" press, a fixed two bars after `wait var turn 0`, came before the screen polled; it stayed up and `wait var turn 1` timed out).
Wait for the event instead: `wait log next_day_waits` (the game logs `next day waits for fire` when the screen polls) and then press.
**A `shot` stalls the peer.** The 8 s freeze of one window stops its lockstep ticks, so the other window stands in its WAITING FOR box (log: `net: waiting tick N, missing
nodes mask M ... (waited 1000 ms)` ... `7000 ms`) while the frozen window's vbl clock stands still: the two logs of one run then show the frozen window "ahead" by the
length of the freeze. Both windows of a pair script must take their shots at the same bars and wait for the same bars, or the window that waits for more is left with bars
its peer never plays (the rejoin tail of `net_pair_*.txt`: both windows end with two bars, shot, dump, one bar, quit).
Pure logic and the queue: `include/engine/tickpoke.hpp` (tests: `tests/test_autoplay.py`); example: `tests/boot/net_end_*.txt`
(`tests/boot/src/mknetpair.py` END plan).

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

## Faster boot runs (2026-10-11)

- **Warp**: `uaeshot.ps1 -Speed warp` (env `MS_EMU_SPEED=warp`, `integrate.py --boot-speed`) sets WinUAE's `warp=true`/`warp_limit=0`: `play_options.txt`
  takes 40-45 s instead of 126 s with the same 8 screens PASS. Autoplay frames are VBL counts, so frame-based waits and the shot freeze
  (400 VBLs) keep their meaning; wall-clock timeouts do not (netpair stays at real speed). `integrate.py --boot` uses warp by default and runs a
  script that fails again at real speed; `perf_*` scripts are always real. `-Speed max` = `cpu_speed=max` alone, not recommended.
- **Event-driven waits instead of fixed `frame` offsets**: the intro skip is `wait file mindscape max 900` + `frame 20 key SPACE tap` (the
  intro's first picture is open, so the skip is armed; ~140 frames instead of 300). The biggest fixed waits left in `tests/boot` (frames
  since the previous barrier): the tails of `perf_coop_bk` 10.7k, `perf_coop_lair` 10.5k, `play_end` 8.6k, `play_netmenu` 5.9k,
  `play_coop_fight4` 5.5k, `play_town_a` 5.2k, the `perf_lair*`/`perf_duel` 4.5k (those are measurements: fixed on purpose), `play_stonehenge` 4.4k,
  and in every `@PRE` script the three fixed gaps after the first menu press (500 frames to Select Knight, 300 to the name entry, 600 to
  the map; `wait input 10 max N` after each press would end them when the screen polls the stick). Convert them when a script is touched anyway.
- **One emulator at a time**: `uaeshot.ps1`, `integrate.py --boot` and `netpair.py` take the machine-wide lock (`tools/emulock.py`,
  `D:\Amiga\.emu.lock`); a second job waits and says who holds it.

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
| `play_end` | Stonehenge with the moonstones: the quest is completed, the ceremony, the whole ending, the main menu |
| `play_valley` | the Valley of the Gods without keys, then with keys: the guardian fight (autopilot), the moonstone, the win text |
| `play_lair` | a lair fight (ratmen), the creature loot screen, back to the map |
| `play_duel` | a duel with a black knight, the meeting screen |
| `play_2p` | two players: Players = 2, two Select Knight rounds, the second human's turn |
| `play_coop` | co-op for two (ROADMAP 8.2): main menu LOCAL, Players 2, Mode = Coop (down, right), a controller per knight (joystick 1; keys arrows picked with the cursor key and Ctrl), the map: the second knight moves with its keys, not with joy1 |
| `play_coop_valley` / `play_coop_dragon` | co-op boss fights (ROADMAP 8.4, emulator pass pending): two knights vs the valley guardian (knight 2 poked weak: the dump shows its lives -3 after the fight) and vs the dragon + bats; `fightdump 1` and `dump 1` log both knights' lives / hp (`play-cvalley-*`, `play-cdragon-*`) |
| `play_coop_town` | co-op loot and towns (ROADMAP 8.5): the party in town A with the shop queue (knight 1 on joystick 1, then knight 2's own town screen that only joystick 2 moves), then knight 2 as leader in a lair fight and the creature loot screen: one click on the gold pile gives both knights the pile (`dump` logs gold, keys, stones per knight and `cur=`) |
| `play_coop4` | co-op for four (ROADMAP 8.2 / 8.3 / 8.3a): main menu LOCAL, Players 4, Mode = Coop (down, right), four knights on the four controllers (joystick 1, joystick 2, keys arrows, keys WASD; a used controller cannot be taken), the map: the leader rotates through all four, each steers the one token with its own controller while the other three (both keyboard sets and both joysticks at once) are ignored, four status sheets each closed with its owner's fire |
| `play_netmenu` | the Multiplayer screen (ROADMAP 12.8a) and the lobby = knight screen (NETPLAY.md 4.2b): the main menu's MULTIPLAYER row, the name and address editors (keys), every join status, the fake host seating us and its start message, HOST GAME into the lobby with faked players, the host's pick (portrait, name, controller), START / Esc, then a normal game from LOCAL; shots `play-netmenu-*` (the `lobby-*` ones are new: no reference yet); writes `PROGDIR:net.ini` |
| `play_mainmenu` | the main menu (owner 2026-10-11: LOCAL / MULTIPLAYER / SETTINGS): up from LOCAL wraps to SETTINGS and down back, LOCAL opens the title menu, Esc and the Back row return to the main menu; shots `play-mainmenu-*` (new: no reference yet). `play_coop_mode` adds Players 3 + Mode Classic / Coop (`play-local-*`) |
| `play_death` | a lost fight with one life left, the status screen without lives, game over, the main menu |

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
