#!/usr/bin/env python3
"""server.py -- the Moonstone lobby server and relay (ROADMAP 12.17 + 12.13a + 12.19, docs/NETPLAY.md 5a / 5b).

One asyncio process, one TCP port (default 6510) and the same port number over UDP, Python 3.8+ standard library only.  It speaks the
game's binary wire format (tools/netserver/netproto.py mirrors include/engine/netproto.hpp):

  LOBBY   a host sends REGISTER (name, mode, players, build hash, game TCP port, game version).  The lobby takes the host's
          public IP from the connection, tries to connect back to ip:port and answers REGISTER_REPLY: listed direct (the
          game port is open), listed with a relay session (it is not), or refused.  The host sends REFRESH every 30 s
          (a listing silent for 75 s is dropped), with state "playing" once the game has started (the listing stays on the
          dashboard as "Playing" but is no longer offered to joiners), UNREGISTER when the game ends, or just closes the
          connection.
          A browser sends LIST_REQ(build hash) and gets the games of that build (no host address in the list), then
          JOIN_REQ(game id, my build, my version) and gets JOIN_REPLY: the host's address + port (direct, to that joiner
          only), or a relay session id (no host address handed out), or not found / full / build mismatch (with the
          host's version, so the player can read "host runs 1.3, you have 1.2").

  RELAY   a host that cannot be reached keeps ONE idle connection to this server, first message RELAY_HELLO(session, host);
          a client connects, first message RELAY_HELLO(session, client).  The server pairs the client with the host's idle
          connection and from then on forwards every whole game message (types 1..20, protocol 5 with the rejoin messages) between the two connections
          unchanged -- a star, one pipe per client; the host opens a fresh idle connection once data arrives on its idle
          one.  The relay does not interpret the messages (it only checks that each is a valid protocol message).

  UDP     (protocol 6, ROADMAP 12.21, NETPLAY.md 2.2) the per-tick traffic of a relayed game travels as datagrams with redundant history:
          INPUT_UDP (client -> host) and TICK_UDP (host -> client), each carrying the relay session id and the receiving client's rejoin
          token.  The server learns the tokens from the CONFIG messages it forwards over the TCP pipes (a datagram with a token that never
          passed through a pipe is dropped).  A member registers its UDP source address by sending: a client's INPUT_UDP registers the
          client (token -> address) and is forwarded to the host's registered address; a TICK_UDP from the host's IP registers the host and is
          forwarded to the client registered for its token.  Nothing is stored beyond those addresses (expired after udp_idle seconds),
          no address is ever logged or handed out, datagrams of unknown sessions / tokens / bad size / over the rate are dropped.

  PUNCH   (protocol 6 extension, NETPLAY.md 2.2b) relay-assisted direct connection.  After the lockstep starts, both ends of a relayed link send
          PUNCH_REG (session, the client's token, role, capability, their own LAN address) from the UDP socket that talks to this relay.  When both
          ends of a link have registered, the server answers each of them with PUNCH_INFO: the other end's public address and port as this server saw
          them (the source of its datagrams) and, when both ends come from the same public IP, the other end's private LAN address.  The nodes then
          probe each other directly (hole punching) and move the per-tick traffic to the direct path when it works; the relay path stays as the
          fallback.  An old node never sends PUNCH_REG, so it never gets (nor causes) a PUNCH_INFO.  An address is handed out only to the holder of the
          link's token, and only for a link whose other end registered; nothing is logged.

Abuse limits: one listing per IP, a name filter (the game font: letters, digits, space, '_' and '.'), per-IP limits for
connections (rate and number) and lobby messages, a rate limit per relay pipe, the protocol's maximum message size,
maximum listings / sessions / connections.  Malformed input closes the connection and nothing else.  Logging: start-up,
errors and a counts-only statistics line; no address is ever logged.

    py -m tools.netserver --port 6510          (from moonstone-ace/)
    python3 tools/netserver/server.py --port 6510
"""
import argparse
import asyncio
import collections
import ipaddress
import logging
import os
import secrets
import signal
import sys
import time

if __package__:
    from . import netproto as P
else:   # started as a script: python3 tools/netserver/server.py
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import netproto as P

log = logging.getLogger('netserver')

DEFAULT_PORT = 6510
MAX_CLIENTS = 3                 # relay pipes per session (a game has at most 4 nodes)
NAME_CHARS = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _.')   # the game font


