"""serve.py -- the local helper server of the monster kit browser editor (ROADMAP 9.8b, docs/MONSTER_KIT.md section 4).

    py tools/monsterkit.py serve [--port N] [--kits DIR] [--disks DIR] [--hd DIR]

Python stdlib http.server only, bound to 127.0.0.1.  Serves the static editor (tools/monsterkit/editor/) and a small JSON API over
the importable functions of tools/monsterkit.py.  Nothing of the original game is stored in the editor files: pixels, palettes
and scripts reach the page only through this API, from the kits under build/kits/ (git-ignored).

Guards (the page is local, but any web page the user visits can talk to 127.0.0.1):
  * the Host header must be 127.0.0.1:<port> or localhost:<port> (DNS rebinding);
  * a request that carries an Origin must come from the same origin;
  * POST / PUT / DELETE need the header `X-MonsterKit: 1` (a cross-site page cannot send it without a preflight, which is never answered);
  * kit names are [a-z][a-z0-9_]{1,31} (never a path), static paths are resolved and must stay inside editor/.

API (JSON unless noted; errors are {"error": text} with a 4xx/5xx status):
  GET  /api/info                       game data availability, creatures, kits root, default HD folder
  GET  /api/catalog                    ai_catalog.json (AIs, roles, tunables, limits, sound banks)
  GET  /api/kits                       kits under the kits root
  POST /api/clone                      {source: creature | kit name, name, force?} -> {kit}
  GET  /api/kit/<name>                 kit.json
  PUT  /api/kit/<name>                 save kit.json (schema-validated; origin is kept from disk when absent)
  GET  /api/kit/<name>/sheet/<n>       {w, h, data: base64 of the uint8 index array}
  PUT  /api/kit/<name>/sheet/<n>       {w, h, data} write the sheet PNG (indexed, the kit palette)
  POST /api/kit/<name>/snap            {png: base64} snap a PNG to the kit palette -> {w, h, data, changed, counts}
  POST /api/kit/<name>/check           {kit?} check the saved kit, or the posted kit.json -> report + checklist
  POST /api/kit/<name>/build           {out?, hd?, install?, reencode?} build T0 (+ install into the HD folder)
"""
import base64
import http.server
import io
import json
import os
import re
import socket
import socketserver
import threading
import urllib.parse

HOST = '127.0.0.1'
EDITOR_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'editor')
NAME_RE = re.compile(r'^[a-z][a-z0-9_]{1,31}$')
MAX_BODY = 64 * 1024 * 1024
TYPES = {'.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
         '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml', '.ico': 'image/x-icon'}


class ApiError(Exception):
    def __init__(self, status, msg):
        super().__init__(msg)
        self.status, self.msg = status, msg


def snap_png(png_bytes, palette_hex, allow=None):
    """Snap an image to the palette (nearest colour in RGB; alpha < 128 = transparent = index 0).  Returns
    (indices uint8 (h, w), changed bool (h, w): the source colour is not exactly the palette colour it snapped to)."""
    import numpy as np
    from PIL import Image
    src = Image.open(io.BytesIO(png_bytes))
    pal = np.array([[int(c[i:i + 2], 16) for i in (1, 3, 5)] for c in palette_hex], dtype=np.int32)
    if src.mode == 'P':
        # An indexed PNG whose used entries carry the kit's colours is taken as is (index = colour register, as read_sheet does):
        # the fight palette has duplicate colours (8 and 13 are both #FF0000 in the troll's), which an RGB snap cannot tell apart.
        pidx = np.array(src, dtype=np.uint8)
        ppal = np.array(src.getpalette()[:3 * 256], dtype=np.int32).reshape(-1, 3)
        used = [int(v) for v in np.unique(pidx) if v != 0]
        if all(v < len(pal) and v < len(ppal) and (ppal[v] == pal[v]).all() and (allow is None or v in allow) for v in used):
            return pidx, np.zeros(pidx.shape, dtype=bool)
    img = src.convert('RGBA')
    rgba = np.array(img, dtype=np.int32)
    ok = [i for i in range(len(pal)) if i == 0 or allow is None or i in allow]
    d = ((rgba[:, :, None, :3] - pal[None, ok, :]) ** 2).sum(axis=3) * 4
    d[:, :, 0] += 2                                          # ties go to the real colours, not to transparent
    for k, i in enumerate(ok):                               # and never to the first fighter's colours 6..8 when another index matches
        if i in (6, 7, 8):
            d[:, :, k] += 1
    idx = np.array(ok, dtype=np.uint8)[d.argmin(axis=2)]
    transparent = rgba[:, :, 3] < 128
    idx[transparent] = 0
    chosen = pal[idx]
    changed = (~transparent) & (chosen != rgba[:, :, :3]).any(axis=2)
    return idx, changed


