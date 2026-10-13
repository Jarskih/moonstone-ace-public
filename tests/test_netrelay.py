"""Host tests of the relay and the game browser (ROADMAP 12.13a / 12.18): engine/netrelay.cpp (the host's idle relay connection: connect, RELAY_HELLO, PING,
PONGs, hand-over of a paired client with its first bytes, refusals, losses, no free link, session changes), engine/netbrowse.cpp (the joining player's LIST_REQ /
JOIN_REQ against the lobby), the byte pushback, and a host NetNode that seats players that arrive through the relay mixed with direct links - plus the
lobby keepalive of engine/netnode.cpp that keeps a quiet relayed pipe alive.

tests/netrelay_driver.cpp is compiled with clang++ (-fno-exceptions -fno-rtti) and runs two ways:
  * self test: the machines over fake connections (partial sends, one byte reads, every refusal, timeouts, retries, the clock wrap, a full host node);
  * bridge mode (end to end): the SAME machines, driven by this test, which owns real TCP connections to the real tools/netserver Server (fake clock, the
    connect-back always fails): a host that is listed via relay opens its idle connection, two clients FIND the game (LIST_REQ), JOIN it (JOIN_REPLY relay),
    connect to the lobby server with RELAY_HELLO, are seated, pick their knights, the host starts the game and 60 lockstep ticks pass through the relay with
    equal frames on every node; a direct listing keeps the relay machine off; a re-listing (mode change) moves the idle connection to the new session.
The mutation test proves the self test has teeth.
"""
import asyncio
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
CXX = shutil.which('clang++')

from tests.test_netserver import ServerCase, BUILD, V13   # noqa: E402
from tools.netserver import netproto as P                  # noqa: E402

DRIVER = 'tests/netrelay_driver.cpp'
SOURCES = ['src/engine/netrelay.cpp', 'src/engine/netbrowse.cpp', 'src/engine/netlobby.cpp', 'src/engine/netnode.cpp', 'src/engine/netudp.cpp', 'src/engine/netstats.cpp', 'src/engine/netroom.cpp',
           'src/engine/netplay.cpp', 'src/engine/netproto.cpp', 'src/engine/pad.cpp', 'src/engine/input.cpp', 'src/engine/netrejoin.cpp']

# NetLobbyStatus / NetRelayPhase / NetBrowseStatus / NetSeatState
NLS_LISTED, NLS_LISTED_RELAY = 2, 3
NR_OFF, NR_START, NR_CONNECTING, NR_HELLO, NR_IDLE, NR_RETRY = range(6)
NBS_IDLE, NBS_CONTACTING, NBS_LISTED, NBS_JOINED = range(4)
SEAT_FREE, SEAT_BROWSE, SEAT_CLAIM, SEAT_DONE = range(4)


def build(out_dir, overrides=None):
    exe = os.path.join(out_dir, 'netrelay.exe')
    cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-Wno-missing-field-initializers',
           '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), '-o', exe, os.path.join(ROOT, DRIVER)]
    for s in SOURCES:
        if overrides and s in overrides:
            p = os.path.join(out_dir, s.replace('/', '_'))
            with open(p, 'w', encoding='utf-8') as fh:
                fh.write(overrides[s])
            cmd.append(p)
        else:
            cmd.append(os.path.join(ROOT, s))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return exe


