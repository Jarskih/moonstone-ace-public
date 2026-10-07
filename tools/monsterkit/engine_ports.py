"""engine_ports.py -- Python ports of the engine routines the monster kit needs (ROADMAP 9.8a1, docs/MONSTER_KIT.md 1.3, 1.1, 4).

Each port is a literal transcription of the C++ (which transcribes the asm); tests/test_monsterkit.py runs the C++ on the host
(clang++) next to these on random vectors, plus one deliberate mutation that must fail.  The ports are what the converter
(monsterkit.py check/build) and the editor's JS twin (the `?selftest` vectors) are measured against.

    hit_parse(text, size, name)       src/engine/loaders.cpp:172-240   collide.hit text -> binary hit records (None = not found)
    parse_hit_sets(text)              structured view of a whole collide.hit (names -> frames of points), round-trips with
    format_hit_set(name, frames)      the text writer; the gate box (maxDx, maxDy) is what hit_parse computes
    hit_record_at / hit_set_find      src/game/creatures.cpp:99-120    (record walk, {cel table, hit set} pair table)
    contact_test(mem, ...)            src/game/creatures.cpp:122-225   incl. the mirrored-x quirk
    contact_overlap / depth_close     creatures.cpp:76-98
    frame_bytes / cel_size_estimate   tools/artconv.py:255 / src/engine/loaders.cpp:111  (the cel size formula)

Addresses of `Arena` are plain offsets: a cel table's plane base (+2) is an offset into the same Arena, like the host driver of
the C++ test.
"""
import struct


# --------------------------------------------------------------------------------------------------------------- hitParse

def _digits(byte, pos, count):
    """The asm's 2/3 digit evaluation (SUBI.W #$30 on the word, MULU #10 on the low word, ADD.B on the low byte only), loaders.cpp
    digits().  Returns (low word, new position)."""
    d0 = byte(pos)
    pos += 1
    d0 = (d0 & 0xFFFF0000) | ((d0 - 0x30) & 0xFFFF)
    for _ in range(1, count):
        d0 = ((d0 & 0xFFFF) * 10) & 0xFFFFFFFF
        d0 = (d0 & ~0xFF & 0xFFFFFFFF) | ((d0 + byte(pos)) & 0xFF)
        pos += 1
        d0 = (d0 & 0xFFFF0000) | ((d0 - 0x30) & 0xFFFF)
    return d0 & 0xFFFF, pos


def _s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def hit_parse(text, size, name):
    """loaders.cpp hitParse.  `text`: the file bytes (anything beyond reads as 0), `size`: the signed size bound (the original's
    D7), `name`: the cel name (str or bytes).  Returns the bytes the C++ writes, or None when it returns -1."""
    if isinstance(name, str):
        name = name.encode('latin-1')
    name = bytes(name)
    n = len(text)

    def byte(i):
        return text[i] if 0 <= i < n else 0

    a2 = 0
    limit = (size if size > 0 else 0) + 64
    d7 = size
    while True:                                                    # LAB_03CF
        d7 -= 1
        if d7 < 0:
            return None
        a3 = 0
        restart = False
        while True:                                                # LAB_03D0
            c = name[a3] if a3 < len(name) else 0
            a3 += 1
            if c == 0:
                break
            ch = byte(a2)
            a2 += 1
            if c != ch:
                restart = True
                break
            d7 -= 1
            if d7 < 0:
                return None
        if restart:
            continue
        ch = byte(a2)
        a2 += 1
        if ch == 0x0A:                                             # the name must end the line
            break
    out = bytearray()
    while True:                                                    # LAB_03D2
        if a2 > limit:
            return None
        d0, a2 = _digits(byte, a2, 2)
        if d0 == 0x63:
            return bytes(out)
        out.append(d0 & 0xFF)
        a2 += 1
        if d0 == 0:
            continue
        d7w = d0
        typ, a2 = _digits(byte, a2, 2)
        a2 += 1
        out.append(typ & 0xFF)
        pmax = len(out)
        out += b'\0\0'
        d7w = (d7w - 1) & 0xFFFF
        d1 = 0
        d2 = 0
        while True:                                                # LAB_03D3, DBF D7
            x, a2 = _digits(byte, a2, 3)
            x = _s16(x)
            out.append(x & 0xFF)
            if x > d1:
                d1 = x
            y, a2 = _digits(byte, a2, 3)
            y = _s16(y)
            out.append(y & 0xFF)
            if y > d2:
                d2 = y
            done = d7w == 0
            d7w = (d7w - 1) & 0xFFFF
            if done:
                break
        a2 += 1
        out[pmax] = d1 & 0xFF
        out[pmax + 1] = d2 & 0xFF