class Config:
    """All limits in one place; the defaults suit a small public server.  Times are seconds of the injected clock."""

    def __init__(self, **kw):
        self.max_listings = 500
        self.max_sessions = 200
        self.max_conns = 2000
        self.max_conns_per_ip = 16          # a relayed 4-player game uses 5 (host: 1 idle + 3 pipes) + a browser
        self.listings_per_ip = 1
        self.conn_rate = 2.0                # new connections per second per IP (token bucket) ...
        self.conn_burst = 20                # ... with this burst
        self.msg_rate = 5.0                 # lobby messages per second per IP ...
        self.msg_burst = 30
        self.pipe_rate = 300.0              # relayed messages per second per pipe (a game sends ~25-50)
        self.pipe_burst = 600
        self.pipe_buffer_max = 256 * 1024   # bytes queued to a slow pipe end before it is dropped
        self.refresh_timeout = 75.0         # a listing not refreshed for this long is dropped
        self.first_msg_timeout = 15.0       # a connection that has said nothing yet
        self.lobby_idle = 120.0             # a lobby connection (browser) without a listing, silent
        self.host_idle_timeout = 600.0      # an idle relay host connection (it may send PING as keepalive)
        self.pipe_idle = 120.0              # a relayed connection with no traffic
        self.udp_rate = 400.0               # relayed datagrams per second per source IP (a client sends ~25-50 and repeats while it waits) ...
        self.udp_burst = 800                # ... with this burst
        self.udp_idle = 60.0                # a registered UDP address silent for this long is forgotten
        self.udp_enabled = True             # the UDP relay (tests may switch it off)
        self.punch_enabled = True           # answer PUNCH_REG with PUNCH_INFO (False: behave like a server from before the direct path)
        self.pair_wait = 5.0                # a relay client waits this long for the host's idle connection
        self.session_grace = 60.0           # a session nobody uses and no listing points at is removed after this
        self.session_max_age = 12 * 3600.0
        self.connect_back_timeout = 3.0
        self.max_connect_backs = 20         # concurrent
        self.stats_interval = 600.0
        self.sweep_interval = 5.0
        for k, v in kw.items():
            if not hasattr(self, k):
                raise TypeError('unknown option ' + k)
            setattr(self, k, v)


class Bucket:
    """Token bucket per key; the clock is injected."""

    def __init__(self, clock, rate, burst):
        self.clock, self.rate, self.burst = clock, rate, burst
        self.state = {}

    def allow(self, key, cost=1.0):
        now = self.clock()
        tokens, stamp = self.state.get(key, (float(self.burst), now))
        tokens = min(float(self.burst), tokens + (now - stamp) * self.rate)
        ok = tokens >= cost
        self.state[key] = (tokens - cost if ok else tokens, now)
        return ok

    def prune(self, keep):
        """Forget keys that are full again (their memory carries no information) unless keep(key)."""
        now = self.clock()
        for key, (tokens, stamp) in list(self.state.items()):
            if tokens + (now - stamp) * self.rate >= self.burst and not keep(key):
                del self.state[key]


def name_ok(name):
    """The game-font filter for a public game name: only letters, digits, space, '_' and '.', and something visible."""
    return bool(name.strip()) and all(c in NAME_CHARS for c in name)


class Listing:
    def __init__(self, game_id, ip, conn, msg, now):
        self.game_id, self.ip, self.conn = game_id, ip, conn
        self.name, self.mode, self.players, self.max_players = msg.name, msg.mode, msg.players, msg.max_players
        self.build, self.port, self.version = msg.build, msg.port, msg.version
        self.relay_session = 0
        self.pending = True            # connect-back in progress: not listed yet
        self.hidden = False            # full: not listed, JOIN answers "full"
        self.playing = False           # started (REFRESH state playing): still on the dashboard, not offered for joining
        self.last_refresh = now

    def wire(self):
        return P.listing(self.name, self.mode, self.players, self.max_players, self.build, self.port, self.version)


class Session:
    def __init__(self, sid, ip, now):
        self.sid, self.ip = sid, ip
        self.idle = collections.deque()    # the host's idle relay connections
        self.host_conns = 0                # idle + paired
        self.clients = 0                   # relay clients waiting or paired
        self.created = self.last_active = now
        self.event = asyncio.Event()       # set when an idle host connection arrives
        self.tokens = collections.deque(maxlen=2 * MAX_CLIENTS)   # rejoin tokens seen in CONFIGs forwarded host -> client: the secrets of the UDP path
        self.udp_host = None               # (address, last seen) of the host's UDP source
        self.udp_clients = {}              # token -> (address, last seen)
        self.punch_host = {}               # token -> (address, local (ip int, port), last seen): the host's PUNCH_REG for that client's link
        self.punch_client = {}             # token -> (address, local, last seen): the client's own PUNCH_REG


