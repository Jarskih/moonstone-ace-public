"""Host tests of the lobby server + relay (ROADMAP 12.17 / 12.13a, tools/netserver/): fake nodes (asyncio clients on localhost)
against a real Server instance on an ephemeral port.  Time is injected (a fake clock), so listing expiry needs no sleeping.
The wire codec itself is checked against the C++ driver in tests/test_netproto.py.
"""
import asyncio
import os
import random
import socket
import struct
import subprocess
import sys
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from tools.netserver import netproto as P   # noqa: E402
from tools.netserver.server import Config, Server, name_ok   # noqa: E402

BUILD = 0xCAFEBABE
V12, V13 = 0x0102, 0x0103
LOCALHOST = struct.unpack('>I', socket.inet_aton('127.0.0.1'))[0]


class Clock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def register_msg(name='Castle Game', port=6502, players=1, max_players=4, build=BUILD, version=V13, mode=0):
    return P.Msg(P.REGISTER, **P.listing(name, mode, players, max_players, build, port, version))


class Node:
    """A fake game node: one TCP connection to the server speaking the wire format."""

    def __init__(self, reader, writer):
        self.r, self.w = reader, writer
        self.framer = P.Framer()
        self.eof = False

    @classmethod
    async def connect(cls, port):
        r, w = await asyncio.open_connection('127.0.0.1', port)
        return cls(r, w)

    async def send(self, msg):
        self.w.write(P.pack(msg))
        await self.w.drain()

    async def send_raw(self, data):
        self.w.write(data)
        await self.w.drain()

    async def recv(self, timeout=3.0):
        """Next message, or None when the server closed the connection."""
        while True:
            st, msg, raw = self.framer.next()
            if st == P.OK:
                self.last_raw = raw
                return msg
            assert st == P.MORE, 'server sent garbage: %d' % st
            if self.eof:
                return None
            data = await asyncio.wait_for(self.r.read(4096), timeout)
            if not data:
                self.eof = True
            self.framer.push(data)

    async def expect_closed(self, timeout=3.0):
        try:
            while True:
                data = await asyncio.wait_for(self.r.read(4096), timeout)
                if not data:
                    return True
        except (ConnectionError, OSError):
            return True

    async def close(self):
        self.w.close()
        await asyncio.sleep(0)


class ServerCase(unittest.IsolatedAsyncioTestCase):
    cfg = {}
    real_connect_back = False      # False: a fake that succeeds for the ports opened by open_game_port (a refused connect to a
                                   # closed localhost port takes ~2 s on Windows); True: the server's own TCP connect-back

    async def asyncSetUp(self):
        asyncio.get_running_loop().set_debug(False)   # IsolatedAsyncioTestCase turns it on; it makes every connection 0.1 s slower
        self.clock = Clock()
        self.listeners = []
        self.nodes = []
        self.open_ports = set()

        async def fake_connect_back(ip, port):
            return port in self.open_ports

        self.server = Server(Config(**self.cfg), clock=self.clock,
                             connect_back=None if self.real_connect_back else fake_connect_back)
        self.port = await self.server.start('127.0.0.1', 0)

    async def asyncTearDown(self):
        for n in self.nodes:
            await n.close()
        await self.server.stop()
        for s in self.listeners:
            s.close()

    async def node(self):
        n = await Node.connect(self.port)
        self.nodes.append(n)
        return n

    async def open_game_port(self):
        """A fake host's game port that accepts connections (so the connect-back succeeds)."""
        srv = await asyncio.start_server(lambda r, w: w.close(), '127.0.0.1', 0)
        self.listeners.append(srv)
        port = srv.sockets[0].getsockname()[1]
        self.open_ports.add(port)
        return port

    async def register(self, node, port=None, **kw):
        await node.send(register_msg(port=port if port is not None else free_port(), **kw))
        reply = await node.recv()
        self.assertEqual(reply.type, P.REGISTER_REPLY)
        return reply

    async def list_games(self, build=BUILD):
        n = await self.node()
        await n.send(P.Msg(P.LIST_REQ, build=build))
        reply = await n.recv()
        self.assertEqual(reply.type, P.LIST_REPLY)
        await n.close()
        return reply.entries

    async def join(self, node, game_id, build=BUILD, version=V12):
        await node.send(P.Msg(P.JOIN_REQ, game_id=game_id, build=build, version=version))
        reply = await node.recv()
        self.assertEqual(reply.type, P.JOIN_REPLY)
        return reply


