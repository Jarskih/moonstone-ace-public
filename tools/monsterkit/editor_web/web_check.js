'use strict';
/* Monster kit editor, web edition, part 2: the kit check in the browser.  A port of check_kit() of tools/monsterkit.py (the parts that
   need no original game data: schema, frame sizes, colour classes, memory budget, hit text budget, script pools, the AI checklist R1,
   the die sequence, tier).  The sound-bank usability test needs the original synth tables and is skipped (reported as info). */
const WCHK = {};
WCHK.FORBIDDEN = [6, 7, 8];
WCHK.REGION = [].concat([1, 2, 3, 4, 5], Array.from({ length: 16 }, (_, i) => 16 + i));
const wordsOf = (w) => (w + 15) >> 4;
const popcount = (v) => { let n = 0; while (v) { n += v & 1; v >>>= 1; } return n; };
const pad2 = (n) => String(n).padStart(2, '0');

/* the normalised ops of an animation: from the verbatim asm, else built from the steps */
WCHK.animOps = function (kit, anim) {
  const ops = [], asm = anim.asm;
  if (asm) {
    let fs = 2;
    for (let line of asm.split('\n')) {
      line = line.split(';')[0].trim();
      if (!line || line.startsWith('.org') || line.endsWith(':')) continue;
      const w = line.split(/\s+/), m = /^draw (\d+) (\d+) dx=(-?\d+) dy=(-?\d+)(.*)$/.exec(line);
      if (m) {
        const flags = m[5].trim().split(/\s+/);
        ops.push({ op: 'draw', slot: +m[1], frame: +m[2], hurt: flags.includes('hurt'), attack: flags.includes('attack'), creature: fs === 2 });
      } else if (w[0] === 'frameset') fs = { knights: 1, creature: 2, map: 3, effects: 4 }[w[1]] || 0;
      else if (['ifdead', 'engine', 'jump', 'call'].includes(w[0])) ops.push({ op: w[0], target: w[1] });
      else if (w[0] === 'sound') ops.push({ op: 'sound', id: w[1].startsWith('$') ? parseInt(w[1].slice(1), 16) : parseInt(w[1], 10) });
      else if (['end', 'done', 'kill'].includes(w[0])) ops.push({ op: w[0] });
    }
    return ops;
  }
  const ids = {};
  for (const cs of kit.celsets) cs.frames.forEach((fr, i) => { ids[fr.id] = [cs.slot, i]; });
  for (const st of anim.steps) {
    for (const d of st.draws) {
      const [s, i] = ids[d.frame] || [d.slot || 0, 0];
      ops.push({ op: 'draw', slot: d.slot !== undefined ? d.slot : s, frame: i, hurt: !!d.hurt, attack: !!d.attack, creature: true });
    }
    if (st.sound !== undefined && st.sound !== null) ops.push({ op: 'sound', id: st.sound });
    for (let n = Math.max(1, st.ticks || 1); n > 0; n--) ops.push({ op: 'end' });
  }
  if (anim.on_dead) ops.push({ op: 'ifdead', target: anim.on_dead });
  if (ops.length && ops[ops.length - 1].op !== 'end') ops.push({ op: 'end' });
  ops.push({ op: 'done' });
  return ops;
};

WCHK.maxTickArea = function (kit) {
  const dims = {};
  for (const cs of kit.celsets) cs.frames.forEach((fr, i) => { dims[cs.slot + ',' + i] = fr.src ? fr.src.rect[2] * fr.src.rect[3] : 0; });
  let best = 0;
  for (const an of Object.values(kit.animations)) {
    let area = 0;
    for (const op of WCHK.animOps(kit, an)) {
      if (op.op === 'draw' && op.creature) area += dims[op.slot + ',' + op.frame] || 0;
      else if (op.op === 'end' || op.op === 'done') { best = Math.max(best, area); area = 0; }
    }
  }
  return best;
};

