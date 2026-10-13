"""netproto.py -- the wire format of Moonstone network play, in Python (ROADMAP 12.17).

A line-for-line mirror of include/engine/netproto.hpp + src/engine/netproto.cpp (the layout table is in the header comment
there).  tests/test_netserver.py checks it against the C++ driver: the same fixed messages, the same random messages and the
same status code for corrupted bytes.  Standard library only.

    status, msg, used = decode(buf)        # OK / MORE / one of the ERR_* codes, exactly as netDecode
    status, data = encode(msg)             # OK + bytes, or an error
    framer = Framer(); framer.push(chunk); framer.next()    # cuts messages out of a TCP stream

A message is `Msg(type, **fields)`; fields are plain Python values (names are str, an input frame is 11 bytes).
"""
import struct

MAGIC = 0x4D53
PROTO_VERSION = 6
HEADER_BYTES = 6
PAYLOAD_MAX = 512
MSG_MAX = HEADER_BYTES + PAYLOAD_MAX
NAME_LEN = 16
LIST_MAX = 8
NODE_MAX = 4
KNIGHTS_MAX = 4
DELAY_MAX = 8
CHUNK_BYTES = 384
SNAP_MAX = 6144
JOIN_FLAG_REJOIN = 1
UDP_HISTORY_MAX = 8      # ticks one INPUT_UDP / TICK_UDP datagram carries
UDP_HEARS = 1            # datagram flag: "I have received a valid datagram from you"
NODE_NONE = 0xFF
NODE_DROPPED = 0xFE      # a binding whose node was dropped (engine/netplay.hpp NET_NODE_DROPPED): valid in the CONFIG of a rejoiner
PAD_SOURCE_COUNT = 6
FRAME_BYTES = 11
KEYS_MAX = 4
PADS_USED_MASK = (1 << (5 * PAD_SOURCE_COUNT)) - 1

# status codes (NetStatus)
OK = 0
MORE = 1
ERR_MAGIC = -1
ERR_VERSION = -2
ERR_TYPE = -3
ERR_LENGTH = -4
ERR_FIELD = -5
ERR_SPACE = -6

# message types (NetMsgType)
HELLO, CONFIG, READY, SEED, INPUT, TICK, HASH, PING, PONG, BYE, DROP, LOBBY_PICK, LOBBY_STATE = range(1, 14)
REJOIN, REJOIN_OK, STATE_BEGIN, STATE_CHUNK, STATE_END, RESUME, BOUNDARY = range(14, 21)
INPUT_UDP, TICK_UDP = 21, 22      # protocol 6: one datagram each, never on a TCP stream
ANNOUNCE, REGISTER, REFRESH, UNREGISTER, LIST_REQ, LIST_REPLY, JOIN_REQ, JOIN_REPLY = range(32, 40)
RELAY_HELLO, REGISTER_REPLY = 40, 41

TYPE_NAMES = {1: 'HELLO', 2: 'CONFIG', 3: 'READY', 4: 'SEED', 5: 'INPUT', 6: 'TICK', 7: 'HASH', 8: 'PING', 9: 'PONG',
              10: 'BYE', 11: 'DROP', 12: 'LOBBY_PICK', 13: 'LOBBY_STATE', 14: 'REJOIN', 15: 'REJOIN_OK', 16: 'STATE_BEGIN', 17: 'STATE_CHUNK',
              18: 'STATE_END', 19: 'RESUME', 20: 'BOUNDARY', 21: 'INPUT_UDP', 22: 'TICK_UDP', 32: 'ANNOUNCE', 33: 'REGISTER', 34: 'REFRESH', 35: 'UNREGISTER', 36: 'LIST_REQ',
              37: 'LIST_REPLY', 38: 'JOIN_REQ', 39: 'JOIN_REPLY', 40: 'RELAY_HELLO', 41: 'REGISTER_REPLY'}