@unittest.skipUnless(CXX, 'needs clang++')
class SelfTest(unittest.TestCase):
    def test_machines_over_fake_connections(self):
        tmp = tempfile.mkdtemp(prefix='netrelay_')
        try:
            r = subprocess.run([build(tmp)], capture_output=True, text=True, timeout=300)
            self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
            self.assertIn('OK ', r.stdout)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_mutations_are_caught(self):
        def src(path):
            with open(os.path.join(ROOT, path), encoding='utf-8') as fh:
                return fh.read()
        mutations = [
            # the relay machine hands a PONG over as if it were a client
            ('src/engine/netrelay.cpp', 'if(msg.ubType == NET_MSG_PONG) {', 'if(false) {'),
            # the server's refusal is taken for a client
            ('src/engine/netrelay.cpp', 'if(msg.ubType == NET_MSG_BYE && msg.u.bye.uwVersion == 0) {', 'if(false) {'),
            # a prefix that could be the server's PONG is handed over
            ('src/engine/netrelay.cpp', 'isPartialServerMsg = ulLeft <= 3 || ubType == NET_MSG_PONG || ubType == NET_MSG_BYE;', 'isPartialServerMsg = ubType == 255;'),
            # the bytes read so far are lost at the hand-over
            ('src/engine/netrelay.cpp', 'const int32_t lLink = io.pfnHandOver(io.pCtx, m.aubIn, m.ubInFill);', 'const int32_t lLink = io.pfnHandOver(io.pCtx, m.aubIn, 0);'),
            # no fresh idle connection after a client
            ('src/engine/netrelay.cpp', 'm.ubPhase = NR_START;                               // the connection is the link\'s now', 'm.ubPhase = NR_OFF;                               // the connection is the link\'s now'),
            # no keepalive PING
            ('src/engine/netrelay.cpp', 'isDue(ulNow, m.ulLastSend + NR_PING_MS)', 'isDue(ulNow, m.ulLastSend + NR_PING_MS * 100)'),
            # a new session does not move the idle connection
            ('src/engine/netrelay.cpp', 'if(ulSession == m.ulSession && (ulSession == 0 || m.ubPhase != NR_OFF)) {', 'if(true) {'),
            # RELAY_HELLO says client instead of host
            ('src/engine/netrelay.cpp', 'msg.u.relayHello.ubRole = NET_ROLE_HOST;', 'msg.u.relayHello.ubRole = NET_ROLE_CLIENT;'),
            # a lost idle connection is not noticed
            ('src/engine/netrelay.cpp', 'if(eRead == RD_LOST) {', 'if(false) {'),
            # the pushback gives the same bytes twice
            ('src/engine/netrelay.cpp', 'p.ubFill = static_cast<uint8_t>(p.ubFill - ulN);', 'p.ubFill = p.ubFill;'),
            # JOIN_REQ names the first game whatever the player picked
            ('src/engine/netbrowse.cpp', 'b.ulJoinGame = b.aEntries[ubIndex].ulGameId;', 'b.ulJoinGame = b.aEntries[0].ulGameId;'),
            # a failed request keeps a stale list
            ('src/engine/netbrowse.cpp', 'if(b.ubCount != 0) {\n\t\tb.ubCount = 0;', 'if(false) {\n\t\tb.ubCount = 0;'),
            # the join reply is not kept
            ('src/engine/netbrowse.cpp', 'b.join = m.u.joinReply;', ''),
            # the address is formatted back to front
            ('src/engine/netbrowse.cpp', 'for(int8_t i = 3; i >= 0; --i) {', 'for(int8_t i = 0; i <= 3; ++i) {'),
            # no stack is reported as a missing server
            ('src/engine/netbrowse.cpp', 'fail(b, io, lRc == NB_BEGIN_NO_STACK ? NBS_NO_STACK : NBS_NO_SERVER);', 'fail(b, io, NBS_NO_SERVER);'),
            # a quiet lobby does not ping (the relay would close the pipe)
            ('src/engine/netnode.cpp', 'if(n.ePhase == NN_LOBBY && ulNow - n.ulLastPing >= NN_LOBBY_KEEPALIVE_MS) {', 'if(false) {'),
        ]
        for path, old, new in mutations:
            text = src(path)
            self.assertIn(old, text, 'mutation target vanished: ' + old)
            tmp = tempfile.mkdtemp(prefix='netrelay_mut_')
            try:
                exe = build(tmp, {path: text.replace(old, new, 1)})
                r = subprocess.run([exe], capture_output=True, text=True, timeout=300)
                self.assertNotEqual(r.returncode, 0, 'mutation survived: ' + new)
            finally:
                shutil.rmtree(tmp, ignore_errors=True)