class LobbyTests(ServerCase):
    cfg = dict(listings_per_ip=8)

    async def test_register_list_join_direct(self):
        host = await self.node()
        gport = await self.open_game_port()
        reply = await self.register(host, gport, name='Dragon Hunt', mode=1, players=2, max_players=3)
        self.assertEqual((reply.result, reply.session), (P.REG_DIRECT, 0))
        self.assertNotEqual(reply.game_id, 0)

        entries = await self.list_games()
        self.assertEqual(len(entries), 1)
        gid, lst = entries[0]
        self.assertEqual(gid, reply.game_id)
        self.assertEqual((lst['name'], lst['mode'], lst['players'], lst['max_players'], lst['build'], lst['version']),
                         ('Dragon Hunt', 1, 2, 3, BUILD, V13))

        client = await self.node()
        jr = await self.join(client, gid)
        self.assertEqual((jr.result, jr.addr, jr.port, jr.session, jr.host_version), (P.JOIN_DIRECT, LOCALHOST, gport, 0, V13))

    async def test_list_is_filtered_by_build_and_never_carries_an_address(self):
        a, b = await self.node(), await self.node()
        # one IP can hold several listings in this fixture
        await self.register(a, await self.open_game_port(), name='Mine', build=BUILD)
        await self.register(b, await self.open_game_port(), name='Other build', build=BUILD + 1)
        names = [l['name'] for _, l in await self.list_games(BUILD)]
        self.assertEqual(names, ['Mine'])
        self.assertEqual([l['name'] for _, l in await self.list_games(BUILD + 1)], ['Other build'])
        self.assertEqual(await self.list_games(12345), [])
        n = await self.node()
        await n.send(P.Msg(P.LIST_REQ, build=BUILD))
        await n.recv()
        self.assertNotIn(socket.inet_aton('127.0.0.1'), n.last_raw)      # no host address in the list

    async def test_list_has_at_most_eight_games_newest_first(self):
        self.server.cfg.listings_per_ip = 20
        hosts = []
        for i in range(10):
            h = await self.node()
            hosts.append(h)
            await self.register(h, await self.open_game_port(), name='G%d' % i)
        names = [l['name'] for _, l in await self.list_games()]
        self.assertEqual(names, ['G%d' % i for i in range(9, 1, -1)])

    async def test_join_not_found_build_mismatch_and_full(self):
        host = await self.node()
        gport = await self.open_game_port()
        reply = await self.register(host, gport, players=1, max_players=2, version=V13)
        gid = reply.game_id
        client = await self.node()
        jr = await self.join(client, gid + 1)
        self.assertEqual((jr.result, jr.addr, jr.port, jr.session), (P.JOIN_NOT_FOUND, 0, 0, 0))
        # build mismatch: nothing handed out, but the host's version is, so the player can read "host runs 1.3, you have 1.2"
        jr = await self.join(client, gid, build=BUILD ^ 1, version=V12)
        self.assertEqual((jr.result, jr.addr, jr.port, jr.session, jr.host_version), (P.JOIN_BUILD, 0, 0, 0, V13))
        # same build, other version: the build hash is the strict check, the version is information
        jr = await self.join(client, gid, version=V12)
        self.assertEqual((jr.result, jr.host_version), (P.JOIN_DIRECT, V13))
        # full: leaves the list, JOIN says full; a player leaving brings it back
        await host.send(P.Msg(P.REFRESH, players=2))
        await asyncio.sleep(0.05)
        self.assertEqual(await self.list_games(), [])
        self.assertEqual((await self.join(client, gid)).result, P.JOIN_FULL)
        await host.send(P.Msg(P.REFRESH, players=1))
        await asyncio.sleep(0.05)
        self.assertEqual(len(await self.list_games()), 1)

    async def test_registered_full_game_is_not_listed(self):
        host = await self.node()
        reply = await self.register(host, await self.open_game_port(), players=4, max_players=4)
        self.assertEqual(reply.result, P.REG_DIRECT)
        self.assertEqual(await self.list_games(), [])

    async def test_unregister_and_disconnect_remove_the_listing(self):
        a, b = await self.node(), await self.node()
        await self.register(a, await self.open_game_port(), name='A')
        await self.register(b, await self.open_game_port(), name='B')
        self.assertEqual(len(await self.list_games()), 2)
        await a.send(P.Msg(P.UNREGISTER))                 # the game started
        await asyncio.sleep(0.05)
        self.assertEqual([l['name'] for _, l in await self.list_games()], ['B'])
        await b.close()                                   # the host went away without a word
        await asyncio.sleep(0.1)
        self.assertEqual(await self.list_games(), [])

    async def test_listing_expires_without_refresh(self):
        host = await self.node()
        await self.register(host, await self.open_game_port())
        self.clock.t += 60
        self.server.sweep()
        self.assertEqual(len(await self.list_games()), 1)
        await host.send(P.Msg(P.REFRESH, players=2))      # refresh at t+60 ...
        await asyncio.sleep(0.05)
        self.clock.t += 60                                # ... 60 s later: still within 75 s of the refresh
        self.server.sweep()
        self.assertEqual(len(await self.list_games()), 1)
        self.clock.t += 30                                # 90 s without a refresh
        self.server.sweep()
        self.assertEqual(await self.list_games(), [])
        self.assertTrue(await host.expect_closed())       # the lobby hangs up on the silent host
        self.assertEqual(self.server.stats.expired, 1)

    async def test_refresh_without_listing_drops_the_connection(self):
        n = await self.node()
        await n.send(P.Msg(P.REFRESH, players=1))
        self.assertTrue(await n.expect_closed())

    async def test_playing_game_stays_listed_but_is_not_offered_for_joining(self):
        host, other = await self.node(), await self.node()
        gid = (await self.register(host, await self.open_game_port(), name='Going', players=1, max_players=4)).game_id
        await self.register(other, await self.open_game_port(), name='Waiting')
        self.assertEqual(sorted(l['name'] for _, l in await self.list_games()), ['Going', 'Waiting'])
        await host.send(P.Msg(P.REFRESH, players=2, state=P.GAME_PLAYING))
        await asyncio.sleep(0.05)
        self.assertEqual([l['name'] for _, l in await self.list_games()], ['Waiting'])   # not offered for joining
        client = await self.node()
        self.assertEqual((await self.join(client, gid)).result, P.JOIN_FULL)             # a stale list entry is refused
        lst = self.server.listings[gid]
        self.assertTrue(lst.playing)
        self.assertEqual(lst.players, 2)
        # sticky: a late "waiting" refresh does not bring a started game back, and refreshing keeps it alive
        self.clock.t += 60
        await host.send(P.Msg(P.REFRESH, players=3, state=P.GAME_WAITING))
        await asyncio.sleep(0.05)
        self.clock.t += 60
        self.server.sweep()
        self.assertIn(gid, self.server.listings)
        self.assertTrue(self.server.listings[gid].playing)
        self.assertEqual(await self.list_games(), [])    # 'Waiting' went silent and expired, 'Going' still plays
        # a playing game leaves with its host: silence expires it, UNREGISTER removes it
        self.clock.t += 80
        self.server.sweep()
        self.assertNotIn(gid, self.server.listings)

    async def test_a_playing_game_is_joined_only_with_the_rejoin_flag(self):
        """Protocol 5: a started game is not offered, but a player who holds its rejoin token is told where it is (NETPLAY.md 4.5)."""
        host = await self.node()
        gport = await self.open_game_port()
        gid = (await self.register(host, gport, name='Going', players=2, max_players=4)).game_id
        await host.send(P.Msg(P.REFRESH, players=2, state=P.GAME_PLAYING))
        await asyncio.sleep(0.05)
        client = await self.node()
        self.assertEqual((await self.join(client, gid)).result, P.JOIN_FULL)
        await client.send(P.Msg(P.JOIN_REQ, game_id=gid, build=BUILD, version=V12, flags=P.JOIN_FLAG_REJOIN))
        reply = await client.recv()
        self.assertEqual((reply.result, reply.port), (P.JOIN_DIRECT, gport))
        self.assertEqual(reply.addr, int.from_bytes(socket.inet_aton('127.0.0.1'), 'big'))
        await client.send(P.Msg(P.JOIN_REQ, game_id=gid, build=BUILD + 1, version=V12, flags=P.JOIN_FLAG_REJOIN))
        self.assertEqual((await client.recv()).result, P.JOIN_BUILD)       # the build check still holds
        # the flag on a waiting game changes nothing: it is offered like any other
        waiting = await self.node()
        wid = (await self.register(waiting, await self.open_game_port(), name='Waiting')).game_id
        await client.send(P.Msg(P.JOIN_REQ, game_id=wid, build=BUILD, version=V12, flags=P.JOIN_FLAG_REJOIN))
        self.assertEqual((await client.recv()).result, P.JOIN_DIRECT)
        # a flag bit that does not exist is a protocol error
        bad = bytearray(P.pack(P.Msg(P.JOIN_REQ, game_id=gid, build=BUILD, version=V12, flags=0)))
        bad[-1] = 2
        await client.send_raw(bytes(bad))
        self.assertTrue(await client.expect_closed())

    async def test_playing_game_unregister_and_disconnect(self):
        a, b = await self.node(), await self.node()
        ga = (await self.register(a, await self.open_game_port(), name='A')).game_id
        gb = (await self.register(b, await self.open_game_port(), name='B')).game_id
        await a.send(P.Msg(P.REFRESH, players=1, state=P.GAME_PLAYING))
        await b.send(P.Msg(P.REFRESH, players=1, state=P.GAME_PLAYING))
        await asyncio.sleep(0.05)
        self.assertEqual(await self.list_games(), [])
        self.assertEqual(set(self.server.listings), {ga, gb})
        await a.send(P.Msg(P.UNREGISTER))
        await b.close()
        await asyncio.sleep(0.1)
        self.assertEqual(self.server.listings, {})

    async def test_refresh_state_is_validated_on_the_wire(self):
        n = await self.node()
        await self.register(n, await self.open_game_port())
        bad = P.pack(P.Msg(P.REFRESH, players=1, state=1))[:-1] + bytes([2])    # state 2 does not exist
        await n.send_raw(bad)
        self.assertTrue(await n.expect_closed())
        st, _ = P.encode(P.Msg(P.REFRESH, players=1, state=5))
        self.assertEqual(st, P.ERR_FIELD)
        self.assertEqual(P.pack(P.Msg(P.REFRESH, players=3, state=1)), bytes.fromhex('4d530622000203' '01'))   # one exact encoding