# NetByeReason
BYE_NORMAL, BYE_VERSION, BYE_BUILD, BYE_FULL, BYE_DESYNC, BYE_TIMEOUT, BYE_DROPPED, BYE_REFUSED, BYE_NO_SEAT, BYE_BAD_TOKEN, BYE_REJOIN_FAILED = range(11)
BYE_REASONS = 11
# NetJoinResult
JOIN_DIRECT, JOIN_RELAY, JOIN_NOT_FOUND, JOIN_FULL, JOIN_BUILD = range(5)
JOIN_RESULTS = 5
# NetRelayRole
ROLE_HOST, ROLE_CLIENT = 0, 1
ROLES = 2
# NetRegisterResult
REG_DIRECT, REG_RELAY, REG_BAD_NAME, REG_LISTED, REG_FULL = range(5)
REG_RESULTS = 5
# NetGameState (REFRESH)
GAME_WAITING, GAME_PLAYING = 0, 1
GAME_STATES = 2
# NetMode
MODE_CLASSIC, MODE_COOP = 0, 1
# the lobby = the knight screen (protocol 4)
PORTRAITS = 4
PICK_BROWSE, PICK_CLAIM, PICK_DONE = range(3)
PICK_STAGES = 3
SEAT_FREE, SEAT_BROWSE, SEAT_CLAIM, SEAT_DONE = range(4)
SEAT_STATES = 4

GAME_TYPES = range(HELLO, BOUNDARY + 1)         # the types a relay passes between the nodes of a game over TCP (protocol 5: with the rejoin messages)
UDP_TYPES = (INPUT_UDP, TICK_UDP)               # the datagram types the relay forwards between the members of a session (protocol 6)


class Msg:
    """One message: .type and the fields of its layout as attributes."""

    def __init__(self, mtype, **fields):
        if mtype == REFRESH:
            fields.setdefault('state', GAME_WAITING)   # a REFRESH built without a state is a waiting one
        if mtype == HELLO:
            fields.setdefault('rules', 0)       # the rule options byte added in 12.8b: a HELLO built without it carries none
        if mtype == CONFIG:
            fields.setdefault('token', 0)       # the rejoin token (protocol 5): a CONFIG built without it carries none
        if mtype == JOIN_REQ:
            fields.setdefault('flags', 0)       # protocol 5: the rejoin flag
        if mtype in (INPUT_UDP, TICK_UDP):
            fields.setdefault('count', len(fields.get('frames', [])))   # protocol 6: the number of ticks in the datagram
        self.__dict__['type'] = mtype
        self.__dict__['f'] = fields

    def __getattr__(self, key):
        try:
            return self.f[key]
        except KeyError:
            raise AttributeError(key)

    def __eq__(self, other):
        return isinstance(other, Msg) and self.type == other.type and self.f == other.f

    def __repr__(self):
        return 'Msg(%s, %r)' % (TYPE_NAMES.get(self.type, self.type), self.f)


def listing(name, mode, players, max_players, build, port, version):
    return dict(name=name, mode=mode, players=players, max_players=max_players, build=build, port=port, version=version)


# ---- validation -----------------------------------------------------------------------------------------------------------------

def is_name_valid(name):
    """A name as a str: printable ASCII, at most 15 characters (the wire form NUL-pads it to 16)."""
    return len(name) <= NAME_LEN - 1 and all(0x20 <= ord(c) <= 0x7E for c in name)


def is_frame_valid(fr):
    """netFrameRead: canonical 11 byte input frame."""
    if len(fr) != FRAME_BYTES:
        return False
    pads = struct.unpack('>I', fr[:4])[0]
    flags = fr[4]
    if pads & ~PADS_USED_MASK & 0xFFFFFFFF or flags & 0xF0 or (flags & 7) > KEYS_MAX:
        return False
    if fr[5] == 0x80 or fr[6] == 0x80:
        return False
    return all(fr[7 + i] == 0 for i in range(flags & 7, KEYS_MAX))