WCHK.hitSetTextLength = function (cs) {
  const fr = cs.frames.map((f) => (f.attack && f.attack.points && f.attack.points.length ? f.attack : null));
  let last = 0; fr.forEach((f, i) => { if (f) last = i + 1; });
  const n = Math.max(cs.hit_frames || 0, last);
  const lines = [cs.hit_name || cs.file || ''];
  for (const f of fr.slice(0, n)) {
    if (!f) lines.push('00');
    else { lines.push(pad2(f.points.length), pad2(f.type || 0), f.points.map(([x, y]) => String(x).padStart(3, '0') + String(y).padStart(3, '0')).join('')); }
  }
  lines.push('99');
  return lines.join('\n').length + 1;
};

WCHK.detectTier = function (kit) {
  const o = kit.origin;
  if (!o) return kit.cloned_from ? 't1' : 't2';
  let same = kit.celsets.length === o.celsets.length;
  if (same) {
    kit.celsets.forEach((cs, k) => {
      const oc = o.celsets[k];
      if (cs.frames.length !== oc.frames.length || cs.frames.some((fr, i) => fr.src && (fr.src.rect[2] !== oc.frames[i].w || fr.src.rect[3] !== oc.frames[i].h))) same = false;
    });
  }
  const scripts = WIO.sha1hex(Object.values(kit.animations).map((a) => a.asm || '').join('')) === o.scripts_sha1;
  if (same && scripts) return 't0';
  return scripts ? 't1' : 't2';
};

