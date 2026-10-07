'use strict';
/* Monster kit editor, web edition, part 3: the example kit.  A small original blob creature drawn by code (ellipses, a shaded rim,
   two eyes), with the frames and animations the 'stalker' AI needs.  Nothing here comes from the game. */
const WEX = {};
WEX.NAME = 'example_blob';
WEX.TITLE = 'Example kit (not from the game)';
/* a neutral example palette (the real fight palette is a property of the loaded kit) */
WEX.palette = function () {
  const p = ['#000000', '#2b3340', '#3a4454', '#4b576b', '#5e6b82', '#74829b', '#7a4a4a', '#9a5c5c', '#b87070',
    '#1d3b2a', '#2f7d4f', '#46a86c', '#8fdc9c', '#f2f2e8', '#20202a', '#cc0000'];
  for (let i = 0; i < 16; i++) { const v = 40 + i * 12; p.push('#' + [v, v + 6, v + 14].map((c) => Math.min(255, c).toString(16).padStart(2, '0')).join('')); }
  return p;
};

const FW = 64, FH = 40, CX = 22;
/* one frame: indices 0 (transparent), 9 outline, 10..12 body shading, 13 eye white, 14 pupil / mouth */
WEX.blob = function (o) {
  const d = new Uint8Array(FW * FH), sq = o.squash || 0;
  const rx = 15 * (1 + sq * 0.8), ry = 15 * (1 - sq), cx = CX + (o.shift || 0), cy = FH - 2 - ry;
  const put = (x, y, v) => { x = Math.round(x); y = Math.round(y); if (x >= 0 && x < FW && y >= 0 && y < FH) d[y * FW + x] = v; };
  if (o.arm) {                                   // a reaching arm: outlined band with a fist
    const y0 = Math.round(cy - 2 - (o.armUp || 0)), th = o.thick || 5, x1 = Math.min(FW - 2, Math.round(cx + rx * 0.6 + o.arm));
    for (let x = Math.round(cx); x <= x1; x++) for (let j = -1; j <= th; j++) put(x, y0 + j, j < 0 || j === th ? 9 : (j < 2 ? 12 : 11));
    for (let j = -2; j <= th + 1; j++) for (let k = 0; k < 5; k++) { const e = j < -1 || j > th; put(x1 - 3 + k, y0 + j, e || k === 4 ? 9 : 12); }
  }
  for (let y = 0; y < FH; y++) for (let x = 0; x < FW; x++) {
    const nx = (x - cx) / rx, ny = (y - cy) / ry, q = nx * nx + ny * ny;
    if (q > 1) continue;
    if (q > 0.74) { d[y * FW + x] = 9; continue; }
    const t = -0.5 * nx - 0.7 * ny;
    d[y * FW + x] = t > 0.3 ? 12 : t > -0.2 ? 11 : 10;
  }
  const ey = Math.round(cy - ry * 0.2), look = o.look || 0;
  for (const s of [-1, 1]) {
    const ex = Math.round(cx + s * rx * 0.38 + look);
    if (o.dead || o.hurt) { for (let k = -2; k <= 2; k++) { put(ex + k, ey + k, 14); put(ex + k, ey - k, 14); } continue; }
    for (let j = -2; j <= 2; j++) for (let k = -2; k <= 2; k++) if (j * j + k * k <= 5) put(ex + k, ey + j, 13);
    put(ex + 1 + look, ey, 14); put(ex + 1 + look, ey + 1, 14);
  }
  const my = Math.round(cy + ry * 0.4), mw = o.mouth ? 6 : 4;
  for (let k = -mw; k <= mw; k++) { put(cx + k + look, my, 14); if (o.mouth) put(cx + k + look, my + 1, 14); }
  return d;
};

/* the frame list: [id, options, attack points or null] */
WEX.frameDefs = function () {
  const gb = (arm, up) => { const x = Math.min(FW - 3, Math.round(CX + 15 * 0.6 + arm)), y = Math.round(FH - 2 - 15 - 2 - (up || 0)); return [[x - 2, y + 2], [x, y + 2], [x - 1, y]]; };
  return [
    ['stand0', {}, null], ['stand1', { squash: 0.08 }, null],
    ['walk0', { shift: -1, squash: 0.04 }, null], ['walk1', { shift: 0, squash: 0.12 }, null], ['walk2', { shift: 1, squash: 0.04 }, null], ['walk3', { shift: 0, squash: 0.12, look: 1 }, null],
    ['strike0', { squash: 0.12, arm: 5, shift: -2 }, null], ['strike1', { arm: 22, mouth: true, look: 1 }, gb(22)],
    ['heavy0', { squash: 0.2, arm: 6, armUp: 6, thick: 7, shift: -3 }, null], ['heavy1', { arm: 26, armUp: 2, thick: 8, mouth: true, look: 2 }, gb(26, 2)],
    ['hurt0', { squash: 0.15, hurt: true, shift: -2 }, null],
    ['die0', { squash: 0.3, dead: true, shift: -2 }, null], ['die1', { squash: 0.5, dead: true, shift: -3 }, null], ['die2', { squash: 0.7, dead: true, shift: -3 }, null],
  ];
};

