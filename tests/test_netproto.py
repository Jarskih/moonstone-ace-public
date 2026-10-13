"""Host test of the network wire protocol and the state checksum (ROADMAP 12.6 / 12.7 pure half): engine/netproto.cpp,
engine/netplay.cpp (the frame codec it uses) and engine/statehash.hpp.

tests/netproto_driver.cpp is compiled with clang++ (-fno-exceptions -fno-rtti, the engine sources only) and run three ways:
  * self checks: round trip of every message type with random contents, every proper prefix is "need more", header and field
    corruption, over-read detection (the result must not depend on the bytes after the buffer), 40000 fuzz cases, the stream
    framer with six chunking strategies, crc32;
  * dump: the C++ encoder's bytes for one fixed message of every type are compared with bytes built here with struct.pack, i.e.
    the layout table of include/engine/netproto.hpp is checked by an independent encoder;
  * reenc: bytes built here are decoded and re-encoded by the C++ side (independent encoder -> C++ decoder).
"""
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

SOURCES = ['tests/netproto_driver.cpp', 'src/engine/netproto.cpp', 'src/engine/netplay.cpp']


def name16(text):
    return text.encode('ascii').ljust(16, b'\0')


def header(msg_type, payload):
    return struct.pack('>HBBH', 0x4D53, 6, msg_type, len(payload)) + payload


def frame(k):
    """fixedFrame(k) of the driver: pads (k + 5 i) & 31 in six 5-bit fields, keys $1C down and $7F up, mouse -5 / 300, button."""
    pads = 0
    for i in range(6):
        pads |= ((k + i * 5) & 0x1F) << (5 * i)
    return struct.pack('>IBbb4B', pads, 2 | 8, -5, 127, 0x1C | 0x80, 0x7F, 0, 0)


def listing(k):
    return (name16('Castle Game' if k == 0 else 'Dragon Hunt') + bytes([k & 1, 1 + k % 4, 4]) +
            struct.pack('>IHH', 0xCAFEBABE, 6502, 0x0103))


def expected_messages():
    """name -> bytes, the fixedMsg() set of the driver."""
    m = {}
    m['HELLO'] = header(1, struct.pack('>II', 0x01020304, 0xA0B0C0D0) + name16('Jari') + bytes([0x0F]) + struct.pack('>HB', 0x0102, 0x05))
    cfg = struct.pack('>BBBBIIB', 1, 3, 2, 2, 0x12345678, 0xA1B2C3D4, 3) + bytes([0, 0, 0, 1, 0, 1, 0xFF, 0xFF, 0xFF])
    cfg += name16('Host') + name16('Anna') + name16('Bo') + bytes([2, 0, 3])
    m['CONFIG'] = header(2, cfg)
    m['READY'] = header(3, bytes([2]))
    m['SEED'] = header(4, struct.pack('>II', 7, 0xDEADBEEF))
    m['INPUT'] = header(5, struct.pack('>IB', 0x00010203, 3) + frame(3))
    m['TICK'] = header(6, struct.pack('>IB', 99, 2) + frame(1) + frame(9))
    m['HASH'] = header(7, struct.pack('>II', 50, 0xCBF43926))
    m['PING'] = header(8, struct.pack('>I', 0x11223344))
    m['PONG'] = header(9, struct.pack('>I', 0x11223344))
    m['BYE'] = header(10, struct.pack('>BH', 2, 0x0103))
    m['DROP'] = header(11, struct.pack('>IB', 1234, 2))
    m['LOBBY_PICK'] = header(12, bytes([2, 2, 3]) + name16('Sir Bo'))
    seats = [(3, 2, 0, 'Host'), (1, 1, 0xFF, 'Anna'), (2, 3, 0xFF, 'Bo'), (0, 0, 0xFF, '')]
    m['LOBBY_STATE'] = header(13, bytes([1, 2]) + b''.join(bytes([st, sel, pad]) + name16(nm) for st, sel, pad, nm in seats))
    m['REJOIN'] = header(14, struct.pack('>IIHBBI', 0x0A0B0C0D, 0x01020304, 0x0103, 5, 2, 0xC0FFEE11))
    m['REJOIN_OK'] = header(15, bytes([2]))
    m['STATE_BEGIN'] = header(16, struct.pack('>IIIIHHB', 1500, 3010, 0x0BADF00D, 0xCBF43926, 800, 3, 7))
    m['STATE_CHUNK'] = header(17, struct.pack('>HH', 1, 5) + bytes(0xA0 + i for i in range(5)))
    m['STATE_END'] = header(18, struct.pack('>HI', 3, 0xCBF43926))
    m['RESUME'] = header(19, struct.pack('>II', 1500, 0x0BADF00D))
    m['BOUNDARY'] = header(20, struct.pack('>IBI', 1500, 6, 0x0BADF00D))
    # protocol 6: per-tick input over UDP (fixedMsg of the driver: frames fixedFrame(3), (4) / fixedFrame(10 * i + k))
    m['INPUT_UDP'] = header(21, struct.pack('>IIBBIIB', 0xA1B2C3D4, 0xC0FFEE11, 2, 1, 1000, 1003, 2) + frame(3) + frame(4))
    m['TICK_UDP'] = header(22, struct.pack('>IIBBIBBIB', 0, 0xC0FFEE11, 2, 0, 1005, 3, 3, 1001, 2) +
                           b''.join(frame(10 * i + k) for i in range(2) for k in range(3)))
    m['ANNOUNCE'] = header(32, listing(1))
    m['REGISTER'] = header(33, listing(1))
    m['REFRESH'] = header(34, bytes([3, 1]))
    m['UNREGISTER'] = header(35, b'')
    m['LIST_REQ'] = header(36, struct.pack('>I', 0xCAFEBABE))
    m['LIST_REPLY'] = header(37, bytes([2]) + struct.pack('>I', 1000) + listing(0) + struct.pack('>I', 1001) + listing(1))
    m['JOIN_REQ'] = header(38, struct.pack('>IIHB', 1001, 0xCAFEBABE, 0x0102, 1))
    m['JOIN_REPLY'] = header(39, struct.pack('>BIHIH', 1, 0xC0A80114, 6502, 77, 0x0103))
    m['RELAY_HELLO'] = header(40, struct.pack('>IB', 0xA1B2C3D4, 1))
    m['REGISTER_REPLY'] = header(41, struct.pack('>BII', 1, 1001, 77))
    return m