/* kit: the kit.json object; sheets: [{w, h, data}]; cat / schema: the embedded catalog and schema */
WCHK.check = function (kit, sheets, cat, schema) {
  const rep = { errors: [], warnings: [], info: [], checklist: [], tier: null };
  const err = (m) => rep.errors.push(m), warn = (m) => rep.warnings.push(m);
  const finish = () => {
    rep.ok = !rep.errors.length;
    const out = ['checklist:'];
    for (const c of rep.checklist) out.push('  [' + (c.ok ? 'ok' : 'MISSING') + '] ' + c.role.padEnd(18) + ' ' + c.detail);
    rep.errors.forEach((e) => out.push('ERROR: ' + e)); rep.warnings.forEach((e) => out.push('warning: ' + e)); rep.info.forEach((e) => out.push('info: ' + e));
    out.push('tier: ' + rep.tier + '; ' + (rep.ok ? 'OK' : rep.errors.length + ' error(s): export blocked'));
    rep.text = out.join('\n');
    return rep;
  };
  const errs = WIO.validate(kit, schema);
  if (errs.length) { errs.forEach((e) => err('schema: ' + e)); rep.tier = '?'; return finish(); }
  const ai = cat.ais[kit.ai];
  if (!ai) { err('unknown ai ' + JSON.stringify(kit.ai) + ' (known: ' + Object.keys(cat.ais).join(' ') + ')'); rep.tier = '?'; return finish(); }
  if (ai.advanced) warn('ai ' + kit.ai + ' is marked advanced (more hard-wired scripts than a kit can express yet)');
  const limits = cat.limits, origin = kit.origin || {}, planes = kit.planes;

  /* ---- cel sets: sizes, memory, palette classes */
  const slots = kit.celsets.map((c) => c.slot);
  if (new Set(slots).size !== slots.length) err('two cel sets use the same slot');
  let arena = 0;
  const agg = {};
  for (const cs of kit.celsets) {
    if (cs.frames.length > limits.frames_per_cel_file) err('slot ' + cs.slot + ': ' + cs.frames.length + ' frames, at most ' + limits.frames_per_cel_file);
    const ids = cs.frames.map((f) => f.id);
    if (new Set(ids).size !== ids.length) err('slot ' + cs.slot + ': duplicate frame ids');
    const ofs = (origin.celsets || []).find((oc) => oc.slot === cs.slot);
    const dims = [];
    cs.frames.forEach((fr, i) => {
      if (!fr.src) { err('frame ' + fr.id + ' has no src'); return; }
      const sh = sheets[fr.src.sheet];
      if (!sh) { err('frame ' + fr.id + ': sheet ' + fr.src.sheet + ' does not exist'); return; }
      const [x, y, w, h] = fr.src.rect;
      if (x + w > sh.w || y + h > sh.h) { err('frame ' + fr.id + ': rect ' + JSON.stringify(fr.src.rect) + ' lies outside sheet ' + fr.src.sheet + ' (' + sh.w + 'x' + sh.h + ')'); return; }
      if (w > limits.frame_size.max_width_px || (2 * wordsOf(w) + 2) * h > limits.frame_size.temp_plane_bytes) err('frame ' + fr.id + ': ' + w + 'x' + h + ' exceeds the blit temp plane ((2 * words + 2) * h <= 4800, w <= 336)');
      if (h > limits.frame_size.bltsize.max_rows) err('frame ' + fr.id + ': height ' + h + ' above BLTSIZE limit');
      const seen = new Set();
      let mx = 0, orv = 0;
      for (let j = 0; j < h; j++) for (let k = 0; k < w; k++) { const v = sh.data[(y + j) * sh.w + x + k]; seen.add(v); if (v > mx) mx = v; orv |= v; }
      if (mx >= (1 << planes)) err('frame ' + fr.id + ': colour index ' + mx + ' does not fit ' + planes + ' planes');
      const inherited = new Set(ofs ? ofs.colors_used : []);
      for (const v of WCHK.FORBIDDEN.filter((c) => seen.has(c))) {
        if (inherited.has(v)) { const a = agg.inh || (agg.inh = []); if (!a.includes(fr.id)) a.push(fr.id); }
        else err('frame ' + fr.id + ' uses index ' + v + ' (the first fighter colour)');
      }
      const rv = WCHK.REGION.filter((c) => seen.has(c) && !inherited.has(c));
      if (rv.length) (agg.rv || (agg.rv = [])).push(fr.id + '[' + rv.join(', ') + ']');
      const bits = ((ofs && i < ofs.frames.length) ? ofs.frames[i].plane_bits : 0) | orv;
      dims.push([w, h, bits]);
      if (fr.attack) {
        const pts = fr.attack.points;
        if (pts.length > limits.attack_points_per_frame) err('frame ' + fr.id + ': ' + pts.length + ' attack points, at most 98');
        for (const [px, py] of pts) {
          if (!(px >= 0 && px <= 255 && py >= 0 && py <= 255)) err('frame ' + fr.id + ': attack point (' + px + ',' + py + ') outside 0..255');
          else if (px > w || py > h) (agg.out || (agg.out = [])).push(fr.id + '(' + px + ',' + py + ' in ' + w + 'x' + h + ')');
        }
      }
    });
    arena += dims.reduce((a, [w, h, b]) => a + wordsOf(w) * 2 * h * popcount(b), 0) + 0x168 + dims.length * 10 + 10;
  }
  for (const [key, msg] of [['inh', 'frames use the first fighter colours 6..8 inherited from the source art: '], ['rv', 'frames use region/backdrop-variable indices: '], ['out', 'attack points beyond their frame: ']]) {
    if (agg[key]) warn(agg[key].length + ' ' + msg + agg[key].slice(0, 4).join(', ') + (agg[key].length > 4 ? ', ...' : ''));
  }
  if (kit.celsets.length > limits.cel_files_per_creature) err(kit.celsets.length + ' cel sets, at most ' + limits.cel_files_per_creature);
  if (arena > limits.creature_arena_bytes) {
    warn('cel sets need ~' + arena + ' bytes of the creature arena (' + limits.creature_arena_bytes + '); a set living in the second area is budgeted there');
    if (arena > limits.creature_arena_bytes + limits.second_area_bytes) err('cel sets need ~' + arena + ' bytes, arena + second area hold ' + (limits.creature_arena_bytes + limits.second_area_bytes));
  }
  const hitSets = kit.celsets.filter((c) => c.hits).length;
  if (hitSets + 1 > limits.hit_pairs) err(hitSets + " hit sets (+ the knights' kn4) exceed the " + limits.hit_pairs + ' pairs of LAB_0A51');
  if (origin.celsets && origin.celsets.length) {
    const base = limits.hit_original_bytes - origin.celsets.reduce((a, oc) => a + (oc.hit_text_bytes || 0), 0);
    const now = kit.celsets.filter((c) => c.hits).reduce((a, cs) => a + WCHK.hitSetTextLength(cs), 0);
    if (base + now > limits.hit_text_bytes) err('collide.hit would be ' + (base + now) + ' bytes, the game reads at most ' + limits.hit_text_bytes);
  }

  /* ---- scripts: pools, hurt flags */
  const lmap = {};
  for (const n of Object.keys(kit.animations)) lmap[n] = n;
  for (const [n, a] of Object.entries(kit.animations)) for (let line of (a.asm || '').split('\n')) { line = line.split(';')[0].trim(); if (line.endsWith(':')) lmap[line.slice(0, -1).toLowerCase()] = n; }
  const opsOf = {};
  for (const [n, a] of Object.entries(kit.animations)) opsOf[n] = WCHK.animOps(kit, a);
  const hasPoints = {};
  for (const cs of kit.celsets) cs.frames.forEach((fr, i) => { hasPoints[cs.slot + ',' + i] = !!(cs.hits && fr.attack && fr.attack.points && fr.attack.points.length); });
  for (const [n, ops] of Object.entries(opsOf)) {
    let hurt = 0, att = 0;
    for (const op of ops) {
      if (op.op === 'draw') { hurt += op.hurt; att += op.attack; }
      else if (op.op === 'end' || op.op === 'done') {
        if (hurt > limits.draws_per_tick_lists || att > limits.draws_per_tick_lists) err('animation ' + n + ': ' + hurt + ' hurt / ' + att + ' attack draws in one tick, the lists hold 8');
        hurt = att = 0;
      }
    }
    for (const op of ops) {
      if (op.op === 'draw' && op.creature) {
        const cs = kit.celsets.find((c) => c.slot === op.slot);
        if (!cs || op.frame >= cs.frames.length) err('animation ' + n + ' draws slot ' + op.slot + ' frame ' + op.frame + ' which the kit does not have');
      }
    }
  }
  rep.info.push('sound ids not checked (the web editor has no original sound tables)');
  if (!(kit.sounds.bank in cat.sound_banks.creature_files)) err('unknown sound bank ' + JSON.stringify(kit.sounds.bank) + ' (' + Object.keys(cat.sound_banks.creature_files).join(' ') + ')');

  /* ---- R1 checklist */
  const resolve = (v) => {
    if (v === null || v === undefined) return [null, false];
    if (typeof v === 'string' && v.startsWith('ext:')) return [v, true];
    if (typeof v === 'string' && v in kit.animations) return [v, false];
    return [null, false];
  };
  const hasAttack = (name) => (opsOf[name] || []).some((o) => o.op === 'draw' && o.attack && o.creature && hasPoints[o.slot + ',' + o.frame]);
  const hasHurt = (name) => (opsOf[name] || []).some((o) => o.op === 'draw' && o.hurt);
  const gap = (msg) => { const inh = (origin.inherited_gaps || []).includes(msg); (inh ? warn : err)(msg + (inh ? ' [inherited from the source]' : '')); return inh; };
  const hurtRoots = [];
  for (const r of ai.roles) {
    const nm = r.name;
    if (r.kind === 'field' || r.kind === 'label') {
      const [a, ext] = resolve(kit.roles[nm]);
      if (r.required && !a) { rep.checklist.push({ role: nm, ok: false, detail: 'no animation (' + r.meaning + ')' }); err('role ' + nm + ' is missing: ' + r.meaning); continue; }
      if (!a) { rep.checklist.push({ role: nm, ok: true, detail: 'not set (optional)' }); continue; }
      let detail = ext ? 'shared engine script ' + a : a + ' (' + opsOf[a].filter((o) => o.op === 'draw').length + ' frames drawn)', ok = true;
      if (!ext) {
        if (r.attack === 'required' && !hasAttack(a)) { detail = a + ': needs an attack-flag draw whose frame has attack points'; ok = gap('role ' + nm + ' (' + a + '): no attack draw with attack points (a cel without a hit set never hits)'); }
        if (nm.startsWith('hurt')) hurtRoots.push(a);
        if ((nm === 'idle' || r.attack === 'required') && !hasHurt(a)) warn('role ' + nm + ' (' + a + ') has no hurt-flag draw: it cannot be hit while it plays');
      }
      rep.checklist.push({ role: nm, ok, detail });
    } else {
      const req = r.required_indices || [];
      const arr = nm === 'hurt_by_action' ? kit.hurt_by_action : kit.roles[nm];
      const base = (nm === 'hurt_by_action' || nm === 'action') ? 0 : 8 * Math.floor((req.length ? Math.min(...req) : 0) / 8);
      if (!req.length) continue;
      const miss = [];
      for (const i of req) {
        const v = arr && i - base >= 0 && i - base < arr.length ? arr[i - base] : null;
        const [a, ext] = resolve(v);
        if (!a) miss.push(i);
        else if (!ext) {
          if (r.attack === 'required' && !hasAttack(a)) { gap('role ' + nm + '[' + i + '] (' + a + '): no attack draw with attack points'); miss.push(i); }
          if (nm === 'hurt_by_action') hurtRoots.push(a);
        }
      }
      if (miss.length) err('role ' + nm + ' is missing entries [' + miss.join(', ') + ']');
      rep.checklist.push({ role: nm, ok: !miss.length, detail: !miss.length ? 'entries [' + req.join(', ') + ']' : 'missing entries [' + miss.join(', ') + ']' });
    }
  }
  /* die: every hurt role jumps (ifdead) to a die sequence that counts the death once and kills the job */
  const [die] = resolve(kit.roles.die);
  if (!die) { rep.checklist.push({ role: 'die', ok: false, detail: 'no die sequence (ifdead target of the hurt roles)' }); err('role die is missing: the creature only dies through its scripts (hurt role: ifdead -> die sequence)'); }
  else {
    const engine = opsOf[die].filter((o) => o.op === 'engine').map((o) => o.target.toLowerCase());
    const kills = opsOf[die].filter((o) => o.op === 'kill').length, probs = [];
    if (engine.filter((t) => t === 'lab_0005').length !== 1 && !engine.includes('creature_died')) probs.push('must call engine LAB_0005 exactly once');
    if (kills !== 1) probs.push('must end with kill');
    probs.forEach((p) => err('die sequence ' + die + ' ' + p));
    rep.checklist.push({ role: 'die', ok: !probs.length, detail: !probs.length ? die : die + ': ' + probs.join('; ') });
  }
  for (const hname of [...new Set(hurtRoots)].sort()) {
    if (!(opsOf[hname] || []).some((o) => o.op === 'ifdead')) warn('hurt script ' + hname + ' has no ifdead: a creature hit by it cannot die');
  }
  void lmap;
  /* ---- tunables, frame rate proxy */
  for (const t of ai.tunables) {
    if (!t.used && kit.stats[t.name] !== undefined && kit.stats[t.name] !== null && origin.stats && kit.stats[t.name] !== origin.stats[t.name]) rep.info.push('stat ' + t.name + ' is not read by the ' + kit.ai + ' AI');
  }
  const mta = WCHK.maxTickArea(kit);
  if (origin.max_tick_area && mta > 1.2 * origin.max_tick_area) warn('largest drawn area in one tick ' + mta + ' px is above 120% of the source (' + origin.max_tick_area + '): a slower fight frame');
  rep.tier = WCHK.detectTier(kit);
  return finish();
};
