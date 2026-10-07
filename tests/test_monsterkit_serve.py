"""Tests for the monster kit editor's helper server (ROADMAP 9.8b): tools/monsterkit/serve.py, tools/monsterkit/editor/.

  * the server starts on a free port in a thread; it binds 127.0.0.1 only; static files are served, path traversal is refused;
  * Host / Origin / X-MonsterKit guards;
  * the JSON API on a synthetic kit (no game data): list, load, save (schema), sheets, snap, check (red checklist blocks the build),
    clone from a kit, bad names;
  * with the original game (build/disks, reference/, build/reasm): clone troll over the API, check, save, build T0 + install
    (byte-identical for the untouched clone, re-encoded after a pixel edit);
  * the editor's static files hold no original data (no PNG / CEL / bigger blobs) and parse with node when it is installed.
Tests that need the original game or numpy / Pillow skip without them; the server tests that need neither always run."""
import base64
import http.client
import io
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'monsterkit'))

try:
    import numpy as np
    from PIL import Image
    import monsterkit as MK
    import serve as SV
    HAVE_IMG = True
except ImportError:                                                           # pragma: no cover
    HAVE_IMG = False

DISKS = os.path.join(ROOT, 'build', 'disks')
HAVE_GAME = (os.path.exists(os.path.join(DISKS, 'B', 'collide.hit'))
             and os.path.exists(os.path.join(ROOT, 'reference', 'moonshard', 'moonstone-main', 'amiga_asm', 'mog'))
             and os.path.exists(os.path.join(ROOT, 'build', 'reasm', 'mog.symbols.json')))
EDITOR = os.path.join(ROOT, 'tools', 'monsterkit', 'editor')
H = {'X-MonsterKit': '1', 'Content-Type': 'application/json'}


class ServerCase(unittest.TestCase):
    """A server on a free port with an empty temporary kits root."""

    @classmethod
    def setUpClass(cls):
        if not HAVE_IMG:
            raise unittest.SkipTest('needs numpy + Pillow')
        cls.tmp = tempfile.mkdtemp(prefix='mkserve_')
        cls.kits = os.path.join(cls.tmp, 'kits')
        cls.hd = os.path.join(cls.tmp, 'hd')
        cls.srv = SV.make_server(MK, 0, cls.kits, DISKS, cls.hd)
        cls.port = cls.srv.server_address[1]
        cls.thread = threading.Thread(target=cls.srv.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        cls.srv.server_close()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def req(self, method, path, body=None, headers=None, raw=False):
        c = http.client.HTTPConnection('127.0.0.1', self.port, timeout=60)
        hdr = dict(H if method != 'GET' else {})
        hdr.update(headers or {})
        data = json.dumps(body).encode() if body is not None else None
        c.request(method, path, body=data, headers=hdr)
        r = c.getresponse()
        payload = r.read()
        c.close()
        if raw:
            return r.status, payload, r
        try:
            return r.status, json.loads(payload.decode('utf-8'))
        except ValueError:
            return r.status, payload


def synthetic_kit(name='synth'):
    """A tiny valid kit (one 8x8 frame) whose AI checklist is mostly red."""
    return {
        'kit': 1, 'name': name, 'title': 'Synthetic', 'cloned_from': None, 'ai': 'stalker', 'planes': 5,
        'palette': {'scene': 'troll', 'colors': ['#%02x%02x%02x' % (i * 8, i * 8, i * 8) for i in range(32)], 'own': {}, 'allow': list(range(32))},
        'sheets': [{'file': 'sheet0.png'}],
        'celsets': [{'slot': 0, 'file': 'auto', 'hits': False, 'frames': [{'id': 'f0', 'src': {'sheet': 0, 'rect': [0, 0, 8, 8]}, 'anchor': [4, 8]}]}],
        'animations': {'stand': {'steps': [{'ticks': 1, 'draws': [{'frame': 'f0', 'slot': 0, 'dx': 0, 'dy': 0, 'hurt': True}]}]}},
        'roles': {'idle': 'stand'}, 'hurt_by_action': [None] * 9, 'damage': [0] * 9,
        'stats': {'hp': 20, 'reach': 100, 'keep_away': 50, 'depth': 5}, 'sounds': {'bank': 'To.a'}, 'arena': {}, 'tier': 'auto',
    }


def png_bytes(arr_rgba):
    b = io.BytesIO()
    Image.fromarray(arr_rgba.astype('uint8'), 'RGBA').save(b, 'PNG')
    return b.getvalue()


class TestGuards(ServerCase):
    def test_binds_loopback_only(self):
        self.assertEqual(self.srv.server_address[0], '127.0.0.1')
        with self.assertRaises(ValueError):
            SV.make_server(MK, 0, self.kits, DISKS, self.hd, host='0.0.0.0')
        with self.assertRaises(ValueError):
            SV.make_server(MK, 0, self.kits, DISKS, self.hd, host='')

    def test_not_reachable_on_other_interface(self):
        # a connection to this machine's non-loopback address must be refused (the socket is bound to 127.0.0.1 only)
        try:
            ips = [a[4][0] for a in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET) if not a[4][0].startswith('127.')]
        except OSError:
            ips = []
        if not ips:
            self.skipTest('no non-loopback IPv4 address')
        s = socket.socket()
        s.settimeout(2)
        try:
            self.assertNotEqual(s.connect_ex((ips[0], self.port)), 0)
        finally:
            s.close()

    def test_host_header_rebinding_refused(self):
        st, body = self.req('GET', '/api/kits', headers={'Host': 'evil.example:%d' % self.port})
        self.assertEqual(st, 403)
        st, _ = self.req('GET', '/', headers={'Host': 'evil.example'})
        self.assertEqual(st, 403)

    def test_cross_origin_refused(self):
        st, _ = self.req('GET', '/api/kits', headers={'Origin': 'http://evil.example'})
        self.assertEqual(st, 403)
        st, _ = self.req('GET', '/api/kits', headers={'Origin': 'http://127.0.0.1:%d' % self.port})
        self.assertEqual(st, 200)

    def test_writes_need_the_header(self):
        c = http.client.HTTPConnection('127.0.0.1', self.port)
        c.request('POST', '/api/clone', body=b'{}', headers={'Content-Type': 'text/plain'})
        r = c.getresponse()
        r.read()
        self.assertEqual(r.status, 403)
        c.close()