class Conn:
    def __init__(self, reader, writer, ip, now, bucket):
        self.reader, self.writer, self.ip = reader, writer, ip
        self.state = 'new'                 # new | lobby | host_idle | client_wait | pipe
        self.framer = P.Framer()
        self.created = self.last_seen = now
        self.listing = None
        self.session = None
        self.is_host = False
        self.peer = None
        self.bucket = bucket               # pipe rate (per connection)
        self.closed = False

    def send(self, data):
        if not self.closed:
            try:
                self.writer.write(data)
            except Exception:   # a transport that died under us: the read loop notices
                pass

    def close(self):
        if not self.closed:
            self.closed = True
            try:
                self.writer.close()
            except Exception:
                pass


class Stats:
    FIELDS = ('connections', 'refused_conns', 'rate_limited', 'malformed', 'registers', 'register_refused', 'direct', 'relayed_games',
              'lists', 'joins', 'pairs', 'relayed_msgs', 'relayed_bytes', 'expired', 'errors',
              'udp_in', 'udp_forwarded', 'udp_dropped', 'udp_limited', 'punch_reg', 'punch_info')

    def __init__(self):
        for f in self.FIELDS:
            setattr(self, f, 0)


class Server:
    def __init__(self, config=None, clock=time.monotonic, connect_back=None, rand32=None):
        self.cfg = config or Config()
        self.clock = clock
        self.connect_back = connect_back or self._tcp_connect_back
        self.rand32 = rand32 or (lambda: secrets.randbits(32))
        self.listings = collections.OrderedDict()   # game id -> Listing (registration order)
        self.by_ip = collections.defaultdict(list)  # ip -> its Listings
        self.sessions = {}
        self.conns = set()
        self.ip_conns = collections.Counter()
        self.conn_bucket = Bucket(clock, self.cfg.conn_rate, self.cfg.conn_burst)
        self.msg_bucket = Bucket(clock, self.cfg.msg_rate, self.cfg.msg_burst)
        self.udp_bucket = Bucket(clock, self.cfg.udp_rate, self.cfg.udp_burst)
        self.stats = Stats()
        self.server = None
        self.udp = None                              # the datagram transport (None = no UDP relay)
        self.udp_port = 0
        self._tasks = []
        self._cb_sem = None
        self._last_stats = clock()

    # ---- life cycle -----------------------------------------------------------------------------------------------------------

    async def start(self, host='0.0.0.0', port=DEFAULT_PORT):
        self._cb_sem = asyncio.Semaphore(self.cfg.max_connect_backs)
        self.server = await asyncio.start_server(self._handle, host, port)
        self._tasks.append(asyncio.ensure_future(self._sweeper()))
        tcp_port = self.server.sockets[0].getsockname()[1]
        if self.cfg.udp_enabled:
            try:   # the same port number over UDP; a port that is taken only costs the UDP relay (the games fall back to TCP, silently)
                transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(lambda: _UdpRelay(self), local_addr=(host, tcp_port))
                self.udp, self.udp_port = transport, tcp_port
            except OSError as e:
                log.warning('no UDP relay on port %d: %s', tcp_port, e)
        return tcp_port

    async def stop(self):
        for t in self._tasks:
            t.cancel()
        if self.server:
            self.server.close()
        if self.udp is not None:
            self.udp.close()
            self.udp = None
        for c in list(self.conns):
            c.close()
        await asyncio.sleep(0)   # let the handlers see their closed connections

    async def _sweeper(self):
        while True:
            await asyncio.sleep(self.cfg.sweep_interval)
            try:
                self.sweep()
            except Exception:
                self.stats.errors += 1
                log.exception('sweep failed')

    # ---- helpers --------------------------------------------------------------------------------------------------------------

    @staticmethod
    def _peer_ip(writer):
        peer = writer.get_extra_info('peername')
        if not peer:
            return None
        try:
            addr = ipaddress.ip_address(peer[0].split('%')[0])
        except ValueError:
            return None
        if addr.version == 6 and addr.ipv4_mapped:
            addr = addr.ipv4_mapped
        return str(addr) if addr.version == 4 else None   # the wire format carries IPv4 only

    async def _tcp_connect_back(self, ip, port):
        async with self._cb_sem:
            try:
                _, w = await asyncio.wait_for(asyncio.open_connection(ip, port), self.cfg.connect_back_timeout)
            except (OSError, asyncio.TimeoutError):
                return False
            w.close()
            return True

    def _new_id(self, taken):
        while True:
            v = self.rand32()
            if v and v not in taken:
                return v

    def _bye(self, conn, reason):
        conn.send(P.pack(P.Msg(P.BYE, reason=reason, version=0)))   # version 0: the server is not a game node
        return False

    # ---- connection handling --------------------------------------------------------------------------------------------------

    async def _handle(self, reader, writer):
        ip = self._peer_ip(writer)
        now = self.clock()
        if ip is None or len(self.conns) >= self.cfg.max_conns or self.ip_conns[ip] >= self.cfg.max_conns_per_ip:
            self.stats.refused_conns += 1
            writer.close()
            return
        if not self.conn_bucket.allow(ip):
            self.stats.rate_limited += 1
            writer.close()
            return
        self.stats.connections += 1
        conn = Conn(reader, writer, ip, now, Bucket(self.clock, self.cfg.pipe_rate, self.cfg.pipe_burst))
        self.conns.add(conn)
        self.ip_conns[ip] += 1
        try:
            while not conn.closed:
                data = await reader.read(4096)
                if not data:
                    break
                conn.last_seen = self.clock()
                conn.framer.push(data)
                while not conn.closed:
                    st, msg, raw = conn.framer.next()
                    if st == P.MORE:
                        break
                    if st != P.OK:
                        self.stats.malformed += 1
                        conn.close()
                        break
                    if not await self._on_message(conn, msg, raw):
                        conn.close()
                        break
        except (OSError, asyncio.IncompleteReadError):
            pass
        except asyncio.CancelledError:
            raise
        except Exception:
            self.stats.errors += 1
            log.exception('connection handler failed')
        finally:
            self._cleanup(conn)

    def _cleanup(self, conn):
        conn.close()
        self.conns.discard(conn)
        self.ip_conns[conn.ip] -= 1
        if self.ip_conns[conn.ip] <= 0:
            del self.ip_conns[conn.ip]
        if conn.listing is not None:
            self._drop_listing(conn.listing)
        sess = conn.session
        if sess is not None:
            sess.last_active = self.clock()
            if conn.is_host:
                sess.host_conns -= 1
                try:
                    sess.idle.remove(conn)
                except ValueError:
                    pass
            else:
                sess.clients -= 1
        if conn.peer is not None:
            peer, conn.peer = conn.peer, None
            peer.peer = None
            peer.close()

    def _drop_listing(self, lst):
        self.listings.pop(lst.game_id, None)
        sess = self.sessions.get(lst.relay_session)
        if sess is not None:
            sess.last_active = self.clock()    # the grace period for the host's relay connection starts when the listing goes
        if lst in self.by_ip.get(lst.ip, ()):
            self.by_ip[lst.ip].remove(lst)
            if not self.by_ip[lst.ip]:
                del self.by_ip[lst.ip]
        if lst.conn is not None and lst.conn.listing is lst:
            lst.conn.listing = None

    # ---- messages -------------------------------------------------------------------------------------------------------------

    async def _on_message(self, conn, msg, raw):
        """-> False to close the connection (after any reply already queued)."""
        t = msg.type
        if conn.state == 'pipe':
            return self._forward(conn, t, raw, msg)
        if conn.state == 'host_idle':
            if t == P.PING and self.msg_bucket.allow(conn.ip):   # keepalive of an idle relay connection
                conn.send(P.pack(P.Msg(P.PONG, token=msg.token)))
                return True
            return False
        if conn.state == 'client_wait':
            return False
        if not self.msg_bucket.allow(conn.ip):
            self.stats.rate_limited += 1
            return False
        if t == P.RELAY_HELLO:
            return await self._relay_hello(conn, msg) if conn.state == 'new' else False
        conn.state = 'lobby'
        if t == P.REGISTER:
            return await self._register(conn, msg)
        if t == P.REFRESH:
            return self._refresh(conn, msg)
        if t == P.UNREGISTER:
            if conn.listing is not None:
                self._drop_listing(conn.listing)
            return True
        if t == P.LIST_REQ:
            return self._list(conn, msg)
        if t == P.JOIN_REQ:
            return self._join(conn, msg)
        return False   # game messages and server replies have no business on a lobby connection

    def _reply_register(self, conn, result, game_id=0, session=0):
        conn.send(P.pack(P.Msg(P.REGISTER_REPLY, result=result, game_id=game_id, session=session)))
        return True

    async def _register(self, conn, msg):
        self.stats.registers += 1
        if msg.port == 0:
            return False
        if not name_ok(msg.name):
            self.stats.register_refused += 1
            return self._reply_register(conn, P.REG_BAD_NAME)
        if conn.listing is not None or len(self.by_ip.get(conn.ip, ())) >= self.cfg.listings_per_ip:
            self.stats.register_refused += 1
            return self._reply_register(conn, P.REG_LISTED)
        if len(self.listings) >= self.cfg.max_listings:
            self.stats.register_refused += 1
            return self._reply_register(conn, P.REG_FULL)
        lst = Listing(self._new_id(self.listings), conn.ip, conn, msg, self.clock())
        self.listings[lst.game_id] = lst          # reserved while the connect-back runs: a second REGISTER sees it
        self.by_ip[lst.ip].append(lst)
        conn.listing = lst
        reachable = await self.connect_back(conn.ip, msg.port)
        if conn.closed or conn.listing is not lst:
            self._drop_listing(lst)
            return False
        if reachable:
            self.stats.direct += 1
        else:
            if len(self.sessions) >= self.cfg.max_sessions:
                self._drop_listing(lst)
                self.stats.register_refused += 1
                return self._reply_register(conn, P.REG_FULL)
            sess = Session(self._new_id(self.sessions), conn.ip, self.clock())
            self.sessions[sess.sid] = sess
            lst.relay_session = sess.sid
            self.stats.relayed_games += 1
        lst.pending = False
        lst.last_refresh = self.clock()
        lst.hidden = lst.players >= lst.max_players
        return self._reply_register(conn, P.REG_RELAY if lst.relay_session else P.REG_DIRECT, lst.game_id, lst.relay_session)

    def _refresh(self, conn, msg):
        lst = conn.listing
        if lst is None or lst.pending:
            return False
        lst.last_refresh = self.clock()
        lst.players = min(msg.players, 4)
        if msg.state == P.GAME_PLAYING:
            lst.playing = True                        # sticky: a started game does not go back to waiting
        lst.hidden = lst.players >= lst.max_players   # full: leaves the list; fewer players again: back on it
        return True

    def _list(self, conn, msg):
        self.stats.lists += 1
        entries = []
        for lst in reversed(self.listings.values()):   # newest first
            if not lst.pending and not lst.hidden and not lst.playing and lst.build == msg.build:
                entries.append((lst.game_id, lst.wire()))
                if len(entries) == P.LIST_MAX:
                    break
        conn.send(P.pack(P.Msg(P.LIST_REPLY, entries=entries)))
        return True

    def _join(self, conn, msg):
        self.stats.joins += 1
        lst = self.listings.get(msg.game_id)
        result, addr, port, session, version = P.JOIN_NOT_FOUND, 0, 0, 0, 0
        if lst is not None and not lst.pending:
            version = lst.version
            is_rejoin = lst.playing and bool(msg.flags & P.JOIN_FLAG_REJOIN)   # a started game: a player who holds its rejoin token may come back
            if lst.build != msg.build:
                result = P.JOIN_BUILD
            elif (lst.hidden or lst.playing) and not is_rejoin:   # full, or the game has already started: no seat to offer (the host checks the token itself)
                result = P.JOIN_FULL
            elif lst.relay_session:
                result, session = P.JOIN_RELAY, lst.relay_session    # address 0 / port 0: "the server you are talking to"
            else:
                result, addr, port = P.JOIN_DIRECT, int(ipaddress.IPv4Address(lst.ip)), lst.port
        conn.send(P.pack(P.Msg(P.JOIN_REPLY, result=result, addr=addr, port=port, session=session, host_version=version)))
        return True

    # ---- relay ----------------------------------------------------------------------------------------------------------------

    async def _relay_hello(self, conn, msg):
        sess = self.sessions.get(msg.session)
        if sess is None:
            return self._bye(conn, P.BYE_REFUSED)
        if msg.role == P.ROLE_HOST:
            if conn.ip != sess.ip:
                return self._bye(conn, P.BYE_REFUSED)
            if sess.host_conns >= MAX_CLIENTS + 2:
                return self._bye(conn, P.BYE_FULL)
            conn.state, conn.session, conn.is_host = 'host_idle', sess, True
            sess.host_conns += 1
            sess.idle.append(conn)
            sess.last_active = self.clock()
            sess.event.set()
            return True
        if sess.clients >= MAX_CLIENTS:
            return self._bye(conn, P.BYE_FULL)
        conn.state, conn.session = 'client_wait', sess
        sess.clients += 1
        deadline = asyncio.get_running_loop().time() + self.cfg.pair_wait
        host = None
        while host is None and not conn.closed:
            while sess.idle and host is None:
                cand = sess.idle.popleft()
                if not cand.closed:
                    host = cand
            if host is not None:
                break
            sess.event.clear()
            left = deadline - asyncio.get_running_loop().time()
            if left <= 0:
                break
            try:
                await asyncio.wait_for(sess.event.wait(), left)
            except asyncio.TimeoutError:
                break
        if conn.closed:
            if host is not None:
                sess.idle.appendleft(host)
            return False
        if host is None:
            return self._bye(conn, P.BYE_REFUSED)     # no host connection showed up in time
        conn.peer, host.peer = host, conn
        conn.state = host.state = 'pipe'
        self.stats.pairs += 1
        return True

    def _forward(self, conn, t, raw, msg=None):
        peer = conn.peer
        if t not in P.GAME_TYPES or peer is None or peer.closed or not conn.bucket.allow(1):
            if t in P.GAME_TYPES:
                self.stats.rate_limited += 1
            return False
        if peer.writer.transport.get_write_buffer_size() > self.cfg.pipe_buffer_max:
            return False
        if t == P.CONFIG and conn.is_host and conn.session is not None and msg is not None and msg.token \
                and msg.token not in conn.session.tokens:
            conn.session.tokens.append(msg.token)       # the secret of the datagram path of that client (protocol 6)
        peer.send(raw)
        peer.last_seen = conn.last_seen
        self.stats.relayed_msgs += 1
        self.stats.relayed_bytes += len(raw)
        return True

    # ---- UDP relay (protocol 6) -----------------------------------------------------------------------------------------------

    def on_datagram(self, data, addr):
        """One datagram from addr = (ip, port).  Dropped silently unless it is a valid INPUT_UDP / TICK_UDP of a known relay session whose
        token the server saw in a forwarded CONFIG; then it registers the sender and is forwarded to the other end."""
        st = self.stats
        st.udp_in += 1
        if self.udp is None or len(data) > P.MSG_MAX or len(data) < P.HEADER_BYTES:
            st.udp_dropped += 1
            return
        ip = addr[0]
        if not self.udp_bucket.allow(ip):
            st.udp_limited += 1
            return
        status, msg, used = P.decode(data)
        if status != P.OK or used != len(data) or msg.type not in P.UDP_TYPES + (P.PUNCH_REG,):
            st.udp_dropped += 1
            return
        sess = self.sessions.get(msg.session)
        if sess is None or msg.token not in sess.tokens:
            st.udp_dropped += 1                          # unknown session (a direct game's datagram has no business here), or a token nobody was given
            return
        now = self.clock()
        if msg.type == P.PUNCH_REG:
            self._punch_reg(sess, msg, addr, now)
            return
        if msg.type == P.INPUT_UDP:                      # a client: register it, hand the datagram to the host
            if msg.token not in sess.udp_clients and len(sess.udp_clients) >= MAX_CLIENTS:
                st.udp_dropped += 1
                return
            sess.udp_clients[msg.token] = (addr, now)
            target = sess.udp_host
        else:                                            # the host: only from the address that registered the game
            if ip != sess.ip:
                st.udp_dropped += 1
                return
            sess.udp_host = (addr, now)
            target = sess.udp_clients.get(msg.token)
        if target is None:
            return                                       # the other end has not shown up yet: its probe will register it
        self.udp.sendto(data, target[0])
        st.udp_forwarded += 1

    # ---- relay-assisted direct path (PUNCH_REG / PUNCH_INFO) --------------------------------------------------------------------

    @staticmethod
    def _ip_int(ip):
        try:
            a = ipaddress.ip_address(ip.split('%')[0])
        except ValueError:
            return None
        if a.version == 6 and a.ipv4_mapped:
            a = a.ipv4_mapped
        return int(a) if a.version == 4 else None

    @staticmethod
    def _is_lan(ip_int):
        """A node may only name a private (RFC 1918), link-local or loopback address as its local one: the peer sends probes there, so it must never be
        an arbitrary internet address."""
        a = ipaddress.IPv4Address(ip_int)
        return a.is_private or a.is_link_local or a.is_loopback

    def _punch_reg(self, sess, msg, addr, now):
        """A node says it can try a direct path for the link named by the token.  Registers its address (the source of this datagram, which is the
        mapping its peer will have to hit) and, when the other end of the link has registered too, tells both ends where the other one is."""
        st = self.stats
        st.punch_reg += 1
        ip_int = self._ip_int(addr[0])
        if not self.cfg.punch_enabled or ip_int is None or not msg.caps & P.PUNCH_CAP_DIRECT:
            return
        is_host = msg.role == P.ROLE_HOST
        if is_host and addr[0] != sess.ip:               # as for TICK_UDP: only the address that registered the game speaks for the host
            st.udp_dropped += 1
            return
        mine, theirs = (sess.punch_host, sess.punch_client) if is_host else (sess.punch_client, sess.punch_host)
        if msg.token not in mine and len(mine) >= MAX_CLIENTS:
            st.udp_dropped += 1
            return
        mine[msg.token] = (addr, (msg.laddr, msg.lport), now)
        if is_host:                                      # a registered address is a forwarding target too: the datagrams that follow find each other earlier
            sess.udp_host = (addr, now)
        elif msg.token in sess.udp_clients or len(sess.udp_clients) < MAX_CLIENTS:
            sess.udp_clients[msg.token] = (addr, now)
        other = theirs.get(msg.token)
        if other is None or now - other[2] > self.cfg.udp_idle:
            return                                       # the other end has not registered (yet, or it is an old node): its PUNCH_REG will trigger the answer
        self._send_info(sess, msg.token, addr, (msg.laddr, msg.lport), other[0], other[1])
        self._send_info(sess, msg.token, other[0], other[1], addr, (msg.laddr, msg.lport))

    def _send_info(self, sess, token, to_addr, to_local, peer_addr, peer_local):
        """PUNCH_INFO to to_addr: the peer's public address, and its LAN address when both come from the same public IP and it is a LAN address."""
        pub, to_pub = self._ip_int(peer_addr[0]), self._ip_int(to_addr[0])
        if pub is None or self.udp is None:
            return
        flags, laddr, lport = 0, 0, 0
        if pub == to_pub and peer_local[0] and peer_local[1] and self._is_lan(peer_local[0]):
            flags, laddr, lport = P.PUNCH_INFO_LOCAL, peer_local[0], peer_local[1]
        self.udp.sendto(P.pack(P.Msg(P.PUNCH_INFO, session=sess.sid, token=token, flags=flags, paddr=pub, pport=peer_addr[1], laddr=laddr, lport=lport)), to_addr)
        self.stats.punch_info += 1

    # ---- time based clean-up --------------------------------------------------------------------------------------------------

    def sweep(self):
        """Drops what has gone silent.  Called every sweep_interval by the server; tests call it after moving the clock."""
        now, cfg = self.clock(), self.cfg
        for lst in list(self.listings.values()):
            if now - lst.last_refresh > cfg.refresh_timeout:
                self.stats.expired += 1
                conn = lst.conn
                self._drop_listing(lst)
                if conn is not None:
                    conn.close()
        for conn in list(self.conns):
            idle = now - conn.last_seen
            limit = {'new': cfg.first_msg_timeout, 'lobby': cfg.lobby_idle, 'host_idle': cfg.host_idle_timeout,
                     'client_wait': cfg.pair_wait * 4, 'pipe': cfg.pipe_idle}[conn.state]
            if conn.listing is not None:
                continue   # a registered host is governed by its refresh time
            if idle > limit:
                self.stats.expired += 1
                conn.close()
        live = {lst.relay_session for lst in self.listings.values() if lst.relay_session}
        for sess in list(self.sessions.values()):
            idle = sess.host_conns == 0 and sess.clients == 0 and sess.sid not in live and now - sess.last_active > cfg.session_grace
            if idle or now - sess.created > cfg.session_max_age:
                del self.sessions[sess.sid]
                for c in list(sess.idle):
                    c.close()
        for sess in self.sessions.values():              # UDP addresses nobody has used for a while
            if sess.udp_host is not None and now - sess.udp_host[1] > cfg.udp_idle:
                sess.udp_host = None
            for token in [t for t, (_, seen) in sess.udp_clients.items() if now - seen > cfg.udp_idle]:
                del sess.udp_clients[token]
            for table in (sess.punch_host, sess.punch_client):
                for token in [t for t, (_, _, seen) in table.items() if now - seen > cfg.udp_idle]:
                    del table[token]
        self.conn_bucket.prune(lambda ip: ip in self.ip_conns)
        self.msg_bucket.prune(lambda ip: ip in self.ip_conns)
        self.udp_bucket.prune(lambda ip: False)
        if now - self._last_stats >= cfg.stats_interval:
            self._last_stats = now
            log.info('stats: %s', self.stats_line())

    def stats_line(self):
        s = self.stats
        return 'conns=%d listings=%d sessions=%d ' % (len(self.conns), len(self.listings), len(self.sessions)) + \
            ' '.join('%s=%d' % (f, getattr(s, f)) for f in Stats.FIELDS)


