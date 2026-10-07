#!/usr/bin/env python3
"""monsterkit.py -- clone / check / build for monster kits (ROADMAP 9.8a2, docs/MONSTER_KIT.md sections 2-3).

    py tools/monsterkit.py clone <creature> <kitdir> [--name NAME]   export an original creature into a kit (build/kits/...)
    py tools/monsterkit.py clone <kit> <kitdir> [--name NAME]        copy an existing kit under a new name
    py tools/monsterkit.py check <kitdir>                            schema, palette, sizes, memory, pools, the R1 checklist
    py tools/monsterkit.py build <kitdir> --tier T0 [--out DIR] [--reencode] [--install HDDIR]
    py tools/monsterkit.py install <outdir> <hddir>                  copy a staged <outdir>/art/* to <hddir>/art/
    py tools/monsterkit.py catalog | creatures
    py tools/monsterkit.py serve [--port N] [--kits DIR] [--disks DIR] [--hd DIR] [--open]   the browser editor (127.0.0.1 only)

Creatures: be mudmen trogg_axe trogg_axe_b trogg_spear ratmen balok troll.  A kit is a folder: kit.json (schema
tools/monsterkit/kit.schema.json) + one sheet PNG per cel file (`sheet<slot>.png`, indexed, index = colour register, palette =
the creature's fight palette, index 0 transparent).  A kit cloned from the game holds original pixels: it must live in a git-ignored
place (build/kits/); `clone` refuses a path git would track.

T0 reskin (the first deliverable): the kit's pixels go back into the ORIGINAL cel files (same frame count, sizes, scripts), written
to <out>/art/<ORIGINAL NAME> plus <out>/art/collide.hit (this creature's attack-point sets replaced, everything else kept).  The game
serves PROGDIR:art/<name> before the disk file (ROADMAP 5.2a), so `install` into an HD tree (<hd>/art/) is all it takes.  The LZSS
encoder of the original is not reproducible (tools/artconv.py's is smaller): an unchanged cel is written byte for byte from the original
stream when its decoded content equals the original's (`passthrough`), anything else is re-encoded (decode-equal).  --reencode forces the
encoder (proves format equivalence).

Importable (the 9.8b helper server calls these): clone_game, clone_kit, load_kit, save_kit, check_kit, build_t0, install_art, catalog.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'monsterkit'))

import numpy as np  # noqa: E402
from PIL import Image as PImage  # noqa: E402

import artconv  # noqa: E402
import engine_ports as P  # noqa: E402
import jsonschema_lite as J  # noqa: E402

KITDIR = os.path.join(ROOT, 'tools', 'monsterkit')
DISKS = os.path.join(ROOT, 'build', 'disks')
KIT_VERSION = 1
SHEET_W = 640

# Per original creature: the cel files in frame-set slot order (src/game/combat_load.cpp loaders; hit = the set name in collide.hit
# when the loader registers one with hitLoad / hitAdv).  trogg_axe and trogg_axe_b share their files (so does every T0 build of them).
GAME_CELS = {
    'be':          [(0, 'be1.c', 'be1.c'), (1, 'be2.c', None)],
    'mudmen':      [(0, 'Mudmen1.CEL', 'Mudmen1.cel'), (1, 'Mudmen2.CEL', None)],
    'trogg_axe':   [(0, 'TROGGAxe1.CEL', None), (1, 'TROGGAxe2.CEL', 'TroggAxe2.cel')],
    'trogg_axe_b': [(0, 'TROGGAxe1.CEL', None), (1, 'TROGGAxe2.CEL', 'TroggAxe2.cel')],
    'trogg_spear': [(0, 'TROGGSpear1.CEL', None), (1, 'TROGGSpear2.CEL', 'TroggSpear2.cel')],
    'ratmen':      [(0, 'RATMEN1.CEL', 'Ratmen1.cel'), (1, 'RATMEN2.CEL', None)],
    'balok':       [(0, 'Balok1.CEL', 'Balok1.cel'), (1, 'Balok3.CEL', None), (2, 'Balok2.CEL', None)],
    'troll':       [(0, 'TROLL1.CEL', 'Troll1.cel'), (1, 'TROLL2.CEL', 'Troll2.cel')],
}
OFFERED = list(GAME_CELS)

# Fight palette indices 9..15 per scene code = the creature's type byte (src/engine/display_fx.cpp sceneColors 162-205; 15 is forced
# $C00 outside the dragon scene by sceneTail 240-242).  The cave scenes ($18/$20) use the "other regions" row; $1C has no row.
SCENES = {
    0x00: [0x776, 0x443, 0x731, 0x520, 0x300, 0x09A, 0xC00],
    0x04: [0x332, 0xCCB, 0xB81, 0x851, 0x630, 0xF52, 0x900],
    0x18: [0x500, 0x200, 0x000, 0xB40, 0x610, 0x895, 0xC00],
    0x20: [0x500, 0x200, 0x000, 0xB40, 0x610, 0x895, 0xC00],
    0x24: [0x653, 0x942, 0x720, 0x500, 0xA96, 0x875, 0xC00],
    0x30: [0xF96, 0xC63, 0x930, 0x842, 0x521, 0xF63, 0xC00],
    0x40: [0x55A, 0x347, 0x123, 0x001, 0xF00, 0x800, 0xC00],
}
FORBIDDEN = (6, 7, 8)                       # the first fighter's colours
REGION_VARIABLE = tuple(range(1, 6)) + tuple(range(16, 32))
OWN = tuple(range(9, 16))


class KitError(Exception):
    pass


# ------------------------------------------------------------------------------------------------------------------ helpers

def _sha1(b):
    return hashlib.sha1(b).hexdigest()


def _hex24(rgb):
    return '#%02x%02x%02x' % tuple(rgb)


def _unhex(s):
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


def catalog():
    with open(os.path.join(KITDIR, 'ai_catalog.json'), encoding='utf-8') as f:
        return json.load(f)


def schema():
    with open(os.path.join(KITDIR, 'kit.schema.json'), encoding='utf-8') as f:
        return json.load(f)


def find_game_file(name, disks=None):
    """Path of an original data file (case-insensitive) on disks B, C, A (the order of tools/hdstage.py is irrelevant: B and C hold the
    same bytes), or None."""
    disks = disks or DISKS
    for d in 'BCA':
        base = os.path.join(disks, d)
        if os.path.isdir(base):
            for n in os.listdir(base):
                if n.lower() == name.lower():
                    return os.path.join(base, n)
    return None


def _refuse_tracked(path):
    """Game clones hold original pixels: never write them where git would track them."""
    path = os.path.abspath(path)
    probe = path if os.path.isdir(path) else os.path.dirname(path)
    while probe and not os.path.isdir(probe):
        probe = os.path.dirname(probe)
    try:
        top = subprocess.run(['git', 'rev-parse', '--show-toplevel'], cwd=probe, capture_output=True, text=True)
        if top.returncode != 0:
            return                                                    # not in a repository
        r = subprocess.run(['git', 'check-ignore', '-q', path], cwd=probe, capture_output=True)
    except OSError:
        return
    if r.returncode == 1:
        raise KitError('%s is not git-ignored: a clone of the game holds original pixels and must stay under build/ (build/kits/)' % path)


def kit_path(kitdir):
    return os.path.join(kitdir, 'kit.json')


def load_kit(kitdir):
    with open(kit_path(kitdir), encoding='utf-8') as f:
        return json.load(f)


def save_kit(kitdir, kit):
    os.makedirs(kitdir, exist_ok=True)
    with open(kit_path(kitdir), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(kit, f, indent=1)
        f.write('\n')


def scene_palette(type_byte, disks=None):
    """The 32-entry fight palette preview (rgb24 list): bg1a.PIV as the backdrop stand-in, indices 0 black, 15 blood red, the creature's
    own indices 9..15 from its scene row.  Indices 1-8 / 16-31 are backdrop / fighter / region colours at run time."""
    try:
        pal = list(artconv.preview_palette_default(disks or DISKS))[:32]
    except (OSError, ValueError, KeyError, struct.error):
        pal = [(i * 8, i * 8, i * 8) for i in range(32)]
    pal += [(0, 0, 0)] * (32 - len(pal))
    pal[0] = (0, 0, 0)
    for i, w in enumerate(SCENES.get(type_byte, [])):
        pal[9 + i] = artconv.rgb12_to_rgb24(w)
    pal[15] = artconv.rgb12_to_rgb24(0xC00)
    return pal


def _own_of(pal):
    return {str(i): _hex24(pal[i]) for i in OWN}


# --------------------------------------------------------------------------------------------------------------------- sheets

def pack_sheet(sizes, width=SHEET_W, pad=1):
    """Shelf-pack rectangles: sizes = [(w, h)] -> ([(x, y)], sheet_h)."""
    x = y = rowh = 0
    pos = []
    for w, h in sizes:
        if x and x + w > width:
            x, y, rowh = 0, y + rowh + pad, 0
        pos.append((x, y))
        x += w + pad
        rowh = max(rowh, h)
    return pos, y + rowh


def write_indexed_png(path, idx, pal):
    img = PImage.fromarray(np.ascontiguousarray(idx.astype(np.uint8)), 'P')
    flat = []
    for r, g, b in pal:
        flat += [r, g, b]
    img.putpalette(flat + [0] * (768 - len(flat)))
    img.save(path, optimize=False, transparency=0)


def read_sheet(path, palette_hex=None):
    """uint8 index array (h, w).  An indexed PNG is taken as is (index = colour register); an RGB(A) PNG is snapped to the nearest colour
    of `palette_hex` (alpha < 128 or the colour of index 0 = transparent = 0)."""
    img = PImage.open(path)
    if img.mode == 'P':
        return np.array(img, dtype=np.uint8)
    if img.mode not in ('RGB', 'RGBA'):
        img = img.convert('RGBA')
    if not palette_hex:
        raise KitError('%s: an RGB sheet needs palette.colors in kit.json to snap to' % path)
    rgba = np.array(img.convert('RGBA'), dtype=np.int32)
    pal = np.array([_unhex(c) for c in palette_hex], dtype=np.int32)
    d = ((rgba[:, :, None, :3] - pal[None, None, :, :]) ** 2).sum(axis=3) * 4
    d[:, :, 0] += 2                                    # ties go to the real colours, not to transparent
    d[:, :, [i for i in FORBIDDEN if i < len(pal)]] += 1   # nor to the first fighter's colours (8 and 13 can both be #FF0000)
    idx = d.argmin(axis=2).astype(np.uint8)
    idx[rgba[:, :, 3] < 128] = 0
    return idx


class SheetCache:
    def __init__(self, kitdir, kit):
        self.kitdir, self.kit, self.cache = kitdir, kit, {}

    def sheet(self, n):
        if n not in self.cache:
            f = self.kit['sheets'][n]['file']
            self.cache[n] = read_sheet(os.path.join(self.kitdir, f), self.kit['palette'].get('colors'))
        return self.cache[n]

    def frame(self, fr):
        """Index array of a kit frame or None (no src); raises KitError for a rect outside the sheet."""
        src = fr.get('src')
        if not src:
            return None
        sh = self.sheet(src['sheet'])
        x, y, w, h = src['rect']
        if x + w > sh.shape[1] or y + h > sh.shape[0]:
            raise KitError('frame %s: rect %s lies outside sheet %s (%dx%d)' % (fr['id'], src['rect'], self.kit['sheets'][src['sheet']]['file'],
                                                                              sh.shape[1], sh.shape[0]))
        return sh[y:y + h, x:x + w]


# ------------------------------------------------------------------------------------------------------------------ the clone

def _segments(img, prog):
    """Split the program's instructions into segments: runs of back-to-back instructions ending at the first `stop` instruction.
    -> [{'start': addr, 'insns': [(addr, Insn)]}]"""
    segs = []
    cur = None
    prev_end = None
    for addr in sorted(prog.insns):
        ins = prog.insns[addr]
        if cur is None or addr != prev_end:
            cur = {'start': addr, 'insns': []}
            segs.append(cur)
        cur['insns'].append((addr, ins))
        prev_end = (addr[0], addr[1] + ins.size)
        if ins.flow == 'stop':
            cur = None
    return segs


def _anim_name(img, seg, used):
    n = img.name(seg['start'])
    base = re.sub(r'[^a-z0-9_.]', '_', (n or 's_h%d_%04x' % seg['start']).lower())
    if not base[0].isalpha():
        base = 's_' + base
    name, k = base, 2
    while name in used:
        name, k = '%s_%d' % (base, k), k + 1
    used.add(name)
    return name


def _seg_asm(img, seg):
    h, o = seg['start']
    lines = ['.org h%d+$%X' % (h, o)]
    for addr, ins in seg['insns']:
        n = img.name(addr)
        if n:
            lines.append(n + ':')
        lines.append('\t' + ins.text)
    return '\n'.join(lines) + '\n'


_DRAW = re.compile(r'^draw (\d+) (\d+) dx=(-?\d+) dy=(-?\d+)(.*)$')


def _parse_draw(text):
    m = _DRAW.match(text)
    if not m:
        return None
    flags = m.group(5).split()
    return {'slot': int(m.group(1)), 'frame': int(m.group(2)), 'dx': int(m.group(3)), 'dy': int(m.group(4)), 'flags': flags}


def _steps_of(seg, frame_ids, anchors):
    """Informational structured view of a segment: one step per tick; draws of the creature's cel slots become frame references.
    The `asm` text stays authoritative (a clone converts back byte-identical)."""
    steps, draws, move, sound = [], [], None, None
    frameset, loop = 2, 0
    for _addr, ins in seg['insns']:
        t = ins.text
        d = _parse_draw(t)
        if d:
            fid = frame_ids.get((d['slot'], d['frame'])) if frameset == 2 else None
            if fid:
                ax, ay = anchors.get(fid, (0, 0))
                draw = {'frame': fid, 'slot': d['slot'], 'dx': d['dx'] + ax, 'dy': d['dy'] + ay}
                for fl, key in (('hurt', 'hurt'), ('attack', 'attack'), ('bg', 'bg'), ('text', 'text'), ('nobox', 'nobox'), ('nogore', 'nogore')):
                    if fl in d['flags']:
                        draw[key] = True
                draws.append(draw)
        elif t.startswith('frameset'):
            frameset = {'knights': 1, 'creature': 2, 'map': 3, 'effects': 4}.get(t.split()[1], 0)
        elif t.startswith('move '):
            kv = dict(p.split('=') for p in t.split()[1:])
            move = [int(kv['x']), int(kv['y']), int(kv['z'])] if all(k in kv for k in 'xyz') else None
        elif t.startswith('sound'):
            sound = int(t.split()[1].lstrip('$'), 16)
        elif t.startswith('loop ') and not loop:
            w = t.split()[1]
            loop = 0 if w == 'random' else int(w)
        elif t.startswith('end') or t.startswith('done'):
            step = {'ticks': 1, 'draws': draws}
            if move:
                step['move'] = move
            if sound is not None:
                step['sound'] = sound
            steps.append(step)
            draws, move, sound = [], None, None
    if draws or move or sound is not None or not steps:
        steps.append({'ticks': 1, 'draws': draws})
    return steps, loop


def _tab(cr, kind):
    t = cr.table(kind)
    if t is None:
        return []
    out = []
    for v in t[1]:
        out.append(v if isinstance(v, tuple) else None)
    return out


def clone_game(creature, kitdir, name=None, disks=None, force=False):
    """Export an original creature into a kit under `kitdir` (git-ignored place).  Returns the kit dict."""
    if creature not in GAME_CELS:
        raise KitError('unknown creature %r (offered: %s)' % (creature, ' '.join(OFFERED)))
    _refuse_tracked(kitdir)
    if os.path.exists(kit_path(kitdir)) and not force:
        raise KitError('%s exists (use --force to overwrite)' % kit_path(kitdir))
    import fightscript as F
    disks = disks or DISKS
    cat = catalog()
    cinfo = next(c for c in cat['creatures'] if c['name'] == creature)
    ai = cat['ais'][cinfo['ai']]
    img = F.Image()
    cr = F.Creature(img, F.Catalog(), creature)
    pal = scene_palette(cinfo['record']['type'], disks)
    os.makedirs(kitdir, exist_ok=True)

    hit_text = None
    hp = find_game_file('collide.hit', disks)
    if hp:
        hit_text = open(hp, 'rb').read()
        hit_sets = {n.lower(): (n, fr) for n, fr in P.parse_hit_sets(hit_text)}
    else:
        hit_sets = {}

    sheets, celsets, origin_cels = [], [], []
    frame_ids, anchors_by_id = {}, {}
    for si, (slot, fname, hname) in enumerate(GAME_CELS[creature]):
        path = find_game_file(fname, disks)
        if not path:
            raise KitError('original %s not found under %s (tools/adfx.py)' % (fname, disks))
        data = open(path, 'rb').read()
        cel = artconv.parse_cel(data)
        fr = cel['frames']
        pos, sh = pack_sheet([(max(f['w'], 1), max(f['h'], 1)) for f in fr])
        sheet = np.zeros((max(sh, 1), SHEET_W), dtype=np.uint8)
        frames, ofr, used = [], [], set()
        hitfr = hit_sets[hname.lower()][1] if hname else None
        if hname and hname.lower() not in hit_sets:
            raise KitError('no hit set %s in collide.hit' % hname)
        for i, f in enumerate(fr):
            w, h = f['w'], f['h']
            idx = artconv.planes_to_indices(cel['blob'], f['offset'], w, h, f['b9']) if w and h else np.zeros((h, w), dtype=np.uint8)
            x, y = pos[i]
            sheet[y:y + h, x:x + w] = idx
            used |= set(int(v) for v in np.unique(idx))
            fid = 's%d_%03d' % (slot, i)
            frame_ids[(slot, i)] = fid
            fe = {'id': fid, 'src': {'sheet': si, 'rect': [x, y, w, h]}}
            if hitfr is not None and i < len(hitfr) and hitfr[i]:
                fe['attack'] = {'type': hitfr[i]['type'], 'points': [list(p) for p in hitfr[i]['points']]}
            frames.append(fe)
            ofr.append({'w': w, 'h': h, 'plane_bits': f['b9'], 'b8': f['b8']})
        fname_sheet = 'sheet%d.png' % slot
        write_indexed_png(os.path.join(kitdir, fname_sheet), sheet, pal)
        sheets.append({'file': fname_sheet})
        cs = {'slot': slot, 'file': fname, 'hits': bool(hname), 'frames': frames}
        if hname:
            cs['hit_name'] = hname
            cs['hit_frames'] = len(hitfr)
        celsets.append(cs)
        origin_cels.append({'slot': slot, 'file': fname, 'sha1': _sha1(data), 'blob_sha1': _sha1(cel['blob']), 'packed': cel['packed'],
                            'colors_used': sorted(used), 'frames': ofr, 'hit_name': hname,
                            'hit_text_bytes': len(P.format_hit_set(hit_sets[hname.lower()][0], hitfr)) if hname else 0})

    # ---- scripts
    prog = cr.program(scope='own')
    segs = _segments(img, prog)
    used_names, seg_of_addr, names = set(), {}, []
    for s in segs:
        nm = _anim_name(img, s, used_names)
        names.append(nm)
        for a, _ in s['insns']:
            seg_of_addr[a] = nm

    def role_target(addr):
        if addr is None:
            return None
        if addr in seg_of_addr:
            return seg_of_addr[addr]
        return 'ext:%s' % img.label(addr)

    # anchors: the most common (-dx, -dy) of the creature-frameset draws of each frame
    votes = {}
    for s in segs:
        fs = 2
        for _a, ins in s['insns']:
            if ins.text.startswith('frameset'):
                fs = {'knights': 1, 'creature': 2, 'map': 3, 'effects': 4}.get(ins.text.split()[1], 0)
            d = _parse_draw(ins.text)
            if d and fs == 2 and (d['slot'], d['frame']) in frame_ids:
                votes.setdefault(frame_ids[(d['slot'], d['frame'])], {}).setdefault((-d['dx'], -d['dy']), 0)
                votes[frame_ids[(d['slot'], d['frame'])]][(-d['dx'], -d['dy'])] += 1
    for cs, oc in zip(celsets, origin_cels):
        for fe, of in zip(cs['frames'], oc['frames']):
            v = votes.get(fe['id'])
            anc = max(sorted(v), key=lambda k: v[k]) if v else (of['w'] // 2, of['h'])
            fe['anchor'] = [anc[0], anc[1]]
            of['anchor'] = [anc[0], anc[1]]
            anchors_by_id[fe['id']] = anc

    animations, asm_all = {}, []
    for s, nm in zip(segs, names):
        steps, loop = _steps_of(s, frame_ids, anchors_by_id)
        txt = _seg_asm(img, s)
        asm_all.append(txt)
        an = {'steps': steps, 'asm': txt}
        if loop:
            an['loop'] = loop
        animations[nm] = an

    # ---- roles
    roles, hurt_by_action = {}, [None] * 9
    for r in ai['roles']:
        if r['kind'] == 'field':
            off = {'idle': 22, 'after_hit': 26}[r['field'] if r['field'] != 'alt' else 'after_hit']
            lab = cr.field(off)
            roles[r['name']] = role_target(img.syms[lab]) if lab else None
        elif r['kind'] == 'label':
            roles[r['name']] = role_target(img.syms[r['label']])
    for kind, rname in (('hurt', 'hurt_by_action'), ('action', 'action')):
        tab = _tab(cr, kind)
        if tab:
            ent = [role_target(a) if a and cr.img.hunk(a[0]).kind != 'BSS' else None for a in tab[:9]]
            if kind == 'hurt':
                hurt_by_action = ent
            elif any(ent):
                roles['action'] = ent
    walk = _tab(cr, 'walk')
    for base, nm in ((0, 'walk.side'), (8, 'walk.up'), (16, 'walk.down')):
        ent = [role_target(a) if a and img.hunk(a[0]).kind != 'BSS' else None for a in walk[base:base + 8]]
        if any(ent):
            roles[nm] = ent
    if 'walk.air' in [r['name'] for r in ai['roles']] and 'walk.up' in roles:
        roles['walk.air'] = roles.pop('walk.up')
    # die: the target of the first ifdead of a hurt role
    hurt_roots = [v for k, v in roles.items() if k.startswith('hurt') and isinstance(v, str)] + [v for v in hurt_by_action if v]
    die = None
    for hr in hurt_roots:
        if hr in animations:
            m = re.search(r'ifdead (\S+)', animations[hr]['asm'])
            if m:
                die = role_target(img.resolve(m.group(1)))
                break
    roles['die'] = die
    dmg_tab = cr.table('damage')
    damage = [v if isinstance(v, int) else 0 for v in (dmg_tab[1] if dmg_tab else [])][:9] or [0] * 9
    damage += [0] * (9 - len(damage))
    rec = cinfo['record']
    stats = {'hp': rec['hp'], 'reach': rec['reach'], 'keep_away': rec['keep_away'], 'depth': rec['depth']}
    colors = [_hex24(c) for c in pal]
    kit = {
        'kit': KIT_VERSION, 'name': name or creature, 'title': cinfo['title'], 'cloned_from': {'kind': 'game', 'creature': creature},
        'ai': cinfo['ai'], 'planes': 5,
        'palette': {'scene': creature, 'colors': colors, 'own': _own_of(pal), 'allow': list(range(32))},
        'sheets': sheets, 'celsets': celsets, 'animations': animations, 'roles': roles, 'hurt_by_action': hurt_by_action,
        'damage': damage, 'stats': stats, 'sounds': {'bank': cinfo['bank_file']}, 'arena': {}, 'tier': 'auto',
        'origin': {'game': creature, 'celsets': origin_cels, 'scripts_sha1': _sha1(''.join(asm_all).encode()),
                   'stats': dict(stats), 'type_byte': rec['type'], 'max_tick_area': _max_tick_area(animations, celsets)},
    }
    errs = J.validate(kit, schema())
    if errs:
        raise KitError('clone produced an invalid kit: ' + '; '.join(errs[:5]))
    save_kit(kitdir, kit)
    rep = check_kit(kitdir, disks=disks)
    kit['origin']['inherited_gaps'] = [e for e in rep.errors if 'no attack draw with attack points' in e]
    save_kit(kitdir, kit)
    return kit


def clone_kit(srcdir, dstdir, name=None, force=False):
    """Copy a kit under a new name (cloned_from kind kit); origin (T0 provenance) is kept."""
    if os.path.exists(kit_path(dstdir)) and not force:
        raise KitError('%s exists (use --force to overwrite)' % kit_path(dstdir))
    kit = load_kit(srcdir)
    if kit.get('origin'):
        _refuse_tracked(dstdir)
    os.makedirs(dstdir, exist_ok=True)
    for s in kit['sheets']:
        shutil.copyfile(os.path.join(srcdir, s['file']), os.path.join(dstdir, s['file']))
    src_name = kit['name']
    kit['cloned_from'] = {'kind': 'kit', 'name': src_name}
    if name:
        kit['name'] = name
    save_kit(dstdir, kit)
    return kit


# ------------------------------------------------------------------------------------------------------------------ script ops

def anim_ops(kit, anim):
    """Normalised view of an animation: [{'op': 'draw'|'end'|'done'|'ifdead'|'engine'|'kill'|'sound'|'jump'|'call'|..., ...}].  Taken from
    the verbatim `asm` when there is one, else built from the steps (a hand-made kit)."""
    ops = []
    asm = anim.get('asm')
    if asm:
        fs = 2
        for line in asm.split('\n'):
            line = line.split(';')[0].strip()
            if not line or line.startswith('.org') or line.endswith(':'):
                continue
            w = line.split()
            d = _parse_draw(line)
            if d:
                ops.append({'op': 'draw', 'slot': d['slot'], 'frame': d['frame'], 'hurt': 'hurt' in d['flags'], 'attack': 'attack' in d['flags'],
                            'creature': fs == 2})
            elif w[0] == 'frameset':
                fs = {'knights': 1, 'creature': 2, 'map': 3, 'effects': 4}.get(w[1], 0)
            elif w[0] in ('ifdead', 'engine', 'jump', 'call'):
                ops.append({'op': w[0], 'target': w[1]})
            elif w[0] == 'sound':
                ops.append({'op': 'sound', 'id': int(w[1].lstrip('$'), 16 if w[1].startswith('$') else 10)})
            elif w[0] in ('end', 'done', 'kill'):
                ops.append({'op': w[0]})
        return ops
    ids = {}
    for cs in kit['celsets']:
        for i, fr in enumerate(cs['frames']):
            ids[fr['id']] = (cs['slot'], i)
    for st in anim['steps']:
        for d in st['draws']:
            s, i = ids.get(d['frame'], (d.get('slot', 0), 0))
            ops.append({'op': 'draw', 'slot': d.get('slot', s), 'frame': i, 'hurt': bool(d.get('hurt')), 'attack': bool(d.get('attack')), 'creature': True})
        if st.get('sound') is not None:
            ops.append({'op': 'sound', 'id': st['sound']})
        for _ in range(max(1, st.get('ticks', 1))):
            ops.append({'op': 'end'})
    if anim.get('on_dead'):
        ops.append({'op': 'ifdead', 'target': anim['on_dead']})
    if anim.get('asm') is None and ops and ops[-1]['op'] != 'end':
        ops.append({'op': 'end'})
    ops.append({'op': 'done'})
    return ops


def _max_tick_area(animations, celsets, kit=None):
    """Largest sum of the drawn frames' areas (w * h) in one tick: the frame-rate proxy (draws are 85-90% of a fight frame, ROADMAP 8.4a)."""
    dims = {}
    for cs in celsets:
        for i, fr in enumerate(cs['frames']):
            dims[(cs['slot'], i)] = fr['src']['rect'][2] * fr['src']['rect'][3] if fr.get('src') else 0
    best = 0
    for an in animations.values():
        area = 0
        for op in anim_ops({'celsets': celsets, **(kit or {})}, an):
            if op['op'] == 'draw' and op.get('creature'):
                area += dims.get((op['slot'], op['frame']), 0)
            elif op['op'] in ('end', 'done'):
                best = max(best, area)
                area = 0
    return best


