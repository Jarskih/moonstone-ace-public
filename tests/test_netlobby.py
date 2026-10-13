"""Host test of the host's lobby client (ROADMAP 12.18 host half): engine/netlobby.cpp, the pure state machine that lists a hosted game on
the lobby server (REGISTER / REFRESH / UNREGISTER), keeps it listed while waiting and while playing, and survives a dead lobby.

tests/netlobby_driver.cpp is compiled with clang++ (-fno-exceptions -fno-rtti) and runs two ways:
  * self test: the machine against an in-process fake lobby over fake pipes (partial sends, one byte reads, every refusal, timeouts and
    retries, the REFRESH cadence over ten minutes, the 32 bit clock wrap);
  * relay mode (end to end): the SAME machine is driven by this test, which owns real TCP connections to the real tools/netserver
    Server (fake clock, fake connect-back): register direct / relay, players, START -> Playing on the dashboard and gone from the join list,
    survival of ten minutes of server sweeps, the server hanging up and the machine re-registering as Playing, UNREGISTER at the end.
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

from tests.test_netserver import ServerCase, BUILD   # noqa: E402
from tools.netserver import netproto as P            # noqa: E402
from tools.netserver.dashboard import snapshot       # noqa: E402

DRIVER = 'tests/netlobby_driver.cpp'
SOURCES = ['src/engine/netlobby.cpp', 'src/engine/netproto.cpp', 'src/engine/netplay.cpp']

# NetLobbyStatus / NetLobbyPhase
NLS_PRIVATE, NLS_CONTACTING, NLS_LISTED, NLS_LISTED_RELAY, NLS_UNREACHABLE, NLS_REFUSED_NAME = range(6)
NL_OFF, NL_START, NL_CONNECTING, NL_REGISTERING, NL_LISTED, NL_RETRY, NL_DEAD = range(7)


def build(out_dir, overrides=None):
    exe = os.path.join(out_dir, 'netlobby.exe')
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
    def test_state_machine_over_fake_pipes(self):
        tmp = tempfile.mkdtemp(prefix='netlobby_')
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
            # the REFRESH never says "playing"
            ('src/engine/netlobby.cpp', 'l.isPlaying ? NET_LIST_PLAYING : NET_LIST_WAITING', 'NET_LIST_WAITING'),
            # a name lookup is allowed during play
            ('src/engine/netlobby.cpp', 'io.pfnBegin(io.pCtx, !l.isPlaying)', 'io.pfnBegin(io.pCtx, true)'),
            # a relay listing shows as direct
            ('src/engine/netlobby.cpp', 'r.ubResult == NET_REG_DIRECT ? NLS_LISTED : NLS_LISTED_RELAY', 'NLS_LISTED'),
            # a refused name is retried for ever
            ('src/engine/netlobby.cpp', 'l.ubPhase = NL_DEAD;', 'l.ubPhase = NL_RETRY;'),
            # a changed player count is not reported
            ('src/engine/netlobby.cpp', 'const bool isChanged = l.ubPlayers != l.ubSentPlayers || l.isPlaying != l.isSentPlaying;',
             'const bool isChanged = false;'),
            # the refresh period is wrong
            ('src/engine/netlobby.cpp', 'isDue(ulNow, l.ulLastSend + NL_REFRESH_MS) || (isChanged',
             'isDue(ulNow, l.ulLastSend + NL_REFRESH_MS * 2) || (isChanged'),
            # no UNREGISTER at the end
            ('src/engine/netlobby.cpp', 'if(queueUnregister(l)) {\n\t\t\tflush(l, io);', 'if(false) {\n\t\t\tflush(l, io);'),
            # a changed mode never re-lists the game
            ('src/engine/netlobby.cpp', 'return !l.isPlaying && l.listing.ubMode != l.ubSentMode;', 'return l.ubSentMode > 9;'),
            # a lost connection is not noticed
            ('src/engine/netlobby.cpp', 'if(readReplies(l, io, ulNow) < 0) {          // the server closed', 'if(false) {          // the server closed'),
        ]
        for path, old, new in mutations:
            text = src(path)
            self.assertIn(old, text, 'mutation target vanished: ' + old)
            tmp = tempfile.mkdtemp(prefix='netlobby_mut_')
            try:
                exe = build(tmp, {path: text.replace(old, new, 1)})
                r = subprocess.run([exe], capture_output=True, text=True, timeout=300)
                self.assertNotEqual(r.returncode, 0, 'mutation survived: ' + new)
            finally:
                shutil.rmtree(tmp, ignore_errors=True)


class Driver:
    """The C++ state machine in relay mode plus its real TCP connection to the server under test."""

    def __init__(self, proc, server_port):
        self.proc, self.server_port = proc, server_port
        self.now = 0
        self.w = None
        self.inbox = bytearray()
        self.eof = False
        self.status = self.phase = None
        self.log = []
        self._task = None

    async def command(self, line):
        self.proc.stdin.write((line + '\n').encode())
        self.proc.stdin.flush()
        events = []
        while True:
            raw = await asyncio.get_running_loop().run_in_executor(None, self.proc.stdout.readline)
            text = raw.decode().strip()
            if text == '':
                raise AssertionError('the driver died')
            if text == '.':
                return events
            if text.startswith('STATUS'):
                parts = text.split()
                self.status, self.phase = int(parts[1]), int(parts[3])
            else:
                events.append(text)

    async def _reader(self, r):
        try:
            while True:
                data = await r.read(4096)
                if not data:
                    break
                self.inbox += data
        except (ConnectionError, OSError):
            pass
        self.eof = True

    async def handle(self, events):
        slept = False
        for ev in events:
            self.log.append(ev)
            if ev.startswith('BEGIN'):
                if self.w is not None:
                    self.w.close()
                r, self.w = await asyncio.open_connection('127.0.0.1', self.server_port)
                self.inbox.clear()
                self.eof = False
                self._task = asyncio.ensure_future(self._reader(r))
                await self.command('S 1')
            elif ev.startswith('SEND'):
                self.w.write(bytes.fromhex(ev.split()[1]))
                await self.w.drain()
                slept = True
            elif ev == 'CLOSE':
                if self.w is not None:
                    self.w.close()
                    self.w = None
        if slept:
            await asyncio.sleep(0.03)

    async def step(self):
        """One driver step at the current clock (ms); then the network's answer is delivered."""
        await self.handle(await self.command('T %d' % self.now))
        await self.deliver()

    async def deliver(self):
        if self.inbox:
            data = bytes(self.inbox)
            self.inbox.clear()
            await self.handle(await self.command('R ' + data.hex()))
        if self.eof and self.w is not None:
            self.eof = False
            self.w = None
            await self.command('X')

    async def close(self):
        if self.w is not None:
            self.w.close()
        self.proc.stdin.close()
        self.proc.wait(timeout=10)
        self.proc.stdout.close()
        if self._task:
            self._task.cancel()