class Api:
    """The request logic, independent of http.server (tests can call it directly)."""

    def __init__(self, mk, kits_root, disks, hd_default):
        self.mk, self.kits_root, self.disks, self.hd_default = mk, os.path.abspath(kits_root), disks, hd_default
        os.makedirs(self.kits_root, exist_ok=True)
        self.lock = threading.Lock()

    # ---- paths
    def kit_dir(self, name):
        if not NAME_RE.match(name or ''):
            raise ApiError(400, 'bad kit name %r (lower case, digits, underscore; 2..32 characters)' % (name,))
        return os.path.join(self.kits_root, name)

    def existing_kit(self, name):
        d = self.kit_dir(name)
        if not os.path.isfile(self.mk.kit_path(d)):
            raise ApiError(404, 'no kit %s' % name)
        return d

    def game_available(self):
        return bool(self.mk.find_game_file('collide.hit', self.disks) and self.mk.find_game_file('TROLL1.CEL', self.disks))

    # ---- GET
    def info(self):
        cat = self.mk.catalog()
        return {'game': self.game_available(), 'kits_root': self.kits_root, 'disks': self.disks, 'hd_default': self.hd_default,
                'creatures': [{'name': c['name'], 'title': c['title'], 'ai': c['ai']} for c in cat['creatures'] if c['name'] in self.mk.OFFERED],
                'sheet_width': self.mk.SHEET_W, 'kit_version': self.mk.KIT_VERSION}

    def catalog(self):
        return self.mk.catalog()

    def kits(self):
        out = []
        for n in sorted(os.listdir(self.kits_root)):
            p = self.mk.kit_path(os.path.join(self.kits_root, n))
            if NAME_RE.match(n) and os.path.isfile(p):
                try:
                    k = self.mk.load_kit(os.path.dirname(p))
                except (OSError, ValueError):
                    out.append({'name': n, 'broken': True})
                    continue
                out.append({'name': n, 'title': k.get('title', ''), 'ai': k.get('ai'), 'cloned_from': k.get('cloned_from'),
                            'celsets': len(k.get('celsets', [])), 'animations': len(k.get('animations', {})),
                            'mtime': os.path.getmtime(p)})
        return {'kits': out}

    def get_kit(self, name):
        return self.mk.load_kit(self.existing_kit(name))

    def get_sheet(self, name, n):
        d = self.existing_kit(name)
        kit = self.mk.load_kit(d)
        if not (0 <= n < len(kit['sheets'])):
            raise ApiError(404, 'kit %s has no sheet %d' % (name, n))
        idx = self.mk.read_sheet(os.path.join(d, kit['sheets'][n]['file']), kit['palette'].get('colors'))
        return {'w': int(idx.shape[1]), 'h': int(idx.shape[0]), 'data': base64.b64encode(idx.tobytes()).decode('ascii')}

    # ---- writes
    def clone(self, body):
        src, name = body.get('source'), body.get('name')
        d = self.kit_dir(name)
        force = bool(body.get('force'))
        if os.path.exists(self.mk.kit_path(d)) and not force:
            raise ApiError(409, 'kit %s already exists' % name)
        try:
            if src in self.mk.OFFERED:
                if not self.game_available():
                    raise ApiError(409, 'the original game data is not available under %s (tools/adfx.py): clone a kit instead' % self.disks)
                with self.lock:
                    kit = self.mk.clone_game(src, d, name, disks=self.disks, force=force)
            else:
                sd = self.existing_kit(src) if isinstance(src, str) else None
                if sd is None:
                    raise ApiError(400, 'source must be a creature or a kit name')
                with self.lock:
                    kit = self.mk.clone_kit(sd, d, name, force=force)
        except self.mk.KitError as e:
            raise ApiError(400, str(e))
        return {'kit': kit}

    def put_kit(self, name, body):
        d = self.kit_dir(name)
        if not isinstance(body, dict):
            raise ApiError(400, 'kit.json must be an object')
        if body.get('name') != name:
            raise ApiError(400, 'kit name %r does not match the folder %s' % (body.get('name'), name))
        errs = self.mk.J.validate(body, self.mk.schema())
        if errs:
            raise ApiError(400, 'schema: ' + '; '.join(errs[:8]))
        with self.lock:
            if 'origin' not in body and os.path.isfile(self.mk.kit_path(d)):
                old = self.mk.load_kit(d)
                if 'origin' in old:
                    body['origin'] = old['origin']
            self.mk.save_kit(d, body)
        return {'ok': True}

    def put_sheet(self, name, n, body):
        d = self.existing_kit(name)
        kit = self.mk.load_kit(d)
        if not (0 <= n < len(kit['sheets'])):
            raise ApiError(404, 'kit %s has no sheet %d' % (name, n))
        import numpy as np
        try:
            w, h = int(body['w']), int(body['h'])
            raw = base64.b64decode(body['data'])
        except (KeyError, ValueError, TypeError):
            raise ApiError(400, 'sheet body must be {w, h, data}')
        if not (1 <= w <= 4096 and 1 <= h <= 4096) or len(raw) != w * h:
            raise ApiError(400, 'sheet data is %d bytes, %dx%d expects %d' % (len(raw), w, h, w * h))
        idx = np.frombuffer(raw, dtype=np.uint8).reshape(h, w)
        cols = kit['palette'].get('colors') or ['#%02x%02x%02x' % (i * 8, i * 8, i * 8) for i in range(32)]
        pal = [self.mk._unhex(c) for c in cols]
        pal += [(0, 0, 0)] * (max(32, int(idx.max()) + 1) - len(pal))
        with self.lock:
            self.mk.write_indexed_png(os.path.join(d, kit['sheets'][n]['file']), idx, pal)
        return {'ok': True}

    def snap(self, name, body):
        d = self.existing_kit(name)
        kit = self.mk.load_kit(d)
        try:
            png = base64.b64decode(body['png'])
            idx, changed = snap_png(png, kit['palette'].get('colors') or [], kit['palette'].get('allow'))
        except (KeyError, ValueError, TypeError) as e:
            raise ApiError(400, 'snap needs a PNG (base64): %s' % e)
        except Exception as e:                                # PIL.UnidentifiedImageError and friends
            raise ApiError(400, 'cannot read that image: %s' % e)
        import numpy as np
        counts = np.bincount(idx.ravel(), minlength=64)
        forbidden = int(sum(counts[i] for i in self.mk.FORBIDDEN))
        return {'w': int(idx.shape[1]), 'h': int(idx.shape[0]), 'data': base64.b64encode(idx.tobytes()).decode('ascii'),
                'changed': base64.b64encode(changed.astype(np.uint8).tobytes()).decode('ascii'),
                'counts': {'pixels': int(idx.size), 'opaque': int((idx != 0).sum()), 'changed': int(changed.sum()), 'forbidden': forbidden}}

    def report(self, rep):
        return {'ok': rep.ok, 'errors': rep.errors, 'warnings': rep.warnings, 'info': rep.info, 'checklist': rep.checklist,
                'tier': rep.tier, 'text': rep.text()}

    def check(self, name, body):
        d = self.existing_kit(name)
        kit = body.get('kit') if isinstance(body, dict) else None
        try:
            if kit is not None:
                kit = dict(kit)
                kit['_dir'] = d
                rep = self.mk.check_kit(kit, disks=self.disks)
            else:
                rep = self.mk.check_kit(d, disks=self.disks)
        except (self.mk.KitError, OSError, KeyError, ValueError, TypeError) as e:
            raise ApiError(400, 'check failed: %s' % e)
        return self.report(rep)

    def build(self, name, body):
        d = self.existing_kit(name)
        out = body.get('out') or 'out'
        out = out if os.path.isabs(out) else os.path.join(d, out)
        try:
            with self.lock:
                res = self.mk.build_t0(d, out, reencode=bool(body.get('reencode')), disks=self.disks)
                installed = None
                if body.get('install'):
                    hd = body.get('hd') or self.hd_default
                    if not hd:
                        raise ApiError(400, 'no HD folder given')
                    installed = {'hd': os.path.abspath(hd), 'files': self.mk.install_art(out, hd)}
        except self.mk.KitError as e:
            raise ApiError(409, str(e))
        except OSError as e:
            raise ApiError(500, 'build failed: %s' % e)
        return {'out': os.path.abspath(os.path.join(out, 'art')), 'files': res['files'], 'warnings': res['warnings'],
                'report': self.report(res['report']), 'installed': installed}


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = 'MonsterKit/1'
    api = None                                              # set by make_server

    def log_message(self, fmt, *args):                      # quiet by default
        if getattr(self.server, 'verbose', False):
            super().log_message(fmt, *args)

    # ---- plumbing
    def _send(self, status, body, ctype):
        self.send_response(status)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        if self.command != 'HEAD':
            self.wfile.write(body)

    def _json(self, status, obj):
        self._send(status, json.dumps(obj).encode('utf-8'), 'application/json')

    def _same_origin(self):
        port = self.server.server_address[1]
        hosts = ('%s:%d' % (HOST, port), 'localhost:%d' % port)
        if self.headers.get('Host') not in hosts:
            return False
        o = self.headers.get('Origin')
        return o is None or o in ['http://%s' % h for h in hosts]

    def _guard(self, write):
        if not self._same_origin():
            self._json(403, {'error': 'bad Host / Origin'})
            return False
        if write and self.headers.get('X-MonsterKit') != '1':
            self._json(403, {'error': 'missing X-MonsterKit header'})
            return False
        return True

    def _body(self):
        n = int(self.headers.get('Content-Length') or 0)
        if n > MAX_BODY:
            raise ApiError(413, 'body too large')
        raw = self.rfile.read(n) if n else b''
        if not raw:
            return {}
        try:
            return json.loads(raw.decode('utf-8'))
        except ValueError:
            raise ApiError(400, 'body is not JSON')

    def _static(self, path):
        rel = urllib.parse.unquote(path.split('?', 1)[0].split('#', 1)[0])
        if rel in ('', '/'):
            rel = '/index.html'
        if '\x00' in rel or '\\' in rel or ':' in rel or not rel.startswith('/') or any(p == '..' for p in rel.split('/')):
            return self._json(404, {'error': 'not found'})
        full = os.path.realpath(os.path.join(EDITOR_DIR, *rel.split('/')[1:]))
        root = os.path.realpath(EDITOR_DIR)
        try:
            inside = os.path.commonpath([full, root]) == root
        except ValueError:                                  # another drive
            inside = False
        if not inside or not os.path.isfile(full):
            return self._json(404, {'error': 'not found'})
        with open(full, 'rb') as f:
            data = f.read()
        self._send(200, data, TYPES.get(os.path.splitext(full)[1].lower(), 'application/octet-stream'))

    # ---- dispatch
    def _route(self, method):
        u = urllib.parse.urlsplit(self.path)
        parts = [urllib.parse.unquote(p) for p in u.path.strip('/').split('/')]
        if parts[0] != 'api':
            if method not in ('GET', 'HEAD'):
                return self._json(405, {'error': 'method not allowed'})
            try:
                return self._static(u.path)
            except OSError as e:
                return self._json(500, {'error': str(e)})
        a = self.api
        try:
            if method == 'GET' and parts[1:] == ['info']:
                return self._json(200, a.info())
            if method == 'GET' and parts[1:] == ['catalog']:
                return self._json(200, a.catalog())
            if method == 'GET' and parts[1:] == ['kits']:
                return self._json(200, a.kits())
            if method == 'POST' and parts[1:] == ['clone']:
                return self._json(200, a.clone(self._body()))
            if len(parts) >= 3 and parts[1] == 'kit':
                name = parts[2]
                rest = parts[3:]
                if not rest and method == 'GET':
                    return self._json(200, a.get_kit(name))
                if not rest and method == 'PUT':
                    return self._json(200, a.put_kit(name, self._body()))
                if len(rest) == 2 and rest[0] == 'sheet' and rest[1].isdigit():
                    if method == 'GET':
                        return self._json(200, a.get_sheet(name, int(rest[1])))
                    if method == 'PUT':
                        return self._json(200, a.put_sheet(name, int(rest[1]), self._body()))
                if rest == ['snap'] and method == 'POST':
                    return self._json(200, a.snap(name, self._body()))
                if rest == ['check'] and method == 'POST':
                    return self._json(200, a.check(name, self._body()))
                if rest == ['build'] and method == 'POST':
                    return self._json(200, a.build(name, self._body()))
            return self._json(404, {'error': 'no such API call'})
        except ApiError as e:
            return self._json(e.status, {'error': e.msg})
        except Exception as e:                                # a bug must not kill the server thread silently
            return self._json(500, {'error': '%s: %s' % (type(e).__name__, e)})

    def do_GET(self):
        if self._guard(False):
            self._route('GET')

    do_HEAD = do_GET

    def do_POST(self):
        if self._guard(True):
            self._route('POST')

    def do_PUT(self):
        if self._guard(True):
            self._route('PUT')


