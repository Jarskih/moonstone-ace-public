# Moonstone lobby server and relay

One small Python 3 program (standard library only, Python 3.8 or newer, one TCP port and the same port number over UDP, no data files, no database) that
does two jobs for network play (`docs/NETPLAY.md` sections 5a and 5b, ROADMAP 12.13a / 12.17 / 12.19):

* **Lobby**: hosts that tick "Public" list their game; players browse the list (only games of their own build) and join.
  The host's address is handed to the joining player only, never shown in the list.
* **Relay**: a host that cannot open a port at its router is reached through this server instead; both sides connect *out*
  to the server, which forwards the game messages between them. The server never runs the game and sees nothing but input
  frames, hashes and player names.

Direct play by IP address (`moonstone JOIN <ip>`) needs no server at all; this is for the public game browser and for
players behind routers that cannot forward a port.

## What you need

* A machine that is always on, with a public IPv4 address (a small VPS, a home server with a port forward, a free cloud tier).
  Load is tiny: lockstep traffic is a few KB/s per game.
* Python 3.8+ (`python3 --version`). Nothing to install with pip.
* **One inbound TCP port** (default **6510**) open in the firewall and, on a cloud server, in the provider's security group, and **UDP 6510 as well**
  (protocol 6: the per-tick traffic of relayed games travels as UDP datagrams with redundant history; without UDP the games simply stay on TCP).
* A **DNS name** pointing at the machine (an `A` record, for example `lobby.example.org`). The game stores a host name, not an
  IP, so the server can move later without a new game build.

## Run it by hand (test)

From the repository (`moonstone-ace/`):

    py -m tools.netserver --port 6510            # Windows
    python3 -m tools.netserver --port 6510       # Linux

or from a copy of just two files (`server.py` and `netproto.py` in one directory):

    python3 server.py --port 6510

Options: `--port`, `--bind` (default all IPv4 addresses), `--max-listings`, `--max-sessions`, `--max-conns`, `--verbose`, `--http-port` / `--http-bind` (dashboard),
`--no-connect-back` (**tests only**: the connect-back always fails, so every game is listed via relay; `py tools/netpair.py --relay` starts a local server with it - never
use it on a public server).
Ctrl+C stops it. All other limits are in the `Config` class at the top of `server.py`.

## Install as a service (Linux, systemd)

    sudo install -d /opt/moonstone-netserver
    sudo install -m 644 tools/netserver/server.py tools/netserver/netproto.py tools/netserver/dashboard.py tools/netserver/dashboard.html /opt/moonstone-netserver/
    sudo install -m 644 tools/netserver/moonstone-netserver.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable --now moonstone-netserver
    systemctl status moonstone-netserver
    journalctl -u moonstone-netserver -f         # the log

The unit restarts the server if it ever dies and runs it as a throw-away user with no write access anywhere.
To change the port, edit `--port` in the unit file and the firewall rule, then `sudo systemctl daemon-reload && sudo systemctl restart moonstone-netserver`.

## Or as a container

    cd tools/netserver
    docker build -t moonstone-netserver .
    docker run -d --name moonstone-netserver --restart unless-stopped -p 6510:6510 -p 6510:6510/udp -p 8080:8080 moonstone-netserver

## Firewall

Open TCP 6510 and UDP 6510 (or your port) inbound, nothing else:

    sudo ufw allow 6510/tcp && sudo ufw allow 6510/udp        # Ubuntu / Debian with ufw
    sudo firewall-cmd --permanent --add-port=6510/tcp --add-port=6510/udp && sudo firewall-cmd --reload     # Fedora / CentOS