def parse_hit_sets(text):
    """Structured view of a collide.hit text: [(name, [frame, ...])] in file order; frame = None (no points) or
    {'type': int, 'points': [(x, y), ...]}.  A set ends at its `99` line.  Names are the lines that are not a frame count
    (they hold something other than digits).  Tolerant: stops at the first line it cannot read."""
    lines = text.decode('latin-1').split('\n')
    lines = [l.rstrip('\r') for l in lines]
    sets = []
    i = 0
    while i < len(lines):
        name = lines[i]
        i += 1
        if not name or name.isdigit():
            continue
        frames = []
        while i < len(lines):
            ln = lines[i]
            if ln == '99':
                i += 1
                break
            if len(ln) != 2 or not ln.isdigit():
                break
            i += 1
            cnt = int(ln)
            if cnt == 0:
                frames.append(None)
                continue
            typ = int(lines[i])
            pts = lines[i + 1]
            i += 2
            if len(pts) != 6 * cnt:
                raise ValueError('%s: %d points need %d digits, got %d' % (name, cnt, 6 * cnt, len(pts)))
            frames.append({'type': typ, 'points': [(int(pts[6 * k:6 * k + 3]), int(pts[6 * k + 3:6 * k + 6])) for k in range(cnt)]})
        sets.append((name, frames))
    return sets


def format_hit_set(name, frames):
    """The text of one set: name line, per frame `NN` (+ `TT` + the NN x `XXXYYY` line), `99`.  Inverse of parse_hit_sets."""
    out = [name]
    for f in frames:
        if not f or not f['points']:
            out.append('00')
            continue
        if len(f['points']) > 98:
            raise ValueError('at most 98 points per frame (99 ends the set)')
        out.append('%02d' % len(f['points']))
        out.append('%02d' % f.get('type', 0))
        out.append(''.join('%03d%03d' % (x, y) for x, y in f['points']))
    out.append('99')
    return '\n'.join(out) + '\n'


def gate_box(points):
    """(maxDx, maxDy) of hit_parse: the largest x and y of the frame's points, as bytes (never below 0)."""
    mx = max([0] + [x for x, _ in points])
    my = max([0] + [y for _, y in points])
    return mx & 0xFF, my & 0xFF


def hit_record(frame):
    """The binary record hit_parse produces for one frame (1 byte when empty): {count, type, maxDx, maxDy, (x, y)*count}."""
    if not frame or not frame['points']:
        return b'\0'
    pts = frame['points']
    mx, my = gate_box(pts)
    out = bytes([len(pts), frame.get('type', 0) & 0xFF, mx, my])
    for x, y in pts:
        out += bytes([x & 0xFF, y & 0xFF])
    return out


# ------------------------------------------------------------------------------------------------------ contact (creatures)

class Arena:
    """Big-endian guest memory: bytes addressed by offset."""

    def __init__(self, data=b''):
        self.data = bytearray(data)

    def ensure(self, n):
        if len(self.data) < n:
            self.data += bytes(n - len(self.data))

    def r8(self, a):
        return self.data[a] if 0 <= a < len(self.data) else 0

    def r16(self, a):
        return (self.r8(a) << 8) | self.r8(a + 1)

    def r32(self, a):
        return (self.r16(a) << 16) | self.r16(a + 2)

    def w16(self, a, v):
        self.ensure(a + 2)
        self.data[a] = (v >> 8) & 0xFF
        self.data[a + 1] = v & 0xFF

    def w32(self, a, v):
        self.w16(a, v >> 16)
        self.w16(a + 2, v & 0xFFFF)


def _u16(v):
    return v & 0xFFFF


def _abs_w(v):
    return _u16(0 - v) if v & 0x8000 else v


def contact_overlap(d0, d1, d2, d3):
    """creatures.cpp contactOverlap (LAB_03CA): 32-bit signed compares on N only."""
    def n(a, b):
        return ((a - b) & 0xFFFFFFFF) >> 31

    if n(d2, d0):
        return d3 >= d0
    if n(d2, d1):
        return True
    if n(d3, d0):
        return False
    return not (d3 >= d1)


def depth_close(a, b):
    """creatures.cpp depthClose (LAB_03B3)."""
    d = _abs_w(_u16(a - b))
    return _s16(d) <= 10


def hit_record_at(arena, set_addr, index):
    """creatures.cpp hitRecordAt: the offset of the record of frame `index`."""
    p = set_addr
    for _ in range(index & 0xFFFF):
        twice = _u16(2 * arena.r8(p))
        p += 1
        if twice:
            p += _u16(twice + 3)
    return p


def hit_set_find(arena, pairs, count, key):
    """creatures.cpp hitSetFind: pairs = address of `count` {cel table, hit set} big-endian long pairs."""
    for i in range(count):
        if arena.r32(pairs + 8 * i) == key:
            return arena.r32(pairs + 8 * i + 4)
    return 0