def _is_listing_valid(lst):
    return (is_name_valid(lst['name']) and lst['mode'] <= MODE_COOP and 1 <= lst['max_players'] <= NODE_MAX and
            1 <= lst['players'] <= lst['max_players'])


def is_known_type(t):
    return HELLO <= t <= TICK_UDP or ANNOUNCE <= t <= REGISTER_REPLY


def _is_udp_header_valid(f):
    """Shared by INPUT_UDP and TICK_UDP: a client node, a token, the known flag bits, a window that does not wrap."""
    return 1 <= f['node'] < NODE_MAX and f['token'] != 0 and f['flags'] & ~UDP_HEARS == 0 and f['count'] <= UDP_HISTORY_MAX and \
        f['first'] <= 0xFFFFFFFF - f['count']


def _is_pad_valid(pad):
    return pad < PAD_SOURCE_COUNT or pad == 0xFF


def is_seat_valid(seat):
    """One seat (state, portrait, pad, name) of LOBBY_STATE: a free seat is all zero (pad $FF), a joined one has a name, the pad is only
    set when the pick is done."""
    state, sel, pad, name = seat
    if state >= SEAT_STATES or sel >= PORTRAITS or not is_name_valid(name):
        return False
    if state == SEAT_FREE:
        return sel == 0 and pad == 0xFF and name == ''
    if state == SEAT_DONE:
        return _is_pad_valid(pad) and name != ''
    return pad == 0xFF and name != ''


def _is_lobby_state_valid(f):
    seats = f['seats']
    if f['mode'] > MODE_COOP or not 1 <= f['your_seat'] < NODE_MAX or len(seats) != NODE_MAX:
        return False
    if not all(is_seat_valid(s) for s in seats):
        return False
    if seats[0][0] == SEAT_FREE or seats[f['your_seat']][0] == SEAT_FREE:
        return False
    held = [s[1] for s in seats if s[0] >= SEAT_CLAIM]
    return len(held) == len(set(held))      # a portrait belongs to one player


def _is_lobby_pick_valid(f):
    if f['stage'] >= PICK_STAGES or f['sel'] >= PORTRAITS or not is_name_valid(f['name']):
        return False
    if f['stage'] == PICK_DONE:
        return _is_pad_valid(f['pad']) and f['name'] != ''
    return f['pad'] == 0xFF and f['name'] == ''