On a cloud server also add inbound rules for TCP 6510 and UDP 6510 in the provider's console. At home, forward both on the router
to the server. **The server also makes outgoing connections** (the connect-back test to the registering host's own game port);
default firewalls allow that.

## Check that it works

From any other machine (replace the host name; this asks for the game list of build `0` and prints the reply bytes):

    python3 - <<'PY'
    import socket, struct
    s = socket.create_connection(("lobby.example.org", 6510), timeout=5)
    s.sendall(struct.pack(">HBBH", 0x4D53, 6, 36, 4) + struct.pack(">I", 0))
    print(s.recv(100).hex())      # 4d530625000100 = a LIST_REPLY with 0 games
    PY

(or `python3 -m unittest tests.test_netserver` in the repository for the full test suite).

## What goes into the game

The game needs the **host name** of this server (and the port if it is not 6510):

* built-in default: decided for 12.18/12.19 (NETPLAY.md open question 8) -- tell the developers the final DNS name;
* players who run their own server put `lobby = your.host.name` (and `lobby_port = 6510` if different) into `net.ini`
  (ROADMAP 12.18: the host side reads them since protocol 3; the game browser screen, FIND GAME on the Multiplayer screen, reads the same two keys, and so does the
  relay: a host listed via relay keeps its idle connection and a client that joins through the relay connects to this same address and port).

## Web dashboard

The shipped service also serves a read-only dashboard at `http://SERVER:8080/`.
Open inbound TCP 8080 in Ubuntu (`sudo ufw allow 8080/tcp`) and the provider firewall.
It refreshes every five seconds and shows all registered games across builds, including full lobbies:
name, classic/co-op, player count, game version/build, direct/relay and open/full/playing status.
`GET /api/games` provides the same information as JSON. No host addresses, game ports or relay session IDs are exposed.

A game stays on the dashboard after it started, with the status **Playing** (protocol 3: the host's REFRESH carries a state byte,
0 waiting / 1 playing, and keeps being sent every 30 s during the game). A Playing game is not offered to joiners: it is left out of
`LIST_REPLY` and JOIN_REQ for it answers "full" - unless the JOIN_REQ carries the rejoin flag (protocol 5, below): then the host's address
(direct) or relay session is handed out, so a dropped player holding its token can come back. It disappears when the host unregisters (the session ended), disconnects or stops
refreshing (75 s). The "players in lobbies" figure counts waiting games only; `playing_games` counts the others.
The active relay session counter counts connected relay pipes. This is a lobby view, not a history of matches.

For manual launch add `--http-port 8080`; without it HTTP is disabled. Use `--http-bind 127.0.0.1`
when putting the dashboard behind a reverse proxy. The shipped service binds HTTP on all IPv4 interfaces.
No Python packages, database or separate web server are required.

**Protocol 3 (2026-10-10, ROADMAP 12.18): redeploy the server together with the game.** `REFRESH` changed from 1 to 2 bytes
(players, state), so a protocol 2 game and a protocol 3 server (or the reverse) refuse each other's messages: install the new
`netproto.py`, `server.py`, `dashboard.py` and `dashboard.html` (the unit file is unchanged).

To update an existing installation, upload all five files from `tools/netserver/`:
`server.py`, `netproto.py`, `dashboard.py`, `dashboard.html`, `moonstone-netserver.service`.
Then in the server's SSH terminal:

```sh
sudo install -m 644 ~/server.py ~/netproto.py ~/dashboard.py ~/dashboard.html /opt/moonstone-netserver/
sudo install -m 644 ~/moonstone-netserver.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl restart moonstone-netserver
sudo ufw allow 8080/tcp
curl http://127.0.0.1:8080/api/games
```

Restarting also disconnects running relayed games. Tests: `py -m unittest tests.test_netserver tests.test_netdashboard`.

## Behaviour and limits (so you know what is normal)

* A game is listed after the server could open a TCP connection back to the host's game port (3 s). If that fails the game
  is listed anyway and played through the relay. Listings need a refresh every 30 s; silent for 75 s and the listing goes.
* One listing per IP address, 500 listings and 200 relay sessions at most, game names only letters, digits, space, `_`, `.`.
* Per IP address: 20 new connections burst then 2 per second, 16 connections at once, 30 lobby messages burst then 5 per second.
  A relayed connection may send 300 messages per second. Malformed input, unknown messages or breaking a limit simply closes
  that one connection.
* A relayed game needs 1 + 3 connections from the host at most and 1 per client. Idle relayed connections are closed after
  120 s without traffic.
* **UDP relay** (protocol 6, docs/NETPLAY.md 2.2a): a UDP socket on the TCP port's number forwards the per-tick datagrams (INPUT_UDP client -> host, TICK_UDP host -> client)
  of relayed games.  A datagram carries the relay session id and the receiving client's token; the server learns the tokens from the CONFIG messages it forwards over the TCP pipes
  and drops datagrams of unknown sessions or tokens.  A member registers by sending (the server remembers its UDP source address for 60 s of silence, never logs or hands it out);
  a TICK_UDP counts as the host's only from the IP that registered the game; 3 clients per session; oversized, undecodable and other datagrams are dropped; 400 datagrams per second
  per source IP (burst 800).  If the UDP port cannot be opened the server says so once and carries on (the games stay on TCP); `--no-udp` switches it off.  The statistics line
  counts `udp_in`, `udp_forwarded`, `udp_dropped`, `udp_limited`.
* **Direct path (hole punching)** (docs/NETPLAY.md 2.2b): the same UDP socket also answers `PUNCH_REG` (type 23) with `PUNCH_INFO` (type 24).  A node of a relayed game registers the
  link (session, the client's token, its role, its own LAN address); when both ends of a link did, each is told the other's public address and port (as seen on the datagrams, so
  **the server now hands addresses to the players of a game**: the client learns the host's public IP and the host the clients') and, only when both come from the same public IP and the
  address is a private/link-local/loopback one, the other's LAN address.  A host's registration only counts from the IP that registered the game; tokens and sessions are checked as for
  the per-tick datagrams (the token must have passed a forwarded CONFIG); 3 links per session; registrations expire with the UDP registration (60 s).  Nothing is logged.  The statistics
  line counts `punch_reg` and `punch_info`.  `--no-punch` makes the server ignore `PUNCH_REG` (games then stay on the relay, exactly as with a server from before this feature).
  Old games (no `PUNCH_REG`) and old servers (they drop the unknown type) work together with new ones; the nodes just stay on the relay.
* **Logging**: start-up, errors and one statistics line every 10 minutes (connection, listing and session counts, relayed
  messages and bytes). No IP address is ever written to the log; the only address memory is the in-RAM rate limiter, which
  forgets an address when its allowance is full again.
* Everything is plain TCP and public by nature: nothing secret travels (names, game list, input frames). There is no login.

## Updating

Copy the new `server.py`, `netproto.py`, `dashboard.py` and `dashboard.html` over the installed ones and `sudo systemctl restart moonstone-netserver`.
A restart drops the list and any running relayed games (hosts re-register within 30 s, running games lose their relay).
Server and game must speak the same protocol version (`PROTO_VERSION` in `netproto.py`, `NET_PROTO_VERSION` in
`include/engine/netproto.hpp`); a game with another version is simply refused.  **Protocol 4** (the lobby = the knight screen, docs/NETPLAY.md 4.2b)
adds the game messages LOBBY_PICK (12) and LOBBY_STATE (13), which the relay forwards like the other game messages, and the portraits in CONFIG: a
server of protocol 3 must be replaced (copy `netproto.py` and `server.py`, restart) before protocol-4 games can list themselves or be relayed.
**Protocol 5** (rejoin, docs/NETPLAY.md 4.5, ROADMAP 12.15a) adds the game messages REJOIN (14) .. BOUNDARY (20) - the relay now forwards the
types 1..20 - a token in CONFIG, a flags byte in JOIN_REQ (bit 0 = "I rejoin") and three BYE reasons (no seat, bad token, rejoin failed).  The server
does not look at tokens (the host checks them); it only lets a rejoin-flagged JOIN_REQ through to a Playing game.  **The server must be redeployed with
the game** (copy `netproto.py` and `server.py`, restart): a protocol-4 server refuses every protocol-5 header.  `selftest.py` has a "playing game" leg for it.
**Protocol 6** (UDP input with history, docs/NETPLAY.md 2.2a, ROADMAP 12.21) adds the datagram types INPUT_UDP (21) and TICK_UDP (22) and the UDP relay described above; the TCP relay
forwards the types 1..20 as before.  The server now reads the token in each CONFIG it forwards (to know which datagram tokens are real).  **Redeploy the server with the game and
open UDP 6510** (copy `netproto.py` and `server.py`, restart, `sudo ufw allow 6510/udp`; a Docker container needs `-p 6510:6510/udp`): a protocol-5 server refuses every protocol-6
header.  `selftest.py` has a "UDP relay" leg (it reports a missing UDP path and the firewall hint).

## Deploying the direct-path version (2026-10-26)

The wire protocol version does not change (`NET_PROTO_VERSION` stays 6): a server of this version and games of any protocol-6 build work together in every combination (see
docs/NETPLAY.md 2.2b), so the server and the games can be updated in either order.  Only **new games on a new server** use the direct path.  To deploy, as for any update:

```sh
# from this repository, copy the two changed files (dashboard.* and the unit file are unchanged)
scp tools/netserver/server.py tools/netserver/netproto.py user@moonstone.shatteredallegiance.com:~/
# on the server
sudo install -m 644 ~/server.py ~/netproto.py /opt/moonstone-netserver/
sudo systemctl restart moonstone-netserver
journalctl -u moonstone-netserver -n 5            # "protocol 6, listening on ...:6510 (tcp + udp)"
py tools/netserver/selftest.py --server moonstone.shatteredallegiance.com --require-punch   # from any machine: leg 5 must say OK, not "skipped"
```

Nothing else changes: the same TCP and UDP 6510, no new port, no new package.  The restart drops running relayed games (hosts re-register within 30 s).  The firewall needs nothing new:
the direct datagrams never touch the server.  (A Docker container is rebuilt from the same two files.)

## Self-test

`py tools/netserver/selftest.py [--server HOST] [--port 6510]` plays a host and a client against a running server: a direct
game (register, connect-back, list, join, HELLO over the game connection), a relayed game (relay session, HELLO and INPUT
through the relay), the UDP relay (a relayed game's INPUT_UDP and TICK_UDP datagrams reach the other end unchanged, a datagram with an unknown token does not), a playing game
(rejoin flag), the direct path (PUNCH_REG from both ends of a relayed link is answered with PUNCH_INFO; nothing for one end alone, a wrong token or session; `--require-punch`
turns "an old server that ignores PUNCH_REG" from a skip into a failure) and a join with another build (refused with the host's version).  `--bind 127.0.0.1` keeps the test's own
sockets off the network when the server is local. Locally: start `py tools/netserver/server.py`
in one terminal, run the self-test in another. Against a remote server the direct test needs the server to reach this
machine's game port; otherwise it reports that the game was listed as relay.