# ------------------------------------------------------------------------------------------------------------------- the check

class Report:
    def __init__(self):
        self.errors, self.warnings, self.info, self.checklist = [], [], [], []
        self.tier = None

    @property
    def ok(self):
        return not self.errors

    def err(self, m):
        self.errors.append(m)

    def warn(self, m):
        self.warnings.append(m)

    def text(self):
        out = ['checklist:']
        for c in self.checklist:
            out.append('  [%s] %-18s %s' % ('ok' if c['ok'] else 'MISSING', c['role'], c['detail']))
        out += ['ERROR: ' + e for e in self.errors] + ['warning: ' + w for w in self.warnings] + ['info: ' + i for i in self.info]
        out.append('tier: %s; %s' % (self.tier, 'OK' if self.ok else '%d error(s): export blocked' % len(self.errors)))
        return '\n'.join(out)


def _resolve_anim(kit, value):
    """The animation a role value names: (name or None, is_external)."""
    if value is None:
        return None, False
    if isinstance(value, str) and value.startswith('ext:'):
        return value, True
    if value in kit['animations']:
        return value, False
    return None, False


def _label_map(kit):
    """lower-case label -> animation name, from the `.org` / `label:` lines of the verbatim asm (and the animation names)."""
    m = {n: n for n in kit['animations']}
    for n, an in kit['animations'].items():
        for line in (an.get('asm') or '').split('\n'):
            line = line.split(';')[0].strip()
            if line.endswith(':'):
                m[line[:-1].lower()] = n
    return m


