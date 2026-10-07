'use strict';
/* Monster kit editor, web edition, part 1: byte helpers (CRC-32, SHA-1, PNG read / write, zip read / write) and the JSON schema
   subset validator (a port of tools/monsterkit/jsonschema_lite.py).  Pure functions, no DOM.  Compression uses the browser's
   own CompressionStream / DecompressionStream, so no library is needed. */
const WIO = {};

/* ---- CRC-32, SHA-1 */
WIO.crcTable = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
WIO.crc32 = function (u) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < u.length; i++) c = WIO.crcTable[(c ^ u[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
};
WIO.utf8 = (s) => new TextEncoder().encode(s);
WIO.unutf8 = (u) => new TextDecoder('utf-8').decode(u);
WIO.sha1hex = function (str) {
  const m = WIO.utf8(str), l = m.length, n = ((l + 8) >> 6) + 1, w = new Uint32Array(n * 16);
  for (let i = 0; i < l; i++) w[i >> 2] |= m[i] << (24 - (i & 3) * 8);
  w[l >> 2] |= 0x80 << (24 - (l & 3) * 8);
  w[n * 16 - 1] = l * 8;
  let h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  const rol = (x, s) => (x << s) | (x >>> (32 - s)), x = new Uint32Array(80);
  for (let b = 0; b < n; b++) {
    for (let i = 0; i < 16; i++) x[i] = w[b * 16 + i];
    for (let i = 16; i < 80; i++) x[i] = rol(x[i - 3] ^ x[i - 8] ^ x[i - 14] ^ x[i - 16], 1);
    let a = h0, bb = h1, c = h2, d = h3, e = h4;
    for (let i = 0; i < 80; i++) {
      const f = i < 20 ? (bb & c) | (~bb & d) : i < 40 ? bb ^ c ^ d : i < 60 ? (bb & c) | (bb & d) | (c & d) : bb ^ c ^ d;
      const k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
      const t = (rol(a, 5) + f + e + k + x[i]) >>> 0;
      e = d; d = c; c = rol(bb, 30); bb = a; a = t;
    }
    h0 = (h0 + a) >>> 0; h1 = (h1 + bb) >>> 0; h2 = (h2 + c) >>> 0; h3 = (h3 + d) >>> 0; h4 = (h4 + e) >>> 0;
  }
  return [h0, h1, h2, h3, h4].map((v) => v.toString(16).padStart(8, '0')).join('');
};

/* ---- (de)compression through the browser streams */
WIO.pipe = async function (bytes, Ctor, fmt) {
  if (typeof Ctor !== 'function') throw new Error('this browser has no CompressionStream / DecompressionStream');
  const s = new Blob([bytes]).stream().pipeThrough(new Ctor(fmt));
  return new Uint8Array(await new Response(s).arrayBuffer());
};
WIO.inflate = (b, raw) => WIO.pipe(b, window.DecompressionStream, raw ? 'deflate-raw' : 'deflate');
WIO.deflate = (b) => WIO.pipe(b, window.CompressionStream, 'deflate');

WIO.concat = function (parts) {
  const out = new Uint8Array(parts.reduce((a, p) => a + p.length, 0));
  let o = 0; for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
};

/* ---- PNG */
const be32 = (u, o) => ((u[o] << 24) | (u[o + 1] << 16) | (u[o + 2] << 8) | u[o + 3]) >>> 0;
WIO.pngChunks = function (u) {
  const sig = [137, 80, 78, 71, 13, 10, 26, 10];
  if (u.length < 33 || sig.some((v, i) => u[i] !== v)) throw new Error('not a PNG file');
  const out = [];
  for (let o = 8; o + 12 <= u.length;) {
    const n = be32(u, o), type = String.fromCharCode(u[o + 4], u[o + 5], u[o + 6], u[o + 7]);
    out.push({ type, data: u.subarray(o + 8, o + 8 + n) });
    o += 12 + n;
    if (type === 'IEND') break;
  }
  return out;
};
/* an 8-bit-or-less non-interlaced indexed PNG -> {w, h, data (indices), palette: ['#rrggbb', ...]}; anything else -> null */
WIO.pngIndexed = async function (u) {
  const ch = WIO.pngChunks(u), ih = ch.find((c) => c.type === 'IHDR');
  if (!ih) throw new Error('PNG without IHDR');
  const w = be32(ih.data, 0), h = be32(ih.data, 4), bd = ih.data[8], ct = ih.data[9], il = ih.data[12];
  if (ct !== 3 || il !== 0 || bd > 8) return null;
  const pl = ch.find((c) => c.type === 'PLTE');
  if (!pl) throw new Error('indexed PNG without PLTE');
  const idat = ch.filter((c) => c.type === 'IDAT'), tot = idat.reduce((a, c) => a + c.data.length, 0), z = new Uint8Array(tot);
  let p = 0; for (const c of idat) { z.set(c.data, p); p += c.data.length; }
  const raw = await WIO.inflate(z, false), rb = Math.ceil(w * bd / 8), data = new Uint8Array(w * h);
  if (raw.length < (rb + 1) * h) throw new Error('PNG data is truncated');
  const prev = new Uint8Array(rb), cur = new Uint8Array(rb);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (rb + 1)], src = raw.subarray(y * (rb + 1) + 1, (y + 1) * (rb + 1));
    for (let i = 0; i < rb; i++) {
      const a = i > 0 ? cur[i - 1] : 0, b = prev[i], c = i > 0 ? prev[i - 1] : 0;
      let v = src[i];
      if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) { const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c); v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); }
      else if (f !== 0) throw new Error('bad PNG filter');
      cur[i] = v & 255;
    }
    for (let x = 0; x < w; x++) {
      if (bd === 8) data[y * w + x] = cur[x];
      else { const bit = x * bd, sh = 8 - bd - (bit & 7); data[y * w + x] = (cur[bit >> 3] >> sh) & ((1 << bd) - 1); }
    }
    prev.set(cur);
  }
  const palette = [];
  for (let i = 0; i + 2 < pl.data.length; i += 3) palette.push('#' + [pl.data[i], pl.data[i + 1], pl.data[i + 2]].map((v) => v.toString(16).padStart(2, '0')).join(''));
  return { w, h, data, palette };
};
/* any PNG / image -> RGBA through the browser decoder */
WIO.imageRGBA = async function (bytes) {
  const bmp = await createImageBitmap(new Blob([bytes], { type: 'image/png' }));
  const cv = document.createElement('canvas'); cv.width = bmp.width; cv.height = bmp.height;
  const ctx = cv.getContext('2d', { willReadFrequently: true }); ctx.drawImage(bmp, 0, 0);
  const d = ctx.getImageData(0, 0, cv.width, cv.height);
  return { w: cv.width, h: cv.height, rgba: d.data };
};
/* indices + palette -> an indexed PNG (index 0 transparent) */
WIO.pngEncode = async function (w, h, data, paletteHex) {
  let mx = 0; for (let i = 0; i < data.length; i++) if (data[i] > mx) mx = data[i];
  const cnt = Math.min(256, Math.max(32, paletteHex.length, mx + 1));
  const rows = new Uint8Array((w + 1) * h);
  for (let y = 0; y < h; y++) rows.set(data.subarray(y * w, (y + 1) * w), y * (w + 1) + 1);
  const z = await WIO.deflate(rows);
  const pal = new Uint8Array(cnt * 3);
  for (let i = 0; i < cnt; i++) { const s = paletteHex[i] || '#000000'; pal[i * 3] = parseInt(s.slice(1, 3), 16); pal[i * 3 + 1] = parseInt(s.slice(3, 5), 16); pal[i * 3 + 2] = parseInt(s.slice(5, 7), 16); }
  const ihdr = new Uint8Array(13), dv = new DataView(ihdr.buffer);
  dv.setUint32(0, w); dv.setUint32(4, h); ihdr[8] = 8; ihdr[9] = 3;
  const parts = [new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10])];
  const add = (type, body) => {
    const c = new Uint8Array(12 + body.length), d = new DataView(c.buffer);
    d.setUint32(0, body.length); c.set(WIO.utf8(type), 4); c.set(body, 8);
    d.setUint32(8 + body.length, WIO.crc32(c.subarray(4, 8 + body.length)));
    parts.push(c);
  };
  add('IHDR', ihdr); add('PLTE', pal); add('tRNS', new Uint8Array([0])); add('IDAT', z); add('IEND', new Uint8Array(0));
  return WIO.concat(parts);
};

