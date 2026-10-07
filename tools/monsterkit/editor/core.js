'use strict';
/* Monster kit editor, core: state, API client, small DOM helper, pixel/palette helpers.
   No original game data lives in these files: everything (pixels, palettes, scripts) arrives through /api from the local kits. */
const MK = {};
MK.S = {
  info: null, cat: null, name: null, kit: null, sheets: [], dirtyKit: false, dirtySheets: new Set(),
  cs: 0, fi: 0, tool: 'anchor', color: 1, zoom: 6, onion: 'prev', grid: true, hurt: false, gate: true,
  center: 'frame', bottom: 'anim', report: null, reportStale: true, undo: [], imp: null, ver: 0, busy: '', log: [],
  lastBuild: null, hd: '', animName: null,
};
MK.listeners = {};
MK.on = (ev, fn) => { (MK.listeners[ev] = MK.listeners[ev] || []).push(fn); };
MK.emit = (ev, arg) => { (MK.listeners[ev] || []).forEach((fn) => fn(arg)); };

/* ---- DOM */
MK.h = function (tag, props, ...kids) {
  const e = document.createElement(tag);
  for (const k in (props || {})) {
    const v = props[k];
    if (v === null || v === undefined || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'style') e.style.cssText = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'value') e.value = v;
    else if (k === 'checked' || k === 'disabled' || k === 'selected') e[k] = !!v;
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const c of kids.flat(2)) {
    if (c === null || c === undefined || c === false) continue;
    e.append(c.nodeType ? c : document.createTextNode(String(c)));
  }
  return e;
};
MK.$ = (s, r) => (r || document).querySelector(s);
MK.clear = (e) => { while (e.firstChild) e.removeChild(e.firstChild); return e; };

/* ---- API */
MK.api = async function (method, url, body) {
  const opt = { method, headers: { 'X-MonsterKit': '1' } };
  if (body !== undefined) { opt.headers['Content-Type'] = 'application/json'; opt.body = JSON.stringify(body); }
  let r;
  try { r = await fetch(url, opt); } catch (e) { throw new Error('server not reachable (is `py tools/monsterkit.py serve` running?)'); }
  let j = null;
  try { j = await r.json(); } catch (e) { /* not JSON */ }
  if (!r.ok) throw new Error((j && j.error) || (r.status + ' ' + r.statusText));
  return j;
};
MK.b64d = (s) => { const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u; };
MK.b64e = (u) => { let s = ''; for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); return btoa(s); };

MK.log = (kind, msg) => { MK.S.log.push({ kind, msg, t: new Date() }); MK.emit('status', { kind, msg }); };

/* ---- kit access */
MK.celset = () => MK.S.kit && MK.S.kit.celsets[MK.S.cs];
MK.frame = () => { const c = MK.celset(); return c && c.frames[MK.S.fi]; };
MK.frameById = function (id) {
  const kit = MK.S.kit;
  for (let ci = 0; ci < kit.celsets.length; ci++) {
    const fi = kit.celsets[ci].frames.findIndex((f) => f.id === id);
    if (fi >= 0) return { ci, fi, cs: kit.celsets[ci], fr: kit.celsets[ci].frames[fi] };
  }
  return null;
};
MK.colors = function () {
  const c = (MK.S.kit && MK.S.kit.palette.colors) || [];
  const out = [];
  for (let i = 0; i < 64; i++) {
    const s = c[i];
    out.push(s ? [parseInt(s.slice(1, 3), 16), parseInt(s.slice(3, 5), 16), parseInt(s.slice(5, 7), 16)] : [i * 4, i * 4, i * 4]);
  }
  return out;
};
/* colour classes of the fight palette (docs/MONSTER_KIT.md 1.1) */
MK.colorClass = (i) => (i === 0 ? 'transparent' : (i >= 6 && i <= 8) ? 'forbidden' : (i >= 9 && i <= 15) ? 'own' : 'region');