class RejoinListenSource(unittest.TestCase):
    """The transport is Amiga-only (bsdsocket), so the rule that cost the relay rejoin is pinned in the source: a game in which a seat is away listens
    AGAIN without closing the lobby connection (the listing would be re-registered with a new relay session that the dropped player does not know), the
    relay's idle connection or the links of the players who stay."""

    def read(self, path):
        with open(os.path.join(ROOT, path), encoding='utf-8') as fh:
            return fh.read()

    def test_the_rejoin_listener_does_not_reset_the_other_sockets(self):
        rejoin = self.read('src/rt/net_rejoin.cpp')
        self.assertIn('.listenAgain(', rejoin)
        self.assertNotIn('.hostListen(', rejoin)
        net = self.read('src/rt/net.cpp')
        start = net.index('bool NetTransport::listenAgain(')
        body = net[start:net.index('}', start)]
        for forbidden in ('closeStreams', 'closeAll', 'm_lobby', 'm_relay', 'm_aConn'):
            self.assertNotIn(forbidden, body)


class Conn:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer
        self.inbox = bytearray()
        self.eof = False
        self.eof_sent = False
        self.closed = False
        self.task = asyncio.ensure_future(self._read())

    async def _read(self):
        try:
            while True:
                data = await self.reader.read(4096)
                if not data:
                    break
                self.inbox += data
        except (ConnectionError, OSError):
            pass
        self.eof = True


class Bridge:
    """The C++ driver in bridge mode plus its real TCP connections to the server under test (one per connection id the driver asks for)."""

    def __init__(self, proc, server_port):
        self.proc, self.server_port = proc, server_port
        self.now = 0
        self.conns = {}
        self.info = {}
        self.events = []

    async def command(self, line):
        self.proc.stdin.write((line + '\n').encode())
        self.proc.stdin.flush()
        events = []
        while True:
            raw = await asyncio.get_running_loop().run_in_executor(None, self.proc.stdout.readline)
            text = raw.decode().strip()
            if text == '':
                raise AssertionError('the driver died (last events: %s)' % self.events[-8:])
            if text == '.':
                return events
            if text.startswith('INFO'):
                for kv in text.split()[1:]:
                    k, _, v = kv.partition('=')
                    self.info[k] = int(v) if v.lstrip('-').isdigit() else v
            else:
                events.append(text)

    async def handle(self, events):
        for ev in events:
            self.events.append(ev)
            parts = ev.split()
            if parts[0] == 'BEGIN':
                cid = int(parts[1])
                r, w = await asyncio.open_connection('127.0.0.1', self.server_port)
                self.conns[cid] = Conn(r, w)
                await self.handle(await self.command('U %d' % cid))
            elif parts[0] == 'SEND':
                c = self.conns.get(int(parts[1]))
                if c is not None and not c.closed:
                    c.writer.write(bytes.fromhex(parts[2]))
                    await c.writer.drain()
            elif parts[0] == 'CLOSE':
                c = self.conns.get(int(parts[1]))
                if c is not None and not c.closed:
                    c.closed = True
                    c.writer.close()

    async def deliver(self):
        for cid, c in list(self.conns.items()):
            if c.closed:
                continue
            if c.inbox:
                data = bytes(c.inbox)
                c.inbox.clear()
                await self.handle(await self.command('R %d %s' % (cid, data.hex())))
            if c.eof and not c.eof_sent:
                c.eof_sent = True
                await self.handle(await self.command('X %d' % cid))

    async def step(self, dt=50):
        self.now += dt
        await self.handle(await self.command('T %d' % self.now))
        await asyncio.sleep(0.008)       # the server answers in real time
        await self.deliver()

    async def send(self, line):
        await self.handle(await self.command(line))

    async def close(self):
        for c in self.conns.values():
            c.task.cancel()
            try:
                c.writer.close()
            except Exception:
                pass
        self.proc.stdin.close()
        self.proc.wait(timeout=10)
        self.proc.stdout.close()