class ConnectBackTests(ServerCase):
    real_connect_back = True

    async def test_real_connect_back_decides_direct_or_relay(self):
        a, b = await self.node(), await self.node()
        self.server.cfg.listings_per_ip = 2
        open_reply = await self.register(a, await self.open_game_port(), name='Open')
        closed_reply = await self.register(b, free_port(), name='Closed')
        self.assertEqual((open_reply.result, open_reply.session), (P.REG_DIRECT, 0))
        self.assertEqual(closed_reply.result, P.REG_RELAY)
        self.assertNotEqual(closed_reply.session, 0)


class AbuseTests(ServerCase):
    async def test_one_listing_per_ip(self):
        a, b = await self.node(), await self.node()
        self.assertEqual((await self.register(a, await self.open_game_port())).result, P.REG_DIRECT)
        self.assertEqual((await self.register(b, await self.open_game_port())).result, P.REG_LISTED)    # other connection, same IP
        self.assertEqual((await self.register(a, await self.open_game_port())).result, P.REG_LISTED)    # same connection twice
        await a.send(P.Msg(P.UNREGISTER))                                                               # leaving frees the slot
        await asyncio.sleep(0.05)
        self.assertEqual((await self.register(b, await self.open_game_port())).result, P.REG_DIRECT)

    async def test_name_filter(self):
        self.assertTrue(all(name_ok(n) for n in ['Castle Game', 'a', 'Game_1.5', 'ABC xyz 0']))
        self.assertFalse(any(name_ok(n) for n in ['', '   ', 'a!b', 'a-b', 'x#', 'quote"', 'a/b', 'a:b', 'tab\t']))
        n = await self.node()
        for bad in ['Hello!', 'a:b', '   ']:
            self.assertEqual((await self.register(n, name=bad)).result, P.REG_BAD_NAME, bad)
        self.assertNotEqual((await self.register(n, name='Fine_Name.1')).result, P.REG_BAD_NAME)

    async def test_port_zero_is_malformed(self):
        n = await self.node()
        await n.send(register_msg(port=0))
        self.assertTrue(await n.expect_closed())

    async def test_non_ascii_name_is_rejected_by_the_codec(self):
        n = await self.node()
        await n.send_raw(P.pack(register_msg()).replace(b'Castle Game', b'Castle G\xe9me'))
        self.assertTrue(await n.expect_closed())

    async def test_server_limits(self):
        self.server.cfg.max_listings = 2
        self.server.cfg.listings_per_ip = 5
        nodes = [await self.node() for _ in range(3)]
        self.assertEqual((await self.register(nodes[0], await self.open_game_port())).result, P.REG_DIRECT)
        self.assertEqual((await self.register(nodes[1], await self.open_game_port())).result, P.REG_DIRECT)
        self.assertEqual((await self.register(nodes[2], await self.open_game_port())).result, P.REG_FULL)

    async def test_session_limit(self):
        self.server.cfg.max_sessions = 1
        self.server.cfg.listings_per_ip = 5
        a, b = await self.node(), await self.node()
        self.assertEqual((await self.register(a)).result, P.REG_RELAY)
        self.assertEqual((await self.register(b)).result, P.REG_FULL)

    async def test_connection_rate_limit(self):
        self.server.conn_bucket.burst = 4
        self.server.conn_bucket.rate = 0.0
        self.server.conn_bucket.state.clear()
        ok = 0
        for _ in range(6):
            n = await Node.connect(self.port)
            self.nodes.append(n)
            await n.send(P.Msg(P.LIST_REQ, build=BUILD))
            try:
                if (await n.recv(1.0)) is not None:
                    ok += 1
            except (ConnectionError, OSError, asyncio.TimeoutError):
                pass
        self.assertEqual(ok, 4)
        self.assertGreaterEqual(self.server.stats.rate_limited, 2)
        # the bucket refills with (fake) time
        self.server.conn_bucket.rate = 1.0
        self.clock.t += 10
        n = await self.node()
        await n.send(P.Msg(P.LIST_REQ, build=BUILD))
        self.assertIsNotNone(await n.recv())

    async def test_message_rate_limit_closes_the_connection(self):
        self.server.msg_bucket.burst = 5
        self.server.msg_bucket.rate = 0.0
        n = await self.node()
        for _ in range(12):
            try:
                await n.send(P.Msg(P.LIST_REQ, build=BUILD))
            except (ConnectionError, OSError):
                break
        got = 0
        while True:
            try:
                m = await n.recv(1.0)
            except (ConnectionError, OSError):
                break
            if m is None:
                break
            got += 1
        self.assertEqual(got, 5)

    async def test_connections_per_ip(self):
        self.server.cfg.max_conns_per_ip = 3
        keep = [await self.node() for _ in range(3)]
        for n in keep:
            await n.send(P.Msg(P.LIST_REQ, build=BUILD))
            self.assertIsNotNone(await n.recv())
        extra = await self.node()
        self.assertTrue(await extra.expect_closed())
        self.assertGreaterEqual(self.server.stats.refused_conns, 1)

    async def test_silent_connections_are_dropped(self):
        n = await self.node()
        await asyncio.sleep(0.05)
        self.clock.t += 20
        self.server.sweep()
        self.assertTrue(await n.expect_closed())


