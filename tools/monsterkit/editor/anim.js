'use strict';
/* Bottom panel, animation preview: the kit's animation steps played on a 50 Hz clock (one step tick = 5 or 6 VBLs, the fight
   frame budget, docs/MONSTER_KIT.md 1.2), left / right facing, motion, hurt boxes and attack points. */
(function () {
  const { S, h, $ } = MK;
  const P = { playing: !(window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches), tick: 0, facing: 1, vbl: 6, loop: true, hurt: true, attack: true, zoom: 1, acc: 0, last: 0, raf: 0, cache: {}, cacheVer: -1 };
  MK.preview = P;
  const W = 320, H = 176, GY = 146;

  /* flatten the steps into ticks: [{si, first, step}] */
  function ticksOf(an) {
    const out = [];
    an.steps.forEach((st, si) => { const n = Math.max(1, st.ticks || 1); for (let i = 0; i < n; i++) out.push({ si, first: i === 0, step: st }); });
    return out;
  }
  MK.previewTicks = ticksOf;

  function frameCanvas(id, mirror) {
    if (P.cacheVer !== S.ver) { P.cache = {}; P.cacheVer = S.ver; }
    const k = id + (mirror ? 'm' : '');
    if (P.cache[k]) return P.cache[k];
    const f = MK.frameById(id);
    const px = f && MK.framePixels(f.fr);
    if (!px) return null;
    const c = MK.toCanvas(px.data, px.w, px.h);
    if (mirror) {
      const m = document.createElement('canvas'); m.width = c.width; m.height = c.height;
      const x = m.getContext('2d'); x.translate(c.width, 0); x.scale(-1, 1); x.drawImage(c, 0, 0);
      return (P.cache[k] = m);
    }
    return (P.cache[k] = c);
  }

  function render() {
    const cvs = $('#animcv');
    if (!cvs || !S.kit) return;
    const an = S.kit.animations[S.animName];
    const ctx = cvs.getContext('2d');
    ctx.imageSmoothingEnabled = false;
    ctx.fillStyle = '#20242d'; ctx.fillRect(0, 0, W, H);
    ctx.fillStyle = '#2d3340'; ctx.fillRect(0, GY, W, H - GY);
    ctx.strokeStyle = '#46506a'; ctx.beginPath(); ctx.moveTo(0, GY + .5); ctx.lineTo(W, GY + .5); ctx.stroke();
    if (!an) return;
    const ticks = ticksOf(an);
    if (!ticks.length) return;
    if (P.tick >= ticks.length) P.tick = 0;
    // position: the moves of all steps up to this tick (x facing, y, z)
    let mx = 0, my = 0, mz = 0;
    for (let t = 0; t <= P.tick; t++) if (ticks[t].first && ticks[t].step.move) { mx += ticks[t].step.move[0]; my += ticks[t].step.move[1]; mz += ticks[t].step.move[2]; }
    const X = Math.round(W / 2 + P.facing * mx), Y = GY + my + mz;
    ctx.strokeStyle = '#4ad6ff'; ctx.beginPath(); ctx.moveTo(X - 6, Y + .5); ctx.lineTo(X + 7, Y + .5); ctx.moveTo(X + .5, Y - 6); ctx.lineTo(X + .5, Y + 7); ctx.stroke();
    const st = ticks[P.tick].step;
    for (const d of st.draws) {
      const f = MK.frameById(d.frame);
      if (!f) continue;
      const c = frameCanvas(d.frame, P.facing < 0);
      if (!c) continue;
      const ax = (f.fr.anchor || [0, 0])[0], ay = (f.fr.anchor || [0, 0])[1];
      const dxa = (d.dx || 0) - ax, dya = (d.dy || 0) - ay;               // the asm dx / dy: frame top-left from the job origin
      const left = P.facing > 0 ? X + dxa : X - dxa - c.width, top = Y + dya;
      ctx.drawImage(c, left, top);
      if (P.hurt && d.hurt) { ctx.strokeStyle = 'rgba(80,255,120,.8)'; ctx.strokeRect(left + .5, top + .5, c.width - 1, c.height - 1); }
      if (P.attack && d.attack && f.fr.attack) {
        ctx.fillStyle = '#ff4040';
        for (const p of f.fr.attack.points) ctx.fillRect(P.facing > 0 ? left + p[0] - 1 : left + c.width - 1 - p[0] - 1, top + p[1] - 1, 3, 3);
      }
    }
    ctx.fillStyle = '#9fb0d0'; ctx.font = '10px monospace';
    ctx.fillText('tick ' + (P.tick + 1) + '/' + ticks.length + '  step ' + (ticks[P.tick].si + 1) + '/' + an.steps.length + (st.sound != null ? '  sound $' + st.sound.toString(16).toUpperCase() : ''), 4, 11);
    document.querySelectorAll('.chip').forEach((c) => c.classList.toggle('on', +c.dataset.si === ticks[P.tick].si));
    const info = $('#anim-info');
    if (info) info.textContent = (50 / P.vbl).toFixed(1) + ' fps, ' + P.vbl + ' VBL per tick, ' + (ticks.length * P.vbl * 20 / 1000).toFixed(2) + ' s per run';
  }

  function loop(t) {
    P.raf = requestAnimationFrame(loop);
    if (!P.last) P.last = t;
    const dt = t - P.last; P.last = t;
    if (!P.playing || !S.kit || !$('#animcv')) return;
    P.acc += dt;
    const ms = P.vbl * 20;
    let moved = false;
    while (P.acc >= ms) {
      P.acc -= ms; moved = true;
      const an = S.kit.animations[S.animName];
      const n = an ? ticksOf(an).length : 0;
      if (!n) break;
      P.tick++;
      if (P.tick >= n) { if (P.loop) P.tick = 0; else { P.tick = n - 1; P.playing = false; MK.emit('anim-state'); } }
    }
    if (moved) render();
  }

  function stepBy(d) {
    const an = S.kit.animations[S.animName]; if (!an) return;
    const n = ticksOf(an).length;
    P.playing = false; P.tick = (P.tick + d + n) % n; MK.emit('anim-state'); render();
  }

  MK.renderBottomAnim = function (root) {
    if (!S.kit) return;
    const names = Object.keys(S.kit.animations);
    if (!S.animName || !S.kit.animations[S.animName]) S.animName = names[0];
    const an = S.kit.animations[S.animName];
    const role = Object.entries(S.kit.roles).filter(([, v]) => v === S.animName || (Array.isArray(v) && v.includes(S.animName))).map(([k]) => k);
    const ctl = h('div', { class: 'animctl' },
      h('select', { id: 'animsel', onchange: (e) => { S.animName = e.target.value; P.tick = 0; P.acc = 0; MK.emit('anim-state'); MK.renderBottom(); } },
        names.map((n) => h('option', { value: n, selected: n === S.animName }, n))),
      h('div', { class: 'btnrow' },
        h('button', { onclick: () => stepBy(-1), title: 'previous tick' }, '|<'),
        h('button', { id: 'playbtn', class: 'primary', onclick: () => { P.playing = !P.playing; MK.emit('anim-state'); } }, P.playing ? 'Pause' : 'Play'),
        h('button', { onclick: () => stepBy(1), title: 'next tick' }, '>|')),
      h('label', {}, 'Facing ', h('select', { onchange: (e) => { P.facing = +e.target.value; render(); } }, [[1, 'right'], [-1, 'left']].map(([v, t]) => h('option', { value: v, selected: v === P.facing }, t)))),
      h('label', {}, 'Speed ', h('select', { onchange: (e) => { P.vbl = +e.target.value; render(); } }, [[6, '6 VBL (budget 8.3 fps)'], [5, '5 VBL (10 fps)'], [3, '3 VBL'], [10, '10 VBL (slow)']].map(([v, t]) => h('option', { value: v, selected: v === P.vbl }, t)))),
      h('label', { class: 'chk' }, h('input', { type: 'checkbox', checked: P.loop, onchange: (e) => { P.loop = e.target.checked; } }), 'loop'),
      h('label', { class: 'chk' }, h('input', { type: 'checkbox', checked: P.hurt, onchange: (e) => { P.hurt = e.target.checked; render(); } }), 'hurt boxes'),
      h('label', { class: 'chk' }, h('input', { type: 'checkbox', checked: P.attack, onchange: (e) => { P.attack = e.target.checked; render(); } }), 'attack points'),
      h('div', { id: 'anim-info', class: 'dim mono' }),
      h('div', { class: 'dim' }, role.length ? 'role: ' + role.join(', ') : 'not used as a role'));
    const cvs = h('canvas', { id: 'animcv', width: W, height: H, class: 'animcv' });
    const chips = h('div', { class: 'chips' }, an.steps.map((st, i) => h('span', {
      class: 'chip', 'data-si': i, title: st.draws.map((d) => d.frame + (d.hurt ? ' hurt' : '') + (d.attack ? ' attack' : '')).join('\n'),
      onclick: () => { const t = ticksOf(an).findIndex((x) => x.si === i); P.tick = Math.max(0, t); P.playing = false; MK.emit('anim-state'); render(); },
    }, (i + 1) + ': ' + st.draws.length + ' draw' + (st.draws.length === 1 ? '' : 's') + (st.ticks > 1 ? ' x' + st.ticks : '') + (st.move ? ' move ' + st.move.join(',') : '') + (st.sound != null ? ' snd' : ''))));
    root.append(h('div', { class: 'animwrap' }, ctl, h('div', { class: 'animstage' }, cvs, h('div', { class: 'dim small' }, 'Steps are read-only here: the verbatim script of a clone stays authoritative (T0 keeps it).'), chips)));
    render();
  };

  MK.on('anim-state', () => { const b = $('#playbtn'); if (b) b.textContent = P.playing ? 'Pause' : 'Play'; });
  MK.on('pixels', () => { if (S.bottom === 'anim') render(); });
  MK.on('select', () => { if (S.bottom === 'anim') render(); });
  if (!P.raf) P.raf = requestAnimationFrame(loop);
  MK.renderPreview = render;
})();