/* returns {kit, sheets: [{w, h, data}]} */
WEX.build = function () {
  const defs = WEX.frameDefs(), pad = 1, per = Math.floor(640 / (FW + pad));
  const rows = Math.ceil(defs.length / per), sw = 640, sh = rows * (FH + pad) - pad;
  const sheet = new Uint8Array(sw * sh), frames = [], idx = {};
  defs.forEach(([id, o, pts], i) => {
    const x = (i % per) * (FW + pad), y = Math.floor(i / per) * (FH + pad), px = WEX.blob(o);
    for (let j = 0; j < FH; j++) for (let k = 0; k < FW; k++) sheet[(y + j) * sw + x + k] = px[j * FW + k];
    const fr = { id, src: { sheet: 0, rect: [x, y, FW, FH] }, anchor: [CX + (o.shift || 0), FH - 2] };
    if (pts) fr.attack = { type: 0, points: pts };
    frames.push(fr); idx[id] = i;
  });
  const draw = (f, flags) => Object.assign({ frame: f, slot: 0, dx: 0, dy: 0, hurt: true }, flags || {});
  const step = (ticks, f, flags, move) => ({ ticks, draws: [draw(f, flags)], move: move || [0, 0, 0], sound: null, events: [] });
  const an = (steps, extra) => Object.assign({ loop: 0, steps, motion: null, on_dead: null, asm: null }, extra || {});
  const atk = { attack: true };
  const dieAsm = ['fall:', 'draw 0 ' + idx.die0 + ' dx=0 dy=0', 'end', 'end', 'end', 'draw 0 ' + idx.die1 + ' dx=0 dy=0', 'end', 'end', 'end',
    'draw 0 ' + idx.die2 + ' dx=0 dy=0', 'end', 'end', 'end', 'end', 'end', 'end', 'engine LAB_0005', 'kill', 'done'].join('\n');
  const kit = {
    kit: 1, name: WEX.NAME, title: WEX.TITLE, cloned_from: null, ai: 'stalker', planes: 5,
    palette: { scene: 'example', colors: WEX.palette(), own: { 9: '#1d3b2a', 10: '#2f7d4f', 11: '#46a86c', 12: '#8fdc9c', 13: '#f2f2e8', 14: '#20202a', 15: '#cc0000' }, allow: [0, 9, 10, 11, 12, 13, 14, 15] },
    sheets: [{ file: 'sheet.png', grid: { w: FW, h: FH, pad } }],
    celsets: [{ slot: 0, file: 'auto', hits: true, hit_name: 'blob0.cel', frames }],
    animations: {
      stand: an([step(3, 'stand0'), step(3, 'stand1')]),
      walk0: an([step(2, 'walk0', null, [16, 0, 0])]), walk1: an([step(2, 'walk1', null, [26, 0, 0])]),
      walk2: an([step(2, 'walk2', null, [13, 0, 0])]), walk3: an([step(2, 'walk3', null, [26, 0, 0])]),
      ouch: an([step(2, 'hurt0'), step(2, 'stand0')], { on_dead: 'fall' }),
      club: an([step(3, 'strike0'), step(2, 'strike1', atk), step(3, 'strike0')]),
      slam: an([step(4, 'heavy0'), step(2, 'heavy1', atk), step(4, 'heavy0')]),
      fall: an([step(3, 'die0'), step(3, 'die1'), step(6, 'die2', { hurt: false })], { asm: dieAsm }),
    },
    roles: { idle: 'stand', after_hit: 'stand', hurt: 'ouch', strike: 'club', heavy: 'slam', die: 'fall', 'walk.side': ['walk0', 'walk1', 'walk2', 'walk3', null, null, null, null] },
    damage: [0, 0, 0, 0, 0, 0, 0, 0, 0],
    stats: { hp: 30, reach: 60, keep_away: 30, depth: 5 },
    sounds: { bank: 'To.a' },
    arena: { alive_max: 1, total: 1, spawn: 'alternating', frame_budget: 6 },
    tier: 'auto',
  };
  return { kit, sheets: [{ file: 'sheet.png', w: sw, h: sh, data: sheet }] };
};