class MalformedTests(ServerCase):
    cfg = dict(conn_burst=10000, conn_rate=10000.0, msg_burst=100000, msg_rate=100000.0, max_conns_per_ip=10000)

    async def assert_dropped_and_alive(self, data):
        n = await self.node()
        await n.send_raw(data)
        self.assertTrue(await n.expect_closed(), data.hex())
        await n.close()
        # the server still answers a well-formed browser
        self.assertEqual(await self.list_games(), [])

    async def test_garbage_drops_only_that_connection(self):
        good = P.pack(P.Msg(P.LIST_REQ, build=BUILD))
        cases = [
            b'GET / HTTP/1.1\r\n\r\n',
            b'\x00' * 32,
            good[:2] + b'\x09' + good[3:],                             # protocol version
            good[:2] + b'\x02\x63' + good[4:],                         # unknown type
            good[:4] + b'\x02\x01',                                    # payload length 513
            good[:5] + b'\x03' + good[6:],                             # length does not fit the type
            P.pack(P.Msg(P.BYE, reason=0, version=1))[:-1] + b'\x00' + b'\x07',   # trailing garbage after a good message
            P.pack(P.Msg(P.LIST_REPLY, entries=[])),                   # a server reply sent by a client
            P.pack(P.Msg(P.JOIN_REPLY, result=0, addr=1, port=2, session=3, host_version=4)),
            P.pack(P.Msg(P.PING, token=1)),                            # a game message on a lobby connection
            P.pack(P.Msg(P.REGISTER_REPLY, result=0, game_id=1, session=0)),
            P.pack(P.Msg(P.RELAY_HELLO, session=1, role=P.ROLE_HOST)) + P.pack(P.Msg(P.RELAY_HELLO, session=1, role=P.ROLE_HOST)),
        ]
        for data in cases:
            await self.assert_dropped_and_alive(data)

    async def test_truncated_message_then_close(self):
        n = await self.node()
        await n.send_raw(P.pack(P.Msg(P.LIST_REQ, build=BUILD))[:5])
        await n.close()
        await asyncio.sleep(0.05)
        self.assertEqual(await self.list_games(), [])

    async def test_random_bytes_fuzz(self):
        rng = random.Random(7)
        good = [P.pack(register_msg(port=free_port())), P.pack(P.Msg(P.LIST_REQ, build=BUILD)),
                P.pack(P.Msg(P.JOIN_REQ, game_id=5, build=BUILD, version=V12)), P.pack(P.Msg(P.UNREGISTER))]
        for _ in range(150):
            data = bytearray(rng.choice(good))
            for _ in range(rng.randrange(1, 4)):
                data[rng.randrange(len(data))] = rng.randrange(256)
            n = await self.node()
            await n.send_raw(bytes(data))
            await n.close()
        # drain: the server is still up and consistent
        await asyncio.sleep(0.3)
        self.assertEqual(len(self.server.conns), 0)
        self.assertEqual(self.server.listings.get(0), None)
        self.assertEqual(await self.list_games(), [])
        self.assertEqual(self.server.stats.errors, 0)