/* ---- zip: read (stored or deflated entries) and write (stored) */
WIO.zipRead = async function (u) {
  const dv = new DataView(u.buffer, u.byteOffset, u.byteLength);
  let e = -1;
  for (let i = u.length - 22; i >= Math.max(0, u.length - 65557); i--) if (dv.getUint32(i, true) === 0x06054b50) { e = i; break; }
  if (e < 0) throw new Error('not a zip file (no end-of-directory record)');
  const count = dv.getUint16(e + 10, true);
  let p = dv.getUint32(e + 16, true);
  const out = [];
  for (let i = 0; i < count; i++) {
    if (dv.getUint32(p, true) !== 0x02014b50) throw new Error('damaged zip directory');
    const method = dv.getUint16(p + 10, true), csize = dv.getUint32(p + 20, true), nlen = dv.getUint16(p + 28, true), xlen = dv.getUint16(p + 30, true), clen = dv.getUint16(p + 32, true), lo = dv.getUint32(p + 42, true);
    const name = WIO.unutf8(u.subarray(p + 46, p + 46 + nlen));
    p += 46 + nlen + xlen + clen;
    if (name.endsWith('/')) continue;
    const start = lo + 30 + dv.getUint16(lo + 26, true) + dv.getUint16(lo + 28, true), body = u.subarray(start, start + csize);
    out.push({ name, method, body });
  }
  const res = [];
  for (const f of out) {
    if (f.method !== 0 && f.method !== 8) throw new Error('zip entry ' + f.name + ': unsupported compression ' + f.method);
    res.push({ name: f.name, bytes: f.method === 0 ? f.body : await WIO.inflate(f.body, true) });
  }
  return res;
};
WIO.zipWrite = function (files) {
  const parts = [], cen = [];
  let off = 0;
  const d = new Date(), dos = ((d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1)), dd = (((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate());
  for (const f of files) {
    const nm = WIO.utf8(f.name), crc = WIO.crc32(f.bytes), lh = new Uint8Array(30 + nm.length), v = new DataView(lh.buffer);
    v.setUint32(0, 0x04034b50, true); v.setUint16(4, 20, true); v.setUint16(6, 0x0800, true); v.setUint16(10, dos, true); v.setUint16(12, dd, true);
    v.setUint32(14, crc, true); v.setUint32(18, f.bytes.length, true); v.setUint32(22, f.bytes.length, true); v.setUint16(26, nm.length, true); lh.set(nm, 30);
    const ce = new Uint8Array(46 + nm.length), c = new DataView(ce.buffer);
    c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true); c.setUint16(12, dos, true); c.setUint16(14, dd, true);
    c.setUint32(16, crc, true); c.setUint32(20, f.bytes.length, true); c.setUint32(24, f.bytes.length, true); c.setUint16(28, nm.length, true); c.setUint32(42, off, true); ce.set(nm, 46);
    parts.push(lh, f.bytes); cen.push(ce); off += lh.length + f.bytes.length;
  }
  const cd = WIO.concat(cen), end = new Uint8Array(22), e = new DataView(end.buffer);
  e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true); e.setUint32(12, cd.length, true); e.setUint32(16, off, true);
  return WIO.concat([...parts, cd, end]);
};