@unittest.skipUnless(CXX, 'needs clang++')
class EndToEnd(ServerCase):
    """The state machine against the real server (tools/netserver/server.py)."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='netlobby_e2e_')
        cls.exe = build(cls.tmp)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    async def asyncSetUp(self):
        await super().asyncSetUp()
        self.drivers = []

    async def asyncTearDown(self):
        for d in self.drivers:
            await d.close()
        await super().asyncTearDown()

    async def driver(self, gameport, name='Castle_Game', players=1, max_players=4, mode=0, version=0x0103):
        proc = subprocess.Popen([self.exe, 'relay'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        d = Driver(proc, self.port)
        self.drivers.append(d)
        await d.command('O %s %d %d %d %d %d %d' % (name, mode, players, max_players, BUILD, gameport, version))
        return d

    async def advance(self, d, ms, step=500):
        """Moves the driver's clock, the server's clock and the server's sweeps together."""
        t = 0
        while t < ms:
            t += step
            d.now += step
            self.clock.t += step / 1000.0
            self.server.sweep()
            if d.phase == NL_REGISTERING:       # the answer is real network traffic: give it real time
                await asyncio.sleep(0.01)
            await d.step()

    async def until(self, d, cond, limit_ms=20000, step=100):
        t = 0
        while not cond() and t < limit_ms:
            await self.advance(d, step, step)
            t += step
        self.assertTrue(cond(), 'condition not reached; driver log: %s' % d.log[-6:])

    def games(self):
        return snapshot(self.server)['games']

    async def test_register_direct_playing_and_unregister(self):
        d = await self.driver(await self.open_game_port(), players=1)
        await self.until(d, lambda: d.status == NLS_LISTED)
        (g,) = self.games()
        self.assertEqual((g['name'], g['mode'], g['players'], g['max_players'], g['status'], g['connection'], g['version']),
                         ('Castle Game', 'Classic', 1, 4, 'Open', 'Direct', '1.3'))
        self.assertEqual(len(self.server.listings), 1)
        # a joiner's list shows it
        browser = await self.node()
        await browser.send(P.Msg(P.LIST_REQ, build=BUILD))
        self.assertEqual(len((await browser.recv()).entries), 1)
        # a player joins: the count follows within a second
        await d.command('P 2')
        await self.advance(d, 1500)
        self.assertEqual(self.games()[0]['players'], 2)
        # START: Playing on the dashboard, off the join list
        await d.command('G')
        await self.advance(d, 1500)
        self.assertEqual(self.games()[0]['status'], 'Playing')
        await browser.send(P.Msg(P.LIST_REQ, build=BUILD))
        self.assertEqual((await browser.recv()).entries, [])
        gid = next(iter(self.server.listings))
        await browser.send(P.Msg(P.JOIN_REQ, game_id=gid, build=BUILD, version=0x0103))
        self.assertEqual((await browser.recv()).result, P.JOIN_FULL)
        # ten minutes of play: the listing never expires (a REFRESH every 30 s against the server's 75 s)
        await self.advance(d, 600000, step=1000)
        self.assertEqual([g['status'] for g in self.games()], ['Playing'])
        self.assertEqual(self.server.listings[gid].players, 2)
        # the session ends: UNREGISTER
        await d.handle(await d.command('E'))
        await asyncio.sleep(0.1)
        self.assertEqual(self.games(), [])
        self.assertEqual(self.server.listings, {})
        self.assertEqual(d.status, NLS_PRIVATE)

    async def test_the_lobby_mode_follows_the_listing(self):
        """The host's MODE line (CO-OP / CLASSIC): the dashboard shows it, the join list offers it, one listing at all times."""
        d = await self.driver(await self.open_game_port(), mode=1, players=2)       # registered as co-op (the room's default)
        await self.until(d, lambda: d.status == NLS_LISTED)
        self.assertEqual([g['mode'] for g in self.games()], ['Co-op'])
        first = next(iter(self.server.listings))
        await d.command('M 0')                                                       # the host switches to classic
        await self.until(d, lambda: [g['mode'] for g in self.games()] == ['Classic'] and d.status == NLS_LISTED)
        (g,) = self.games()
        self.assertEqual((g['name'], g['players'], g['status']), ('Castle Game', 2, 'Open'))
        self.assertEqual(len(self.server.listings), 1)
        self.assertNotEqual(next(iter(self.server.listings)), first)                 # a fresh listing, never two
        browser = await self.node()
        await browser.send(P.Msg(P.LIST_REQ, build=BUILD))
        (entry,) = (await browser.recv()).entries
        self.assertEqual(entry[1]['mode'], 0)
        await d.command('M 1')                                                       # and back
        await self.until(d, lambda: [g['mode'] for g in self.games()] == ['Co-op'] and d.status == NLS_LISTED)
        # START: Playing; a later switch changes nothing
        await d.command('G')
        await self.advance(d, 1500)
        await d.command('M 0')
        await self.advance(d, 3000)
        (g,) = self.games()
        self.assertEqual((g['mode'], g['status']), ('Co-op', 'Playing'))

    async def test_relay_listing(self):
        d = await self.driver(6502)         # nothing listens on this port: the connect-back fails
        await self.until(d, lambda: d.status == NLS_LISTED_RELAY)
        (g,) = self.games()
        self.assertEqual(g['connection'], 'Relay')

    async def test_name_refused_is_final(self):
        d = await self.driver(await self.open_game_port(), name='Bad!Name')
        await self.until(d, lambda: d.phase == NL_DEAD)
        self.assertEqual(self.games(), [])
        self.assertEqual(d.status, NLS_REFUSED_NAME)

    async def test_server_hangs_up_and_the_game_is_listed_as_playing_again(self):
        port = await self.open_game_port()
        d = await self.driver(port, players=1)
        await self.until(d, lambda: d.status == NLS_LISTED)
        await d.command('P 2')
        await d.command('G')
        await self.advance(d, 2000)
        self.assertEqual(self.games()[0]['status'], 'Playing')
        lst = next(iter(self.server.listings.values()))
        lst.conn.close()                     # the lobby drops the connection (a restart, a crash, a network blip)
        await asyncio.sleep(0.1)
        self.assertEqual(self.games(), [])
        await self.until(d, lambda: bool(self.games()), limit_ms=60000)
        await self.advance(d, 2000)
        (g,) = self.games()
        self.assertEqual((g['status'], g['players']), ('Playing', 2))

    async def test_lobby_down_costs_nothing_and_comes_back(self):
        d = await self.driver(await self.open_game_port())
        await d.command('B 0')               # no stack / no such host name
        await self.advance(d, 1000)
        self.assertEqual(d.status, NLS_UNREACHABLE)
        await d.command('B 1')
        await self.until(d, lambda: d.status == NLS_LISTED, limit_ms=70000, step=1000)
        self.assertEqual(len(self.games()), 1)


if __name__ == '__main__':
    unittest.main()