@unittest.skipUnless(CXX, 'needs clang++')
class EndToEnd(ServerCase):
    """The machines against the real server (tools/netserver/server.py): the connect-back fails (nothing listens), so the game is listed via relay."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='netrelay_e2e_')
        cls.exe = build(cls.tmp)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    async def asyncSetUp(self):
        await super().asyncSetUp()
        self.bridges = []

    async def asyncTearDown(self):
        for b in self.bridges:
            await b.close()
        await super().asyncTearDown()

    async def bridge(self):
        proc = subprocess.Popen([self.exe, 'bridge'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        b = Bridge(proc, self.port)
        self.bridges.append(b)
        return b

    async def until(self, b, cond, limit_ms=60000, dt=50, what=''):
        t = 0
        while not cond() and t < limit_ms:
            self.clock.t += dt / 1000.0
            self.server.sweep()
            await b.step(dt)
            t += dt
        self.assertTrue(cond(), 'not reached: %s (info %s)' % (what, b.info))

    async def host_listed(self, b, name='Castle'):
        await b.send('OH %s %d %d' % (name, BUILD, V13))
        await self.until(b, lambda: b.info.get('lobby_status') in (NLS_LISTED, NLS_LISTED_RELAY), what='the host is listed')

    async def client_joins(self, b, i):
        await b.send('OC %d %x %x' % (i, BUILD, V13))
        await b.send('FL %d' % i)
        await self.until(b, lambda: b.info.get('c%d_browse' % i) == NBS_LISTED and b.info.get('c%d_listed' % i) == 1, what='client %d has the list' % i)
        await b.send('FJ %d 0' % i)
        await self.until(b, lambda: b.info.get('c%d_known' % i) == 1, what='client %d is seated' % i)

    async def test_host_and_two_clients_through_the_relay(self):
        b = await self.bridge()
        await self.host_listed(b)
        self.assertEqual(b.info['lobby_status'], NLS_LISTED_RELAY)
        (lst,) = self.server.listings.values()
        self.assertTrue(lst.relay_session and b.info['session'] == lst.relay_session)
        # the host's idle connection is on the server
        await self.until(b, lambda: b.info.get('relay_phase') == NR_IDLE, what='the idle relay connection is up')
        await self.until(b, lambda: len(self.server.sessions[lst.relay_session].idle) == 1, what='the server holds the idle connection')

        await self.client_joins(b, 0)
        self.assertEqual((b.info['c0_join'], b.info['c0_session'], b.info['c0_direct']), (P.JOIN_RELAY, lst.relay_session, 0))
        self.assertEqual((b.info['c0_seat'], b.info['host_joined']), (1, 2))
        await self.until(b, lambda: b.info.get('relay_phase') == NR_IDLE and b.info['pairs'] == 1, what='a fresh idle connection after the first client')
        # the listing follows the player count (a REFRESH), the second client finds the game in the list
        await self.until(b, lambda: lst.players == 2, what='the lobby sees two players')
        await self.client_joins(b, 1)
        self.assertEqual((b.info['c1_seat'], b.info['host_joined'], b.info['pairs']), (2, 3, 2))
        self.assertEqual(self.server.stats.pairs, 2)

        # the lobby works through the pipes: picks both ways, then START
        await b.send('PK 0 0')
        await b.send('PK 1 1')
        await b.send('PK 2 2')
        await self.until(b, lambda: b.info.get('h_seat1') == '%d:1' % SEAT_DONE and b.info.get('h_seat2') == '%d:2' % SEAT_DONE, what='every pick reached the host')
        await b.send('GO')
        self.assertEqual(b.info['started'], 1)
        await self.until(b, lambda: b.info.get('c0_configured') == 1 and b.info.get('c1_configured') == 1, what='CONFIG reached both relayed clients')
        # 60 lockstep ticks on all three nodes: the frames of every tick are the same everywhere (and a state hash passed at tick 50)
        await self.until(b, lambda: min(int(x) for x in str(b.info['ticks']).split(',')) >= 60, limit_ms=120000, what='60 ticks on every node')
        self.assertEqual(b.info['mismatches'], 0)
        self.assertEqual((b.info['c0_ended'], b.info['c1_ended'], b.info['h_ended']), (0, 0, 0))
        self.assertGreater(self.server.stats.relayed_msgs, 100)
        # the listing stays as Playing, and START closed the idle connection
        self.assertTrue(next(iter(self.server.listings.values())).playing)
        self.assertEqual(b.info['relay_phase'], NR_OFF)

    async def test_a_direct_listing_keeps_the_relay_out(self):
        self.open_ports.add(6502)        # the connect-back works: the listing is direct
        b = await self.bridge()
        await self.host_listed(b)
        self.assertEqual(b.info['lobby_status'], NLS_LISTED)
        for _ in range(40):
            await b.step(50)
        self.assertEqual((b.info['relay_phase'], b.info['session']), (NR_OFF, 0))
        self.assertEqual(self.server.sessions, {})
        await b.send('OC 0 %x %x' % (BUILD, V13))
        await b.send('FL 0')
        await self.until(b, lambda: b.info.get('c0_browse') == NBS_LISTED, what='the list')
        await b.send('FJ 0 0')
        await self.until(b, lambda: b.info.get('c0_browse') == NBS_JOINED, what='the join reply')
        self.assertEqual((b.info['c0_join'], b.info['c0_direct']), (P.JOIN_DIRECT, 1))      # the client would connect to the host's address

    async def test_a_relisted_game_moves_its_idle_connection_to_the_new_session(self):
        b = await self.bridge()
        await self.host_listed(b)
        await self.until(b, lambda: b.info.get('relay_phase') == NR_IDLE, what='idle')
        first = b.info['session']
        await b.send('MD 0')              # the host switches the mode: UNREGISTER + REGISTER, a new listing with a new relay session
        await self.until(b, lambda: b.info['session'] not in (0, first) and b.info.get('relay_phase') == NR_IDLE, what='the idle connection of the new session')
        (lst,) = self.server.listings.values()
        self.assertEqual(lst.relay_session, b.info['session'])
        await self.until(b, lambda: len(self.server.sessions[lst.relay_session].idle) == 1, what='the new session has the idle connection')
        await self.client_joins(b, 0)                                # a joiner after the change gets in through the new session
        self.assertEqual((b.info['c0_session'], b.info['host_joined']), (b.info['session'], 2))

    async def test_a_quiet_relayed_lobby_survives_the_servers_idle_limit(self):
        """The server closes a pipe after 120 quiet seconds; the lobby keepalive (a PING every 30 s from every node) keeps it open for ten minutes."""
        b = await self.bridge()
        await self.host_listed(b)
        await self.client_joins(b, 0)
        for _ in range(600):              # ten minutes of fake time in one second steps
            self.clock.t += 1.0
            self.server.sweep()
            await b.step(1000)
        self.assertEqual((b.info['c0_ended'], b.info['h_ended'], b.info['host_joined'], b.info['c0_known']), (0, 0, 2, 1))
        self.assertEqual(self.server.stats.expired, 0)

    async def test_no_idle_connection_means_the_client_is_refused(self):
        """A client that joins while the host's idle connection is missing waits pair_wait and is refused; the browser says NO ANSWER (the pipe was closed)."""
        b = await self.bridge()
        await self.host_listed(b)
        await self.until(b, lambda: b.info.get('relay_phase') == NR_IDLE, what='idle')
        (lst,) = self.server.listings.values()
        for c in list(self.server.sessions[lst.relay_session].idle):   # the idle connection dies on the server's side
            c.close()
        self.server.sessions[lst.relay_session].idle.clear()
        await b.send('OC 0 %x %x' % (BUILD, V13))
        await b.send('FL 0')
        await self.until(b, lambda: b.info.get('c0_browse') == NBS_LISTED, what='the list')
        self.server.cfg.pair_wait = 0.2
        await b.send('FJ 0 0')
        await self.until(b, lambda: b.info.get('c0_ended') == 1 or b.info.get('c0_known') == 1, limit_ms=30000, what='the refusal or a seat')


if __name__ == '__main__':
    unittest.main()
