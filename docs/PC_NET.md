# The PC build as a network node (ROADMAP 12.14)

The PC build (`build-sdl/moonstone.exe`) plays the same lockstep network game as the Amiga: Host game, Join game, co-op or classic, UDP
with history, the state hash every 50 ticks.  The session logic is shared (`src/rt/net_session.cpp`, `net_lobby`, `net_relay`,
`net_rejoin`, `net_find`, `net_box` and the pure `src/engine/net*.cpp`); only the transport differs.

## How to host and join

```
moonstone.exe --host                       [--port 6502] [--name TEXT]      # pre-fills the Multiplayer screen; the game starts from it
moonstone.exe --join 192.168.1.20          [--port 6502] [--name TEXT]
```

`--host`, `--join ADDR`, `--port N`, `--name TEXT`, `--delay N` (input delay in ticks), `--timeout SEC`, `--tcp` (no UDP) are the
Amiga CLI template (`HOST/S,JOIN/K,PORT/K/N,NAME/K,DELAY/K/N,TIMEOUT/K/N,TCP/S`) spelled with dashes.  They only pre-fill the
Multiplayer screen (title menu, second row): choose HOST GAME there, or JOIN GAME and CONNECT.  `net.ini` (in the user directory,
`--pref DIR`, or next to the exe) works as on the Amiga: `name`, `address`, `port`, `public`, `udp`, `stats`.

* **Ports:** the host listens on **TCP 6502** (`--port`) and uses **UDP 6502** for the per-tick traffic; a joining machine uses TCP to
  that port and an ephemeral UDP port.  Forward / allow both protocols on that port for a game over a router.
* **Firewall:** the first time a host listens, Windows Defender Firewall asks whether `moonstone.exe` may accept connections; allow it
  for the network type you play on (private for a LAN).  Loopback games (two instances on one machine) need no rule.
* **Public listing:** `public = yes` (the default) registers the game with the public lobby server at host time; set `public = no` in
  `net.ini` for a private game (tests do this).  With a private game nothing leaves the machine except the traffic to the other players.
* A joiner and a host must run the same build / game data / rule options (the lobby refuses another build).  Two PC copies of one
  `moonstone.exe` agree; a PC against an Amiga does not yet (see "Open").

## Transport

`src/rt/net.hpp` is the seam: `NetTransport` (listen / accept / connect / send / recv / UDP / lobby, relay and browser connections), `OutConn`,
`Socket`, the window guards, `netClockMs`, `netPauseMs`.  The Amiga implements it over bsdsocket.library inside OS windows
(`src/rt/net.cpp`); the PC implements the same classes in `src/rt/sdl/net_winsock.cpp` over Winsock:

* `src/rt/sdl/winsock.{hpp,cpp}` is the only file that includes `<winsock2.h>` (its `LONG` / `ULONG` typedefs clash with ACE's);
  the rest sees plain functions over `int32_t`.
* There is no OS to give back, so `NetWindow` / `LeanNetWindow` are empty brackets and `window(fn)` just runs `fn` (the first call
  starts Winsock).  `netPauseMs` is `SDL_Delay`; the PC game is one thread, so the tick does not advance while it waits for a peer.
* **Byte order** is decided in the Winsock layer: addresses are host-order numbers (`127.0.0.1` = `0x7F000001`, as the Amiga's
  big-endian `s_addr` is, and as `JOIN_REPLY` and the browser's `netFormatIpv4` expect), ports are host-order numbers, and `htonl` /
  `htons` / `ntohl` / `ntohs` run inside `winsock.cpp` only.
* Windows differences handled there: a refused non-blocking `connect` is reported in the exception set of `select` (not as
  writable), would-block is `WSAEWOULDBLOCK`, a UDP `recvfrom` after an ICMP port-unreachable would fail with `WSAECONNRESET`
  (`SIO_UDP_CONNRESET` turns it off), and a second listener on the port must fail (`SO_EXCLUSIVEADDRUSE` instead of `SO_REUSEADDR`).

## Testing two PC instances on one machine

Each instance needs its own exe directory (the log `moonstone.log` is written next to the exe) and `--pref` directory; put
`public = no` in the pref's `net.ini`.  Compare the `net: hash tick N crc XXXXXXXX` lines of the two logs: equal CRCs at every
common tick means lockstep.  `tests/pc_net/pc_net.sh` runs a host and a joiner headless (SDL dummy drivers) and compares them.

## Relayed games and the direct path (docs/NETPLAY.md 2.2b)

A game joined through FIND GAME on a host that cannot open a port goes through the lobby server's relay.  After the lockstep starts, the nodes of each client-host link ask the relay
where the other end is (`PUNCH_REG` / `PUNCH_INFO`, UDP to the lobby port) and try a **direct UDP path** (hole punching, from the same UDP socket that talks to the relay, so the NAT
mapping matches; the LAN address is tried too and preferred on one LAN).  It works per link: in a 4-player game each of the three clients is direct or relayed on its own.  When it works the
per-tick traffic leaves the relay; if it never works (symmetric NAT, firewall) or dies later it stays on / goes back to the relayed UDP, which has its own fallback to TCP.  The log says:

```
net: udp: direct ok (node 1, 203.0.113.7:51234)
net: udp: direct failed, via relay (node 1, no answer from the peer)      # also: no answer from the relay | path lost
net: udp: session: sent N received M ... links active 1 tcp 0 direct 1
```

Nothing to configure.  `net.ini` (tests and troubleshooting): `direct = no` never tries it (the relay then learns nothing about the node); `direct_test = block` drops every received direct
datagram (the punch cannot succeed, so the link stays on the relay), `direct_test = kill` with `direct_kill_ms = N` drops them N ms after the link went direct (the path dies mid-game).
The direct path needs outgoing UDP only; the node's UDP socket on the PC is the one `src/rt/sdl/net_winsock.cpp` binds (`bindAddress()`: `MS_NET_BIND=loopback` keeps it, and the direct
probes, on 127.0.0.1 in tests; the "LAN address" a node reports is then 127.0.0.1 as well).

`tests/pc_net/pc_net_relay.py <outdir> --joiners N` runs a host and 1..3 joiners through a local `server.py` (`--no-connect-back`: the host's game is listed via relay, every joiner uses
FIND GAME), all on 127.0.0.1, compares the hash lines of all instances and checks the direct lines per link: `--ini j2:direct_test=block --expect j1:D,j2:R,j3:D` is a mixed session.

### Results of the relayed runs (2026-10-26, `build-sdl`, all instances on 127.0.0.1 through a local `server.py --no-connect-back`, dummy video / audio, real time)

| run | what | hash ticks compared | direct lines |
|---|---|---|---|
| 2 nodes, nothing special | host + 1 joiner via FIND GAME | 19 (tick 950) all equal | both ends `direct ok (node 1, 127.0.0.1:...)`, summary `direct 1` |
| 2 nodes, `direct_test = block` on the joiner | the punch cannot succeed | 19 all equal | both ends `direct failed, via relay (node 1, no answer from the peer)` after 5 s, `direct 0` |
| 2 nodes, `direct_test = kill`, `direct_kill_ms = 8000` | the path dies 8 s after it came up | 31 (tick 1550) all equal | both ends `direct ok`, then `direct failed, via relay (node 1, path lost)`; `covered 3 escalated 0` |
| 2 nodes, the joiner has `direct = no` (an old client) | the host registers, nobody answers | 19 all equal | host `direct failed, via relay (node 1, no answer from the relay)`, the joiner logs nothing |
| 2 nodes, the **old server** (`server.py` + `netproto.py` of commit 6cf6c92) | `PUNCH_REG` is dropped as an unknown type | 19 all equal | both ends `direct failed ... (no answer from the relay)` after 8 s |
| 3 nodes, fight scenario, joiner 2 `block` | mixed session: link 1 direct, link 2 relayed | 57 (tick 2850) all equal on 3 instances | node 1 `direct ok` on both ends, node 2 `direct failed (no answer from the peer)` on both |
| 4 nodes, fight scenario, joiner 2 `block`, joiner 3 `kill` after 9 s | link 1 direct, link 2 relayed, link 3 direct then lost | 65 (tick 3250) all equal on 4 instances | node 1 `direct ok`; node 2 `direct failed`; node 3 `direct ok` then `direct failed (path lost)` |

`tests/pc_net/pc_net_relay.py` prints this report and checks it (`--expect j1:D,j2:R,j3:L`).  A localhost run proves the logic, the lockstep and the fallbacks; it cannot show how a
real NAT treats the probes (there is none in between).

## Results (2026-10-25, build-sdl, two instances on localhost, real time, dummy video / audio)

| run | mode | what was played | hash ticks compared | result |
|---|---|---|---|---|
| menu -> lobby -> START, idle map | co-op | 2 knights, UDP probe ok | 19 (tick 950) | all equal |
| same | classic | 2 knights | 12 (tick 600) | all equal |
| `pc_net.sh ... coop 8000 fight` | co-op | map walk, end of turn, "Next Day", lair arena, joystick mash through the fight | 118 (tick 5900) | all equal, no `desync` line |
| `pc_net.sh ... classic 8000 ...` | classic | map, turn change, status sheets (no fight reached: the classic turn order needs its own route) | 122 (tick 6100) | all equal |

Bugs found and fixed on the way:

* **Frozen frame counter, game spinning at full speed** (`rt/sdl/timer.cpp timerWaitBeam`): the lockstep only waits with the raw beam wait, which
  only delivers frame boundaries from inside its own loop.  After a stall of more than one frame outside the pump (a peer that is slow, a busy
  machine) the beam target of the stale frame start was already in the past, the loop never ran, no boundary was ever delivered again and every
  wait returned at once: the tick counter ran at ~1500 / s, `--frames` never ended.  A wait that finds itself more than a frame behind now pumps
  (which re-bases the frame start) and aims again.
* **Byte order**: no hash difference showed up PC <-> PC (both are little-endian), which says nothing about PC <-> Amiga.  The transport keeps
  addresses and ports as host-order numbers (see above); the hash is over field values (`game/net_hash.cpp`), not raw memory.

## Open

* PC <-> Amiga (WinUAE with `bsdsocket_emu`) is not done: the lobby compares the build fingerprint (`exeFingerprint` hashes `PROGDIR:moonstone`,
  which differs per platform) and the state capture reads the PC's native cells; both need a look with an Amiga log of the same scripted game.
* The title menu's wipe (`rt/wipe.cpp wipeRows`) crashed twice with an access violation under CPU load (a tile-map word out of range): not a
  network bug, seen in a plain Settings / Multiplayer transition.
* The classic mode's fight route (turn order of the hot-seat) is not scripted yet.