/* the frame's pixels out of its sheet: {w, h, data} (a copy) or null */
MK.framePixels = function (fr) {
  if (!fr || !fr.src) return null;
  const sh = MK.S.sheets[fr.src.sheet];
  if (!sh) return null;
  const [x, y, w, h] = fr.src.rect;
  const data = new Uint8Array(w * h);
  for (let j = 0; j < h; j++) {
    if (y + j >= sh.h) break;
    for (let i = 0; i < w; i++) if (x + i < sh.w) data[j * w + i] = sh.data[(y + j) * sh.w + x + i];
  }
  return { w, h, data };
};
/* indices -> an offscreen canvas (index 0 transparent); `mask` (optional bytes) tints those pixels */
MK.toCanvas = function (data, w, h, opt) {
  opt = opt || {};
  const cv = document.createElement('canvas');
  cv.width = Math.max(w, 1); cv.height = Math.max(h, 1);
  const ctx = cv.getContext('2d');
  const img = ctx.createImageData(cv.width, cv.height);
  const pal = MK.colors();
  for (let i = 0; i < w * h; i++) {
    const v = data[i];
    let o = i * 4;
    if (v === 0 && !opt.opaque0) continue;
    const c = pal[v] || [255, 0, 255];
    img.data[o] = c[0]; img.data[o + 1] = c[1]; img.data[o + 2] = c[2]; img.data[o + 3] = 255;
    if (opt.mask && opt.mask[i]) { img.data[o] = 255; img.data[o + 1] = 0; img.data[o + 2] = 255; }
    if (opt.hurt) { img.data[o] = (img.data[o] + 255) >> 1; img.data[o + 1] >>= 1; img.data[o + 2] >>= 1; }
  }
  ctx.putImageData(img, 0, 0);
  return cv;
};
/* gate box of the attack points: the byte of the largest x / y (engine_ports.gate_box) */
MK.gateBox = function (pts) {
  if (!pts || !pts.length) return null;
  let mx = 0, my = 0;
  for (const p of pts) { mx = Math.max(mx, p[0] & 255); my = Math.max(my, p[1] & 255); }
  return [mx, my];
};

MK.dirtyKit = function () { MK.S.dirtyKit = true; MK.S.reportStale = true; MK.S.ver++; MK.emit('dirty'); MK.scheduleCheck(); };
MK.dirtySheet = function (n) { MK.S.dirtySheets.add(n); MK.S.ver++; MK.emit('dirty'); };
MK.isDirty = () => MK.S.dirtyKit || MK.S.dirtySheets.size > 0;

/* live check of the kit as edited (sheets as last saved): feeds the checklist */
MK.scheduleCheck = function () {
  clearTimeout(MK._chk);
  MK._chk = setTimeout(() => MK.runCheck(false), 350);
};
MK.runCheck = async function (announce) {
  const S = MK.S;
  if (!S.kit) return null;
  const my = ++MK._chkSeq || (MK._chkSeq = 1);
  try {
    const rep = await MK.api('POST', '/api/kit/' + S.name + '/check', { kit: S.kit });
    if (my !== MK._chkSeq) return rep;
    S.report = rep; S.reportStale = false;
    MK.emit('report', rep);
    if (announce) MK.log(rep.ok ? 'ok' : 'err', rep.ok ? 'check passed (tier ' + rep.tier + ', ' + rep.warnings.length + ' warning(s))' : 'check: ' + rep.errors.length + ' error(s)');
    return rep;
  } catch (e) {
    S.report = { ok: false, errors: [String(e.message)], warnings: [], info: [], checklist: [], tier: '?', text: String(e.message) };
    MK.emit('report', S.report);
    if (announce) MK.log('err', e.message);
    return S.report;
  }
};

MK.loadKit = async function (name) {
  const S = MK.S;
  const kit = await MK.api('GET', '/api/kit/' + name);
  const sheets = [];
  for (let i = 0; i < kit.sheets.length; i++) {
    const s = await MK.api('GET', '/api/kit/' + name + '/sheet/' + i);
    sheets.push({ w: s.w, h: s.h, data: MK.b64d(s.data) });
  }
  Object.assign(S, { name, kit, sheets, dirtyKit: false, cs: 0, fi: 0, undo: [], imp: null, report: null, reportStale: true, lastBuild: null });
  S.dirtySheets = new Set();
  S.animName = Object.keys(kit.animations)[0] || null;
  S.ver++;
  MK.emit('kit');
  MK.runCheck(false);
};

MK.save = async function () {
  const S = MK.S;
  if (!S.kit) return;
  for (const n of S.dirtySheets) {
    const sh = S.sheets[n];
    await MK.api('PUT', '/api/kit/' + S.name + '/sheet/' + n, { w: sh.w, h: sh.h, data: MK.b64e(sh.data) });
  }
  S.dirtySheets = new Set();
  await MK.api('PUT', '/api/kit/' + S.name, S.kit);
  S.dirtyKit = false;
  MK.emit('dirty');
  MK.log('ok', 'saved kit ' + S.name);
};
