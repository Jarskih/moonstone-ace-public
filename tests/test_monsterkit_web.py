"""Tests for the standalone web edition of the monster kit editor (tools/monsterkit/build_web.py, editor_web/).

  * monster_editor.html is current with its sources (editor/*.js, editor.css, editor_web/*, catalog, schema);
  * it is an Artifact page fragment: starts with <title>, no doctype / html / head / body tags, under 2 MB, no alert / confirm / prompt,
    no external resource outside the allowed CDNs, colour tokens with the dark-mode guard;
  * it holds no original bytes: no embedded PNG data and tools/leakscan.py is clean when the extracted disks exist;
  * every inline script parses (node --check) and the in-browser check (WCHK) passes the example kit and fails a broken one (node).
Tests that need node or the extracted disks skip without them."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'monsterkit'))
import build_web  # noqa: E402

NODE = shutil.which('node')
OUT = build_web.OUT
SRC = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''


def scripts():
    return re.findall(r'<script(?: type="application/json" id="[^"]+")?>(.*?)</script>', SRC, re.S)


class TestWebEdition(unittest.TestCase):
    def test_built_file_is_current(self):
        self.assertTrue(os.path.exists(OUT), 'run py tools/monsterkit/build_web.py')
        with open(OUT, 'rb') as f:
            self.assertEqual(f.read(), build_web.build().encode('utf-8'), 'stale: run py tools/monsterkit/build_web.py')

    def test_artifact_fragment_contract(self):
        self.assertTrue(SRC.startswith('<title>Moonstone Monster Editor</title>\n<style>'))
        self.assertLess(len(SRC.encode('utf-8')), 2 * 1024 * 1024)
        low = SRC.lower()
        for tag in ('<!doctype', '<html', '<head', '<body', '</body', '</html'):
            self.assertNotIn(tag, low)
        for call in ('alert(', 'confirm(', 'prompt('):
            self.assertNotRegex(SRC, r'(?<![\w.])' + re.escape(call), call)
        self.assertNotRegex(SRC, r'<script[^>]+src=')
        self.assertNotRegex(SRC, r'<link[^>]+href=(?!"https://fonts\.)')
        for host in re.findall(r'https?://([^/"\'\s)]+)', SRC):
            self.assertIn(host, ('cdnjs.cloudflare.com', 'fonts.googleapis.com', 'fonts.gstatic.com', 'json-schema.org'))
        self.assertIn(':root:not([data-theme="light"])', SRC)
        self.assertIn(':root[data-theme="dark"]', SRC)
        self.assertIn('color-scheme: dark', SRC)
        self.assertIn('prefers-reduced-motion', SRC)
        self.assertIn(':focus-visible', SRC)

    def test_no_original_bytes(self):
        self.assertNotIn('iVBORw0KGgo', SRC)                       # an embedded PNG
        disks = os.path.join(ROOT, 'build', 'disks')
        if not os.path.isdir(disks):
            self.skipTest('no extracted disks')
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'leakscan.py'), '--tree', os.path.dirname(OUT)], capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    @unittest.skipUnless(NODE, 'needs node')
    def test_scripts_parse(self):
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d, True)
        for i, s in enumerate(scripts()):
            if s.lstrip().startswith('{'):
                continue                                          # JSON data block
            p = os.path.join(d, 's%d.js' % i)
            with open(p, 'w', encoding='utf-8') as f:
                f.write(s)
            r = subprocess.run([NODE, '--check', p], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)

    @unittest.skipUnless(NODE, 'needs node')
    def test_example_kit_checks_green_and_breaks_when_edited(self):
        d = os.path.join(ROOT, 'tools', 'monsterkit')
        js = r"""
const fs = require('fs'), vm = require('vm');
globalThis.window = globalThis;
const load = (p) => vm.runInThisContext(fs.readFileSync(p, 'utf8') + '\n;globalThis.__x = 1;', { filename: p });
const E = %(ed)r;
const src = (n) => fs.readFileSync(E + n, 'utf8');
vm.runInThisContext(src('web_io.js').replace('const WIO', 'globalThis.WIO'));
vm.runInThisContext(src('web_check.js').replace('const WCHK', 'globalThis.WCHK'));
vm.runInThisContext(src('web_example.js').replace('const WEX', 'globalThis.WEX'));
const cat = JSON.parse(fs.readFileSync(%(cat)r, 'utf8')), schema = JSON.parse(fs.readFileSync(%(sch)r, 'utf8'));
const ex = WEX.build();
let r = WCHK.check(ex.kit, ex.sheets, cat, schema);
if (!r.ok || r.errors.length || r.tier !== 't2' || r.checklist.some((c) => !c.ok) || r.checklist.length !== 7) { console.log('GREEN FAILED', JSON.stringify(r)); process.exit(1); }
if (ex.kit.celsets[0].frames.some((f) => f.src.rect[2] !== 64)) { console.log('frame size'); process.exit(1); }
const k2 = JSON.parse(JSON.stringify(ex.kit)); k2.roles.strike = null; k2.roles.die = null;
r = WCHK.check(k2, ex.sheets, cat, schema);
if (r.ok || r.errors.length < 2) { console.log('RED FAILED', JSON.stringify(r)); process.exit(1); }
const k3 = JSON.parse(JSON.stringify(ex.kit)); k3.planes = 7;
if (WCHK.check(k3, ex.sheets, cat, schema).ok) { console.log('schema not enforced'); process.exit(1); }
const k4 = JSON.parse(JSON.stringify(ex.kit)); k4.celsets[0].frames[0].src.rect = [600, 0, 64, 40];
if (WCHK.check(k4, ex.sheets, cat, schema).ok) { console.log('rect outside not caught'); process.exit(1); }
if (WIO.sha1hex('abc') !== 'a9993e364706816aba3e25717850c26c9cd0d89d') { console.log('sha1'); process.exit(1); }
(async () => {
  const png = await WIO.pngEncode(ex.sheets[0].w, ex.sheets[0].h, ex.sheets[0].data, ex.kit.palette.colors);
  const back = await WIO.pngIndexed(png);
  if (back.w !== ex.sheets[0].w || Buffer.compare(Buffer.from(back.data), Buffer.from(ex.sheets[0].data))) { console.log('png roundtrip'); process.exit(1); }
  const z = await WIO.zipRead(WIO.zipWrite([{ name: 'a/kit.json', bytes: WIO.utf8('{"x":1}') }, { name: 'b.png', bytes: png }]));
  if (z.length !== 2 || WIO.unutf8(z[0].bytes) !== '{"x":1}' || z[1].bytes.length !== png.length) { console.log('zip roundtrip'); process.exit(1); }
  console.log('OK');
})();
""" % {'ed': os.path.join(d, 'editor_web') + os.sep, 'cat': os.path.join(d, 'ai_catalog.json'), 'sch': os.path.join(d, 'kit.schema.json')}
        r = subprocess.run([NODE, '-e', js], capture_output=True, text=True)
        self.assertEqual(r.stdout.strip(), 'OK', r.stdout + r.stderr)


if __name__ == '__main__':
    unittest.main()
