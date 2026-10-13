"""HTTP dashboard integration: real lobby registrations, expiry, full games and public-field filtering."""
import asyncio
import json
import unittest

from tests.test_netserver import ServerCase, register_msg
from tools.netserver import netproto as P
from tools.netserver.dashboard import Dashboard


class DashboardCase(ServerCase):
    cfg = {'listings_per_ip': 4}

    async def asyncSetUp(self):
        await super().asyncSetUp()
        self.dashboard = Dashboard(self.server)
        self.http_port = await self.dashboard.start('127.0.0.1', 0)

    async def asyncTearDown(self):
        await self.dashboard.stop()
        await super().asyncTearDown()

    async def request(self, path='/api/games', method='GET'):
        reader, writer = await asyncio.open_connection('127.0.0.1', self.http_port)
        writer.write(('%s %s HTTP/1.1\r\nHost: localhost\r\n\r\n' % (method, path)).encode())
        await writer.drain()
        response = await asyncio.wait_for(reader.read(), 3)
        writer.close()
        await writer.wait_closed()
        return response.split(b'\r\n\r\n', 1)

    async def test_all_builds_and_full_lobbies_without_private_fields(self):
        direct_port = await self.open_game_port()
        for name, port, build, mode, players in [('Direct Game', direct_port, 123, 0, 1),
                                                ('Full Coop', 6502, 456, 1, 4)]:
            node = await self.node()
            await node.send(register_msg(name=name, port=port, build=build, mode=mode, players=players))
            self.assertEqual((await node.recv()).type, P.REGISTER_REPLY)
        headers, body = await self.request()
        self.assertIn(b'200 OK', headers)
        self.assertIn(b'Cache-Control: no-store', headers)
        data = json.loads(body)
        self.assertEqual(data['listed_games'], 2)
        self.assertEqual(data['players_in_lobbies'], 5)
        games = data['games']
        self.assertEqual((games[0]['name'], games[0]['status'], games[0]['mode'], games[0]['connection']),
                         ('Full Coop', 'Full', 'Co-op', 'Relay'))
        self.assertEqual(games[1]['connection'], 'Direct')
        self.assertNotIn(b'127.0.0.1', body)
        self.assertEqual(set(games[0]), {'id', 'name', 'mode', 'players', 'max_players', 'version',
                                       'build', 'connection', 'status', 'updated_seconds_ago'})

    async def test_refresh_unregister_and_expiry(self):
        node = await self.node()
        await node.send(register_msg())
        await node.recv()
        await node.send(P.Msg(P.REFRESH, players=4))
        # Round-trip barrier ensures the preceding REFRESH has been processed.
        await node.send(P.Msg(P.LIST_REQ, build=0))
        await node.recv()
        _, body = await self.request()
        self.assertEqual(json.loads(body)['games'][0]['status'], 'Full')
        self.clock.t += self.server.cfg.refresh_timeout + 1
        _, body = await self.request()
        self.assertEqual(json.loads(body)['games'], [])
        await node.send(P.Msg(P.UNREGISTER))
        await node.send(P.Msg(P.LIST_REQ, build=0))
        await node.recv()
        self.assertFalse(self.server.listings)

    async def test_playing_status(self):
        node, waiting = await self.node(), await self.node()
        await node.send(register_msg(name='Started', players=2))
        await node.recv()
        await waiting.send(register_msg(name='Lobby', players=1))
        await waiting.recv()
        await node.send(P.Msg(P.REFRESH, players=3, state=P.GAME_PLAYING))
        await node.send(P.Msg(P.LIST_REQ, build=0))      # barrier
        await node.recv()
        _, body = await self.request()
        data = json.loads(body)
        by_name = {g['name']: g for g in data['games']}
        self.assertEqual(by_name['Started']['status'], 'Playing')
        self.assertEqual(by_name['Started']['players'], 3)
        self.assertEqual(by_name['Lobby']['status'], 'Open')
        self.assertEqual((data['listed_games'], data['playing_games'], data['players_in_lobbies']), (2, 1, 1))
        self.assertIn(b'playing', self.dashboard.html)    # the badge style exists on the page
        self.clock.t += self.server.cfg.refresh_timeout + 1
        _, body = await self.request()
        self.assertEqual(json.loads(body)['games'], [])   # silence ends a Playing game too

    async def test_active_relay_count_after_game_starts(self):
        lobby = await self.node()
        await lobby.send(register_msg())
        session = (await lobby.recv()).session
        host, client = await self.node(), await self.node()
        await host.send(P.Msg(P.RELAY_HELLO, session=session, role=P.ROLE_HOST))
        await client.send(P.Msg(P.RELAY_HELLO, session=session, role=P.ROLE_CLIENT))
        await client.send(P.Msg(P.HELLO, build=123, data=0, name='Player', pad_mask=1, version=0x0100))
        self.assertEqual((await host.recv()).type, P.HELLO)
        await lobby.send(P.Msg(P.UNREGISTER))
        await lobby.send(P.Msg(P.LIST_REQ, build=0))
        await lobby.recv()
        _, body = await self.request()
        data = json.loads(body)
        self.assertEqual(data['listed_games'], 0)
        self.assertEqual(data['active_relay_sessions'], 1)

    async def test_http_routes_and_method(self):
        headers, body = await self.request('/')
        self.assertIn(b'200 OK', headers)
        self.assertIn(b'Moonstone', body)
        self.assertIn(b'Content-Security-Policy:', headers)
        headers, _ = await self.request('/missing')
        self.assertIn(b'404 Not Found', headers)
        headers, _ = await self.request('/api/games', 'POST')
        self.assertIn(b'405 Method Not Allowed', headers)


if __name__ == '__main__':
    unittest.main()