def is_valid(m):
    t, f = m.type, m.f
    if t == HELLO:
        return is_name_valid(f['name'])
    if t == CONFIG:
        if f['mode'] > MODE_COOP or not 1 <= f['nodes'] <= NODE_MAX or f['your_node'] >= f['nodes'] or \
                f['delay'] > DELAY_MAX or len(f['bindings']) > KNIGHTS_MAX or len(f['names']) != f['nodes']:
            return False
        for node, pad, slot in f['bindings']:
            if not (node < f['nodes'] or node in (NODE_NONE, NODE_DROPPED)) or not (pad < PAD_SOURCE_COUNT or pad == 0xFF) or \
                    not (slot < PAD_SOURCE_COUNT or slot == 0xFF):
                return False
        ports = f['portraits']
        return all(is_name_valid(n) for n in f['names']) and len(ports) == f['nodes'] and \
            all(p < PORTRAITS for p in ports) and len(set(ports)) == len(ports)
    if t == REJOIN:
        return 1 <= f['node'] < NODE_MAX and f['token'] != 0
    if t == REJOIN_OK:
        return 1 <= f['node'] < NODE_MAX
    if t == STATE_BEGIN:
        return 1 <= f['nbytes'] <= SNAP_MAX and f['chunks'] == (f['nbytes'] + CHUNK_BYTES - 1) // CHUNK_BYTES and \
            0 < f['live'] < (1 << NODE_MAX) and f['live'] & 1 == 1
    if t == STATE_CHUNK:
        return 1 <= len(f['data']) <= CHUNK_BYTES and f['index'] < SNAP_MAX // CHUNK_BYTES
    if t == STATE_END:
        return 1 <= f['chunks'] <= SNAP_MAX // CHUNK_BYTES
    if t == BOUNDARY:
        return f['mask'] < (1 << NODE_MAX) and f['mask'] & 1 == 0
    if t == JOIN_REQ:
        return f['flags'] & ~JOIN_FLAG_REJOIN == 0
    if t == LOBBY_PICK:
        return _is_lobby_pick_valid(f)
    if t == LOBBY_STATE:
        return _is_lobby_state_valid(f)
    if t == READY:
        return f['node'] < NODE_MAX
    if t == INPUT:
        return f['node'] < NODE_MAX and is_frame_valid(f['frame'])
    if t == TICK:
        return 1 <= len(f['frames']) <= NODE_MAX and all(is_frame_valid(x) for x in f['frames'])
    if t == INPUT_UDP:
        return len(f['frames']) == f['count'] and _is_udp_header_valid(f) and all(is_frame_valid(x) for x in f['frames'])
    if t == TICK_UDP:
        return len(f['frames']) == f['count'] and _is_udp_header_valid(f) and 2 <= f['nodes'] <= NODE_MAX and f['node'] < f['nodes'] and \
            all(len(row) == f['nodes'] and all(is_frame_valid(x) for x in row) for row in f['frames'])
    if t == BYE:
        return f['reason'] < BYE_REASONS
    if t == DROP:
        return f['node'] < NODE_MAX
    if t in (ANNOUNCE, REGISTER):
        return _is_listing_valid(f)
    if t == REFRESH:
        return 1 <= f['players'] <= NODE_MAX and f['state'] < GAME_STATES
    if t == LIST_REPLY:
        return len(f['entries']) <= LIST_MAX and all(_is_listing_valid(lst) for _, lst in f['entries'])
    if t == JOIN_REPLY:
        return f['result'] < JOIN_RESULTS
    if t == RELAY_HELLO:
        return f['role'] < ROLES
    if t == REGISTER_REPLY:
        return f['result'] < REG_RESULTS
    return True


# ---- encode ---------------------------------------------------------------------------------------------------------------------

def _name(s):
    return s.encode('ascii').ljust(NAME_LEN, b'\0')


def _listing_bytes(l):
    return _name(l['name']) + struct.pack('>BBBIHH', l['mode'], l['players'], l['max_players'], l['build'], l['port'],
                                          l['version'])