@unittest.skipUnless(CXX, 'needs clang++')
class NetProto(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='netproto_')
        cls.exe = os.path.join(cls.tmp, 'driver.exe')
        cmd = [CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS',
               '-Wno-missing-field-initializers', '-I', os.path.join(ROOT, 'include'), '-o', cls.exe]
        cmd += [os.path.join(ROOT, s) for s in SOURCES]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stdout + r.stderr)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_driver(self, *args, stdin=None):
        r = subprocess.run([self.exe] + list(args), input=stdin, capture_output=True, text=True, timeout=300)
        return r

    def test_self_checks(self):
        r = self.run_driver()
        self.assertEqual(r.returncode, 0, r.stdout[-3000:] + r.stderr[-1000:])
        self.assertIn('OK ', r.stdout)

    def test_wire_layout_matches_python_encoder(self):
        r = self.run_driver('dump')
        self.assertEqual(r.returncode, 0, r.stderr)
        got = dict(line.split(' ') for line in r.stdout.split('\n') if line)
        exp = expected_messages()
        self.assertEqual(sorted(got), sorted(exp))
        for name, data in exp.items():
            self.assertEqual(got[name], data.hex(), name)

    def test_python_bytes_decode_and_reencode(self):
        exp = expected_messages()
        names = sorted(exp)
        r = self.run_driver('reenc', stdin=''.join(exp[n].hex() + '\n' for n in names))
        self.assertEqual(r.returncode, 0, r.stderr)
        for name, line in zip(names, r.stdout.split('\n')):
            status, used, again = line.split(' ')
            self.assertEqual((status, int(used), again), ('0', len(exp[name]), exp[name].hex()), name)

    def test_python_corruptions_are_rejected(self):
        exp = expected_messages()
        bad = []
        seed = exp['SEED']
        bad.append(('-1', b'\x00' + seed[1:]))                               # magic
        bad.append(('-2', seed[:2] + bytes([9]) + seed[3:]))                    # version
        bad.append(('-3', seed[:3] + b'\x63' + seed[4:]))                    # type
        bad.append(('-4', seed[:5] + b'\x07' + seed[6:]))                    # length -1
        bad.append(('-4', seed[:4] + b'\x02\x01' + seed[6:]))                # length 513
        bad.append(('1', seed[:-1]))                                         # truncated
        bad.append(('-5', exp['BYE'][:6] + bytes([11, 1, 3])))                        # reason out of range

        bad.append(('-5', exp['REFRESH'][:-1] + bytes([2])))                          # REFRESH state out of range
        bad.append(('-4', exp['REFRESH'][:5] + bytes([1]) + exp['REFRESH'][6:7]))     # the old one-byte REFRESH
        r = self.run_driver('reenc', stdin=''.join(d.hex() + '\n' for _, d in bad))
        for (want, _), line in zip(bad, r.stdout.split('\n')):
            self.assertEqual(line.split(' ')[0], want, line)

    # ---- tools/netserver/netproto.py (the server's codec) against the C++ side ------------------------------------------------

    def test_python_codec_matches_cpp(self):
        """Every fixed message decodes in the Python codec and re-encodes to the same bytes; 6000 corrupted / truncated /
        random byte strings give the same status code and the same canonical re-encoding in Python and in C++."""
        import random
        import sys
        sys.path.insert(0, ROOT)
        from tools.netserver import netproto as P
        exp = expected_messages()
        for name, data in exp.items():
            st, msg, used = P.decode(data)
            self.assertEqual((st, used), (P.OK, len(data)), name)
            self.assertEqual(P.encode(msg), (P.OK, data), name)
        rng = random.Random(12)
        names = sorted(exp)
        cases = []
        for _ in range(6000):
            data = bytearray(exp[rng.choice(names)])
            kind = rng.randrange(5)
            if kind == 0 and len(data) > 6:
                data[rng.randrange(len(data))] = rng.randrange(256)
            elif kind == 1:
                data = data[:rng.randrange(len(data) + 1)]
            elif kind == 2:
                data[rng.randrange(len(data))] ^= 1 << rng.randrange(8)
            elif kind == 3:
                data = bytearray(rng.randrange(256) for _ in range(rng.randrange(40)))
            else:
                data = data + bytes(rng.randrange(256) for _ in range(rng.randrange(4)))
            cases.append(bytes(data))
        r = self.run_driver('reenc', stdin=''.join(c.hex() + chr(10) for c in cases))
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = r.stdout.split(chr(10))
        accepted = 0
        for case, line in zip(cases, lines):
            parts = line.split(' ')
            st, msg, used = P.decode(case)
            again = P.pack(msg).hex() if st == P.OK else ''
            self.assertEqual((int(parts[0]), int(parts[1]), parts[2]), (st, used, again), case.hex())
            accepted += st == P.OK
        self.assertGreater(accepted, 500)   # the corpus really reaches the accept path

    def test_lobby_mutations_are_caught(self):
        """Protocol 4 (the lobby = the knight screen): each deliberate loosening of a new field check makes the driver fail."""
        mutations = [
            ('if(!isNameValid(c.acNames[i]) || c.aubPortrait[i] >= NET_PORTRAITS) {', 'if(!isNameValid(c.acNames[i])) {'),              # CONFIG portrait range
            ('if(c.aubPortrait[k] == c.aubPortrait[i]) {', 'if(false) {'),                                                             # two nodes, one knight
            ('return s.ubSel == 0 && s.ubPad == 0xFF && isNameEmpty(s.acName);', 'return true;'),                                      # a free seat is all zero
            ('return isPadValid(s.ubPad) && !isNameEmpty(s.acName);', 'return isPadValid(s.ubPad);'),                                  # a done seat has a name
            ('return s.ubPad == 0xFF && !isNameEmpty(s.acName);', 'return !isNameEmpty(s.acName);'),                                   # browsing / claimed: no controller yet
            ('if(l.aSeats[0].ubState == NET_SEAT_FREE || l.aSeats[l.ubYourSeat].ubState == NET_SEAT_FREE) {', 'if(false) {'),          # the host and the receiver are in
            ('if(isHeldI && isHeldK && l.aSeats[i].ubSel == l.aSeats[k].ubSel) {', 'if(false) {'),                                     # a portrait belongs to one player
            ('if(!isModeValid(l.ubMode) || l.ubYourSeat < 1 || l.ubYourSeat >= NET_NODE_MAX) {', 'if(!isModeValid(l.ubMode)) {'),      # your seat 1..3
            ('return p.ubPad == 0xFF && isNameEmpty(p.acName);', 'return true;'),                                                      # browse / claim carry nothing
            ('if(p.ubStage >= NET_PICK_STAGES || p.ubSel >= NET_PORTRAITS || !isNameValid(p.acName)) {', 'if(!isNameValid(p.acName)) {'),
            ('return (ubType >= NET_MSG_HELLO && ubType <= NET_MSG_TICK_UDP)', 'return (ubType >= NET_MSG_HELLO && ubType <= NET_MSG_LOBBY_STATE)'),   # the rejoin messages are game messages
            ('return (ubType >= NET_MSG_HELLO && ubType <= NET_MSG_TICK_UDP)', 'return (ubType >= NET_MSG_HELLO && ubType <= NET_MSG_BOUNDARY)'),   # the datagram messages are known types
            # protocol 5 (rejoin, docs/NETPLAY.md 4.5)
            ('return m.u.rejoin.ubNode >= 1 && m.u.rejoin.ubNode < NET_NODE_MAX && m.u.rejoin.ulToken != 0;', 'return m.u.rejoin.ubNode < NET_NODE_MAX;'),   # node 1..3 and a token
            ('b.uwChunks == (b.uwBytes + NET_CHUNK_BYTES - 1) / NET_CHUNK_BYTES', 'true'),                                                               # the chunk count fits the size
            ('b.uwBytes >= 1 && b.uwBytes <= NET_SNAP_MAX', 'b.uwBytes >= 1'),                                                                           # the size is bounded
            ('(b.ubLiveMask & 1) != 0;', 'true;'),                                                                                                       # the host is alive
            ('(m.u.boundary.ubMask & 1) == 0;', 'true;'),                                                                                                # the host never rejoins
            ('return (m.u.joinReq.ubFlags & ~NET_JOIN_FLAG_REJOIN) == 0;', 'return true;'),                                                              # unknown join flags
            # protocol 6 (UDP input with history, docs/NETPLAY.md 2.2)
            ('return ubNode >= 1 && ubNode < NET_NODE_MAX && ulToken != 0 && (ubFlags & ~NET_UDP_HEARS) == 0 && ubCount <= NET_UDP_HISTORY_MAX &&',
             'return ubNode < NET_NODE_MAX && (ubFlags & ~NET_UDP_HEARS) == 0 && ubCount <= NET_UDP_HISTORY_MAX &&'),                                    # a client node and a token
            ('return ubNode >= 1 && ubNode < NET_NODE_MAX && ulToken != 0 && (ubFlags & ~NET_UDP_HEARS) == 0 && ubCount <= NET_UDP_HISTORY_MAX &&',
             'return ubNode >= 1 && ubNode < NET_NODE_MAX && ulToken != 0 && ubCount <= NET_UDP_HISTORY_MAX &&'),                                         # unknown datagram flags
            ('ulFirst <= 0xFFFFFFFFu - ubCount;', 'true;'),                                                                                              # a window that wraps the tick counter
            ('u.ubNodes >= 2 && u.ubNodes <= NET_NODE_MAX && u.ubNode < u.ubNodes;', 'u.ubNodes <= NET_NODE_MAX;'),                                      # TICK_UDP: the receiver is one of the nodes
            ('if(u.ubCount > NET_UDP_HISTORY_MAX || u.ubNodes > NET_NODE_MAX) {', 'if(u.ubNodes > NET_NODE_MAX) {'),                                     # TICK_UDP count bound while reading
            ('return 21 + NET_FRAME_BYTES * ulN * ulC;', 'return 21 + NET_FRAME_BYTES * ulC;'),                                                         # TICK_UDP size counts every node's frame
        ]
        with open(os.path.join(ROOT, 'src/engine/netproto.cpp'), encoding='utf-8', newline='') as fh:
            text = fh.read()
        for old, new in mutations:
            self.assertIn(old, text, 'mutation target vanished: ' + old)
            tmp = tempfile.mkdtemp(prefix='netproto_mut_')
            try:
                mutated = os.path.join(tmp, 'netproto_mut.cpp')
                with open(mutated, 'w', encoding='utf-8', newline='') as fh:
                    fh.write(text.replace(old, new, 1))
                exe = os.path.join(tmp, 'driver.exe')
                cmd = [CXX, '-std=c++17', '-O1', '-D_CRT_SECURE_NO_WARNINGS', '-Wno-missing-field-initializers', '-I', os.path.join(ROOT, 'include'), '-o', exe,
                       os.path.join(ROOT, 'tests/netproto_driver.cpp'), mutated, os.path.join(ROOT, 'src/engine/netplay.cpp')]
                r = subprocess.run(cmd, capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stderr[-1500:])
                r = subprocess.run([exe], capture_output=True, text=True, timeout=300)
                self.assertNotEqual(r.returncode, 0, 'mutation not caught: ' + old)
            finally:
                shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
