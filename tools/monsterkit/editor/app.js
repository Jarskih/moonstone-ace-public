'use strict';
/* Monster kit editor: panels and wiring.  Left: AI + R1 checklist + stats.  Centre: frame / sheet / import (frame.js).
   Right: frame list, frame properties, palette.  Bottom: animation preview (anim.js), check output, build + install. */
(function () {
  const { S, h, $, clear } = MK;
  const NAME_RE = /^[a-z][a-z0-9_]{1,31}$/;
  const ROLE_MAX = { reach: 400, keep_away: 400, depth: 100, hp: 255 };

  /* ---------------- status line */
  MK.on('status', ({ kind, msg }) => { const e = $('#status'); if (e) { e.textContent = msg; e.className = 'status ' + kind; } });
  MK.on('dirty', () => { const e = $('#dirty'); if (e) e.textContent = MK.isDirty() ? 'unsaved changes' : ''; updateButtons(); });

  function updateButtons() {
    const rep = S.report, blocked = !S.kit || !rep || !rep.ok;
    const b = $('#btn-build'); if (b) { b.disabled = blocked; b.title = blocked ? 'export blocked while the check shows errors (red items)' : 'build the T0 files'; }
    const i = $('#btn-install'); if (i) i.disabled = blocked;
    for (const id of ['btn-save', 'btn-check']) { const e = $('#' + id); if (e) e.disabled = !S.kit; }
    const t = $('#tier'); if (t) t.textContent = rep ? 'tier ' + rep.tier : '';
  }

  /* ---------------- home: clone from game / open kit */
  function uniqueName(base) {
    const taken = new Set((S.kitList || []).map((k) => k.name));
    let n = base, i = 2;
    while (taken.has(n)) n = base + '_' + (i++);
    return n.slice(0, 32);
  }
  async function refreshKits() { try { S.kitList = (await MK.api('GET', '/api/kits')).kits; } catch (e) { S.kitList = []; MK.log('err', e.message); } }

  async function doClone(source, name) {
    if (!NAME_RE.test(name)) { MK.log('err', 'kit name: lower case letters, digits, underscore, 2..32 characters'); return; }
    S.busy = 'cloning'; MK.log('info', 'cloning ' + source + ' -> build/kits/' + name + ' ...');
    try {
      await MK.api('POST', '/api/clone', { source, name });
      await refreshKits();
      await MK.loadKit(name);
      S.view = 'edit'; MK.emit('view');
      MK.log('ok', 'cloned ' + source + ' as ' + name);
    } catch (e) { MK.log('err', e.message); }
    S.busy = '';
  }

  function renderHome(root) {
    if (MK.web) { MK.web.renderHome(root); return; }
    clear(root);
    const info = S.info;
    const cards = h('div', { class: 'cards' });
    const creatures = info.creatures;
    for (const c of creatures) {
      const inp = h('input', { type: 'text', value: uniqueName(c.name + '_kit'), class: 'namein', spellcheck: 'false', title: 'kit name (a folder under build/kits)' });
      cards.append(h('div', { class: 'card' },
        h('div', { class: 'ctitle' }, c.title), h('div', { class: 'dim' }, 'AI: ' + c.ai),
        inp,
        h('button', { class: 'primary', disabled: !info.game, onclick: () => doClone(c.name, inp.value.trim()) }, 'Clone')));
    }
    root.append(h('div', { class: 'home' },
      h('h2', {}, 'Clone from game'),
      info.game ? h('p', { class: 'dim' }, 'One click copies a creature of the original game (frames, animations, hit points, stats, sounds) into a new kit under ' + info.kits_root + '.')
        : h('p', { class: 'bad' }, 'The original game data was not found under ' + info.disks + ' (tools/adfx.py): cloning from the game is unavailable, existing kits can still be opened.'),
      cards,
      h('h2', {}, 'Open a kit'),
      S.kitList && S.kitList.length ? h('div', { class: 'kitlist' }, S.kitList.map((k) => h('div', { class: 'kititem' },
        h('b', {}, k.name), h('span', { class: 'dim' }, k.broken ? 'unreadable' : ((k.title || '') + ' - AI ' + k.ai + ' - ' + k.celsets + ' cel sets, ' + k.animations + ' animations' + (k.cloned_from ? ' - cloned from ' + (k.cloned_from.creature || k.cloned_from.name) : ''))),
        h('span', { class: 'grow' }),
        h('button', { disabled: !!k.broken, onclick: async () => { try { await MK.loadKit(k.name); S.view = 'edit'; MK.emit('view'); } catch (e) { MK.log('err', e.message); } } }, 'Open'),
        (() => {
          const cp = h('input', { type: 'text', value: uniqueName(k.name + '_copy'), class: 'namein', spellcheck: 'false', 'aria-label': 'name of the copy', style: 'width:180px' });
          return h('span', { class: 'btnrow' }, cp, h('button', { disabled: !!k.broken, title: 'copy this kit under the name in the box', onclick: () => doClone(k.name, cp.value.trim()) }, 'Copy'));
        })())))
        : h('p', { class: 'dim' }, 'No kits yet.')));
  }

  /* ---------------- left: AI picker, checklist, stats */
  function animOptions(cur) {
    const names = Object.keys(S.kit.animations);
    const opts = [h('option', { value: '' }, '(none)')];
    if (cur && !names.includes(cur)) opts.push(h('option', { value: cur, selected: true }, cur));
    names.forEach((n) => opts.push(h('option', { value: n, selected: n === cur }, n)));
    return opts;
  }
  function roleArray(r) {
    const kit = S.kit;
    if (r.name === 'hurt_by_action') return kit.hurt_by_action || (kit.hurt_by_action = new Array(9).fill(null));
    return kit.roles[r.name] || (kit.roles[r.name] = new Array(8).fill(null));
  }
  function renderChecklist(parent, ai) {
    const rep = S.report;
    const find = (n) => rep && rep.checklist.find((c) => c.role === n);
    const rows = h('div', { class: 'checklist' });
    const add = (name, meaning, st, controls) => {
      const cls = !st ? 'unk' : st.ok ? 'ok' : 'bad';
      rows.append(h('div', { class: 'crow ' + cls },
        h('span', { class: 'dot', title: st ? (st.ok ? 'ok' : 'missing') : 'checking ...' }),
        h('div', { class: 'cmain' },
          h('div', { class: 'cname', title: meaning || '' }, name),
          st ? h('div', { class: 'cdetail' }, st.detail) : null,
          controls)));
    };
    for (const r of ai.roles) {
      const st = find(r.name);
      let controls;
      if (r.kind === 'table') {
        const req = r.required_indices || [];
        const arr = roleArray(r);
        const base = (r.name === 'hurt_by_action' || r.name === 'action') ? 0 : 8 * Math.floor((req.length ? Math.min(...req) : 0) / 8);
        controls = h('div', { class: 'tablesel' }, req.map((i) => h('label', {}, '[' + i + '] ',
          h('select', { onchange: (e) => { arr[i - base] = e.target.value || null; MK.dirtyKit(); } }, animOptions(arr[i - base])))));
      } else {
        controls = h('select', { onchange: (e) => { S.kit.roles[r.name] = e.target.value || null; MK.dirtyKit(); } }, animOptions(S.kit.roles[r.name]));
      }
      add(r.name + (r.required ? '' : ' (optional)'), r.meaning, st, controls);
    }
    const tbl = ai.roles.find((r) => r.name === 'hurt_by_action');
    if (!tbl) { /* some AIs keep the hurt scripts in a role; nothing else to add */ }
    add('die', ai.die && ai.die.meaning, find('die'), h('select', { onchange: (e) => { S.kit.roles.die = e.target.value || null; MK.dirtyKit(); } }, animOptions(S.kit.roles.die)));
    parent.append(rows);
    const red = ai.roles.filter((r) => find(r.name) && !find(r.name).ok).length + (find('die') && !find('die').ok ? 1 : 0);
    return red;
  }

  function numField(label, tip, val, min, max, used, onch) {
    return h('label', { class: 'numf' + (used === false ? ' unused' : ''), title: tip },
      h('span', {}, label), h('input', { type: 'number', value: val === undefined || val === null ? '' : val, min, max, onchange: (e) => { let v = e.target.value === '' ? null : Math.max(min, Math.min(max, parseInt(e.target.value, 10) || 0)); onch(v); e.target.value = v === null ? '' : v; } }));
  }

  function renderLeft(root) {
    const keep = root.scrollTop;
    renderLeftBody(root);
    root.scrollTop = keep;
  }
  function renderLeftBody(root) {
    clear(root);
    if (!S.kit) { root.append(h('div', { class: 'empty' }, 'No kit open.')); return; }
    const kit = S.kit, cat = S.cat;
    const ai = cat.ais[kit.ai];
    const aiSel = h('select', { id: 'aisel', onchange: (e) => { kit.ai = e.target.value; MK.dirtyKit(); renderLeft(root); } },
      Object.entries(cat.ais).map(([n, a]) => h('option', { value: n, selected: n === kit.ai }, a.title + (a.advanced ? ' (advanced)' : '') + ' [' + n + ']')));
    if (!ai) { root.append(h('div', { class: 'bad' }, 'Unknown AI ' + kit.ai), aiSel); return; }
    root.append(h('div', { class: 'panelhead' }, 'AI behaviour'), aiSel,
      h('div', { class: 'aisum' }, ai.summary),
      h('details', {}, h('summary', {}, 'knight reaction, handler'),
        h('div', { class: 'small dim' }, 'handler ' + ai.handler + ' (' + ai.handler_cite + ')'),
        ai.knight_reaction ? h('div', { class: 'small dim' }, 'Knight reaction: ' + ai.knight_reaction.summary) : null));
    const head = h('div', { class: 'panelhead' }, 'Required animations ');
    root.append(head);
    const red = renderChecklist(root, ai);
    const rep = S.report;
    head.append(h('span', { class: 'badge ' + (!rep ? 'unk' : rep.ok ? 'ok' : 'bad') }, !rep ? '...' : rep.ok ? 'all green' : (rep.errors.length + ' error(s)')));
    void red;

    root.append(h('div', { class: 'panelhead' }, 'Stats and tunables'));
    const st = kit.stats;
    const box = h('div', { class: 'numgrid' });
    box.append(numField('HP', 'hit points (1..255)', st.hp, 1, ROLE_MAX.hp, true, (v) => { st.hp = v === null ? 1 : v; MK.dirtyKit(); }));
    for (const t of ai.tunables) {
      box.append(numField(t.name.replace('_', ' '), (t.used ? '' : 'NOT READ by this AI. ') + t.meaning + ' (' + t.unit + ', offset ' + t.offset + ')', st[t.name], 0, ROLE_MAX[t.name] || 400, t.used,
        (v) => { if (v === null) delete st[t.name]; else st[t.name] = v; MK.dirtyKit(); }));
    }
    root.append(box);
    for (const t of ai.tunables) root.append(h('div', { class: 'small ' + (t.used ? 'dim' : 'warn') }, t.name + ': ' + (t.used ? '' : 'not read. ') + t.meaning));
    if (!ai.tunables.length) root.append(h('div', { class: 'small dim' }, 'This AI has no tunables (fixed step tables).'));

    root.append(h('div', { class: 'panelhead' }, 'Damage'));
    const dm = ai.damage || {};
    const read = new Set((dm.entries || []).map((e) => e.index));
    const dbox = h('div', { class: 'numgrid d9' });
    (kit.damage || new Array(9).fill(0)).forEach((v, i) => {
      dbox.append(numField('[' + i + ']', 'damage by own action ' + (i * 4), v, 0, 255, dm.mode === 'table' ? read.has(i) : false, (nv) => { kit.damage[i] = nv === null ? 0 : nv; MK.dirtyKit(); }));
    });
    root.append(dbox);
    root.append(h('div', { class: 'small dim' }, dm.mode === 'fixed' ? 'Fixed ' + dm.value + ': ' + dm.note : dm.note || ''));

    root.append(h('div', { class: 'panelhead' }, 'Sound bank'));
    root.append(h('select', { onchange: (e) => { kit.sounds.bank = e.target.value; MK.dirtyKit(); } },
      Object.keys(cat.sound_banks.creature_files).map((b) => h('option', { value: b, selected: b === kit.sounds.bank }, b + '  (' + cat.sound_banks.creature_files[b] + ')'))));
    root.append(h('div', { class: 'panelhead' }, 'Kit'));
    root.append(h('label', { class: 'numf wide' }, h('span', {}, 'title'), h('input', { type: 'text', maxlength: 60, value: kit.title || '', onchange: (e) => { kit.title = e.target.value; MK.dirtyKit(); } })));
    root.append(h('div', { class: 'small dim' }, 'name ' + kit.name + (kit.cloned_from ? ', cloned from ' + (kit.cloned_from.creature || kit.cloned_from.name) : '') + ', ' + kit.planes + ' planes'));
  }

  /* ---------------- right: frames, properties, palette */
  function renderRight(root) {
    const keep = root.scrollTop;
    renderRightBody(root);
    root.scrollTop = keep;
  }
  function renderRightBody(root) {
    clear(root);
    if (!S.kit) return;
    const kit = S.kit;
    root.append(h('div', { class: 'panelhead' }, 'Cel set'),
      h('div', { class: 'tabs small' }, kit.celsets.map((c, i) => h('button', { class: 'tab' + (i === S.cs ? ' on' : ''), title: c.file || '', onclick: () => { S.cs = i; S.fi = 0; MK.emit('select'); } }, 'slot ' + c.slot + ' (' + c.frames.length + ')'))));
    const cs = MK.celset();
    const list = h('div', { class: 'framelist', id: 'framelist' });
    cs.frames.forEach((fr, i) => {
      const px = MK.framePixels(fr);
      const c = px ? MK.toCanvas(px.data, px.w, px.h) : document.createElement('canvas');
      const sc = px ? Math.min(1, 44 / Math.max(px.w, px.h)) : 1;
      const th = h('canvas', { width: Math.max(1, Math.round(c.width * sc)), height: Math.max(1, Math.round(c.height * sc)) });
      const g = th.getContext('2d'); g.imageSmoothingEnabled = false; g.drawImage(c, 0, 0, th.width, th.height);
      const pts = fr.attack && fr.attack.points.length;
      list.append(h('div', { class: 'fitem' + (i === S.fi ? ' on' : ''), title: fr.id + (px ? ' ' + px.w + 'x' + px.h : ''), 'data-i': i, onclick: () => { S.fi = i; MK.emit('select'); } },
        h('div', { class: 'thumb' }, th), h('div', { class: 'fid' }, String(i) + (pts ? ' *' : ''))));
    });
    root.append(list);

    const fr = MK.frame();
    if (fr) {
      root.append(h('div', { class: 'panelhead' }, 'Frame ' + fr.id));
      const a = fr.anchor || [0, 0];
      root.append(h('div', { class: 'numgrid' },
        numField('anchor x', 'ground point (pixels from the frame top-left)', a[0], -300, 600, true, (v) => { fr.anchor = [v === null ? 0 : v, (fr.anchor || [0, 0])[1]]; MK.dirtyKit(); MK.redrawFrame(); }),
        numField('anchor y', 'ground point', a[1], -300, 600, true, (v) => { fr.anchor = [(fr.anchor || [0, 0])[0], v === null ? 0 : v]; MK.dirtyKit(); MK.redrawFrame(); })));
      if (fr.src) root.append(h('div', { class: 'small dim mono' }, 'sheet ' + fr.src.sheet + ' rect ' + fr.src.rect.join(', ')));
      const pts = (fr.attack && fr.attack.points) || [];
      const gb = MK.gateBox(pts);
      root.append(h('div', { class: 'small' }, h('b', {}, 'Attack points ' + pts.length), gb ? '  gate box ' + gb[0] + 'x' + gb[1] : ''));
      if (!cs.hits) root.append(h('div', { class: 'small warn' }, 'This cel file has no hit set in collide.hit: points here are not built (T0).'));
      const pl = h('div', { class: 'ptlist' });
      pts.forEach((p, i) => pl.append(h('div', { class: 'pt' }, h('span', { class: 'mono' }, (i + 1) + ': '),
        h('input', { type: 'number', value: p[0], min: 0, max: 255, onchange: (e) => { p[0] = Math.max(0, Math.min(255, +e.target.value | 0)); MK.dirtyKit(); MK.redrawFrame(); } }),
        h('input', { type: 'number', value: p[1], min: 0, max: 255, onchange: (e) => { p[1] = Math.max(0, Math.min(255, +e.target.value | 0)); MK.dirtyKit(); MK.redrawFrame(); } }),
        h('button', { class: 'x', title: 'remove', onclick: () => { pts.splice(i, 1); if (!pts.length) delete fr.attack; MK.dirtyKit(); MK.emit('props'); MK.redrawFrame(); } }, 'x'))));
      root.append(pl);
      if (pts.length) root.append(h('button', { class: 'small', onclick: () => { delete fr.attack; MK.dirtyKit(); MK.emit('props'); MK.redrawFrame(); } }, 'Clear points'));
    }

    root.append(h('div', { class: 'panelhead' }, 'Palette (fight, 32 colours)'));
    const pal = MK.colors(), grid = h('div', { class: 'palette', id: 'palette' });
    const n = kit.planes === 6 ? 64 : 32;
    for (let i = 0; i < n; i++) {
      const cls = MK.colorClass(i);
      const own = cls === 'own';
      const sw = h('div', { class: 'sw ' + cls + (S.color === i ? ' sel' : ''), style: 'background:rgb(' + pal[i].join(',') + ')', title: i + ' ' + cls + (cls === 'forbidden' ? ' (first fighter colours: avoid)' : cls === 'region' ? ' (varies by backdrop / region)' : cls === 'own' ? (i === 15 ? ' (blood red, fixed)' : ' (creature colour: double-click to edit)') : ' (transparent)'),
        onclick: () => { S.color = i; MK.emit('color'); }, ondblclick: () => { if (own && i !== 15) { const inp = $('#colorpick'); inp.value = kit.palette.colors[i] || '#000000'; inp.dataset.idx = i; inp.click(); } } },
        h('span', {}, String(i)));
      grid.append(sw);
    }
    root.append(grid);
    const pnote = MK.web && MK.web.paletteNote && MK.web.paletteNote();
    if (pnote) root.append(pnote);
    root.append(h('div', { class: 'small dim' }, 'Colour ' + S.color + ' - ' + MK.colorClass(S.color) + '. Creature colours 9..14 are editable (double-click); 6..8 are the first fighter\'s and are marked red; indices 1..5 and 16..31 vary with the backdrop.'));
  }

  MK.on('color', () => { document.querySelectorAll('#palette .sw').forEach((e, i) => e.classList.toggle('sel', i === S.color)); });

  /* ---------------- bottom */
  function renderBottomTabs() {
    const root = $('#bottom');
    clear(root);
    const tabs = h('div', { class: 'tabs' }, [['anim', 'Animation preview'], ['check', 'Check' + (S.report ? ' (' + S.report.errors.length + ' err, ' + S.report.warnings.length + ' warn)' : '')], ['build', MK.web ? 'Save / export' : 'Build and install']].map(([id, t]) =>
      h('button', { class: 'tab' + (S.bottom === id ? ' on' : ''), onclick: () => { S.bottom = id; MK.renderBottom(); } }, t)));
    root.append(tabs);
    const body = h('div', { class: 'bbody' });
    root.append(body);
    return body;
  }
  MK.renderBottom = function () {
    const body = renderBottomTabs();
    if (!S.kit) return;
    if (S.bottom === 'anim') MK.renderBottomAnim(body);
    else if (S.bottom === 'check') renderCheck(body);
    else renderBuild(body);
  };

  function renderCheck(body) {
    const r = S.report;
    if (!r) { body.append(h('div', { class: 'dim' }, 'No check yet.')); return; }
    body.append(h('div', { class: 'checkout' },
      h('div', { class: r.ok ? 'ok' : 'bad' }, (r.ok ? 'OK' : r.errors.length + ' error(s): ' + (MK.web ? 'the game build would be blocked' : 'export blocked')) + ' - tier ' + r.tier + (S.reportStale ? ' (edited since the last check)' : '')),
      r.errors.map((e) => h('div', { class: 'bad small' }, 'ERROR  ' + e)),
      r.warnings.map((e) => h('div', { class: 'warn small' }, 'warning  ' + e)),
      r.info.map((e) => h('div', { class: 'dim small' }, 'info  ' + e))));
  }

  async function doBuild(install) {
    if (!S.kit) return;
    try {
      S.busy = 'building'; MK.log('info', 'saving, then building T0 ...');
      await MK.save();
      const rep = await MK.runCheck(true);
      if (!rep.ok) { S.bottom = 'check'; MK.renderBottom(); return; }
      const res = await MK.api('POST', '/api/kit/' + S.name + '/build', { install, hd: S.hd || undefined });
      S.lastBuild = res;
      MK.log('ok', 'built ' + res.files.length + ' file(s) into ' + res.out + (res.installed ? ' and installed into ' + res.installed.hd : ''));
    } catch (e) { MK.log('err', e.message); S.lastBuild = { error: e.message }; }
    S.busy = '';
    S.bottom = 'build'; MK.renderBottom();
  }

  function renderBuild(body) {
    if (MK.web) { MK.web.renderExport(body); return; }
    const rep = S.report, blocked = !rep || !rep.ok;
    const hd = h('input', { type: 'text', id: 'hdpath', class: 'wide', value: S.hd, spellcheck: 'false', onchange: (e) => { S.hd = e.target.value.trim(); } });
    const lb = S.lastBuild;
    body.append(h('div', { class: 'buildwrap' },
      h('div', { class: 'buildctl' },
        h('div', { class: 'small dim' }, 'T0 reskin: the kit\'s pixels go back into the original cel files + collide.hit (art/ staged in the kit folder). Install copies them into <HD>/art/, which the game reads before the disk files.'),
        h('label', { class: 'numf wide' }, h('span', {}, 'HD folder'), hd),
        h('div', { class: 'btnrow' },
          h('button', { id: 'btn-build2', class: 'primary', disabled: blocked, onclick: () => doBuild(false) }, 'Save + Build T0'),
          h('button', { id: 'btn-install2', disabled: blocked || !S.hd, onclick: () => doBuild(true) }, 'Save + Build + Install into HD')),
        blocked ? h('div', { class: 'bad small' }, 'Export is blocked: ' + (rep ? rep.errors.length + ' error(s), see the red items of the checklist' : 'no check result yet')) : h('div', { class: 'ok small' }, 'Check passed' + (rep.warnings.length ? ' with ' + rep.warnings.length + ' warning(s)' : ''))),
      h('div', { class: 'buildout' }, !lb ? h('div', { class: 'dim' }, 'Nothing built yet.') : lb.error ? h('div', { class: 'bad' }, lb.error) : [
        h('div', { class: 'ok small' }, 'staged in ' + lb.out + (lb.installed ? '; installed into ' + lb.installed.hd + '/art: ' + lb.installed.files.join(' ') : '')),
        h('table', { class: 'ftab' }, h('tr', {}, h('th', {}, 'file'), h('th', {}, 'bytes'), h('th', {}, 'mode'), h('th', {}, 'vs original')),
          lb.files.map((f) => h('tr', {}, h('td', {}, f.name), h('td', { class: 'r' }, f.bytes), h('td', {}, f.mode), h('td', {}, f.identical === true ? 'identical' : f.identical === false ? 'differs' : '')))),
        lb.warnings.slice(0, 6).map((w) => h('div', { class: 'warn small' }, 'warning  ' + w))])));
  }

  /* ---------------- page frame */
  function renderTopbar() {
    const top = $('#topbar');
    clear(top);
    if (MK.web) { MK.web.renderTopbar(top); updateButtons(); return; }
    top.append(h('b', { class: 'brand' }, 'Monster kit editor'),
      h('button', { onclick: async () => { await refreshKits(); S.view = 'home'; MK.emit('view'); } }, 'Kits / Clone from game'),
      h('span', { class: 'kname' }, S.kit ? S.name : 'no kit open'),
      h('span', { id: 'tier', class: 'badge' }, ''),
      h('span', { id: 'dirty', class: 'warn' }, ''),
      h('span', { class: 'grow' }),
      h('button', { id: 'btn-save', onclick: () => MK.save().catch((e) => MK.log('err', e.message)) }, 'Save kit'),
      h('button', { id: 'btn-check', onclick: async () => { try { await MK.save(); } catch (e) { MK.log('err', e.message); return; } await MK.runCheck(true); S.bottom = 'check'; MK.renderBottom(); } }, 'Check'),
      h('button', { id: 'btn-build', class: 'primary', onclick: () => doBuild(false) }, 'Build T0'),
      h('button', { id: 'btn-install', onclick: () => { if (!S.hd) { S.bottom = 'build'; MK.renderBottom(); MK.log('err', 'enter the HD folder first'); return; } doBuild(true); } }, 'Build + Install'));
    updateButtons();
  }

  function renderAll() {
    renderTopbar();
    const home = S.view === 'home' || !S.kit;
    $('#app').classList.toggle('home', home);
    if (home) { renderHome($('#homeview')); return; }
    renderLeft($('#left')); MK.renderCenter($('#center')); renderRight($('#right')); MK.renderBottom();
  }

  MK.on('view', renderAll);
  MK.on('kit', () => { S.view = 'edit'; renderAll(); MK.fitZoom(); });
  MK.on('report', () => {
    updateButtons();
    if (S.view === 'edit' && S.kit) { renderLeft($('#left')); if (S.bottom === 'check') MK.renderBottom(); else { const t = document.querySelectorAll('#bottom .tab')[1]; if (t) t.textContent = 'Check (' + S.report.errors.length + ' err, ' + S.report.warnings.length + ' warn)'; if (S.bottom === 'build') MK.renderBottom(); } }
  });
  MK.on('select', () => { MK.renderCenter($('#center')); renderRight($('#right')); });
  MK.on('center', () => MK.renderCenter($('#center')));
  MK.on('props', () => renderRight($('#right')));
  MK.on('pixels', () => { renderRight($('#right')); MK.redrawFrame(); });

  async function boot() {
    document.addEventListener('keydown', (e) => {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
      if ((e.ctrlKey || e.metaKey) && e.key === 'z') { e.preventDefault(); MK.undo(); return; }
      if ((e.ctrlKey || e.metaKey) && e.key === 's') { e.preventDefault(); MK.save().catch((er) => MK.log('err', er.message)); return; }
      const map = { a: 'anchor', t: 'attack', p: 'pencil', f: 'fill', i: 'pick' };
      if (map[e.key] && S.kit && S.center === 'frame') { S.tool = map[e.key]; MK.emit('center'); }
      if (e.key === 'ArrowLeft' && S.kit && S.fi > 0) { S.fi--; MK.emit('select'); }
      if (e.key === 'ArrowRight' && S.kit && S.fi < MK.celset().frames.length - 1) { S.fi++; MK.emit('select'); }
    });
    $('#pngfile').addEventListener('change', (e) => { const f = e.target.files[0]; e.target.value = ''; if (f) MK.importPng(f); });
    $('#colorpick').addEventListener('change', (e) => {
      const i = +e.target.dataset.idx, kit = S.kit;
      kit.palette.colors[i] = e.target.value; kit.palette.own = kit.palette.own || {}; kit.palette.own[String(i)] = e.target.value;
      S.kit.sheets.forEach((_, n) => MK.dirtySheet(n)); MK.dirtyKit(); MK.emit('pixels'); MK.emit('center');
    });
    window.addEventListener('beforeunload', (e) => { if (MK.isDirty()) { e.preventDefault(); e.returnValue = ''; } });
    S.view = 'home';
    try {
      S.info = await MK.api('GET', '/api/info');
      S.cat = await MK.api('GET', '/api/catalog');
      S.hd = S.info.hd_default || '';
      await refreshKits();
    } catch (e) { $('#homeview').append(h('div', { class: 'bad' }, e.message)); return; }
    renderAll();
    if (MK.web) { await MK.web.afterBoot(); return; }
    MK.log('info', 'ready - ' + (S.info.game ? 'game data found' : 'no game data: open an existing kit'));
    const q = new URLSearchParams(location.search).get('kit');
    if (q) { try { await MK.loadKit(q); } catch (e) { MK.log('err', e.message); } }
  }
  if (document.readyState === 'loading') window.addEventListener('DOMContentLoaded', boot); else boot();
})();