def _body(m):
    t, f = m.type, m.f
    if t == HELLO:
        return struct.pack('>II', f['build'], f['data']) + _name(f['name']) + struct.pack('>BHB', f['pad_mask'], f['version'], f.get('rules', 0))
    if t == CONFIG:
        out = struct.pack('>BBBBIIB', f['mode'], f['nodes'], f['your_node'], f['delay'], f['seed'], f['token'], len(f['bindings']))
        out += b''.join(struct.pack('>BBB', *b) for b in f['bindings'])
        return out + b''.join(_name(n) for n in f['names']) + bytes(f['portraits'])
    if t == LOBBY_PICK:
        return struct.pack('>BBB', f['stage'], f['sel'], f['pad']) + _name(f['name'])
    if t == LOBBY_STATE:
        return struct.pack('>BB', f['mode'], f['your_seat']) + b''.join(
            struct.pack('>BBB', st, sel, pad) + _name(name) for st, sel, pad, name in f['seats'])
    if t == REJOIN:
        return struct.pack('>IIHBBI', f['build'], f['data'], f['version'], f['rules'], f['node'], f['token'])
    if t == REJOIN_OK:
        return struct.pack('>B', f['node'])
    if t == STATE_BEGIN:
        return struct.pack('>IIIIHHB', f['tick'], f['clock'], f['hash'], f['crc'], f['nbytes'], f['chunks'], f['live'])
    if t == STATE_CHUNK:
        return struct.pack('>HH', f['index'], len(f['data'])) + bytes(f['data'])
    if t == STATE_END:
        return struct.pack('>HI', f['chunks'], f['crc'])
    if t == RESUME:
        return struct.pack('>II', f['tick'], f['hash'])
    if t == BOUNDARY:
        return struct.pack('>IBI', f['tick'], f['mask'], f['hash'])
    if t == READY:
        return struct.pack('>B', f['node'])
    if t == SEED:
        return struct.pack('>II', f['tick'], f['seed'])
    if t == INPUT:
        return struct.pack('>IB', f['tick'], f['node']) + f['frame']
    if t == TICK:
        return struct.pack('>IB', f['tick'], len(f['frames'])) + b''.join(f['frames'])
    if t == INPUT_UDP:
        return struct.pack('>IIBBIIB', f['session'], f['token'], f['node'], f['flags'], f['ack'], f['first'], f['count']) + b''.join(f['frames'])
    if t == TICK_UDP:
        return struct.pack('>IIBBIBBIB', f['session'], f['token'], f['node'], f['flags'], f['ack'], f['drop_seq'], f['nodes'], f['first'], f['count']) + \
            b''.join(b''.join(row) for row in f['frames'])
    if t == HASH:
        return struct.pack('>II', f['tick'], f['crc'])
    if t in (PING, PONG):
        return struct.pack('>I', f['token'])
    if t == BYE:
        return struct.pack('>BH', f['reason'], f['version'])
    if t == DROP:
        return struct.pack('>IB', f['tick'], f['node'])
    if t in (ANNOUNCE, REGISTER):
        return _listing_bytes(f)
    if t == REFRESH:
        return struct.pack('>BB', f['players'], f['state'])
    if t == UNREGISTER:
        return b''
    if t == LIST_REQ:
        return struct.pack('>I', f['build'])
    if t == LIST_REPLY:
        return struct.pack('>B', len(f['entries'])) + b''.join(struct.pack('>I', gid) + _listing_bytes(l)
                                                               for gid, l in f['entries'])
    if t == JOIN_REQ:
        return struct.pack('>IIHB', f['game_id'], f['build'], f['version'], f['flags'])
    if t == JOIN_REPLY:
        return struct.pack('>BIHIH', f['result'], f['addr'], f['port'], f['session'], f['host_version'])
    if t == RELAY_HELLO:
        return struct.pack('>IB', f['session'], f['role'])
    if t == REGISTER_REPLY:
        return struct.pack('>BII', f['result'], f['game_id'], f['session'])
    raise ValueError(t)


def encode(m):
    """-> (status, bytes).  Like netEncode: unknown type / out-of-range field is an error, never a different message."""
    if not is_known_type(m.type):
        return ERR_TYPE, b''
    try:
        if not is_valid(m):
            return ERR_FIELD, b''
        body = _body(m)
    except (KeyError, struct.error, UnicodeEncodeError, TypeError):
        return ERR_FIELD, b''
    if len(body) > PAYLOAD_MAX:
        return ERR_SPACE, b''
    return OK, struct.pack('>HBBH', MAGIC, PROTO_VERSION, m.type, len(body)) + body


def pack(m):
    """encode() that must succeed (server-built messages)."""
    st, data = encode(m)
    if st != OK:
        raise ValueError('cannot encode %r: %d' % (m, st))
    return data


# ---- decode ---------------------------------------------------------------------------------------------------------------------

