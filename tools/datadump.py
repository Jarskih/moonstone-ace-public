#!/usr/bin/env python3
"""datadump.py -- compare the start-up data of two builds (ROADMAP 10.2a proof).

    py tools/datadump.py FILLED_DUMP COMPILED_DUMP [--layout build/gen/owned_layout.json]

A build with -DMS_DATA_DUMP=ON writes PROGDIR:datadump.bin right after the start-up (src/rt/origload.cpp origDataDump): every owned
object in the order of build/gen/owned_layout.json (written by tools/gen_data.py), then the synth tables (kSynthBlob, kSynthInst).
One dump comes from the default build (the data filled from program / mog on the disk), the other from -DMS_DATA_COMPILED=ON (the
data compiled in, as before ROADMAP 10.2a).  They must be equal byte for byte except the pointer cells, which hold addresses of
each build's own layout (listed per object in owned_layout.json).  Prints one line per object that differs and a summary.
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYNTH_BYTES = 5890      # kSynthBlobSize (include/engine/synth_data.hpp)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('filled')
    ap.add_argument('compiled')
    ap.add_argument('--layout', default=os.path.join(ROOT, 'build', 'gen', 'owned_layout.json'))
    a = ap.parse_args(argv)
    lay = json.load(open(a.layout, encoding='utf-8'))
    da, db = open(a.filled, 'rb').read(), open(a.compiled, 'rb').read()
    if len(da) != len(db):
        print(f'datadump: sizes differ ({len(da)} vs {len(db)})')
        return 1
    pos = bad = masked = 0
    for o in lay:
        x, y = bytearray(da[pos:pos + o['size']]), bytearray(db[pos:pos + o['size']])
        for p in o['pointers']:
            x[p:p + 4] = y[p:p + 4] = b'\0\0\0\0'
            masked += 4
        if x != y:
            diff = [i for i in range(len(x)) if x[i] != y[i]]
            print(f'{o["object"]}: {len(diff)} bytes differ, first at +{diff[0]:#x}')
            bad += 1
        pos += o['size']
    rest_a, rest_b = da[pos:], db[pos:]
    synth_ok = rest_a == rest_b
    if not synth_ok:
        diff = [i for i in range(min(len(rest_a), len(rest_b))) if rest_a[i] != rest_b[i]]
        print(f'synth tables: {len(diff)} bytes differ, first at +{diff[0] if diff else 0:#x}')
    print(f'datadump: {len(lay)} objects, {pos} bytes ({masked} pointer bytes masked) + {len(rest_a)} synth bytes: '
          f'{"EQUAL" if not bad and synth_ok else "DIFFERENT"}')
    return 0 if not bad and synth_ok else 1


if __name__ == '__main__':
    sys.exit(main())
