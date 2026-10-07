#!/usr/bin/env python3
"""gen_synth_tables.py -- mog synth data (S_44 tables, S_45 instruments + waveforms) -> build/gen/synth_data.cpp (ROADMAP 7.1g, 10.2a).

Reads the ORIGINAL mog hunk executable (build/disks/A/mog, tools/origin.py) and
writes the read-only data the C++ synth (src/engine/synth.cpp) needs, so mog's S_44 CODE hunk no longer has to be both the
program and the data of the synth:

  kSynthBlob[]   the raw bytes of S_44 from LAB_0FCA+2 (the pitch table) to the end of the hunk: pitch tables, the 15-byte
                 vibrato rows (LAB_0FE0+2), the 8-byte envelopes (LAB_0FE1), the sequences (LAB_0FE2..) and the sequence
                 table LAB_1098 (168 longs).  Byte-exact EXCEPT: the word at LAB_0FCA+2 is zero (the original's init
                 LAB_0F89 clears it as a side effect of its stack wipe) and the relocation routine LAB_0FD4 (code that IRA
                 left among the tables, S_44 4628..4979) is zero-filled.  References inside the blob are S_44 offsets,
                 exactly the raw (pre-relocation) values of the original, so "ref" == S_44 offset and 0 is the null pointer.
  kSynthInst[]   the 131 instrument rows of S_45 (LAB_10A2, 14 bytes each: three words, the sample address, the pitch table
                 pointer) with the sample address split into {bank, offset}; bank 0 = the S_45 waveforms (kSynthWave),
                 1..5 = the sound-bank buffers LAB_05C7..LAB_05CB (the original's LAB_0FD4 adds the buffer address; the
                 ranges below are that routine, mog.asm 29138-29219).
  kSynthWave[]   S_45 bytes 0..169: the nine single-cycle waveforms (CHIP) plus the first word of the instrument table
                 (LAB_10A2, the original uses it as the 1-word "silence" buffer).

    py tools/gen_synth_tables.py [--out build/gen/synth_data.cpp] [--check]

Since ROADMAP 10.2a the game fills these tables at start-up (src/rt/synth_fill.cpp + ms::synthDecodeInstruments); this file is
only compiled in by MS_DATA_COMPILED builds and the host tests (git-ignored output: it is original data).
"""
import argparse
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import origin  # noqa: E402
from hunkfile import parse_hunk_file  # noqa: E402

MOG = origin.binary_path('mog')
DEFAULT_OUT = os.path.join(ROOT, 'build', 'gen', 'synth_data.cpp')

S44, S45 = 44, 45
BLOB_BASE = 3570                  # LAB_0FCA + 2: first pitch table word
LAB_0FCA = 3568                   # the lock word (and the stack top of voice 3)
RELOC_CODE = (4628, 4980)         # LAB_0FD4 .. RTS: relocation code the IRA listing decodes inside the tables
SEQ_TABLE = 8788                  # LAB_1098
SEQ_COUNT = 168                   # up to the end of S_44 (9460)
VIB_BASE = 5046                   # LAB_0FE0 + 2: 15-byte rows
ENV_BASE = 5106                   # LAB_0FE1: 8-byte rows
INST_BASE = 168                   # LAB_10A2 (S_45)
INST_COUNT = 131                  # up to LAB_10AE
WAVE_BYTES = 170
NBANK = {'wave': 0, 'C7': 1, 'C8': 2, 'C9': 3, 'CA': 4, 'CB': 5}

# LAB_0FD4 as ranges of instrument indices ((LAB_10Ax offset - 168) / 14); later rules overwrite earlier ones.
BANK_RULES = [
    (9, 17, 'C7'),            # LAB_10A3..LAB_10A4: += LAB_05C7
    (17, 88, 'C8'),           # LAB_10A4..LAB_10A7: += LAB_05C8
    (88, 96, 'CB'),           # LAB_10A7..LAB_10A8: += LAB_05CB
    (96, 103, 'C8'),          # LAB_10A8..LAB_10A9: += LAB_05C8
    (39, 41, 'C7'),           # LAB_10A5, +14: MOVE.L LAB_05C7,6(A0) (offsets $31E2, $3DD2 kept)
    (58, 61, 'C7'),           # LAB_10A6, +14, +28: LAB_05C7 (offsets $4452, $50BE, $50BE)
    (103, 113, 'C9'),         # LAB_10A9..LAB_10AA
    (113, 115, 'CA'),         # LAB_10AA..LAB_10AB
    (115, 128, 'C8'),         # LAB_10AB..LAB_10AC
    (128, 130, 'C9'),         # LAB_10AC..LAB_10AD
    (130, 131, 'C8'),         # LAB_10AD..LAB_10AE
]