class _Reader:
    def __init__(self, buf):
        self.buf, self.pos, self.is_short, self.is_bad = buf, 0, False, False

    def raw(self, n):
        if self.pos + n > len(self.buf):
            self.is_short = True
            self.pos = len(self.buf)
            return b'\0' * n
        out = self.buf[self.pos:self.pos + n]
        self.pos += n
        return out

    def u8(self):
        return self.raw(1)[0]

    def u16(self):
        return struct.unpack('>H', self.raw(2))[0]

    def u32(self):
        return struct.unpack('>I', self.raw(4))[0]

    def name(self):
        raw = self.raw(NAME_LEN)
        end = raw.find(b'\0')
        text = raw if end < 0 else raw[:end]
        # a name the wire form would not produce (no NUL, bytes after the NUL, non-printable) is invalid: keep it
        # recognisable for is_valid by returning a str with a character it rejects
        if end < 0 or any(b != 0 for b in raw[end:]) or any(not 0x20 <= b <= 0x7E for b in text):
            return '\x01'
        return text.decode('ascii')

    def frame(self):
        if len(self.buf) - self.pos < FRAME_BYTES:
            self.is_short = True
            self.pos = len(self.buf)
            return b'\0' * FRAME_BYTES
        fr = bytes(self.buf[self.pos:self.pos + FRAME_BYTES])
        self.pos += FRAME_BYTES
        if not is_frame_valid(fr):
            self.is_bad = True
            return b'\0' * FRAME_BYTES
        return fr

    def listing(self):
        return dict(name=self.name(), mode=self.u8(), players=self.u8(), max_players=self.u8(), build=self.u32(),
                    port=self.u16(), version=self.u16())


def _read_body(t, r):
    if t == HELLO:
        return dict(build=r.u32(), data=r.u32(), name=r.name(), pad_mask=r.u8(), version=r.u16(), rules=r.u8())
    if t == CONFIG:
        f = dict(mode=r.u8(), nodes=r.u8(), your_node=r.u8(), delay=r.u8(), seed=r.u32(), token=r.u32())
        nb = r.u8()
        f['bindings'], f['names'], f['portraits'] = [], [], []
        if nb > KNIGHTS_MAX or f['nodes'] > NODE_MAX:
            r.is_bad = True
            return f
        f['bindings'] = [(r.u8(), r.u8(), r.u8()) for _ in range(nb)]
        f['names'] = [r.name() for _ in range(f['nodes'])]
        f['portraits'] = [r.u8() for _ in range(f['nodes'])]
        return f
    if t == LOBBY_PICK:
        return dict(stage=r.u8(), sel=r.u8(), pad=r.u8(), name=r.name())
    if t == LOBBY_STATE:
        f = dict(mode=r.u8(), your_seat=r.u8())
        f['seats'] = [(r.u8(), r.u8(), r.u8(), r.name()) for _ in range(NODE_MAX)]
        return f
    if t == REJOIN:
        return dict(build=r.u32(), data=r.u32(), version=r.u16(), rules=r.u8(), node=r.u8(), token=r.u32())
    if t == REJOIN_OK:
        return dict(node=r.u8())
    if t == STATE_BEGIN:
        return dict(tick=r.u32(), clock=r.u32(), hash=r.u32(), crc=r.u32(), nbytes=r.u16(), chunks=r.u16(), live=r.u8())
    if t == STATE_CHUNK:
        f = dict(index=r.u16())
        n = r.u16()
        if n > CHUNK_BYTES:
            r.is_bad = True
            f['data'] = b''
            return f
        f['data'] = bytes(r.raw(n))
        return f
    if t == STATE_END:
        return dict(chunks=r.u16(), crc=r.u32())
    if t == RESUME:
        return dict(tick=r.u32(), hash=r.u32())
    if t == BOUNDARY:
        return dict(tick=r.u32(), mask=r.u8(), hash=r.u32())
    if t == READY:
        return dict(node=r.u8())
    if t == SEED:
        return dict(tick=r.u32(), seed=r.u32())
    if t == INPUT:
        return dict(tick=r.u32(), node=r.u8(), frame=r.frame())
    if t == TICK:
        f = dict(tick=r.u32())
        n = r.u8()
        f['frames'] = []
        if n > NODE_MAX:
            r.is_bad = True
            return f
        f['frames'] = [r.frame() for _ in range(n)]
        return f
    if t == INPUT_UDP:
        f = dict(session=r.u32(), token=r.u32(), node=r.u8(), flags=r.u8(), ack=r.u32(), first=r.u32(), count=r.u8())
        f['frames'] = []
        if f['count'] > UDP_HISTORY_MAX:
            r.is_bad = True
            return f
        f['frames'] = [r.frame() for _ in range(f['count'])]
        return f
    if t == TICK_UDP:
        f = dict(session=r.u32(), token=r.u32(), node=r.u8(), flags=r.u8(), ack=r.u32(), drop_seq=r.u8(), nodes=r.u8(), first=r.u32(), count=r.u8())
        f['frames'] = []
        if f['count'] > UDP_HISTORY_MAX or f['nodes'] > NODE_MAX:
            r.is_bad = True
            return f
        f['frames'] = [[r.frame() for _ in range(f['nodes'])] for _ in range(f['count'])]
        return f
    if t == HASH:
        return dict(tick=r.u32(), crc=r.u32())
    if t in (PING, PONG):
        return dict(token=r.u32())
    if t == BYE:
        return dict(reason=r.u8(), version=r.u16())
    if t == DROP:
        return dict(tick=r.u32(), node=r.u8())
    if t in (ANNOUNCE, REGISTER):
        return r.listing()
    if t == REFRESH:
        return dict(players=r.u8(), state=r.u8())
    if t == UNREGISTER:
        return {}
    if t == LIST_REQ:
        return dict(build=r.u32())
    if t == LIST_REPLY:
        n = r.u8()
        if n > LIST_MAX:
            r.is_bad = True
            return dict(entries=[])
        return dict(entries=[(r.u32(), r.listing()) for _ in range(n)])
    if t == JOIN_REQ:
        return dict(game_id=r.u32(), build=r.u32(), version=r.u16(), flags=r.u8())

    if t == JOIN_REPLY:
        return dict(result=r.u8(), addr=r.u32(), port=r.u16(), session=r.u32(), host_version=r.u16())
    if t == RELAY_HELLO:
        return dict(session=r.u32(), role=r.u8())
    if t == REGISTER_REPLY:
        return dict(result=r.u8(), game_id=r.u32(), session=r.u32())
    raise ValueError(t)