/* ---- JSON schema subset (port of jsonschema_lite.py): returns a list of "path: message" strings */
WIO.validate = function (inst, schema) {
  const errs = [];
  const T = {
    object: (v) => v !== null && typeof v === 'object' && !Array.isArray(v), array: Array.isArray, string: (v) => typeof v === 'string',
    integer: (v) => typeof v === 'number' && Number.isInteger(v), number: (v) => typeof v === 'number', boolean: (v) => typeof v === 'boolean', null: (v) => v === null,
  };
  const kind = (v) => (v === null ? 'null' : Array.isArray(v) ? 'array' : typeof v);
  const ref = (r) => { let n = schema; for (const p of r.slice(2).split('/')) n = n[p]; return n; };
  const chk = (v, s, path) => {
    const P = path || '$';
    if (s.$ref) { chk(v, ref(s.$ref), path); return; }
    if (s.type) {
      const ts = Array.isArray(s.type) ? s.type : [s.type];
      if (!ts.some((t) => T[t](v))) { errs.push(P + ': expected ' + ts.join('/') + ', got ' + kind(v)); return; }
    }
    if ('const' in s && v !== s.const) errs.push(P + ': must be ' + JSON.stringify(s.const));
    if (s.enum && !s.enum.includes(v)) errs.push(P + ': ' + JSON.stringify(v) + ' not in ' + JSON.stringify(s.enum));
    if (typeof v === 'number') {
      if ('minimum' in s && v < s.minimum) errs.push(P + ': ' + v + ' below ' + s.minimum);
      if ('maximum' in s && v > s.maximum) errs.push(P + ': ' + v + ' above ' + s.maximum);
    }
    if (typeof v === 'string') {
      if ('minLength' in s && v.length < s.minLength) errs.push(P + ': shorter than ' + s.minLength);
      if ('maxLength' in s && v.length > s.maxLength) errs.push(P + ': longer than ' + s.maxLength);
      if (s.pattern && !new RegExp(s.pattern).test(v)) errs.push(P + ': ' + JSON.stringify(v) + ' does not match ' + s.pattern);
    }
    if (Array.isArray(v)) {
      if ('minItems' in s && v.length < s.minItems) errs.push(P + ': fewer than ' + s.minItems + ' items');
      if ('maxItems' in s && v.length > s.maxItems) errs.push(P + ': more than ' + s.maxItems + ' items');
      if (s.items) v.forEach((x, i) => chk(x, s.items, path + '[' + i + ']'));
    }
    if (T.object(v)) {
      const keys = Object.keys(v);
      if ('minProperties' in s && keys.length < s.minProperties) errs.push(P + ': fewer than ' + s.minProperties + ' properties');
      for (const r of s.required || []) if (!(r in v)) errs.push(P + ': missing "' + r + '"');
      const props = s.properties || {}, pp = s.patternProperties || {}, addl = 'additionalProperties' in s ? s.additionalProperties : true;
      for (const k of keys) {
        const sub = path ? path + '.' + k : k;
        if (s.propertyNames && s.propertyNames.pattern && !new RegExp(s.propertyNames.pattern).test(k)) errs.push(P + ': bad property name ' + JSON.stringify(k));
        let matched = false;
        if (k in props) { matched = true; chk(v[k], props[k], sub); }
        for (const pat in pp) if (new RegExp(pat).test(k)) { matched = true; chk(v[k], pp[pat], sub); }
        if (!matched) { if (addl === false) errs.push(P + ': unknown property "' + k + '"'); else if (typeof addl === 'object') chk(v[k], addl, sub); }
      }
    }
    if (s.oneOf) {
      let n = 0;
      for (const o of s.oneOf) { const save = errs.length; chk(v, o, path); if (errs.length === save) n++; errs.length = save; }
      if (n !== 1) errs.push(P + ': matches ' + n + ' of the oneOf alternatives (need exactly 1)');
    }
    if (s.anyOf) {
      let ok = false;
      for (const o of s.anyOf) { const save = errs.length; chk(v, o, path); ok = ok || errs.length === save; errs.length = save; }
      if (!ok) errs.push(P + ': matches none of the anyOf alternatives');
    }
    for (const o of s.allOf || []) chk(v, o, path);
  };
  chk(inst, schema, '');
  return errs;
};