def popcount(mask):
    return bin(mask).count('1')


def contact_test(arena, def_tab, def_frame, def_x, def_y, att_tab, hit_set, att_frame, att_x, att_y):
    """creatures.cpp contactTest.  Returns (hit, x, y).  Every step is the 16-bit word operation of the asm."""
    att_ofs = _s16(_u16(att_frame * 10))
    mirror = 0
    if not (arena.r8(att_tab + 18 + att_ofs) & 1):
        mirror = arena.r16(att_tab + 14 + att_ofs)

    rec = hit_record_at(arena, hit_set, att_frame)
    count = arena.r8(rec)
    if count == 0:
        return False, 0, 0

    def_ofs = _s16(_u16(def_frame * 10))
    cel = def_tab + 10 + def_ofs
    cel_w = arena.r16(cel + 4)
    cel_h = arena.r16(cel + 6)

    max_dx = arena.r8(rec + 2)
    max_dy = arena.r8(rec + 3)
    d2 = _u16(att_x)
    if mirror:
        d2 = _u16(d2 + mirror)
        d2 = _u16(d2 - max_dx)
    d3x = _u16(max_dx + d2)
    if not contact_overlap(def_x, _u16(def_x + cel_w), d2, d3x):
        return False, 0, 0
    if not contact_overlap(def_y, _u16(def_y + cel_h), att_y, _u16(max_dy + att_y)):
        return False, 0, 0

    planes = arena.r32(def_tab + 2) + arena.r32(cel)
    row_bytes = _u16(((_u16(cel_w + 15)) >> 4) << 1)
    stride = _u16(row_bytes * cel_h)
    plane_mask = arena.r16(cel + 8) & 0xFF
    if plane_mask > 63:
        return False, 0, 0
    n_planes = popcount(plane_mask)
    if n_planes == 0:
        return False, 0, 0

    for i in range(count):
        px = arena.r8(rec + 4 + 2 * i)
        py = arena.r8(rec + 4 + 2 * i + 1)
        sx = px
        if mirror:
            sx = _u16(0 - sx)
            sx = _u16(sx + mirror)
        sx = _u16(sx + att_x)
        if _s16(sx) < _s16(def_x):
            continue
        if _s16(_u16(sx - def_x)) >= _s16(cel_w):
            continue
        sy = _u16(py + att_y)
        if _s16(sy) < _s16(def_y):
            continue
        if _s16(_u16(sy - def_y)) >= _s16(cel_h):
            continue
        # QUIRK: from here the RAW point is used again, mirror or not (the bit tested and the reported point)
        cx = _u16(px + att_x)
        cy = _u16(py + att_y)
        xw = _u16(cx - def_x)
        yw = _u16(cy - def_y)
        ofs = _u16(_u16((xw >> 4) << 1) + ((yw * row_bytes) & 0xFFFF))
        bit = 15 - (xw & 15)
        plane = planes
        for _ in range(n_planes):
            if arena.r16(plane + _s16(ofs)) & (1 << bit):
                return True, cx, cy
            plane += _s16(stride)
    return False, 0, 0


# --------------------------------------------------------------------------------------------------------------- cel sizes

CEL_HEADER_BYTES = 10
CEL_ENTRY_BYTES = 10


def words_of(w):
    return (w + 15) >> 4


def frame_bytes(w, h, plane_bits):
    """tools/artconv.py frame_bytes: ceil(w/16) words per row * 2 * h rows * the number of present planes."""
    return words_of(w) * 2 * h * bin(plane_bits).count('1')


def cel_decoded_bytes(frames):
    """Decoded plane data of a cel: frames = [(w, h, plane_bits)]."""
    return sum(frame_bytes(w, h, b) for w, h, b in frames)


def cel_header_bits(frames):
    """The BE32 at +6 of a cel file: 8 * the decoded size (what artconv writes)."""
    return 8 * cel_decoded_bytes(frames)


def cel_size_estimate(header10):
    """loaders.cpp celSizeEstimate: bytes a cel takes in the creature arena, from its 10-byte file header
    (BE16 frames, BE32 packed size, BE32 8 * decoded size): decoded + $168 + 10 per frame + 10."""
    count, _packed, bits = struct.unpack('>HII', header10[:10])
    return ((bits >> 3) + 0x168 + count * CEL_ENTRY_BYTES + CEL_HEADER_BYTES) & 0xFFFFFFFF


def cel_arena_bytes(frames):
    """cel_size_estimate for a cel built from frames = [(w, h, plane_bits)]."""
    return cel_decoded_bytes(frames) + 0x168 + len(frames) * CEL_ENTRY_BYTES + CEL_HEADER_BYTES
