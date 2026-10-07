'use strict';
/* Centre panel: frame editor (anchor, attack points, pencil, fill, eyedropper, zoom, onion skin), sheet view, PNG import. */
(function () {
  const { S, h, $ } = MK;
  let cv = null, view = null;          // frame canvas and its view bounds {minx, miny, w, h}
  let drag = null;

  const TOOLS = [
    ['anchor', 'Anchor (A)', 'click sets the ground point of the frame'],
    ['attack', 'Attack points (T)', 'click adds a point, drag moves it, right-click or Shift+click removes it'],
    ['pencil', 'Pencil (P)', 'left paints the chosen colour, right paints transparent'],
    ['fill', 'Fill (F)', 'flood fill inside the frame'],
    ['pick', 'Eyedropper (I)', 'takes the colour under the cursor'],
  ];

  function sheetOf(fr) { return fr && fr.src ? S.sheets[fr.src.sheet] : null; }

  /* ---------------- frame canvas */
  function computeView(fr) {
    const [, , w, ht] = fr.src.rect;
    const a = fr.anchor || [0, 0];
    let x0 = Math.min(0, a[0] - 4), y0 = Math.min(0, a[1] - 4), x1 = Math.max(w, a[0] + 5), y1 = Math.max(ht, a[1] + 5);
    for (const p of (fr.attack && fr.attack.points) || []) { x0 = Math.min(x0, p[0] - 3); y0 = Math.min(y0, p[1] - 3); x1 = Math.max(x1, p[0] + 4); y1 = Math.max(y1, p[1] + 4); }
    return { minx: x0 - 2, miny: y0 - 2, w: x1 - x0 + 4, h: y1 - y0 + 4 };
  }

  function drawFrame() {
    const fr = MK.frame();
    if (!cv || !fr || !fr.src) return;
    const z = S.zoom;
    view = computeView(fr);
    cv.width = view.w * z; cv.height = view.h * z;
    const ctx = cv.getContext('2d');
    ctx.imageSmoothingEnabled = false;
    const [, , w, ht] = fr.src.rect;
    const X = (x) => (x - view.minx) * z, Y = (y) => (y - view.miny) * z;
    ctx.fillStyle = '#1a1d24'; ctx.fillRect(0, 0, cv.width, cv.height);
    // checkerboard under the frame
    const cs = Math.max(4, z);
    for (let j = 0; j < ht; j++) for (let i = 0; i < w; i++) {
      ctx.fillStyle = ((i >> 1) + (j >> 1)) & 1 ? '#2b303b' : '#323845';
      ctx.fillRect(X(i), Y(j), z, z);
    }
    void cs;
    // onion skin
    if (S.onion !== 'off') {
      const ci = MK.celset().frames;
      const oi = S.onion === 'prev' ? S.fi - 1 : S.fi + 1;
      const of = ci[oi];
      const px = of && MK.framePixels(of);
      if (px) {
        const c = MK.toCanvas(px.data, px.w, px.h);
        const oa = of.anchor || [0, 0], a = fr.anchor || [0, 0];
        ctx.globalAlpha = 0.35;
        ctx.drawImage(c, X(a[0] - oa[0]), Y(a[1] - oa[1]), px.w * z, px.h * z);
        ctx.globalAlpha = 1;
      }
    }
    const px = MK.framePixels(fr);
    if (px) ctx.drawImage(MK.toCanvas(px.data, px.w, px.h, { hurt: S.hurt }), X(0), Y(0), w * z, ht * z);
    if (S.grid && z >= 6) {
      ctx.strokeStyle = 'rgba(255,255,255,0.10)'; ctx.lineWidth = 1; ctx.beginPath();
      for (let i = 0; i <= w; i++) { ctx.moveTo(X(i) + .5, Y(0)); ctx.lineTo(X(i) + .5, Y(ht)); }
      for (let j = 0; j <= ht; j++) { ctx.moveTo(X(0), Y(j) + .5); ctx.lineTo(X(w), Y(j) + .5); }
      ctx.stroke();
    }
    ctx.strokeStyle = '#6c7a96'; ctx.lineWidth = 1; ctx.strokeRect(X(0) - .5, Y(0) - .5, w * z + 1, ht * z + 1);
    const pts = (fr.attack && fr.attack.points) || [];
    const gb = MK.gateBox(pts);
    if (S.gate && gb) {
      ctx.setLineDash([5, 4]); ctx.strokeStyle = '#ff7a59'; ctx.strokeRect(X(0), Y(0), (gb[0] + 1) * z, (gb[1] + 1) * z); ctx.setLineDash([]);
      ctx.fillStyle = '#ff7a59'; ctx.font = '11px monospace'; ctx.fillText('gate box ' + gb[0] + 'x' + gb[1], X(0) + 3, Math.max(11, Y(0) - 3));
    }
    pts.forEach((p, i) => {
      const cx = X(p[0]) + z / 2, cy = Y(p[1]) + z / 2;
      ctx.fillStyle = 'rgba(255,60,60,.85)'; ctx.strokeStyle = '#fff'; ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.arc(cx, cy, Math.max(4, z * .45), 0, 7); ctx.fill(); ctx.stroke();
      if (z >= 5) { ctx.fillStyle = '#fff'; ctx.font = '10px monospace'; ctx.fillText(String(i + 1), cx + 6, cy - 5); }
    });
    const a = fr.anchor || [0, 0];
    const ax = X(a[0]) + z / 2, ay = Y(a[1]) + z / 2;
    ctx.strokeStyle = '#4ad6ff'; ctx.lineWidth = 2; ctx.beginPath();
    ctx.moveTo(ax - 9, ay); ctx.lineTo(ax + 9, ay); ctx.moveTo(ax, ay - 9); ctx.lineTo(ax, ay + 9); ctx.stroke();
    ctx.beginPath(); ctx.arc(ax, ay, 5, 0, 7); ctx.stroke();
  }

  function eventPixel(ev) {
    const r = cv.getBoundingClientRect();
    const sx = cv.width / r.width, sy = cv.height / r.height;
    const fx = (ev.clientX - r.left) * sx / S.zoom, fy = (ev.clientY - r.top) * sy / S.zoom;
    return { x: Math.floor(fx + view.minx), y: Math.floor(fy + view.miny), fx: fx + view.minx, fy: fy + view.miny };
  }

  function setPixel(fr, x, y, v) {
    const [rx, ry, w, ht] = fr.src.rect, sh = sheetOf(fr);
    if (x < 0 || y < 0 || x >= w || y >= ht || rx + x >= sh.w || ry + y >= sh.h) return false;
    const i = (ry + y) * sh.w + rx + x;
    if (sh.data[i] === v) return false;
    sh.data[i] = v;
    return true;
  }

  function snapshot(fr) {
    const px = MK.framePixels(fr);
    S.undo.push({ ci: S.cs, fi: S.fi, data: px.data });
    if (S.undo.length > 60) S.undo.shift();
  }
  MK.undo = function () {
    const u = S.undo.pop();
    if (!u) return;
    const fr = S.kit.celsets[u.ci].frames[u.fi];
    const [rx, ry, w, ht] = fr.src.rect, sh = sheetOf(fr);
    for (let j = 0; j < ht; j++) for (let i = 0; i < w; i++) sh.data[(ry + j) * sh.w + rx + i] = u.data[j * w + i];
    MK.dirtySheet(fr.src.sheet);
    MK.emit('pixels');
  };

  function floodFill(fr, x0, y0, v) {
    const px = MK.framePixels(fr), w = px.w, ht = px.h;
    if (x0 < 0 || y0 < 0 || x0 >= w || y0 >= ht) return false;
    const from = px.data[y0 * w + x0];
    if (from === v) return false;
    const st = [[x0, y0]];
    while (st.length) {
      const [x, y] = st.pop();
      if (x < 0 || y < 0 || x >= w || y >= ht || px.data[y * w + x] !== from) continue;
      px.data[y * w + x] = v; setPixel(fr, x, y, v);
      st.push([x + 1, y], [x - 1, y], [x, y + 1], [x, y - 1]);
    }
    return true;
  }

  function nearPoint(fr, ev) {
    const pts = (fr.attack && fr.attack.points) || [];
    const r = cv.getBoundingClientRect(), z = S.zoom * r.width / cv.width;
    let best = -1, bd = 1e9;
    pts.forEach((p, i) => {
      const cx = ((p[0] - view.minx) + .5) * z + r.left, cy = ((p[1] - view.miny) + .5) * z + r.top;
      const d = Math.hypot(ev.clientX - cx, ev.clientY - cy);
      if (d < Math.max(8, z * .6) && d < bd) { bd = d; best = i; }
    });
    return best;
  }

  function onDown(ev) {
    const fr = MK.frame();
    if (!fr || !fr.src) return;
    ev.preventDefault();
    try { cv.setPointerCapture(ev.pointerId); } catch (e) { /* synthetic event */ }
    const p = eventPixel(ev), right = ev.button === 2;
    const t = S.tool;
    if (t === 'anchor') { fr.anchor = [p.x, p.y]; MK.dirtyKit(); drawFrame(); MK.emit('props'); }
    else if (t === 'attack') {
      const i = nearPoint(fr, ev);
      if (i >= 0 && (right || ev.shiftKey)) { fr.attack.points.splice(i, 1); if (!fr.attack.points.length) delete fr.attack; MK.dirtyKit(); drawFrame(); MK.emit('props'); }
      else if (i >= 0) drag = { kind: 'pt', i };
      else if (!right && !ev.shiftKey) {
        fr.attack = fr.attack || { type: 0, points: [] };
        fr.attack.points.push([Math.max(0, Math.min(255, p.x)), Math.max(0, Math.min(255, p.y))]);
        drag = { kind: 'pt', i: fr.attack.points.length - 1 };
        MK.dirtyKit(); drawFrame(); MK.emit('props');
      }
    } else if (t === 'pencil') {
      snapshot(fr); drag = { kind: 'pen', v: right ? 0 : S.color, changed: false };
      penLine(fr, drag, p.x, p.y);
      drawFrame();
    } else if (t === 'fill') {
      snapshot(fr);
      if (floodFill(fr, p.x, p.y, right ? 0 : S.color)) { MK.dirtySheet(fr.src.sheet); MK.emit('pixels'); } else S.undo.pop();
      drawFrame();
    } else if (t === 'pick') {
      const px = MK.framePixels(fr);
      if (p.x >= 0 && p.y >= 0 && p.x < px.w && p.y < px.h) { S.color = px.data[p.y * px.w + p.x]; MK.emit('color'); }
    }
  }
  function onMove(ev) {
    const fr = MK.frame();
    if (!fr || !fr.src) return;
    const p = eventPixel(ev);
    const info = $('#cursor-info');
    if (info) {
      const px = MK.framePixels(fr);
      const v = (p.x >= 0 && p.y >= 0 && p.x < px.w && p.y < px.h) ? px.data[p.y * px.w + p.x] : '-';
      info.textContent = 'x ' + p.x + '  y ' + p.y + '  index ' + v;
    }
    if (!drag) return;
    if (drag.kind === 'pt') {
      const pt = fr.attack.points[drag.i];
      pt[0] = Math.max(0, Math.min(255, p.x)); pt[1] = Math.max(0, Math.min(255, p.y));
      drawFrame();
    } else if (drag.kind === 'pen') {
      penLine(fr, drag, p.x, p.y);
      drawFrame();
    }
  }
  /* a stroke is a line between the pointer positions (Bresenham), so a fast drag leaves no gaps */
  function penLine(fr, d, x1, y1) {
    let x0 = d.lx === undefined ? x1 : d.lx, y0 = d.ly === undefined ? y1 : d.ly;
    const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    let err = dx + dy;
    for (;;) {
      if (setPixel(fr, x0, y0, d.v)) d.changed = true;
      if (x0 === x1 && y0 === y1) break;
      const e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
    d.lx = x1; d.ly = y1;
  }
  function onUp() {
    if (!drag) return;
    if (drag.kind === 'pt') { MK.dirtyKit(); MK.emit('props'); }
    else if (drag.kind === 'pen') {
      const fr = MK.frame();
      if (drag.changed) { MK.dirtySheet(fr.src.sheet); MK.emit('pixels'); } else S.undo.pop();
    }
    drag = null;
  }

  /* ---------------- toolbar */
  function toolbar() {
    const bar = h('div', { class: 'toolbar' });
    for (const [id, label, tip] of TOOLS) {
      bar.append(h('button', { class: 'tool' + (S.tool === id ? ' on' : ''), title: tip, 'data-tool': id, onclick: () => { S.tool = id; MK.emit('center'); } }, label.replace(/ \(.\)/, '')));
    }
    bar.append(h('span', { class: 'sep' }));
    bar.append(h('label', {}, 'Zoom ', h('select', { id: 'zoom', onchange: (e) => { S.zoom = +e.target.value; drawFrame(); } },
      [2, 3, 4, 6, 8, 10, 12, 16].map((z) => h('option', { value: z, selected: z === S.zoom }, z + 'x')))));
    bar.append(h('button', { title: 'largest zoom that shows the whole frame', onclick: () => MK.fitZoom() }, 'Fit'));
    bar.append(h('label', {}, 'Onion ', h('select', { id: 'onion', onchange: (e) => { S.onion = e.target.value; drawFrame(); } },
      [['off', 'off'], ['prev', 'previous frame'], ['next', 'next frame']].map(([v, t]) => h('option', { value: v, selected: v === S.onion }, t)))));
    bar.append(h('label', { class: 'chk' }, h('input', { type: 'checkbox', checked: S.grid, onchange: (e) => { S.grid = e.target.checked; drawFrame(); } }), 'grid'));
    bar.append(h('label', { class: 'chk', title: 'tint the pixels that count as hurtable (non-zero pixels)' }, h('input', { type: 'checkbox', checked: S.hurt, onchange: (e) => { S.hurt = e.target.checked; drawFrame(); } }), 'hurt tint'));
    bar.append(h('label', { class: 'chk' }, h('input', { type: 'checkbox', checked: S.gate, onchange: (e) => { S.gate = e.target.checked; drawFrame(); } }), 'gate box'));
    bar.append(h('button', { title: 'Ctrl+Z', onclick: () => MK.undo() }, 'Undo'));
    bar.append(h('span', { id: 'cursor-info', class: 'dim mono' }, ''));
    return bar;
  }

  function renderFrameView(root) {
    const fr = MK.frame();
    root.append(toolbar());
    if (!fr || !fr.src) { root.append(h('div', { class: 'empty' }, 'This frame has no sheet source (src).')); return; }
    cv = h('canvas', { id: 'framecv', class: 'framecv' });
    cv.addEventListener('pointerdown', onDown); cv.addEventListener('pointermove', onMove);
    cv.addEventListener('pointerup', onUp); cv.addEventListener('pointercancel', onUp);
    cv.addEventListener('contextmenu', (e) => e.preventDefault());
    cv.addEventListener('wheel', (e) => {
      if (!e.ctrlKey) return;
      e.preventDefault();
      const zs = [2, 3, 4, 6, 8, 10, 12, 16];
      let i = zs.indexOf(S.zoom); i = Math.max(0, Math.min(zs.length - 1, i + (e.deltaY < 0 ? 1 : -1)));
      S.zoom = zs[i]; $('#zoom').value = S.zoom; drawFrame();
    }, { passive: false });
    const wrap = h('div', { class: 'canvaswrap' }, cv);
    root.append(wrap);
    root.append(h('div', { class: 'hint' }, TOOLS.find((t) => t[0] === S.tool)[2] + '. Frame ' + fr.id + ', rect ' + fr.src.rect.join(',') + '.'));
    drawFrame();
  }

  /* ---------------- sheet view */
  function renderSheetView(root) {
    const cs = MK.celset();
    const sh = S.sheets[S.kit.celsets[S.cs].frames[0].src.sheet];
    root.append(h('div', { class: 'toolbar' }, h('span', { class: 'dim' }, 'Sheet of cel set ' + cs.slot + (cs.file ? ' (' + cs.file + ')' : '') + ': ' + sh.w + 'x' + sh.h + ' px, ' + cs.frames.length + ' frames. Click a frame to select it, double-click to edit it.'),
      h('label', {}, ' Zoom ', h('select', { onchange: (e) => { S.sheetZoom = +e.target.value; MK.emit('center'); } }, [1, 2, 3, 4].map((z) => h('option', { value: z, selected: z === (S.sheetZoom || 2) }, z + 'x'))))));
    const z = S.sheetZoom || 2;
    const c = MK.toCanvas(sh.data, sh.w, sh.h);
    const cvs = h('canvas', { width: sh.w * z, height: sh.h * z, class: 'sheetcv' });
    const ctx = cvs.getContext('2d');
    ctx.imageSmoothingEnabled = false;
    ctx.fillStyle = '#262b36'; ctx.fillRect(0, 0, cvs.width, cvs.height);
    ctx.drawImage(c, 0, 0, sh.w * z, sh.h * z);
    cs.frames.forEach((f, i) => {
      if (!f.src || f.src.sheet !== cs.frames[0].src.sheet) return;
      const [x, y, w, ht] = f.src.rect;
      ctx.strokeStyle = i === S.fi ? '#ffd24a' : 'rgba(120,200,255,.55)'; ctx.lineWidth = i === S.fi ? 2 : 1;
      ctx.strokeRect(x * z + .5, y * z + .5, w * z, ht * z);
      if (f.attack && f.attack.points.length) { ctx.fillStyle = '#ff5050'; ctx.fillRect(x * z + 1, y * z + 1, 4, 4); }
    });
    const pick = (ev) => {
      const r = cvs.getBoundingClientRect(), x = (ev.clientX - r.left) / z, y = (ev.clientY - r.top) / z;
      return cs.frames.findIndex((f) => f.src && x >= f.src.rect[0] && x < f.src.rect[0] + f.src.rect[2] && y >= f.src.rect[1] && y < f.src.rect[1] + f.src.rect[3]);
    };
    cvs.addEventListener('click', (ev) => { const i = pick(ev); if (i >= 0) { S.fi = i; MK.emit('select'); } });
    cvs.addEventListener('dblclick', (ev) => { const i = pick(ev); if (i >= 0) { S.fi = i; S.center = 'frame'; MK.emit('select'); } });
    root.append(h('div', { class: 'canvaswrap' }, cvs));
  }

  /* ---------------- import */
  MK.importPng = function (file) {
    const rd = new FileReader();
    rd.onload = async () => {
      try {
        const b64 = String(rd.result).split(',')[1];
        MK.log('info', 'snapping ' + file.name + ' to the palette ...');
        const r = await MK.api('POST', '/api/kit/' + S.name + '/snap', { png: b64 });
        S.imp = { name: file.name, w: r.w, h: r.h, data: MK.b64d(r.data), changed: MK.b64d(r.changed), counts: r.counts, src: String(rd.result), overlay: true };
        S.center = 'import';
        MK.emit('center');
        MK.log('ok', file.name + ': ' + r.counts.changed + ' of ' + r.counts.opaque + ' opaque pixels changed colour');
      } catch (e) { MK.log('err', e.message); }
    };
    rd.readAsDataURL(file);
  };

  function applyImport(mode) {
    const imp = S.imp, fr = MK.frame(), cs = MK.celset();
    if (mode === 'sheet') {
      const n = cs.frames[0].src.sheet;
      S.sheets[n] = { w: imp.w, h: imp.h, data: new Uint8Array(imp.data) };
      MK.dirtySheet(n);
    } else {
      if (!fr || !fr.src) return;
      const sh = S.sheets[fr.src.sheet], [rx, ry, w, ht] = fr.src.rect;
      snapshotFrame(fr);
      for (let j = 0; j < ht; j++) for (let i = 0; i < w; i++) sh.data[(ry + j) * sh.w + rx + i] = (i < imp.w && j < imp.h) ? imp.data[j * imp.w + i] : 0;
      MK.dirtySheet(fr.src.sheet);
    }
    S.imp = null; S.center = mode === 'sheet' ? 'sheet' : 'frame';
    MK.emit('pixels'); MK.emit('center');
    MK.log('ok', 'imported into the ' + mode);
  }
  function snapshotFrame(fr) { const px = MK.framePixels(fr); S.undo.push({ ci: S.cs, fi: S.fi, data: px.data }); }

  function renderImport(root) {
    const imp = S.imp;
    if (!imp) {
      root.append(h('div', { class: 'empty' }, 'Import a PNG: its colours are snapped to the kit palette (nearest colour, alpha < 50% = transparent), the pixels that changed colour are shown in magenta. ',
        h('br'), h('button', { onclick: () => $('#pngfile').click() }, 'Choose PNG ...')));
      return;
    }
    const c = imp.counts, pct = c.opaque ? (100 * c.changed / c.opaque).toFixed(1) : '0';
    root.append(h('div', { class: 'toolbar' },
      h('b', {}, imp.name), h('span', { class: 'mono' }, imp.w + 'x' + imp.h),
      h('span', { class: c.changed ? 'warn' : 'ok' }, c.changed + ' of ' + c.opaque + ' opaque pixels changed (' + pct + '%)'),
      c.forbidden ? h('span', { class: 'bad' }, c.forbidden + ' pixels on the first fighter colours 6..8') : null,
      h('label', { class: 'chk' }, h('input', { type: 'checkbox', checked: imp.overlay, onchange: (e) => { imp.overlay = e.target.checked; MK.emit('center'); } }), 'show changed pixels'),
      h('span', { class: 'sep' }),
      h('button', { class: 'primary', onclick: () => applyImport('frame') }, 'Apply to current frame'),
      h('button', { onclick: () => applyImport('sheet') }, 'Replace whole sheet'),
      h('button', { onclick: () => { S.imp = null; S.center = 'frame'; MK.emit('center'); } }, 'Cancel')));
    const z = Math.max(1, Math.min(4, Math.floor(600 / Math.max(imp.w, 1))));
    const snapped = MK.toCanvas(imp.data, imp.w, imp.h, { mask: imp.overlay ? imp.changed : null, opaque0: false });
    const a = h('canvas', { width: imp.w * z, height: imp.h * z, class: 'sheetcv' });
    const ctx = a.getContext('2d'); ctx.imageSmoothingEnabled = false; ctx.fillStyle = '#262b36'; ctx.fillRect(0, 0, a.width, a.height); ctx.drawImage(snapped, 0, 0, imp.w * z, imp.h * z);
    const orig = h('img', { src: imp.src, class: 'sheetcv', style: 'width:' + imp.w * z + 'px;height:' + imp.h * z + 'px;image-rendering:pixelated;background:#262b36' });
    root.append(h('div', { class: 'canvaswrap two' }, h('div', {}, h('div', { class: 'dim' }, 'original'), orig), h('div', {}, h('div', { class: 'dim' }, 'snapped to the palette'), a)));
  }

  MK.renderCenter = function (root) {
    MK.clear(root);
    cv = null;
    const tabs = h('div', { class: 'tabs' }, [['frame', 'Frame'], ['sheet', 'Sprite sheet'], ['import', 'Import PNG']].map(([id, t]) =>
      h('button', { class: 'tab' + (S.center === id ? ' on' : ''), onclick: () => { S.center = id; MK.emit('center'); } }, t)));
    tabs.append(h('span', { class: 'grow' }));
    tabs.append(h('button', { onclick: () => $('#pngfile').click(), title: 'snap a PNG to the kit palette' }, 'Import PNG ...'));
    root.append(tabs);
    const body = h('div', { class: 'tabbody' });
    root.append(body);
    if (S.center === 'sheet') renderSheetView(body);
    else if (S.center === 'import') renderImport(body);
    else renderFrameView(body);
  };
  MK.fitZoom = function () {
    const fr = MK.frame(), wrap = $('.canvaswrap');
    if (!fr || !fr.src || !wrap) return;
    const v = computeView(fr), zs = [2, 3, 4, 6, 8, 10, 12, 16];
    let best = zs[0];
    for (const z of zs) if (v.w * z <= wrap.clientWidth - 24 && v.h * z <= wrap.clientHeight - 24) best = z;
    S.zoom = best;
    const sel = $('#zoom'); if (sel) sel.value = best;
    drawFrame();
  };
  MK.redrawFrame = drawFrame;
})();