class RelayTests(ServerCase):
    cfg = dict(pair_wait=0.4)

    async def relay_game(self):
        """A host whose game port is closed: listed with a relay session."""
        host = await self.node()
        reply = await self.register(host, free_port(), name='Behind NAT', version=V13)
        self.assertEqual(reply.result, P.REG_RELAY)
        self.assertNotEqual(reply.session, 0)
        return host, reply.game_id, reply.session

    async def host_idle(self, session):
        n = await self.node()
        await n.send(P.Msg(P.RELAY_HELLO, session=session, role=P.ROLE_HOST))
        return n

    async def relay_client(self, session):
        n = await self.node()
        await n.send(P.Msg(P.RELAY_HELLO, session=session, role=P.ROLE_CLIENT))
        return n

    async def test_join_through_relay_hands_out_no_host_address(self):
        host, gid, session = await self.relay_game()
        client = await self.node()
        jr = await self.join(client, gid)
        self.assertEqual((jr.result, jr.addr, jr.port, jr.session, jr.host_version), (P.JOIN_RELAY, 0, 0, session, V13))
        self.assertNotIn(socket.inet_aton('127.0.0.1'), client.last_raw)

    async def test_a_playing_relayed_game_is_joined_through_the_relay_with_the_rejoin_flag(self):
        host, gid, session = await self.relay_game()
        await host.send(P.Msg(P.REFRESH, players=2, state=P.GAME_PLAYING))
        await asyncio.sleep(0.05)
        client = await self.node()
        self.assertEqual((await self.join(client, gid)).result, P.JOIN_FULL)
        await client.send(P.Msg(P.JOIN_REQ, game_id=gid, build=BUILD, version=V12, flags=P.JOIN_FLAG_REJOIN))
        jr = await client.recv()
        self.assertEqual((jr.result, jr.addr, jr.port, jr.session), (P.JOIN_RELAY, 0, 0, session))

    async def test_relay_forwards_messages_both_ways_unchanged(self):
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        client = await self.relay_client(session)
        hello = P.Msg(P.HELLO, build=BUILD, data=0x11223344, name='Anna', pad_mask=3, version=V12)
        await client.send(hello)
        got = await idle.recv()
        self.assertEqual(got, hello)
        self.assertEqual(idle.last_raw, P.pack(hello))                 # byte for byte
        config = P.Msg(P.CONFIG, mode=0, nodes=2, your_node=1, delay=2, seed=99, bindings=[(0, 0, 0), (1, 0, 1)],
                       names=['Host', 'Anna'], portraits=[2, 0])
        await idle.send(config)
        self.assertEqual(await client.recv(), config)
        # many messages in one burst keep their order
        frame = bytes([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        for tick in range(20):
            await client.send(P.Msg(P.INPUT, tick=tick, node=1, frame=frame))
        for tick in range(20):
            self.assertEqual((await idle.recv()).tick, tick)
        self.assertGreater(self.server.stats.relayed_msgs, 20)

    async def test_the_lobby_messages_pass_the_relay(self):
        """The lobby = the knight screen (NETPLAY.md 4.2b): LOBBY_PICK (client -> host) and LOBBY_STATE (host -> client) are game messages."""
        self.assertEqual(P.PROTO_VERSION, 6)
        self.assertIn(P.LOBBY_PICK, P.GAME_TYPES)
        self.assertIn(P.LOBBY_STATE, P.GAME_TYPES)
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        client = await self.relay_client(session)
        pick = P.Msg(P.LOBBY_PICK, stage=P.PICK_DONE, sel=2, pad=3, name='Sir Anna')
        await client.send(pick)
        self.assertEqual(await idle.recv(), pick)
        self.assertEqual(idle.last_raw, P.pack(pick))
        state = P.Msg(P.LOBBY_STATE, mode=P.MODE_COOP, your_seat=1, seats=[
            (P.SEAT_DONE, 2, 3, 'Host'), (P.SEAT_CLAIM, 1, 0xFF, 'Anna'), (P.SEAT_FREE, 0, 0xFF, ''), (P.SEAT_FREE, 0, 0xFF, '')])
        await idle.send(state)
        self.assertEqual(await client.recv(), state)
        # a message the lobby would never send is a protocol error, not forwarded
        bad = bytearray(P.pack(pick))
        bad[6] = 9                                                       # stage 9
        await client.send_raw(bytes(bad))
        self.assertTrue(await client.expect_closed())

    async def test_the_rejoin_messages_pass_the_relay(self):
        """Protocol 5 (NETPLAY.md 4.5): REJOIN and its answer, the chunked world and the boundary are game messages."""
        for t in (P.REJOIN, P.REJOIN_OK, P.STATE_BEGIN, P.STATE_CHUNK, P.STATE_END, P.RESUME, P.BOUNDARY):
            self.assertIn(t, P.GAME_TYPES)
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        client = await self.relay_client(session)
        rejoin = P.Msg(P.REJOIN, build=BUILD, data=0x1234, version=V13, rules=0, node=2, token=0xCAFE1234)
        await client.send(rejoin)
        self.assertEqual(await idle.recv(), rejoin)
        self.assertEqual(idle.last_raw, P.pack(rejoin))
        for msg in (P.Msg(P.REJOIN_OK, node=2),
                    P.Msg(P.STATE_BEGIN, tick=900, clock=77, hash=1, crc=2, nbytes=800, chunks=3, live=0b0111),
                    P.Msg(P.STATE_CHUNK, index=1, data=bytes(range(200))),
                    P.Msg(P.STATE_END, chunks=3, crc=2),
                    P.Msg(P.BOUNDARY, tick=900, mask=0b0100, hash=5)):
            await idle.send(msg)
            self.assertEqual(await client.recv(), msg)
        resume = P.Msg(P.RESUME, tick=900, hash=5)
        await client.send(resume)
        self.assertEqual(await idle.recv(), resume)
        # a chunk longer than the chunk size is a protocol error, not forwarded
        bad = bytearray(P.pack(P.Msg(P.STATE_CHUNK, index=0, data=bytes(10))))
        bad[8] = 0x7F                                                    # declared length 32522
        await client.send_raw(bytes(bad))
        self.assertTrue(await client.expect_closed())

    async def test_a_dropped_relay_client_can_be_paired_again_with_a_fresh_idle_connection(self):
        """NETPLAY.md 4.5 via relay: the pipe of a dropped player ends on both sides, the host opens a new idle connection for the SAME session
        (its lobby connection stays up: the listing and the session id do not change) and the returning player's RELAY_HELLO + REJOIN pair with it."""
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        client = await self.relay_client(session)
        await client.send(P.Msg(P.HELLO, build=BUILD, data=1, name='Anna', pad_mask=1, version=V12))
        self.assertEqual((await idle.recv()).data, 1)
        await client.close()                                            # the dropped player's pipe goes: the host's end of it with it
        self.assertTrue(await idle.expect_closed())
        await asyncio.sleep(0.05)
        sess = self.server.sessions[session]
        self.assertEqual((sess.host_conns, sess.clients), (0, 0))
        self.assertEqual(list(self.server.listings), [gid])             # the host's listing is untouched
        idle2 = await self.host_idle(session)                           # the host keeps one idle connection ready again
        back = await self.relay_client(session)
        rejoin = P.Msg(P.REJOIN, build=BUILD, data=0x1234, version=V13, rules=0, node=1, token=0xCAFE1234)
        await back.send(rejoin)
        self.assertEqual(await idle2.recv(), rejoin)
        await idle2.send(P.Msg(P.REJOIN_OK, node=1))
        self.assertEqual((await back.recv()).type, P.REJOIN_OK)
        self.assertEqual(self.server.stats.pairs, 2)
        # a host whose lobby connection is re-registered gets a NEW session: the old id of a remembered game finds no host (the reason the game must not
        # close its lobby connection while a seat is away)
        await host.close()
        await asyncio.sleep(0.05)
        late = await self.relay_client(session)
        self.assertEqual((await late.recv()).type, P.BYE)

    async def test_star_with_three_clients_and_a_full_session(self):
        host, gid, session = await self.relay_game()
        pipes, clients = [], []
        for i in range(3):
            idle = await self.host_idle(session)                       # the host keeps one idle connection ready
            client = await self.relay_client(session)
            await client.send(P.Msg(P.HELLO, build=BUILD, data=i, name='P%d' % (i + 1), pad_mask=1, version=V12))
            self.assertEqual((await idle.recv()).data, i)
            pipes.append(idle)
            clients.append(client)
        # each pipe reaches only its own client
        for i, (pipe, client) in enumerate(zip(pipes, clients)):
            await pipe.send(P.Msg(P.SEED, tick=i, seed=1000 + i))
            self.assertEqual((await client.recv()).seed, 1000 + i)
        # a fourth client: the star holds the host and three clients
        idle = await self.host_idle(session)
        fourth = await self.relay_client(session)
        bye = await fourth.recv()
        self.assertEqual((bye.type, bye.reason), (P.BYE, P.BYE_FULL))
        self.assertTrue(await fourth.expect_closed())
        # closing one client closes its pipe on the host side only
        await clients[1].close()
        self.assertTrue(await pipes[1].expect_closed())
        await pipes[0].send(P.Msg(P.PING, token=5))
        self.assertEqual((await clients[0].recv()).token, 5)

    async def test_unknown_session_and_missing_host(self):
        c = await self.relay_client(0x01020304)
        bye = await c.recv()
        self.assertEqual((bye.type, bye.reason, bye.version), (P.BYE, P.BYE_REFUSED, 0))
        self.assertTrue(await c.expect_closed())
        host, gid, session = await self.relay_game()
        c = await self.relay_client(session)                            # no idle host connection ever arrives
        bye = await c.recv(3.0)
        self.assertEqual((bye.type, bye.reason), (P.BYE, P.BYE_REFUSED))

    async def test_client_waits_for_the_hosts_idle_connection(self):
        host, gid, session = await self.relay_game()
        client = await self.relay_client(session)                       # arrives first ...
        await asyncio.sleep(0.1)
        idle = await self.host_idle(session)                            # ... the host's connection follows within pair_wait
        await client.send(P.Msg(P.PING, token=9))
        self.assertEqual((await idle.recv()).token, 9)

    async def test_only_game_messages_pass_the_relay(self):
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        client = await self.relay_client(session)
        await client.send(P.Msg(P.LIST_REQ, build=BUILD))              # a lobby message inside a pipe
        self.assertTrue(await client.expect_closed())
        self.assertTrue(await idle.expect_closed())

    async def test_idle_host_connection_answers_pings_and_nothing_else(self):
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        await idle.send(P.Msg(P.PING, token=77))
        self.assertEqual((await idle.recv()).token, 77)
        await idle.send(P.Msg(P.SEED, tick=1, seed=2))                  # data before a client is paired: protocol error
        self.assertTrue(await idle.expect_closed())

    async def test_relay_pipe_rate_limit(self):
        self.server.cfg.pipe_burst = 10
        self.server.cfg.pipe_rate = 0.0
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        client = await self.relay_client(session)
        for i in range(30):
            try:
                await client.send(P.Msg(P.PING, token=i))
            except (ConnectionError, OSError):
                break
        n = 0
        while (await idle.recv(1.0)) is not None:
            n += 1
        self.assertEqual(n, 10)

    async def test_game_start_unregisters_but_the_session_lives_while_the_host_is_connected(self):
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        await host.send(P.Msg(P.UNREGISTER))
        await asyncio.sleep(0.05)
        self.assertEqual(await self.list_games(), [])
        self.clock.t += 500
        self.server.sweep()
        client = await self.relay_client(session)
        await client.send(P.Msg(P.PING, token=3))
        self.assertEqual((await idle.recv()).token, 3)

    async def test_unused_sessions_expire(self):
        host, gid, session = await self.relay_game()
        for _ in range(10):                                             # 500 s of refreshes: listed, so the session stays
            self.clock.t += 50
            await host.send(P.Msg(P.REFRESH, players=1))
            await asyncio.sleep(0.15)
            self.server.sweep()
        self.assertIn(session, self.server.sessions)
        await host.send(P.Msg(P.UNREGISTER))                            # not listed, no host connection: grace period
        await asyncio.sleep(0.05)
        self.clock.t += 30
        self.server.sweep()
        self.assertIn(session, self.server.sessions)
        self.clock.t += 40
        self.server.sweep()
        self.assertNotIn(session, self.server.sessions)
        c = await self.relay_client(session)
        self.assertEqual((await c.recv()).reason, P.BYE_REFUSED)

    async def test_pipe_idle_timeout(self):
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        client = await self.relay_client(session)
        await client.send(P.Msg(P.PING, token=1))
        await idle.recv()
        self.clock.t += 200
        self.server.sweep()
        self.assertTrue(await client.expect_closed())
        self.assertTrue(await idle.expect_closed())


class UdpRelayTests(ServerCase):
    """Protocol 6 (NETPLAY.md 2.2, 5b): INPUT_UDP / TICK_UDP between the members of a relay session, on the TCP port's number."""
    cfg = dict(pair_wait=0.4)

    async def relay_game(self):
        host = await self.node()
        reply = await self.register(host, free_port(), name='Behind NAT', version=V13)
        self.assertEqual(reply.result, P.REG_RELAY)
        return host, reply.game_id, reply.session

    async def host_idle(self, session):
        n = await self.node()
        await n.send(P.Msg(P.RELAY_HELLO, session=session, role=P.ROLE_HOST))
        return n

    async def relay_client(self, session):
        n = await self.node()
        await n.send(P.Msg(P.RELAY_HELLO, session=session, role=P.ROLE_CLIENT))
        return n

    async def drain(self, sock):
        while await self.recv_dgram(sock, 0.05) is not None:
            pass

    FRAME = bytes(P.FRAME_BYTES)
    TOKEN = 0xC0FFEE11

    def udp_socket(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(('127.0.0.1', 0))
        s.setblocking(False)
        self.listeners.append(s)
        return s

    def to_server(self, sock, msg):
        sock.sendto(P.pack(msg), ('127.0.0.1', self.server.udp_port))

    async def recv_dgram(self, sock, timeout=1.0):
        try:
            data, _ = await asyncio.wait_for(asyncio.get_running_loop().sock_recvfrom(sock, 2048), timeout)
        except asyncio.TimeoutError:
            return None
        return data

    async def settle(self):
        await asyncio.sleep(0.05)

    def input_udp(self, session, token=None, node=1, count=2, flags=0):
        return P.Msg(P.INPUT_UDP, session=session, token=token or self.TOKEN, node=node, flags=flags, ack=10, first=20, count=count,
                     frames=[self.FRAME] * count)

    def tick_udp(self, session, token=None, node=1, count=2, nodes=3):
        return P.Msg(P.TICK_UDP, session=session, token=token or self.TOKEN, node=node, flags=1, ack=21, drop_seq=0, nodes=nodes, first=19, count=count,
                     frames=[[self.FRAME] * nodes] * count)

    async def game_with_token(self):
        """A relayed game in which a CONFIG with TOKEN passed the pipe, so the datagrams of that token are accepted."""
        host, gid, session = await self.relay_game()
        idle = await self.host_idle(session)
        client = await self.relay_client(session)
        await client.send(P.Msg(P.HELLO, build=BUILD, data=0, name='Anna', pad_mask=1, version=V12))
        await idle.recv()
        await idle.send(P.Msg(P.CONFIG, mode=0, nodes=2, your_node=1, delay=2, seed=1, token=self.TOKEN, bindings=[(0, 0, 0), (1, 0, 1)],
                              names=['Host', 'Anna'], portraits=[2, 0]))
        await client.recv()
        return session, idle, client

    async def test_the_server_opens_the_udp_relay_on_the_tcp_port(self):
        self.assertIsNotNone(self.server.udp)
        self.assertEqual(self.server.udp_port, self.port)

    async def test_datagrams_pass_between_the_members_unchanged(self):
        session, idle, client = await self.game_with_token()
        hs, cs = self.udp_socket(), self.udp_socket()
        self.to_server(cs, self.input_udp(session))                       # the client registers (the host is not known yet: nothing to forward to)
        await self.settle()
        self.assertIsNone(await self.recv_dgram(hs, 0.1))
        self.to_server(hs, self.tick_udp(session))                        # the host registers and reaches the client
        got = await self.recv_dgram(cs)
        self.assertEqual(got, P.pack(self.tick_udp(session)))             # byte for byte
        self.to_server(cs, self.input_udp(session))                       # and the client now reaches the host
        got = await self.recv_dgram(hs)
        self.assertEqual(got, P.pack(self.input_udp(session)))
        self.assertGreaterEqual(self.server.stats.udp_forwarded, 2)
        # probes (no frames) pass as well: they are how a member registers
        self.to_server(hs, self.tick_udp(session, count=0))
        self.assertEqual(await self.recv_dgram(cs), P.pack(self.tick_udp(session, count=0)))

    async def test_a_datagram_is_for_the_client_of_its_token_only(self):
        session, idle, client = await self.game_with_token()
        self.server.sessions[session].tokens.append(0xABCDEF01)            # a second client's token (seen in its CONFIG)
        hs, c1, c2 = self.udp_socket(), self.udp_socket(), self.udp_socket()
        self.to_server(c1, self.input_udp(session))
        self.to_server(c2, self.input_udp(session, token=0xABCDEF01, node=2))
        await self.settle()
        self.to_server(hs, self.tick_udp(session, token=0xABCDEF01, node=2))
        self.assertEqual(await self.recv_dgram(c2), P.pack(self.tick_udp(session, token=0xABCDEF01, node=2)))
        self.assertIsNone(await self.recv_dgram(c1, 0.2))

    async def test_what_is_not_a_member_is_dropped(self):
        session, idle, client = await self.game_with_token()
        hs, cs = self.udp_socket(), self.udp_socket()
        self.to_server(hs, self.tick_udp(session))                         # host registered, a client to receive
        self.to_server(cs, self.input_udp(session))
        await self.settle()
        await self.drain(hs)
        before = self.server.stats.udp_dropped
        self.to_server(cs, self.input_udp(session + 1))                    # an unknown session
        self.to_server(cs, self.input_udp(0))                              # session 0: a direct game, not ours
        self.to_server(cs, self.input_udp(session, token=0x12345678))      # a token no CONFIG carried
        self.to_server(cs, self.tick_udp(session, token=0x12345678))
        cs.sendto(b'garbage', ('127.0.0.1', self.server.udp_port))         # not a message
        cs.sendto(P.pack(self.input_udp(session)) + b'\0', ('127.0.0.1', self.server.udp_port))   # a trailing byte: not exactly one message
        cs.sendto(P.pack(P.Msg(P.PING, token=1)), ('127.0.0.1', self.server.udp_port))             # a message of another kind
        cs.sendto(b'x' * 2000, ('127.0.0.1', self.server.udp_port))        # too big
        await self.settle()
        self.assertIsNone(await self.recv_dgram(hs, 0.2))
        self.assertEqual(self.server.stats.udp_dropped - before, 8)

    async def test_only_the_registered_ip_can_be_the_host(self):
        session, idle, client = await self.game_with_token()
        self.server.sessions[session].ip = '10.9.8.7'                      # the game was registered from another address
        hs, cs = self.udp_socket(), self.udp_socket()
        self.to_server(cs, self.input_udp(session))
        await self.settle()
        self.to_server(hs, self.tick_udp(session))
        self.assertIsNone(await self.recv_dgram(cs, 0.2))
        self.assertIsNone(self.server.sessions[session].udp_host)

    async def test_the_rate_limit_applies_per_source_address(self):
        self.server.cfg.udp_rate, self.server.cfg.udp_burst = 1.0, 5
        self.server.udp_bucket = type(self.server.udp_bucket)(self.clock, 1.0, 5)
        session, idle, client = await self.game_with_token()
        hs, cs = self.udp_socket(), self.udp_socket()
        self.to_server(hs, self.tick_udp(session))
        await self.settle()
        for _ in range(20):
            self.to_server(cs, self.input_udp(session))
        await self.settle()
        got = 0
        while await self.recv_dgram(hs, 0.1) is not None:
            got += 1
        self.assertLessEqual(got, 5)
        self.assertGreater(self.server.stats.udp_limited, 10)

    async def test_registered_addresses_expire(self):
        session, idle, client = await self.game_with_token()
        hs, cs = self.udp_socket(), self.udp_socket()
        self.to_server(cs, self.input_udp(session))
        await self.settle()
        self.assertEqual(len(self.server.sessions[session].udp_clients), 1)
        self.clock.t += 100
        self.server.sweep()
        self.assertEqual(self.server.sessions[session].udp_clients, {})
        self.to_server(hs, self.tick_udp(session))                         # the client is forgotten: nothing to forward to
        self.assertIsNone(await self.recv_dgram(cs, 0.2))

    async def test_at_most_three_clients_register(self):
        session, idle, client = await self.game_with_token()
        sess = self.server.sessions[session]
        for t in (0x1001, 0x1002, 0x1003):
            sess.tokens.append(t)
        socks = [self.udp_socket() for _ in range(5)]
        for sock, tok in zip(socks, (self.TOKEN, 0x1001, 0x1002, 0x1003)):
            self.to_server(sock, self.input_udp(session, token=tok))
        await self.settle()
        self.assertEqual(len(sess.udp_clients), 3)

    async def test_tokens_come_from_the_hosts_config_only(self):
        session, idle, client = await self.game_with_token()
        sess = self.server.sessions[session]
        self.assertEqual(list(sess.tokens), [self.TOKEN])
        # a CONFIG from the client's side of the pipe (a hostile client) does not add a token, and a repeated CONFIG adds nothing twice
        await idle.send(P.Msg(P.CONFIG, mode=0, nodes=2, your_node=1, delay=2, seed=1, token=self.TOKEN, bindings=[(0, 0, 0)],
                               names=['Host', 'Anna'], portraits=[2, 0]))
        await client.recv()
        self.assertEqual(list(sess.tokens), [self.TOKEN])

    async def test_a_server_without_udp_still_serves_tcp(self):
        await self.server.stop()
        cfg = Config(udp_enabled=False)
        self.server = Server(cfg, clock=self.clock)
        self.port = await self.server.start('127.0.0.1', 0)
        self.assertIsNone(self.server.udp)
        node = await self.node()
        await node.send(P.Msg(P.LIST_REQ, build=BUILD))
        self.assertEqual((await node.recv()).type, P.LIST_REPLY)

    async def test_udp_types_are_not_game_messages_on_the_tcp_pipe(self):
        session, idle, client = await self.game_with_token()
        await client.send(self.input_udp(session))                         # a datagram type on the stream: the relay refuses to forward it
        self.assertTrue(await client.expect_closed())


class ProcessTests(unittest.TestCase):
    def test_runs_as_a_script_and_as_a_module(self):
        for args in ([os.path.join(ROOT, 'tools', 'netserver', 'server.py')], ['-m', 'tools.netserver']):
            port = free_port()
            proc = subprocess.Popen([sys.executable] + args + ['--port', str(port), '--bind', '127.0.0.1'], cwd=ROOT,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                sock = None
                for _ in range(100):
                    try:
                        sock = socket.create_connection(('127.0.0.1', port), timeout=1)
                        break
                    except OSError:
                        time.sleep(0.1)
                self.assertIsNotNone(sock, 'server did not start: ' + str(args))
                sock.sendall(P.pack(P.Msg(P.LIST_REQ, build=BUILD)))
                buf = b''
                while True:
                    st, msg, used = P.decode(buf)
                    if st != P.MORE:
                        break
                    buf += sock.recv(4096)
                self.assertEqual((st, msg.type, msg.entries), (P.OK, P.LIST_REPLY, []))
                sock.close()
            finally:
                proc.terminate()
                proc.communicate(timeout=10)

    def test_selftest_passes_against_a_local_server_including_the_udp_relay(self):
        """tools/netserver/selftest.py (direct game, relayed game, UDP relay, rejoin, version) against a real server process on localhost."""
        port = free_port()
        proc = subprocess.Popen([sys.executable, '-m', 'tools.netserver', '--port', str(port), '--bind', '127.0.0.1'], cwd=ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                try:
                    socket.create_connection(('127.0.0.1', port), timeout=1).close()
                    break
                except OSError:
                    time.sleep(0.1)
            r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'netserver', 'selftest.py'), '--server', '127.0.0.1', '--port', str(port)],
                               cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn('udp: the host', r.stdout)
            self.assertIn('selftest: PASS', r.stdout)
        finally:
            proc.terminate()
            proc.communicate(timeout=10)

    def test_no_connect_back_lists_every_game_via_relay(self):
        """`--no-connect-back` (tests/netpair --relay): the connect-back test always fails, even for a game port that is open; without the flag it works."""
        game = socket.socket()
        game.bind(('127.0.0.1', 0))
        game.listen(5)
        gport = game.getsockname()[1]
        try:
            for flag, want in (([], P.REG_DIRECT), (['--no-connect-back', '--refresh-timeout', '600'], P.REG_RELAY)):
                port = free_port()
                proc = subprocess.Popen([sys.executable, '-m', 'tools.netserver', '--port', str(port), '--bind', '127.0.0.1'] + flag, cwd=ROOT,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    sock = None
                    for _ in range(100):
                        try:
                            sock = socket.create_connection(('127.0.0.1', port), timeout=5)
                            break
                        except OSError:
                            time.sleep(0.1)
                    self.assertIsNotNone(sock, 'server did not start: %s' % flag)
                    sock.sendall(P.pack(register_msg(port=gport)))
                    buf = b''
                    while True:
                        st, msg, used = P.decode(buf)
                        if st != P.MORE:
                            break
                        buf += sock.recv(4096)
                    self.assertEqual((st, msg.type, msg.result), (P.OK, P.REGISTER_REPLY, want), flag)
                    if want == P.REG_RELAY:
                        self.assertNotEqual(msg.session, 0)
                    sock.close()
                finally:
                    proc.terminate()
                    proc.communicate(timeout=10)
        finally:
            game.close()


if __name__ == '__main__':
    unittest.main()