def check_kit(kitdir, report=None, disks=None):
    """Check a kit folder (or an already loaded kit dict with kit['_dir']).  Returns a Report; errors block the export."""
    rep = report or Report()
    kit = kitdir if isinstance(kitdir, dict) else load_kit(kitdir)
    kdir = kit.pop('_dir', None) if isinstance(kitdir, dict) else kitdir
    cat = catalog()
    errs = J.validate(kit, schema())
    for e in errs:
        rep.err('schema: ' + e)
    if errs:
        rep.tier = '?'
        return rep
    ai = cat['ais'].get(kit['ai'])
    if not ai:
        rep.err('unknown ai %r (known: %s)' % (kit['ai'], ' '.join(cat['ais'])))
        rep.tier = '?'
        return rep
    if ai.get('advanced'):
        rep.warn('ai %s is marked advanced (more hard-wired scripts than a kit can express yet)' % kit['ai'])
    limits = cat['limits']
    origin = kit.get('origin') or {}
    planes = kit['planes']

    # ---- cel sets: sizes, memory, palette classes
    sheets = SheetCache(kdir, kit)
    slots = [cs['slot'] for cs in kit['celsets']]
    if len(set(slots)) != len(slots):
        rep.err('two cel sets use the same slot')
    arena = 0
    ptr_idx = {}
    agg = {}
    for cs in kit['celsets']:
        if len(cs['frames']) > limits['frames_per_cel_file']:
            rep.err('slot %d: %d frames, at most %d' % (cs['slot'], len(cs['frames']), limits['frames_per_cel_file']))
        ids = [f['id'] for f in cs['frames']]
        if len(set(ids)) != len(ids):
            rep.err('slot %d: duplicate frame ids' % cs['slot'])
        ofs = {oc['slot']: oc for oc in origin.get('celsets', [])}.get(cs['slot'])
        frame_dims = []
        for i, fr in enumerate(cs['frames']):
            try:
                idx = sheets.frame(fr)
            except (KitError, OSError) as e:
                rep.err(str(e))
                continue
            if idx is None:
                rep.err('frame %s has no src' % fr['id'])
                continue
            h, w = idx.shape
            if w > limits['frame_size']['max_width_px'] or (2 * P.words_of(w) + 2) * h > limits['frame_size']['temp_plane_bytes']:
                rep.err('frame %s: %dx%d exceeds the blit temp plane ((2 * words + 2) * h <= 4800, w <= 336)' % (fr['id'], w, h))
            if h > limits['frame_size']['bltsize']['max_rows']:
                rep.err('frame %s: height %d above BLTSIZE limit' % (fr['id'], h))
            mx = int(idx.max()) if idx.size else 0
            if mx >= (1 << planes):
                rep.err('frame %s: colour index %d does not fit %d planes' % (fr['id'], mx, planes))
            inherited = set(ofs['colors_used']) if ofs else set()
            bad = sorted(set(int(v) for v in np.unique(idx)) & set(FORBIDDEN))
            for v in bad:
                if v in inherited:
                    if fr['id'] not in agg.setdefault('inh', []):
                        agg['inh'].append(fr['id'])
                else:
                    rep.err('frame %s uses index %d (the first fighter colour)' % (fr['id'], v))
            rv = sorted(set(int(v) for v in np.unique(idx)) & set(REGION_VARIABLE) - inherited)
            if rv:
                agg.setdefault('rv', []).append('%s%s' % (fr['id'], rv))
            if planes == 5 and mx >= 32:
                pass
            ptr_idx[fr['id']] = (w, h)
            bits = (ofs['frames'][i]['plane_bits'] if ofs and i < len(ofs['frames']) else 0) | (int(np.bitwise_or.reduce(idx.ravel())) if idx.size else 0)
            frame_dims.append((w, h, bits))
            if 'attack' in fr:
                pts = fr['attack']['points']
                if len(pts) > limits['attack_points_per_frame']:
                    rep.err('frame %s: %d attack points, at most 98' % (fr['id'], len(pts)))
                for x, y in pts:
                    if not (0 <= x <= 255 and 0 <= y <= 255):
                        rep.err('frame %s: attack point (%d,%d) outside 0..255' % (fr['id'], x, y))
                    elif x > w or y > h:
                        agg.setdefault('out', []).append('%s(%d,%d in %dx%d)' % (fr['id'], x, y, w, h))
        arena += P.cel_arena_bytes(frame_dims)
    for key, msg in (('inh', 'frames use the first fighter colours 6..8 inherited from the source art: %s'),
                     ('rv', 'frames use region/backdrop-variable indices: %s'),
                     ('out', 'attack points beyond their frame: %s')):
        if key in agg:
            rep.warn('%d ' % len(agg[key]) + msg % ', '.join(agg[key][:4] + (['...'] if len(agg[key]) > 4 else [])))
    if len(kit['celsets']) > limits['cel_files_per_creature']:
        rep.err('%d cel sets, at most %d' % (len(kit['celsets']), limits['cel_files_per_creature']))
    if arena > limits['creature_arena_bytes']:
        rep.warn('cel sets need ~%d bytes of the creature arena (%d); a set living in the second area (Mudmen2, Balok/dragon/troll '
                 'beside kn5) is budgeted there' % (arena, limits['creature_arena_bytes']))
        if arena > limits['creature_arena_bytes'] + limits['second_area_bytes']:
            rep.err('cel sets need ~%d bytes, arena + second area hold %d' % (arena, limits['creature_arena_bytes'] + limits['second_area_bytes']))
    hit_sets = sum(1 for cs in kit['celsets'] if cs.get('hits'))
    if hit_sets + 1 > limits['hit_pairs']:
        rep.err('%d hit sets (+ the knights\' kn4) exceed the %d pairs of LAB_0A51' % (hit_sets, limits['hit_pairs']))
    if origin.get('celsets'):
        base = limits['hit_original_bytes'] - sum(oc.get('hit_text_bytes', 0) for oc in origin['celsets'])
        now = sum(len(hit_set_text(cs)) for cs in kit['celsets'] if cs.get('hits'))
        if base + now > limits['hit_text_bytes']:
            rep.err('collide.hit would be %d bytes, the game reads at most %d' % (base + now, limits['hit_text_bytes']))

    # ---- scripts: pools, hurt flags, sounds
    lmap = _label_map(kit)
    ops_of = {n: anim_ops(kit, a) for n, a in kit['animations'].items()}
    frame_has_points = {}
    for cs in kit['celsets']:
        hf = cs.get('hit_frames')
        for i, fr in enumerate(cs['frames']):
            frame_has_points[(cs['slot'], i)] = bool(cs.get('hits') and fr.get('attack', {}).get('points') and (hf is None or i < hf or True))
    for n, ops in ops_of.items():
        hurt = att = 0
        for op in ops:
            if op['op'] == 'draw':
                hurt += op['hurt']
                att += op['attack']
            elif op['op'] in ('end', 'done'):
                if hurt > limits['draws_per_tick_lists'] or att > limits['draws_per_tick_lists']:
                    rep.err('animation %s: %d hurt / %d attack draws in one tick, the lists hold 8' % (n, hurt, att))
                hurt = att = 0
        for op in ops:
            if op['op'] == 'draw' and op['creature']:
                cs = next((c for c in kit['celsets'] if c['slot'] == op['slot']), None)
                if cs is None or op['frame'] >= len(cs['frames']):
                    rep.err('animation %s draws slot %d frame %d which the kit does not have' % (n, op['slot'], op['frame']))
    try:
        import sounds as S
        bank = kit['sounds']['bank']
        seen = set()
        for n, ops in ops_of.items():
            for op in ops:
                if op['op'] == 'sound' and op['id'] not in seen:
                    seen.add(op['id'])
                    if not S.usable_with(op['id'], bank):
                        rep.warn('sound $%02X of %s is not provided by the bank %s' % (op['id'], n, bank))
    except Exception as e:                                   # no original mog hunk in reference/: sounds cannot be judged
        rep.info.append('sound ids not checked (%s)' % e)
    if kit['sounds']['bank'] not in cat['sound_banks']['creature_files']:
        rep.err('unknown sound bank %r (%s)' % (kit['sounds']['bank'], ' '.join(cat['sound_banks']['creature_files'])))

    # ---- R1 checklist
    def anim_for(v):
        return _resolve_anim(kit, v)

    def has_attack(name):
        for op in ops_of.get(name, []):
            if op['op'] == 'draw' and op['attack'] and op['creature'] and frame_has_points.get((op['slot'], op['frame'])):
                return True
        return False

    def has_hurt(name):
        return any(op['op'] == 'draw' and op['hurt'] for op in ops_of.get(name, []))

    def gap(msg):
        # a gap the original has too (clone time record): the clone must keep checking clean
        inh = msg in origin.get('inherited_gaps', [])
        (rep.warn if inh else rep.err)(msg + (' [inherited from the source]' if inh else ''))
        return inh

    hurt_roots = []
    for r in ai['roles']:
        nm = r['name']
        if r['kind'] in ('field', 'label'):
            v = kit['roles'].get(nm)
            a, ext = anim_for(v)
            if r.get('required') and not a:
                rep.checklist.append({'role': nm, 'ok': False, 'detail': 'no animation (%s)' % r['meaning']})
                rep.err('role %s is missing: %s' % (nm, r['meaning']))
                continue
            if not a:
                rep.checklist.append({'role': nm, 'ok': True, 'detail': 'not set (optional)'})
                continue
            detail = 'shared engine script %s' % a if ext else '%s (%d frames drawn)' % (a, sum(1 for o in ops_of[a] if o['op'] == 'draw'))
            ok = True
            if not ext:
                if r.get('attack') == 'required' and not has_attack(a):
                    detail = '%s: needs an attack-flag draw whose frame has attack points' % a
                    ok = gap('role %s (%s): no attack draw with attack points (a cel without a hit set never hits)' % (nm, a))
                if nm.startswith('hurt') or nm == 'hurt':
                    hurt_roots.append(a)
                if nm in ('idle',) or r.get('attack') == 'required':
                    if not has_hurt(a):
                        rep.warn('role %s (%s) has no hurt-flag draw: it cannot be hit while it plays' % (nm, a))
            rep.checklist.append({'role': nm, 'ok': ok, 'detail': detail})
        else:
            req = r.get('required_indices') or []
            arr = kit['hurt_by_action'] if nm == 'hurt_by_action' else kit['roles'].get(nm)
            base = 0 if nm in ('hurt_by_action', 'action') else 8 * (min(req) // 8 if req else 0)
            if not req:
                continue
            miss = []
            for i in req:
                v = arr[i - base] if arr and 0 <= i - base < len(arr) else None
                a, ext = anim_for(v)
                if not a:
                    miss.append(i)
                elif not ext:
                    if r.get('attack') == 'required' and not has_attack(a):
                        gap('role %s[%d] (%s): no attack draw with attack points' % (nm, i, a))
                        miss.append(i)
                    if nm == 'hurt_by_action':
                        hurt_roots.append(a)
            if miss:
                rep.err('role %s is missing entries %s' % (nm, miss))
            rep.checklist.append({'role': nm, 'ok': not miss, 'detail': ('entries %s' % req) if not miss else 'missing entries %s' % miss})

    # ---- die: every hurt role jumps (ifdead) to a die sequence that counts the death once and kills the job
    die, _ = anim_for(kit['roles'].get('die'))
    if not die:
        rep.checklist.append({'role': 'die', 'ok': False, 'detail': 'no die sequence (ifdead target of the hurt roles)'})
        rep.err('role die is missing: the creature only dies through its scripts (hurt role: ifdead -> die sequence)')
    else:
        engine = [o['target'] for o in ops_of[die] if o['op'] == 'engine']
        kills = sum(1 for o in ops_of[die] if o['op'] == 'kill')
        probs = []
        if [t.lower() for t in engine].count('lab_0005') != 1 and 'creature_died' not in [t.lower() for t in engine]:
            probs.append('must call engine LAB_0005 exactly once')
        if kills != 1:
            probs.append('must end with kill')
        for p in probs:
            rep.err('die sequence %s %s' % (die, p))
        rep.checklist.append({'role': 'die', 'ok': not probs, 'detail': die if not probs else '%s: %s' % (die, '; '.join(probs))})
    for h in sorted(set(hurt_roots)):
        tgt = [lmap.get(o['target'].lower()) for o in ops_of.get(h, []) if o['op'] == 'ifdead']
        if not tgt:
            rep.warn('hurt script %s has no ifdead: a creature hit by it cannot die' % h)

    # ---- tunables and stats
    for t in ai['tunables']:
        if not t.get('used') and kit['stats'].get(t['name']) is not None and origin.get('stats') and kit['stats'][t['name']] != origin['stats'].get(t['name']):
            rep.info.append('stat %s is not read by the %s AI' % (t['name'], kit['ai']))
    # ---- frame rate proxy
    mta = _max_tick_area(kit['animations'], kit['celsets'], kit)
    if origin.get('max_tick_area') and mta > 1.2 * origin['max_tick_area']:
        rep.warn('largest drawn area in one tick %d px is above 120%% of the source (%d): a slower fight frame' % (mta, origin['max_tick_area']))
    rep.tier = detect_tier(kit)
    return rep


def hit_set_text(cs):
    """The collide.hit text of a cel set's attack points (length = the original set length or the last frame with points)."""
    frames = []
    for fr in cs['frames']:
        a = fr.get('attack')
        frames.append({'type': a.get('type', 0), 'points': [tuple(p) for p in a['points']]} if a and a['points'] else None)
    n = max(cs.get('hit_frames') or 0, max([i + 1 for i, f in enumerate(frames) if f] or [0]))
    return P.format_hit_set(cs.get('hit_name') or ('%s' % cs['file']), frames[:n])


def detect_tier(kit):
    """t0: a game clone whose frame counts / sizes and scripts equal the source (a pure reskin); t2: scripts changed; t1: new frame
    sizes or count with the source's scripts."""
    o = kit.get('origin')
    if not o:
        return 't1' if kit.get('cloned_from') else 't2'
    same_frames = len(kit['celsets']) == len(o['celsets'])
    if same_frames:
        for cs, oc in zip(kit['celsets'], o['celsets']):
            if len(cs['frames']) != len(oc['frames']) or any(fr.get('src') and fr['src']['rect'][2:] != [of['w'], of['h']]
                                                             for fr, of in zip(cs['frames'], oc['frames'])):
                same_frames = False
    scripts = _sha1(''.join(a.get('asm') or '' for a in kit['animations'].values()).encode()) == o['scripts_sha1']
    if same_frames and scripts:
        return 't0'
    return 't1' if scripts else 't2'


# -------------------------------------------------------------------------------------------------------------------- build T0

def encode_cel(frames_idx, plane_bits, packed=None):
    """frames_idx = [uint8 (h, w) arrays], plane_bits = per-frame minimum plane masks -> (file bytes, decoded blob, [(off, w, h, b8, b9)])."""
    blob, table, off = bytearray(), [], 0
    for idx, pb in zip(frames_idx, plane_bits):
        h, w = idx.shape
        need = artconv.needed_planes(idx) if idx.size else 0
        bits = pb | need
        pix = artconv.indices_to_planes(idx, bits) if bits and w and h else b''
        table.append((off, w, h, 1, bits))
        blob += pix
        off += len(pix)
    body = packed if packed is not None else artconv.lzss_encode(bytes(blob))
    out = struct.pack('>HII', len(table), len(body), 8 * len(blob))
    for e in table:
        out += struct.pack('>IHHBB', *e)
    return out + body, bytes(blob), table


def _fit(idx, anchor, of):
    """Put new art on the original frame's canvas (w x h) so the kit anchor lands on the original anchor; what falls outside is cropped."""
    ow, oh = of['w'], of['h']
    if idx.shape == (oh, ow):
        return idx, False
    out = np.zeros((oh, ow), dtype=np.uint8)
    dx, dy = of['anchor'][0] - anchor[0], of['anchor'][1] - anchor[1]
    h, w = idx.shape
    x0, y0 = max(dx, 0), max(dy, 0)
    x1, y1 = min(dx + w, ow), min(dy + h, oh)
    if x1 > x0 and y1 > y0:
        out[y0:y1, x0:x1] = idx[y0 - dy:y1 - dy, x0 - dx:x1 - dx]
    return out, True


def build_t0(kitdir, outdir, reencode=False, disks=None):
    """T0 reskin: write <outdir>/art/<ORIGINAL NAME> for each cel set and <outdir>/art/collide.hit.  Returns a result dict
    {'files': [{'name', 'bytes', 'mode': 'passthrough'|'encoded', 'identical': bool|None}], 'warnings': [...], 'report': Report}.
    Raises KitError when the check fails or the kit is not a T0 kit."""
    disks = disks or DISKS
    kit = load_kit(kitdir)
    rep = check_kit(kitdir, disks=disks)
    if not rep.ok:
        raise KitError('check failed, export blocked:\n' + rep.text())
    origin = kit.get('origin')
    if not origin or kit.get('cloned_from') is None:
        raise KitError('T0 needs a kit cloned from a game creature (origin data missing): use a later tier')
    if len(kit['celsets']) != len(origin['celsets']):
        raise KitError('T0 keeps the cel files of the source: %d cel sets, the source has %d' % (len(kit['celsets']), len(origin['celsets'])))
    res = {'files': [], 'warnings': list(rep.warnings), 'report': rep}
    art = os.path.join(outdir, 'art')
    os.makedirs(art, exist_ok=True)
    sheets = SheetCache(kitdir, kit)
    for cs, oc in zip(kit['celsets'], origin['celsets']):
        if len(cs['frames']) != len(oc['frames']):
            raise KitError('T0: slot %d has %d frames, the source has %d (frame count is part of the scripts)' % (cs['slot'], len(cs['frames']), len(oc['frames'])))
        idxs, pbits = [], []
        for fr, of in zip(cs['frames'], oc['frames']):
            idx = sheets.frame(fr)
            idx, moved = _fit(idx, fr.get('anchor') or [0, 0], of)
            if moved:
                res['warnings'].append('frame %s: %s fitted to the source canvas %dx%d by its anchor' % (fr['id'], 'art', of['w'], of['h']))
            idxs.append(idx)
            pbits.append(of['plane_bits'])
        data, blob, table = encode_cel(idxs, pbits)
        mode, identical = 'encoded', None
        orig_path = find_game_file(oc['file'], disks)
        orig = open(orig_path, 'rb').read() if orig_path else None
        if orig is not None and _sha1(orig) == oc['sha1']:
            oc_cel = artconv.parse_cel(orig)
            same_hdr = [(f['offset'], f['w'], f['h'], f['b8'], f['b9']) for f in oc_cel['frames']] == table
            if not reencode and same_hdr and oc_cel['blob'] == blob:
                data, mode = orig, 'passthrough'                # decoded content unchanged: the original packed stream, byte for byte
            identical = data == orig
        if len(data) - 10 - 10 * len(table) > 41244:
            raise KitError('%s: packed body %d bytes exceeds the game\'s fixed cel read buffer (41244)' % (oc['file'], len(data)))
        with open(os.path.join(art, oc['file']), 'wb') as f:
            f.write(data)
        res['files'].append({'name': oc['file'], 'bytes': len(data), 'mode': mode, 'identical': identical})
    # collide.hit: start from a staged one (several kits into one folder) else the original, splice this creature's sets
    staged = os.path.join(art, 'collide.hit')
    base_path = staged if os.path.isfile(staged) else find_game_file('collide.hit', disks)
    hits = [cs for cs in kit['celsets'] if cs.get('hits')]
    if hits:
        if not base_path:
            raise KitError('collide.hit of the original not found under %s' % disks)
        text = open(base_path, 'rb').read()
        res_text = splice_hit_sets(text, {cs['hit_name'].lower(): hit_set_text(cs) for cs in hits})
        with open(staged, 'wb') as f:
            f.write(res_text)
        orig_hit = find_game_file('collide.hit', disks)
        res['files'].append({'name': 'collide.hit', 'bytes': len(res_text), 'mode': 'spliced',
                             'identical': (res_text == open(orig_hit, 'rb').read()) if orig_hit else None})
    return res


def splice_hit_sets(text, replacements):
    """Replace the named sets (lower-case names) of a collide.hit text; everything else, the trailing terminator included, is kept."""
    sets = P.parse_hit_sets(text)
    prefix = ''.join(P.format_hit_set(n, f) for n, f in sets).encode('latin-1')
    if not text.startswith(prefix):
        raise KitError('collide.hit does not round-trip through the hit parser')
    tail = text[len(prefix):]
    seen = set()
    out = b''
    for n, f in sets:
        key = n.lower()
        if key in replacements:
            out += replacements[key].encode('latin-1')
            seen.add(key)
        else:
            out += P.format_hit_set(n, f).encode('latin-1')
    miss = set(replacements) - seen
    if miss:
        raise KitError('collide.hit has no set %s' % ', '.join(sorted(miss)))
    return out + tail


def install_art(outdir, hddir):
    """Copy <outdir>/art/* into <hddir>/art/ (the game serves PROGDIR:art/<name> before the disk file)."""
    src = os.path.join(outdir, 'art')
    if not os.path.isdir(src):
        raise KitError('%s has no art/ folder (run build first)' % outdir)
    dst = os.path.join(hddir, 'art')
    os.makedirs(dst, exist_ok=True)
    names = []
    for n in sorted(os.listdir(src)):
        shutil.copyfile(os.path.join(src, n), os.path.join(dst, n))
        names.append(n)
    return names


# ------------------------------------------------------------------------------------------------------------------------ CLI

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('clone')
    c.add_argument('source', help='an original creature (%s) or a kit folder' % ' '.join(OFFERED))
    c.add_argument('kitdir')
    c.add_argument('--name')
    c.add_argument('--force', action='store_true')
    k = sub.add_parser('check')
    k.add_argument('kitdir')
    b = sub.add_parser('build')
    b.add_argument('kitdir')
    b.add_argument('--tier', default='T0')
    b.add_argument('--out')
    b.add_argument('--reencode', action='store_true')
    b.add_argument('--install', metavar='HDDIR')
    i = sub.add_parser('install')
    i.add_argument('outdir')
    i.add_argument('hddir')
    sub.add_parser('catalog')
    sub.add_parser('creatures')
    sv = sub.add_parser('serve', help='local helper server + browser editor (tools/monsterkit/editor/)')
    sv.add_argument('--port', type=int, default=8765)
    sv.add_argument('--kits', help='kits folder (default build/kits)')
    sv.add_argument('--disks', help='original disk extracts (default build/disks)')
    sv.add_argument('--hd', help='default HD folder for Install (default build/autoplay-hd)')
    sv.add_argument('--open', action='store_true', help='open the page in the default browser')
    a = ap.parse_args(argv)
    try:
        if a.cmd == 'clone':
            if os.path.isfile(kit_path(a.source)):
                kit = clone_kit(a.source, a.kitdir, a.name, a.force)
            else:
                kit = clone_game(a.source, a.kitdir, a.name, force=a.force)
            print('cloned %s -> %s (%d cel sets, %d animations, ai %s)' % (a.source, a.kitdir, len(kit['celsets']), len(kit['animations']), kit['ai']))
            return 0
        if a.cmd == 'check':
            rep = check_kit(a.kitdir)
            print(rep.text())
            return 0 if rep.ok else 1
        if a.cmd == 'build':
            if a.tier.lower() != 't0':
                print('only --tier T0 is implemented (T1 needs ROADMAP 9.4 / 9.5e, T2 needs 8.8)', file=sys.stderr)
                return 2
            out = a.out or os.path.join(a.kitdir, 'out')
            res = build_t0(a.kitdir, out, reencode=a.reencode)
            for w in res['warnings']:
                print('warning: ' + w)
            for f in res['files']:
                print('%-18s %7d bytes  %-11s %s' % (f['name'], f['bytes'], f['mode'], {True: 'identical to the original', False: 'differs from the original',
                                                                                         None: ''}[f['identical']]))
            print('staged in %s' % os.path.join(out, 'art'))
            if a.install:
                print('installed: %s into %s' % (' '.join(install_art(out, a.install)), os.path.join(a.install, 'art')))
            return 0
        if a.cmd == 'install':
            print('installed: ' + ' '.join(install_art(a.outdir, a.hddir)))
            return 0
        if a.cmd == 'catalog':
            cat = catalog()
            for n, ai in cat['ais'].items():
                print('%-10s type $%02X  %s%s' % (n, ai['type'], ai['title'], '  [advanced]' if ai.get('advanced') else ''))
                print('           roles: ' + ', '.join(r['name'] for r in ai['roles']))
            return 0
        if a.cmd == 'serve':
            import serve as _serve
            return _serve.serve(sys.modules[__name__], a.port, a.kits, a.disks, a.hd, a.open)
        if a.cmd == 'creatures':
            for n, spec in GAME_CELS.items():
                print('%-12s %s' % (n, ', '.join('%d:%s' % (s, f) for s, f, _ in spec)))
            return 0
    except (KitError, OSError) as e:
        print('error: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