class _UdpRelay(asyncio.DatagramProtocol):
    """The datagram socket on the TCP port's number: every datagram goes to Server.on_datagram."""

    def __init__(self, server):
        self.server = server

    def datagram_received(self, data, addr):
        try:
            self.server.on_datagram(data, addr)
        except Exception:
            self.server.stats.errors += 1
            log.exception('udp handler failed')

    def error_received(self, exc):   # an ICMP error for a datagram we sent: the member is gone, nothing to do
        pass


def main(argv=None):
    ap = argparse.ArgumentParser(description='Moonstone lobby server + relay (one TCP port, the same port number over UDP)')
    ap.add_argument('--port', type=int, default=DEFAULT_PORT, help='TCP port (default %d)' % DEFAULT_PORT)
    ap.add_argument('--bind', default='0.0.0.0', help='address to listen on (default all IPv4)')
    ap.add_argument('--max-listings', type=int, default=None)
    ap.add_argument('--max-sessions', type=int, default=None)
    ap.add_argument('--max-conns', type=int, default=None)
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--refresh-timeout', type=float, default=None,
                    help='seconds without a REFRESH before a listing is dropped (default 75; tests whose emulated clock runs slower than real time raise it)')
    ap.add_argument('--no-udp', action='store_true', help='do not open the UDP relay (games fall back to TCP, silently)')
    ap.add_argument('--no-punch', action='store_true', help='do not help the nodes of a relayed game to connect directly (PUNCH_REG is ignored: no addresses are handed out)')
    ap.add_argument('--no-connect-back', action='store_true',
                    help='test setups: the connect-back test always fails, so every game is listed via relay (tools/netpair.py --relay)')
    ap.add_argument('--http-port', type=int, default=0, help='read-only dashboard port (0 disables it)')
    ap.add_argument('--http-bind', default='0.0.0.0', help='dashboard listen address')
    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
    opts = {k: getattr(args, k) for k in ('max_listings', 'max_sessions', 'max_conns', 'refresh_timeout') if getattr(args, k) is not None}

    async def never_reachable(ip, port):
        return False

    if args.no_udp:
        opts['udp_enabled'] = False
    if args.no_punch:
        opts['punch_enabled'] = False
    srv = Server(Config(**opts), connect_back=never_reachable if args.no_connect_back else None)

    async def run():
        port = await srv.start(args.bind, args.port)
        log.info('moonstone netserver: protocol %d, listening on %s:%d (tcp%s)', P.PROTO_VERSION, args.bind, port, ' + udp' if srv.udp is not None else ' only')
        dashboard = None
        if args.http_port:
            if __package__:
                from .dashboard import Dashboard
            else:
                from dashboard import Dashboard
            dashboard = Dashboard(srv)
            http_port = await dashboard.start(args.http_bind, args.http_port)
            log.info('dashboard listening on %s:%d', args.http_bind, http_port)
        stop = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                loop.add_signal_handler(sig, stop.set)
            except (NotImplementedError, RuntimeError, ValueError):
                pass   # Windows: Ctrl+C raises KeyboardInterrupt below
        await stop.wait()
        if dashboard:
            await dashboard.stop()
        await srv.stop()

    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass
    log.info('stopped: %s', srv.stats_line())
    return 0


if __name__ == '__main__':
    sys.exit(main())