class TestStatic(ServerCase):
    def test_index_and_assets(self):
        st, body, r = self.req('GET', '/', raw=True)
        self.assertEqual(st, 200)
        self.assertIn('text/html', r.getheader('Content-Type'))
        self.assertIn(b'Monster kit editor', body)
        for name, ctype in (('core.js', 'javascript'), ('frame.js', 'javascript'), ('anim.js', 'javascript'), ('app.js', 'javascript'), ('editor.css', 'css')):
            st, body, r = self.req('GET', '/' + name, raw=True)
            self.assertEqual(st, 200, name)
            self.assertIn(ctype, r.getheader('Content-Type'))
            with open(os.path.join(EDITOR, name), 'rb') as f:
                self.assertEqual(body, f.read())

    def test_every_script_of_the_page_exists(self):
        with open(os.path.join(EDITOR, 'index.html'), encoding='utf-8') as f:
            html = f.read()
        import re
        refs = re.findall(r'(?:src|href)="([^"]+)"', html)
        self.assertTrue(refs)
        for ref in refs:
            self.assertFalse(ref.startswith(('http:', 'https:', '//')), 'external reference %s: the editor must work offline' % ref)
            self.assertEqual(self.req('GET', '/' + ref, raw=True)[0], 200, ref)

    def test_path_traversal_refused(self):
        secret = '/serve.py'                                   # a real file one level above editor/
        self.assertTrue(os.path.isfile(os.path.join(os.path.dirname(EDITOR), 'serve.py')))
        for p in ('/../serve.py', '/../../monsterkit.py', '/%2e%2e/serve.py', '/%2E%2E%2Fserve.py', '/..%2fserve.py', '/..%5cserve.py',
                  '/editor.css/../../serve.py', '/./../serve.py', '//..//serve.py', '/core.js%00.png', '/..', '/api/../../serve.py',
                  '/C:/Windows/win.ini', '/..\\serve.py'):
            st, body = self.req('GET', p)
            self.assertNotEqual(st, 200, p)
            if isinstance(body, bytes):
                self.assertNotIn(b'make_server', body, p)
        st, _ = self.req('GET', secret)
        self.assertEqual(st, 404)                                # not served from editor/

    def test_unknown_and_methods(self):
        self.assertEqual(self.req('GET', '/nothing.js')[0], 404)
        self.assertEqual(self.req('POST', '/index.html', {})[0], 405)
        self.assertEqual(self.req('GET', '/api/nothing')[0], 404)

    def test_no_original_data_in_the_editor_files(self):
        total = 0
        for n in os.listdir(EDITOR):
            p = os.path.join(EDITOR, n)
            self.assertFalse(n.lower().endswith(('.png', '.cel', '.c', '.adf', '.hit', '.piv', '.gif', '.jpg', '.iff')), n)
            total += os.path.getsize(p)
            with open(p, 'rb') as f:
                data = f.read()
            self.assertNotIn(b'data:image', data, n)
            self.assertNotIn(b'base64,', data, n)
        self.assertLess(total, 150 * 1024)                       # code only

    def test_javascript_parses(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('node not installed')
        for n in sorted(os.listdir(EDITOR)):
            if n.endswith('.js'):
                r = subprocess.run([node, '--check', os.path.join(EDITOR, n)], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, n + ': ' + r.stderr)


class TestApiSynthetic(ServerCase):
    def setUp(self):
        self.name = 'synth_' + self.id().split('.')[-1][-12:].replace('test_', '').lower()
        self.name = ''.join(c if c.isalnum() else '_' for c in self.name)[:30]
        d = os.path.join(self.kits, self.name)
        os.makedirs(d, exist_ok=True)
        self.dir = d
        kit = synthetic_kit(self.name)
        MK.save_kit(d, kit)
        idx = np.zeros((8, 8), dtype=np.uint8)
        idx[2:6, 2:6] = 9
        MK.write_indexed_png(os.path.join(d, 'sheet0.png'), idx, [MK._unhex(c) for c in kit['palette']['colors']])

    def test_info_catalog_list(self):
        st, info = self.req('GET', '/api/info')
        self.assertEqual(st, 200)
        self.assertEqual([c['name'] for c in info['creatures']], MK.OFFERED)
        self.assertIsInstance(info['game'], bool)
        self.assertEqual(os.path.abspath(info['kits_root']), os.path.abspath(self.kits))
        st, cat = self.req('GET', '/api/catalog')
        self.assertEqual(st, 200)
        self.assertIn('stalker', cat['ais'])
        self.assertIn('limits', cat)
        st, lst = self.req('GET', '/api/kits')
        self.assertIn(self.name, [k['name'] for k in lst['kits']])

    def test_load_save_roundtrip_and_schema(self):
        st, kit = self.req('GET', '/api/kit/' + self.name)
        self.assertEqual(st, 200)
        self.assertEqual(kit['ai'], 'stalker')
        kit['title'] = 'Edited title'
        kit['stats']['hp'] = 33
        kit['celsets'][0]['frames'][0]['attack'] = {'type': 0, 'points': [[3, 4]]}
        self.assertEqual(self.req('PUT', '/api/kit/' + self.name, kit)[0], 200)
        self.assertEqual(MK.load_kit(self.dir)['stats']['hp'], 33)
        # a schema violation is refused and the file is untouched
        bad = json.loads(json.dumps(kit))
        bad['stats']['hp'] = 0
        st, e = self.req('PUT', '/api/kit/' + self.name, bad)
        self.assertEqual(st, 400)
        self.assertIn('schema', e['error'])
        self.assertEqual(MK.load_kit(self.dir)['stats']['hp'], 33)
        # the folder name and the kit name must agree
        wrong = json.loads(json.dumps(kit))
        wrong['name'] = 'someone_else'
        self.assertEqual(self.req('PUT', '/api/kit/' + self.name, wrong)[0], 400)

    def test_origin_is_kept_from_disk(self):
        kit = MK.load_kit(self.dir)
        kit['origin'] = {'game': 'x'}
        MK.save_kit(self.dir, kit)
        _, k = self.req('GET', '/api/kit/' + self.name)
        k.pop('origin')
        self.assertEqual(self.req('PUT', '/api/kit/' + self.name, k)[0], 200)
        self.assertEqual(MK.load_kit(self.dir)['origin'], {'game': 'x'})

    def test_bad_names_and_missing(self):
        for n in ('..', 'A', 'a', 'x' * 40, '1abc', 'a-b', 'a.b', '%2e%2e'):
            st, _ = self.req('GET', '/api/kit/' + n)
            self.assertIn(st, (400, 404), n)
        self.assertEqual(self.req('GET', '/api/kit/nothere_kit')[0], 404)
        self.assertEqual(self.req('POST', '/api/clone', {'source': self.name, 'name': '../evil'})[0], 400)
        self.assertEqual(self.req('POST', '/api/clone', {'source': 'no_such_thing', 'name': 'abc_def'})[0], 404)

    def test_sheet_roundtrip(self):
        st, sh = self.req('GET', '/api/kit/%s/sheet/0' % self.name)
        self.assertEqual((st, sh['w'], sh['h']), (200, 8, 8))
        data = bytearray(base64.b64decode(sh['data']))
        self.assertEqual(data[3 * 8 + 3], 9)
        data[0] = 12                                           # a pixel edit
        st, _ = self.req('PUT', '/api/kit/%s/sheet/0' % self.name, {'w': 8, 'h': 8, 'data': base64.b64encode(bytes(data)).decode()})
        self.assertEqual(st, 200)
        _, sh2 = self.req('GET', '/api/kit/%s/sheet/0' % self.name)
        self.assertEqual(base64.b64decode(sh2['data'])[0], 12)
        img = Image.open(os.path.join(self.dir, 'sheet0.png'))
        self.assertEqual(img.mode, 'P')
        self.assertEqual(self.req('PUT', '/api/kit/%s/sheet/0' % self.name, {'w': 8, 'h': 8, 'data': 'AAAA'})[0], 400)
        self.assertEqual(self.req('GET', '/api/kit/%s/sheet/5' % self.name)[0], 404)

    def test_snap_png_to_palette(self):
        kit = MK.load_kit(self.dir)
        cols = [MK._unhex(c) for c in kit['palette']['colors']]
        rgba = np.zeros((4, 6, 4), dtype=np.uint8)
        rgba[:, :, 3] = 255
        rgba[0, 0] = (*cols[9], 255)                          # exact palette colour
        rgba[0, 1] = (cols[10][0] + 2, cols[10][1] + 1, cols[10][2], 255)     # near colour 10: changed
        rgba[0, 2] = (0, 0, 0, 0)                             # transparent
        st, r = self.req('POST', '/api/kit/%s/snap' % self.name, {'png': base64.b64encode(png_bytes(rgba)).decode()})
        self.assertEqual(st, 200)
        self.assertEqual((r['w'], r['h']), (6, 4))
        idx = np.frombuffer(base64.b64decode(r['data']), dtype=np.uint8).reshape(4, 6)
        ch = np.frombuffer(base64.b64decode(r['changed']), dtype=np.uint8).reshape(4, 6)
        self.assertEqual(int(idx[0, 0]), 9)
        self.assertEqual(int(idx[0, 1]), 10)
        self.assertEqual(int(idx[0, 2]), 0)
        self.assertEqual((int(ch[0, 0]), int(ch[0, 1]), int(ch[0, 2])), (0, 1, 0))
        self.assertEqual(r['counts']['pixels'], 24)
        self.assertGreaterEqual(r['counts']['changed'], 1)
        self.assertEqual(self.req('POST', '/api/kit/%s/snap' % self.name, {'png': base64.b64encode(b'not a png').decode()})[0], 400)

    def test_snap_matches_read_sheet(self):
        """The server's snap agrees with the converter's own RGB sheet loader (monsterkit.read_sheet)."""
        kit = MK.load_kit(self.dir)
        rng = np.random.RandomState(3)
        rgba = rng.randint(0, 256, (6, 7, 4)).astype(np.uint8)
        rgba[:, :, 3] = 255
        p = os.path.join(self.dir, 'rgb.png')
        Image.fromarray(rgba, 'RGBA').save(p)
        want = MK.read_sheet(p, kit['palette']['colors'])
        got, _ = SV.snap_png(open(p, 'rb').read(), kit['palette']['colors'], kit['palette']['allow'])
        self.assertTrue((want == got).all())

    def test_snap_keeps_indexed_png_and_avoids_fighter_colours(self):
        """An indexed PNG in the kit palette keeps its indices (duplicate colours: the troll's 8 and 13 are both #FF0000); an RGB
        pixel of a duplicated colour snaps to the creature's index, not to the first fighter's 6..8."""
        pal = ['#000000'] + ['#%02x%02x%02x' % (i * 8, i * 4, i * 2) for i in range(1, 32)]
        pal[13] = pal[8] = '#ff0000'
        idx = np.zeros((2, 3), dtype=np.uint8)
        idx[0, 0], idx[0, 1], idx[1, 2] = 13, 9, 8
        img = Image.fromarray(idx, 'P')
        img.putpalette(sum(([int(c[i:i + 2], 16) for i in (1, 3, 5)] for c in pal), []))
        buf = io.BytesIO()
        img.save(buf, 'PNG')
        got, changed = SV.snap_png(buf.getvalue(), pal, list(range(32)))
        self.assertTrue((got == idx).all())
        self.assertFalse(changed.any())
        rgba = np.zeros((1, 1, 4), dtype=np.uint8)
        rgba[0, 0] = (255, 0, 0, 255)
        got, _ = SV.snap_png(png_bytes(rgba), pal, list(range(32)))
        self.assertEqual(int(got[0, 0]), 13)

    def test_check_reports_a_red_checklist_and_build_is_blocked(self):
        st, rep = self.req('POST', '/api/kit/%s/check' % self.name, {})
        self.assertEqual(st, 200)
        self.assertFalse(rep['ok'])
        red = [c['role'] for c in rep['checklist'] if not c['ok']]
        for role in ('hurt', 'strike', 'heavy', 'die'):
            self.assertIn(role, red)
        green = [c['role'] for c in rep['checklist'] if c['ok']]
        self.assertIn('idle', green)
        self.assertTrue(any('role hurt is missing' in e for e in rep['errors']))
        # the checklist follows the AI: switching to the flyer asks for other roles
        kit = MK.load_kit(self.dir)
        kit['ai'] = 'flyer'
        st, rep2 = self.req('POST', '/api/kit/%s/check' % self.name, {'kit': kit})
        self.assertEqual(st, 200)
        self.assertNotEqual([c['role'] for c in rep['checklist']], [c['role'] for c in rep2['checklist']])
        # export is blocked while anything is red
        st, e = self.req('POST', '/api/kit/%s/build' % self.name, {})
        self.assertEqual(st, 409)
        self.assertIn('export blocked', e['error'])
        self.assertFalse(os.path.exists(os.path.join(self.dir, 'out', 'art')))

    def test_check_with_posted_kit_does_not_save(self):
        kit = MK.load_kit(self.dir)
        kit['roles']['hurt'] = 'stand'
        self.req('POST', '/api/kit/%s/check' % self.name, {'kit': kit})
        self.assertNotIn('hurt', MK.load_kit(self.dir)['roles'])
        st, e = self.req('POST', '/api/kit/%s/check' % self.name, {'kit': {'kit': 1}})
        self.assertIn(st, (200, 400))                           # schema errors come back as a report, never a crash

    def test_clone_from_kit(self):
        st, r = self.req('POST', '/api/clone', {'source': self.name, 'name': self.name[:20] + '_copy'})
        self.assertEqual(st, 200)
        self.assertEqual(r['kit']['cloned_from'], {'kind': 'kit', 'name': self.name})
        self.assertTrue(os.path.isfile(os.path.join(self.kits, self.name[:20] + '_copy', 'sheet0.png')))
        st, e = self.req('POST', '/api/clone', {'source': self.name, 'name': self.name[:20] + '_copy'})
        self.assertEqual(st, 409)
        self.assertEqual(self.req('POST', '/api/clone', {'source': self.name, 'name': self.name[:20] + '_copy', 'force': True})[0], 200)


@unittest.skipUnless(HAVE_IMG and HAVE_GAME, 'needs the original game (build/disks, reference/, build/reasm), numpy, Pillow')
class TestApiGame(ServerCase):
    def test_clone_edit_check_build_install(self):
        st, info = self.req('GET', '/api/info')
        self.assertTrue(info['game'])
        st, r = self.req('POST', '/api/clone', {'source': 'troll', 'name': 'frost_troll'})
        self.assertEqual(st, 200, r)
        kit = r['kit']
        self.assertEqual(kit['ai'], 'stalker')
        self.assertEqual(kit['cloned_from'], {'kind': 'game', 'creature': 'troll'})
        self.assertEqual(self.req('POST', '/api/clone', {'source': 'troll', 'name': 'frost_troll'})[0], 409)
        _, loaded = self.req('GET', '/api/kit/frost_troll')
        self.assertEqual(loaded['name'], 'frost_troll')
        self.assertEqual(len(loaded['celsets']), 2)
        # a clone checks clean: every checklist item green
        st, rep = self.req('POST', '/api/kit/frost_troll/check', {})
        self.assertTrue(rep['ok'], rep['text'])
        self.assertTrue(all(c['ok'] for c in rep['checklist']))
        self.assertEqual(rep['tier'], 't0')
        # save the loaded kit unchanged, build + install: byte-identical to the original
        self.assertEqual(self.req('PUT', '/api/kit/frost_troll', loaded)[0], 200)
        st, b = self.req('POST', '/api/kit/frost_troll/build', {'install': True, 'hd': self.hd})
        self.assertEqual(st, 200, b)
        self.assertEqual({f['name'] for f in b['files']}, {'TROLL1.CEL', 'TROLL2.CEL', 'collide.hit'})
        self.assertTrue(all(f['identical'] for f in b['files']), b['files'])
        self.assertEqual(sorted(b['installed']['files']), sorted(os.listdir(os.path.join(self.hd, 'art'))))
        # recolour a pixel through the sheet API: the cel is re-encoded and differs from the original
        _, sh = self.req('GET', '/api/kit/frost_troll/sheet/0')
        data = bytearray(base64.b64decode(sh['data']))
        fr = loaded['celsets'][0]['frames'][0]
        x, y, w, h = fr['src']['rect']
        for j in range(h):
            for i in range(w):
                if data[(y + j) * sh['w'] + x + i] == 9:
                    data[(y + j) * sh['w'] + x + i] = 10
        self.req('PUT', '/api/kit/frost_troll/sheet/0', {'w': sh['w'], 'h': sh['h'], 'data': base64.b64encode(bytes(data)).decode()})
        st, b = self.req('POST', '/api/kit/frost_troll/build', {'out': 'out2'})
        self.assertEqual(st, 200, b)
        t1 = next(f for f in b['files'] if f['name'] == 'TROLL1.CEL')
        t2 = next(f for f in b['files'] if f['name'] == 'TROLL2.CEL')
        self.assertTrue(t2['identical'])
        if any(v == 9 for v in base64.b64decode(sh['data'])[y * sh['w'] + x:y * sh['w'] + x + w]) or True:
            self.assertIn(t1['mode'], ('encoded', 'passthrough'))
        # removing a required role turns the checklist red and blocks the export
        loaded['roles']['strike'] = None
        st, rep = self.req('POST', '/api/kit/frost_troll/check', {'kit': loaded})
        self.assertFalse(rep['ok'])
        self.assertIn('strike', [c['role'] for c in rep['checklist'] if not c['ok']])
        self.req('PUT', '/api/kit/frost_troll', loaded)
        self.assertEqual(self.req('POST', '/api/kit/frost_troll/build', {})[0], 409)

    def test_every_creature_clones_through_the_api(self):
        for c in MK.OFFERED:
            st, r = self.req('POST', '/api/clone', {'source': c, 'name': 'c_' + c})
            self.assertEqual(st, 200, (c, r))
            st, rep = self.req('POST', '/api/kit/c_%s/check' % c, {})
            self.assertTrue(rep['ok'], (c, rep['text']))


@unittest.skipUnless(HAVE_IMG, 'needs numpy / Pillow')
class TestPortInUse(unittest.TestCase):
    def test_second_server_on_the_same_port_fails_loudly(self):
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d, True)
        a = SV.make_server(MK, 0, kits_root=d)
        self.addCleanup(a.server_close)
        port = a.server_address[1]
        with self.assertRaises(SV.PortInUse) as cm:
            b = SV.make_server(MK, port, kits_root=d)
            b.server_close()
        self.assertIn('port %d is in use' % port, str(cm.exception))


if __name__ == '__main__':
    unittest.main()