class PortInUse(OSError):
    pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    # No SO_REUSEADDR: on Windows it lets a second server bind a port that is already served (requests then land on either one,
    # e.g. an orphaned editor of another checkout).  SO_EXCLUSIVEADDRUSE makes the second bind fail loudly instead.
    allow_reuse_address = False
    verbose = False

    def server_bind(self):
        if hasattr(socket, 'SO_EXCLUSIVEADDRUSE'):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        try:
            super().server_bind()
        except OSError as e:
            raise PortInUse('port %d is in use (another editor server is running?): %s' % (self.server_address[1], e)) from e


def make_server(mk, port=8765, kits_root=None, disks=None, hd_default=None, host=HOST):
    """The configured HTTP server (not yet serving).  `mk` is the monsterkit module; port 0 picks a free port."""
    if host != HOST:
        raise ValueError('the monster kit server binds to %s only' % HOST)
    kits_root = kits_root or os.path.join(mk.ROOT, 'build', 'kits')
    disks = disks or mk.DISKS
    hd_default = hd_default or os.path.join(mk.ROOT, 'build', 'autoplay-hd')

    class H(Handler):
        pass
    H.api = Api(mk, kits_root, disks, hd_default)
    srv = Server((host, port), H)
    srv.api = H.api
    return srv


def serve(mk, port=8765, kits_root=None, disks=None, hd_default=None, open_browser=False):
    srv = make_server(mk, port, kits_root, disks, hd_default)
    srv.verbose = True
    url = 'http://%s:%d/' % (HOST, srv.server_address[1])
    print('monster kit editor: %s  (kits: %s, game data: %s)  Ctrl+C to stop' % (url, srv.api.kits_root, 'yes' if srv.api.game_available() else 'NO'))
    if open_browser:
        import webbrowser
        webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
    return 0