def build():
    hf = parse_hunk_file(origin.read_binary('mog'))
    blocks = {b.index: b for b in hf.hunks}
    d44, d45 = bytes(blocks[S44].data), bytes(blocks[S45].data)
    assert len(d44) == 9460 and len(d45) == 2016, (len(d44), len(d45))
    reloc45 = {}
    for g in blocks[S45].reloc32:
        for o in g.offsets:
            reloc45[o] = g.target_hunk
    reloc44 = {}
    for g in blocks[S44].reloc32:
        for o in g.offsets:
            reloc44[o] = g.target_hunk

    blob = bytearray(d44[BLOB_BASE:])
    blob[0:2] = b'\x00\x00'                                   # init's stack wipe clears the word at LAB_0FCA+2
    blob[RELOC_CODE[0] - BLOB_BASE:RELOC_CODE[1] - BLOB_BASE] = bytes(RELOC_CODE[1] - RELOC_CODE[0])
    # the sequence table: relocated longs that point into S_44; keep the raw offsets, check they all do
    for i in range(SEQ_COUNT):
        o = SEQ_TABLE + 4 * i
        v = struct.unpack('>I', d44[o:o + 4])[0]
        if i == 0:
            assert v == 0 and o not in reloc44, 'sequence 0 is the null entry'
        else:
            assert reloc44.get(o) == S44 and BLOB_BASE <= v < SEQ_TABLE, (i, v)

    insts = []
    for i in range(INST_COUNT):
        o = INST_BASE + 14 * i
        loop, loopoff, words = struct.unpack('>hHH', d45[o:o + 6])
        smp, pit = struct.unpack('>II', d45[o + 6:o + 14])
        assert reloc45.get(o + 10) == S44 and BLOB_BASE <= pit < RELOC_CODE[0], (i, pit)
        if i < 9:
            assert reloc45.get(o + 6) == S45, i
            bank = 'wave'
        else:
            assert (o + 6) not in reloc45, i
            bank = None
            for a, b, name in BANK_RULES:
                if a <= i < b:
                    bank = name
            assert bank, i
        insts.append((loop, loopoff, words, NBANK[bank], smp, pit))
    wave = d45[:WAVE_BYTES]
    return bytes(blob), insts, wave


def fmt_bytes(b, per=16):
    lines = []
    for i in range(0, len(b), per):
        lines.append('\t' + ','.join('0x%02X' % x for x in b[i:i + per]) + ',')
    return '\n'.join(lines)


def render():
    blob, insts, wave = build()
    o = []
    o.append('// GENERATED by tools/gen_synth_tables.py from the original mog hunk (S_44 tables, S_45 instruments/waveforms) -- do not edit.')
    o.append('// ROADMAP 7.1g. See the header comment of the tool for what is byte-exact and what is not.')
    o.append('#include "engine/synth_data.hpp"')
    o.append('')
    o.append('namespace ms {')
    o.append('')
    o.append('uint8_t kSynthBlob[kSynthBlobSize] = {')
    o.append(fmt_bytes(blob))
    o.append('};')
    o.append('')
    o.append('// {loop word, loop offset, length in words, bank, offset in the bank, pitch table (S_44 offset)}')
    o.append('SynthInstrument kSynthInst[kSynthInstCount] = {')
    for i, (loop, lo, words, bank, smp, pit) in enumerate(insts):
        o.append('\t{%d, %d, %d, %d, 0x%X, %d},  // %d' % (loop, lo, words, bank, smp, pit, i))
    o.append('};')
    o.append('')
    o.append('const SynthTables kSynthTables = {kSynthBlob, kSynthInst};')
    o.append('')
    o.append('// In a game build (MS_SYNTH_WAVE_IN_HUNK: MS_GAME_BUILD) kSynthWave is the first bytes of mog S_45, owned as g_mogSynthData by tools/gen_data.py')
    o.append('// (asm/patches/mog.data_rest.json); the copy below serves the skeleton and host builds, tests/test_gen_data.py checks it equals the hunk.')
    o.append('#if !MS_SYNTH_WAVE_IN_HUNK')
    o.append('MS_SYNTH_CHIP uint8_t kSynthWave[kSynthWaveSize] = {')
    o.append(fmt_bytes(wave))
    o.append('};')
    o.append('#endif')
    o.append('')
    o.append('}  // namespace ms')
    return '\n'.join(o) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--check', action='store_true', help='fail if the file on disk differs')
    a = ap.parse_args()
    text = render()
    if a.check:
        cur = open(a.out, encoding='utf-8', newline='').read() if os.path.exists(a.out) else ''
        if cur.replace('\r\n', '\n') != text:
            print('%s is stale; run py tools/gen_synth_tables.py' % a.out)
            return 1
        return 0
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('wrote', a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
