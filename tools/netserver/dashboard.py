"""Read-only HTTP view of the lobby. Same event loop as the relay; no database or dependencies."""
import asyncio
import json
from pathlib import Path
from urllib.parse import urlsplit


def snapshot(server):
    """All registered games, across builds, including full lobbies. Never expose addresses or relay IDs."""
    now = server.clock()
    games = []
    for listing in reversed(server.listings.values()):
        if listing.pending or now - listing.last_refresh > server.cfg.refresh_timeout:
            continue
        games.append({
            'id': listing.game_id,
            'name': listing.name,
            'mode': 'Co-op' if listing.mode else 'Classic',
            'players': listing.players,
            'max_players': listing.max_players,
            'version': '%d.%d' % (listing.version >> 8, listing.version & 255),
            'build': '%08x' % listing.build,
            'connection': 'Relay' if listing.relay_session else 'Direct',
            'status': 'Playing' if listing.playing else 'Full' if listing.hidden else 'Open',
            'updated_seconds_ago': max(0, int(now - listing.last_refresh)),
        })
    active_relays = len({c.session.sid for c in server.conns
                        if c.state == 'pipe' and not c.closed and c.session is not None})
    return {'games': games, 'listed_games': len(games),
            'players_in_lobbies': sum(g['players'] for g in games if g['status'] != 'Playing'),
            'playing_games': sum(1 for g in games if g['status'] == 'Playing'),
            'active_relay_sessions': active_relays}


class Dashboard:
    def __init__(self, relay):
        self.relay = relay
        self.server = None
        self.connections = set()
        self.html = Path(__file__).with_name('dashboard.html').read_bytes()

    async def start(self, host='0.0.0.0', port=8080):
        self.server = await asyncio.start_server(self.handle, host, port, limit=8192)
        return self.server.sockets[0].getsockname()[1]

    async def stop(self):
        if self.server:
            self.server.close()
            await self.server.wait_closed()
        for writer in list(self.connections):
            writer.close()

    async def handle(self, reader, writer):
        if len(self.connections) >= 64:
            writer.close()
            return
        self.connections.add(writer)
        try:
            try:
                request = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), 5)
            except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, asyncio.TimeoutError):
                return
            if len(request) > 8192:
                return
            parts = request.split(b'\r\n', 1)[0].split()
            status, body, mime = '400 Bad Request', b'Bad request\n', 'text/plain'
            if len(parts) == 3 and parts[2] in (b'HTTP/1.0', b'HTTP/1.1'):
                if parts[0] != b'GET':
                    status, body = '405 Method Not Allowed', b'GET only\n'
                else:
                    path = urlsplit(parts[1].decode('ascii')).path
                    if path == '/':
                        status, body, mime = '200 OK', self.html, 'text/html'
                    elif path == '/api/games':
                        status, mime = '200 OK', 'application/json'
                        body = json.dumps(snapshot(self.relay), ensure_ascii=True).encode('utf-8')
                    else:
                        status, body = '404 Not Found', b'Not found\n'
            headers = ('HTTP/1.1 %s\r\nContent-Type: %s; charset=utf-8\r\n'
                       'Content-Length: %d\r\nConnection: close\r\nCache-Control: no-store\r\n'
                       'X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n'
                       'Content-Security-Policy: default-src \'none\'; style-src \'unsafe-inline\'; '
                       'script-src \'unsafe-inline\'; connect-src \'self\'; base-uri \'none\'; '
                       'frame-ancestors \'none\'; form-action \'none\'\r\n\r\n') % (status, mime, len(body))
            writer.write(headers.encode('ascii') + body)
            await asyncio.wait_for(writer.drain(), 5)
        except (OSError, ValueError, asyncio.TimeoutError):
            pass
        finally:
            self.connections.discard(writer)
            writer.close()
            try:
                await writer.wait_closed()
            except OSError:
                pass
