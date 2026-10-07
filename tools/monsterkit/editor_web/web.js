'use strict';
/* Monster kit editor, web edition, part 4: replaces the local server.  MK.api becomes an in-page store (kits opened from the
   viewer's disk or the generated example), the check runs in the browser, and "save" means copy kit.json / copy a PNG as a data URL
   (an Artifact sandbox blocks page-initiated downloads; a .zip link is offered as an extra).  Nothing leaves the page. */
(function () {
  const { S, h, $, clear } = MK;
  const W = MK.web = { store: {}, exampleName: WEX.NAME, msgs: [], pending: null, exp: null };
  const CAT = JSON.parse(document.getElementById('mk-catalog').textContent);
  const SCHEMA = JSON.parse(document.getElementById('mk-schema').textContent);
  const clone = (o) => JSON.parse(JSON.stringify(o));
  const plural = (n, w) => n + ' ' + w + (n === 1 ? '' : 's');

  /* ------------------------------------------------------------ the in-page API */
  const fail = (m) => { throw new Error(m); };
  const entry = (name) => W.store[name] || fail('no kit ' + name);

  async function snap(name, body) {
    const kit = (name === S.name && S.kit) ? S.kit : entry(name).kit;
    const bytes = MK.b64d(body.png), img = await WIO.imageRGBA(bytes);
    const pal = (kit.palette.colors || []).map((c) => [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16)));
    if (!pal.length) fail('this kit has no palette.colors to snap to');
    const allow = kit.palette.allow, ok = [];
    pal.forEach((_, i) => { if (i === 0 || !allow || allow.includes(i)) ok.push(i); });
    const n = img.w * img.h, data = new Uint8Array(n), changed = new Uint8Array(n);
    let opaque = 0, nchanged = 0, forbidden = 0;
    for (let i = 0; i < n; i++) {
      const r = img.rgba[i * 4], g = img.rgba[i * 4 + 1], b = img.rgba[i * 4 + 2];
      if (img.rgba[i * 4 + 3] < 128) continue;
      let best = 1e12, bi = 0;
      for (const k of ok) { const p = pal[k], d = (r - p[0]) ** 2 + (g - p[1]) ** 2 + (b - p[2]) ** 2 + (k === 0 ? 1 : 0); if (d < best) { best = d; bi = k; } }
      data[i] = bi; opaque += bi !== 0 ? 1 : 0;
      const p = pal[bi];
      if (p[0] !== r || p[1] !== g || p[2] !== b) { changed[i] = 1; nchanged++; }
      if (WCHK.FORBIDDEN.includes(bi)) forbidden++;
    }
    return { w: img.w, h: img.h, data: MK.b64e(data), changed: MK.b64e(changed), counts: { pixels: n, opaque, changed: nchanged, forbidden } };
  }

  MK.api = async function (method, url, body) {
    const p = url.split('?')[0].split('/').filter(Boolean);                // api, kit, <name>, ...
    if (method === 'GET' && p[1] === 'info') return { game: false, kits_root: 'this page', disks: '', hd_default: '', creatures: [], sheet_width: 640, kit_version: 1 };
    if (method === 'GET' && p[1] === 'catalog') return CAT;
    if (method === 'GET' && p[1] === 'kits') {
      return { kits: Object.values(W.store).map((e) => ({ name: e.kit.name, title: e.kit.title || '', ai: e.kit.ai, cloned_from: e.kit.cloned_from, celsets: e.kit.celsets.length, animations: Object.keys(e.kit.animations).length })) };
    }
    if (p[1] === 'kit' && p[2]) {
      const name = p[2], rest = p.slice(3);
      if (!rest.length && method === 'GET') return clone(entry(name).kit);
      if (!rest.length && method === 'PUT') {
        const errs = WIO.validate(body, SCHEMA);
        if (errs.length) fail('schema: ' + errs.slice(0, 8).join('; '));
        const e = entry(name); if (!body.origin && e.kit.origin) body.origin = e.kit.origin;
        e.kit = clone(body); return { ok: true };
      }
      if (rest[0] === 'sheet' && /^\d+$/.test(rest[1])) {
        const e = entry(name), n = +rest[1];
        if (method === 'GET') { const s = e.sheets[n] || fail('kit ' + name + ' has no sheet ' + n); return { w: s.w, h: s.h, data: MK.b64e(s.data) }; }
        if (method === 'PUT') { e.sheets[n] = { file: (e.sheets[n] || {}).file, w: body.w, h: body.h, data: MK.b64d(body.data) }; return { ok: true }; }
      }
      if (rest[0] === 'snap' && method === 'POST') return snap(name, body);
      if (rest[0] === 'check' && method === 'POST') {
        const e = entry(name), kit = body && body.kit ? body.kit : e.kit;
        return WCHK.check(kit, name === S.name ? S.sheets : e.sheets, CAT, SCHEMA);
      }
    }
    throw new Error('not available in the web editor: ' + method + ' ' + url);
  };

  /* "save" keeps the edits in this page; the file itself leaves through the Save / export tab */
  MK.save = async function () {
    if (!S.kit) return;
    for (const n of S.dirtySheets) { const sh = S.sheets[n]; await MK.api('PUT', '/api/kit/' + S.name + '/sheet/' + n, { w: sh.w, h: sh.h, data: MK.b64e(sh.data) }); }
    S.dirtySheets = new Set();
    await MK.api('PUT', '/api/kit/' + S.name, S.kit);
    S.dirtyKit = false; MK.emit('dirty');
    MK.log('ok', 'kept in this page. Use "Save / export" to copy kit.json and the sheet PNGs out.');
  };

  /* ------------------------------------------------------------ opening kits */
  function addKit(kit, sheets) {
    const errs = WIO.validate(kit, SCHEMA);
    if (errs.length) fail('kit.json does not match the schema: ' + errs.slice(0, 6).join('; ') + (errs.length > 6 ? ' ...' : ''));
    W.store[kit.name] = { kit, sheets };
    return kit.name;
  }
  W.loadExample = function () { const e = WEX.build(); return addKit(e.kit, e.sheets); };

  async function gather(files) {
    const map = new Map();
    for (const f of files) {
      const file = f.file || f, path = f.path || file.webkitRelativePath || file.name, bytes = new Uint8Array(await file.arrayBuffer());
      if (/\.zip$/i.test(file.name)) { for (const z of await WIO.zipRead(bytes)) map.set(z.name.replace(/\\/g, '/'), z.bytes); }
      else map.set(path.replace(/\\/g, '/'), bytes);
    }
    return map;
  }
  async function kitFromFiles(files) {
    const map = await gather(files), paths = [...map.keys()];
    const cands = paths.filter((p) => /(^|\/)kit\.json$/i.test(p)).sort((a, b) => a.split('/').length - b.split('/').length);
    const js = cands[0] || (paths.filter((p) => /\.json$/i.test(p)).length === 1 ? paths.find((p) => /\.json$/i.test(p)) : null);
    if (!js) fail('no kit.json found among ' + plural(paths.length, 'file') + ' (select kit.json together with its sheet PNGs, or one .zip of the kit folder)');
    let kit;
    try { kit = JSON.parse(WIO.unutf8(map.get(js))); } catch (e) { fail(js + ' is not valid JSON: ' + e.message); }
    const base = js.includes('/') ? js.slice(0, js.lastIndexOf('/') + 1) : '';
    const sheets = [];
    for (const [n, s] of (Array.isArray(kit.sheets) ? kit.sheets : []).entries()) {
      const want = String(s.file || '');
      let bytes = map.get(base + want) || map.get(want);
      if (!bytes) { const bn = want.split('/').pop().toLowerCase(), hit = paths.find((p) => p.split('/').pop().toLowerCase() === bn); if (hit) bytes = map.get(hit); }
      if (!bytes) fail('sheet ' + n + ' (' + want + ') is missing: select its PNG together with kit.json');
      let ix;
      try { ix = await WIO.pngIndexed(bytes); } catch (e) { fail(want + ': ' + e.message); }
      if (ix) sheets.push({ file: want, w: ix.w, h: ix.h, data: ix.data });
      else {
        const cols = kit.palette && kit.palette.colors;
        if (!cols || !cols.length) fail(want + ' is an RGB PNG: the kit needs palette.colors to snap it to');
        const img = await WIO.imageRGBA(bytes), pal = cols.map((c) => [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16))), d = new Uint8Array(img.w * img.h);
        for (let i = 0; i < d.length; i++) {
          if (img.rgba[i * 4 + 3] < 128) continue;
          let best = 1e12, bi = 0;
          pal.forEach((p, k) => { const dd = (img.rgba[i * 4] - p[0]) ** 2 + (img.rgba[i * 4 + 1] - p[1]) ** 2 + (img.rgba[i * 4 + 2] - p[2]) ** 2 + (k === 0 ? 1 : 0); if (dd < best) { best = dd; bi = k; } });
          d[i] = bi;
        }
        sheets.push({ file: want, w: img.w, h: img.h, data: d });
      }
    }
    return addKit(kit, sheets);
  }

  /* ask inside the page instead of a confirm dialog */
  W.guard = function (what, go) {
    if (!MK.isDirty()) { go(); return; }
    W.pending = { what, go }; renderBar();
  };
  function renderBar() {
    let bar = $('#confirmbar');
    if (!W.pending) { if (bar) bar.remove(); return; }
    if (!bar) { bar = h('div', { id: 'confirmbar', class: 'confirmbar', role: 'alertdialog', 'aria-label': 'Confirm' }); $('#app').append(bar); }
    clear(bar);
    bar.append(h('span', {}, 'You have unsaved edits to ' + S.name + '. ' + W.pending.what + ' would discard them. Copy kit.json first if you want to keep them.'),
      h('button', { class: 'primary', onclick: () => { const g = W.pending.go; W.pending = null; renderBar(); g(); } }, 'Discard and continue'),
      h('button', { onclick: () => { W.pending = null; renderBar(); } }, 'Keep editing'));
    const b = bar.querySelector('button'); if (b) b.focus();
  }

  async function openName(name) {
    await MK.loadKit(name); S.view = 'edit'; S.bottom = 'check'; MK.emit('view'); MK.renderBottom();
  }
  W.openFiles = function (files) {
    files = [...files];
    if (!files.length) return;
    W.guard('Opening another kit', async () => {
      W.msgs = [];
      try {
        MK.log('info', 'reading ' + plural(files.length, 'file') + ' ...');
        const name = await kitFromFiles(files);
        await openName(name);
        MK.log('ok', 'opened kit ' + name + ' from your disk (it stays in this page).');
      } catch (e) { W.msgs = [e.message]; MK.log('err', e.message); S.view = 'home'; renderHomeAgain(); }
    });
  };
  W.openExample = function () {
    W.guard('Loading the example kit', async () => { W.msgs = []; await openName(W.loadExample()); MK.log('info', 'Example kit loaded: drawn by code, not from the game.'); });
  };
  const renderHomeAgain = () => MK.emit('view');

  async function entriesOf(item) {                      // a dropped folder
    const out = [];
    const walk = async (en, path) => {
      if (en.isFile) { const f = await new Promise((res, rej) => en.file(res, rej)); out.push({ file: f, path: path + en.name }); }
      else if (en.isDirectory) {
        const rd = en.createReader();
        for (;;) { const batch = await new Promise((res, rej) => rd.readEntries(res, rej)); if (!batch.length) break; for (const c of batch) await walk(c, path + en.name + '/'); }
      }
    };
    await walk(item, '');
    return out;
  }

  /* ------------------------------------------------------------ UI */
  function pickers() {
    if ($('#webfiles')) return;
    const app = $('#app');
    app.append(
      h('input', { type: 'file', id: 'webfiles', multiple: true, accept: '.zip,.json,.png,application/zip,application/json,image/png', hidden: true, onchange: (e) => { const f = [...e.target.files]; e.target.value = ''; W.openFiles(f); } }),
      h('input', { type: 'file', id: 'webdir', webkitdirectory: true, hidden: true, onchange: (e) => { const f = [...e.target.files]; e.target.value = ''; W.openFiles(f); } }));
    const over = (e) => { if (e.dataTransfer && [...(e.dataTransfer.types || [])].includes('Files')) { e.preventDefault(); app.classList.add('dropping'); } };
    document.addEventListener('dragover', over);
    document.addEventListener('dragleave', (e) => { if (!e.relatedTarget) app.classList.remove('dropping'); });
    document.addEventListener('drop', async (e) => {
      app.classList.remove('dropping');
      if (!e.dataTransfer || !e.dataTransfer.files.length) return;
      e.preventDefault();
      try {
        const items = [...(e.dataTransfer.items || [])].map((i) => (i.webkitGetAsEntry ? i.webkitGetAsEntry() : null));
        if (items.length && items.every(Boolean) && items.some((i) => i.isDirectory)) { let all = []; for (const it of items) all = all.concat(await entriesOf(it)); W.openFiles(all); }
        else W.openFiles(e.dataTransfer.files);
      } catch (err) { MK.log('err', err.message); }
    });
  }

  W.renderTopbar = function (top) {
    const isEx = S.kit && S.name === W.exampleName;
    top.append(h('b', { class: 'brand' }, 'Monster kit editor'),
      h('button', { onclick: () => $('#webfiles').click(), title: 'a .zip of a kit folder, or kit.json together with its sheet PNGs' }, 'Open kit ...'),
      h('button', { onclick: () => { S.view = S.view === 'home' && S.kit ? 'edit' : 'home'; MK.emit('view'); } }, S.view === 'home' && S.kit ? 'Back to the editor' : 'Help / more'),
      h('span', { class: 'kname' }, S.kit ? S.name : 'no kit open'),
      isEx ? h('span', { class: 'badge warn exbadge' }, 'Example kit (not from the game)') : '',
      h('span', { id: 'tier', class: 'badge' }, ''),
      h('span', { id: 'dirty', class: 'warn' }, ''),
      h('span', { class: 'grow' }),
      h('button', { id: 'btn-check', onclick: async () => { await MK.runCheck(true); S.bottom = 'check'; MK.renderBottom(); } }, 'Check'),
      h('button', { id: 'btn-save', class: 'primary', onclick: async () => { try { await MK.save(); } catch (e) { MK.log('err', e.message); } S.bottom = 'build'; MK.renderBottom(); const b = $('#bottom'); if (b && b.scrollIntoView) b.scrollIntoView({ block: 'nearest' }); } }, 'Save / export ...'));
  };
  W.paletteNote = function () {
    return S.name === W.exampleName ? h('div', { class: 'small warn' }, 'This is a neutral example palette, not the game palette. Open a kit to see its own colours.') : null;
  };

  W.renderHome = function (root) {
    clear(root);
    root.append(h('div', { class: 'home' },
      h('h2', {}, 'Monster kit editor, web edition'),
      h('p', { class: 'dim' }, 'Everything runs in this page. Your kit never leaves your computer: it is read from your disk, edited here and copied back out by you.'),
      W.msgs.length ? h('div', { class: 'bad', role: 'alert' }, W.msgs.map((m) => h('div', {}, m))) : null,
      h('h2', {}, 'Open a kit'),
      h('div', { class: 'dropzone', tabindex: '0', role: 'button', 'aria-label': 'Open a kit: choose files or drop them here', onclick: () => $('#webfiles').click(), onkeydown: (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); $('#webfiles').click(); } } },
        h('b', {}, 'Drop a kit here, or click to choose'),
        h('div', { class: 'dim small' }, 'a .zip of a kit folder, or kit.json plus its sheet PNG(s) selected together, or a whole kit folder')),
      h('div', { class: 'btnrow wrap' },
        h('button', { class: 'primary', onclick: () => $('#webfiles').click() }, 'Choose files ...'),
        h('button', { onclick: () => $('#webdir').click() }, 'Choose a kit folder ...'),
        h('button', { onclick: () => W.openExample() }, 'Load the example kit'),
        S.kit ? h('button', { onclick: () => { S.view = 'edit'; MK.emit('view'); } }, 'Back to the editor') : null),
      Object.keys(W.store).length ? h('div', { class: 'kitlist' }, Object.values(W.store).map((e) => h('div', { class: 'kititem' },
        h('b', {}, e.kit.name), h('span', { class: 'dim' }, (e.kit.title || '') + ' - AI ' + e.kit.ai + ' - ' + plural(e.kit.celsets.length, 'cel set') + ', ' + plural(Object.keys(e.kit.animations).length, 'animation')),
        h('span', { class: 'grow' }),
        h('button', { onclick: () => W.guard('Switching kit', () => openName(e.kit.name)) }, 'Open')))) : null,
      h('h2', {}, 'How saving works here'),
      h('p', {}, 'This page runs in a sandbox that blocks downloads started by a page, so "Save" does not write a file. Instead, the Save / export tab gives you:'),
      h('ul', {}, h('li', {}, 'Copy kit.json: puts the kit description on the clipboard (if the browser refuses, a text box with the JSON is selected for Ctrl+C).'),
        h('li', {}, 'Per sheet: Copy PNG as data URL, and the PNG shown as an image you can right-click and "Save image as".'),
        h('li', {}, 'A .zip link as an extra; it may be blocked here, the copy buttons always work.')),
      h('p', { class: 'dim' }, 'Paste kit.json over the one in your kit folder and save the sheets next to it, then use py tools/monsterkit.py check / build on your PC. The check here is the same checklist without the original game data (sound ids are not checked).')));
  };

  /* ---- Save / export */
  function copyText(text, area, what) {
    const manual = (why) => { area.hidden = false; area.value = text; area.focus(); area.select(); try { document.execCommand('copy'); } catch (e) { /* manual copy */ } MK.log('info', 'Clipboard blocked' + (why ? ' (' + why + ')' : '') + ': ' + what + ' is selected in the box below, press Ctrl+C.'); };
    try {
      const p = navigator.clipboard && navigator.clipboard.writeText ? navigator.clipboard.writeText(text) : null;
      if (!p) { manual('no clipboard API'); return; }
      p.then(() => MK.log('ok', 'Copied ' + what + ' (' + text.length + ' characters).')).catch((e) => manual(e && e.name));
    } catch (e) { manual(e && e.name); }
  }
  async function prepareExport() {
    const kit = S.kit, ver = S.ver + ':' + S.name;
    if (W.exp && W.exp.ver === ver) return W.exp;
    const exp = { ver, kitText: JSON.stringify(kit, null, 1) + '\n', sheets: [] };
    const cols = kit.palette.colors || [];
    for (let n = 0; n < kit.sheets.length; n++) {
      const sh = S.sheets[n];
      if (!sh) continue;
      const png = await WIO.pngEncode(sh.w, sh.h, sh.data, cols);
      exp.sheets.push({ n, file: kit.sheets[n].file, w: sh.w, h: sh.h, bytes: png, url: 'data:image/png;base64,' + MK.b64e(png) });
    }
    W.exp = exp;
    return exp;
  }
  W.renderExport = function (body) {
    const rep = S.report;
    const ta = h('textarea', { class: 'jsonbox', readonly: true, rows: 6, 'aria-label': 'kit.json text', spellcheck: 'false' });
    const zipbox = h('div', { class: 'small' });
    const sheets = h('div', { class: 'exsheets' }, h('div', { class: 'dim small' }, 'preparing the PNG sheets ...'));
    const copyBtn = h('button', { class: 'primary', disabled: true }, 'Copy kit.json');
    const zipBtn = h('button', { disabled: true, title: 'builds a .zip of the kit folder in memory and shows a download link' }, 'Prepare .zip (extra)');
    body.append(h('div', { class: 'exportwrap' },
      h('div', { class: 'exportctl' },
        h('div', { class: 'small dim' }, 'The sandbox this page runs in blocks downloads, so the files leave through the clipboard. Always exports the kit as it is now, including edits you did not "save".'),
        rep && !rep.ok ? h('div', { class: 'warn small' }, 'The check shows ' + plural(rep.errors.length, 'error') + ': the kit exports anyway, but the game build would be blocked.') : null,
        h('div', { class: 'btnrow wrap' }, copyBtn, zipBtn),
        zipbox,
        h('label', { class: 'small dim' }, 'kit.json (if the clipboard is blocked, select all and copy):'), ta),
      sheets));
    ta.hidden = false;
    prepareExport().then((exp) => {
      if (!ta.isConnected) return;
      ta.value = exp.kitText;
      copyBtn.disabled = false; zipBtn.disabled = false;
      copyBtn.addEventListener('click', () => copyText(exp.kitText, ta, 'kit.json'));
      zipBtn.addEventListener('click', () => {
        const files = [{ name: 'kit.json', bytes: WIO.utf8(exp.kitText) }].concat(exp.sheets.map((s) => ({ name: s.file, bytes: s.bytes })));
        const url = URL.createObjectURL(new Blob([WIO.zipWrite(files)], { type: 'application/zip' }));
        clear(zipbox).append(h('a', { href: url, download: S.name + '.zip' }, 'Download ' + S.name + '.zip'), ' (this may do nothing in the sandbox: use the copy buttons)');
      });
      clear(sheets);
      for (const s of exp.sheets) {
        const area = h('textarea', { class: 'jsonbox', readonly: true, rows: 3, hidden: true, 'aria-label': 'PNG data URL of ' + s.file });
        const img = h('img', { src: s.url, alt: 'sheet ' + s.file, class: 'sheetimg', width: s.w, height: s.h });
        sheets.append(h('div', { class: 'exsheet' },
          h('div', {}, h('b', {}, 'sheet ' + s.n + ': ' + s.file), h('span', { class: 'dim small' }, '  ' + s.w + 'x' + s.h + ', ' + s.bytes.length + ' bytes')),
          h('div', { class: 'btnrow wrap' },
            h('button', { onclick: () => copyText(s.url, area, 'the PNG data URL of ' + s.file) }, 'Copy PNG as data URL'),
            h('a', { class: 'btnlink', href: s.url, download: s.file }, 'Download (may be blocked)')),
          h('div', { class: 'imgwrap', title: 'right-click the image and choose Save image as' }, img),
          h('div', { class: 'small dim' }, 'Right-click the image, "Save image as ..." to keep the PNG.'), area));
      }
    }).catch((e) => { clear(sheets).append(h('div', { class: 'bad' }, 'could not encode the sheets: ' + e.message)); });
  };

  W.afterBoot = async function () {
    pickers();
    S.hd = '';
    await openName(W.loadExample());
    S.bottom = 'anim'; MK.renderBottom();
    MK.log('info', 'Example kit loaded (drawn by code, not from the game). "Open kit ..." loads your own; "Save / export ..." copies it out.');
  };
})();