def decode(buf):
    """-> (status, msg or None, bytes used).  Same checks in the same order as netDecode, so the status code of any byte
    string is the same on both sides."""
    n = len(buf)
    if n >= 1 and buf[0] != MAGIC >> 8:
        return ERR_MAGIC, None, 0
    if n >= 2 and buf[1] != MAGIC & 0xFF:
        return ERR_MAGIC, None, 0
    if n >= 3 and buf[2] != PROTO_VERSION:
        return ERR_VERSION, None, 0
    if n >= 4 and not is_known_type(buf[3]):
        return ERR_TYPE, None, 0
    if n < HEADER_BYTES:
        return MORE, None, 0
    payload = (buf[4] << 8) | buf[5]
    if payload > PAYLOAD_MAX:
        return ERR_LENGTH, None, 0
    if n < HEADER_BYTES + payload:
        return MORE, None, 0
    r = _Reader(bytes(buf[HEADER_BYTES:HEADER_BYTES + payload]))
    f = _read_body(buf[3], r)
    if r.is_short or (not r.is_bad and r.pos != payload):
        return ERR_LENGTH, None, 0
    msg = Msg(buf[3], **f)
    if r.is_bad or not is_valid(msg):
        return ERR_FIELD, None, 0
    return OK, msg, HEADER_BYTES + payload


class Framer:
    """Cuts messages out of a TCP byte stream (NetFramer).  next() -> (status, msg, raw bytes of that message); the error
    status is sticky -- the caller closes the connection."""

    def __init__(self):
        self.buf = bytearray()
        self.status = OK

    def push(self, data):
        self.buf += data

    def next(self):
        if self.status < 0:
            return self.status, None, b''
        st, msg, used = decode(self.buf)
        if st == OK:
            raw = bytes(self.buf[:used])
            del self.buf[:used]
            return OK, msg, raw
        if st < 0:
            self.status = st
        return st, None, b''
