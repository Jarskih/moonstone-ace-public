#!/usr/bin/env python3
"""build_web.py -- the standalone, single-file web edition of the monster kit editor (ROADMAP 9.8w).

    py tools/monsterkit/build_web.py            # write tools/monsterkit/editor_web/monster_editor.html
    py tools/monsterkit/build_web.py --check    # exit 1 when the committed file is not what the sources produce

The page is a *fragment* for the claude.ai Artifact wrapper: <title>, <style>, the body markup and <script>s, with no doctype / html /
head / body tags.  It is generated from the same sources as the local editor (editor/{editor.css,core.js,frame.js,anim.js,app.js})
plus editor_web/{web.css,web_io.js,web_check.js,web_example.js,web.js,body.html}; the embedded catalog and schema hold names and
rules only (no original bytes: confirm with tools/leakscan.py).  Nothing is fetched at run time."""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ED = os.path.join(HERE, 'editor')
WB = os.path.join(HERE, 'editor_web')
OUT = os.path.join(WB, 'monster_editor.html')
TITLE = 'Moonstone Monster Editor'
# order matters: core defines MK, the web files override MK.api and define MK.web before app.js can boot
SCRIPTS = [(ED, 'core.js'), (WB, 'web_io.js'), (WB, 'web_check.js'), (WB, 'web_example.js'), (WB, 'web.js'),
           (ED, 'frame.js'), (ED, 'anim.js'), (ED, 'app.js')]


def read(d, n):
    with open(os.path.join(d, n), encoding='utf-8', newline='') as f:
        return f.read().replace('\r\n', '\n')


def safe_json(path):
    with open(path, encoding='utf-8') as f:
        obj = json.load(f)
    return json.dumps(obj, separators=(',', ':'), ensure_ascii=True).replace('<', '\\u003c').replace('>', '\\u003e').replace('&', '\\u0026')


def build():
    css = read(ED, 'editor.css')
    m = re.match(r':root \{.*?\n\}\n', css, re.S)
    assert m, 'editor.css must start with its :root token block'
    css = css[m.end():]
    parts = ['<title>%s</title>\n<style>\n%s\n%s</style>\n' % (TITLE, css.rstrip('\n'), read(WB, 'web.css').rstrip('\n'))]
    parts.append(read(WB, 'body.html'))
    parts.append('<script type="application/json" id="mk-catalog">%s</script>\n' % safe_json(os.path.join(HERE, 'ai_catalog.json')))
    parts.append('<script type="application/json" id="mk-schema">%s</script>\n' % safe_json(os.path.join(HERE, 'kit.schema.json')))
    for d, n in SCRIPTS:
        js = read(d, n)
        for bad in ('</script', '<!--'):
            assert bad not in js, '%s contains %s' % (n, bad)
        parts.append('<script>\n/* ---- %s */\n%s</script>\n' % (n, js))
    return ''.join(parts)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    text = build()
    data = text.encode('utf-8')
    if '--check' in argv:
        cur = open(OUT, 'rb').read() if os.path.exists(OUT) else b''
        if cur != data:
            print('monster_editor.html is stale: run py tools/monsterkit/build_web.py')
            return 1
        print('monster_editor.html is current (%d bytes)' % len(data))
        return 0
    with open(OUT, 'wb') as f:
        f.write(data)
    print('wrote %s (%d bytes)' % (OUT, len(data)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
