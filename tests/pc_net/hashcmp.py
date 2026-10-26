"""Compares the `net: hash tick N crc X` lines of the logs of two nodes of one network game (ROADMAP 12.5 / 12.14).

    py tests/pc_net/hashcmp.py host.log guest.log

Exit 0: the lockstep began, at least one common hash tick, no mismatch, no `desync` line.  Prints the counts and the first mismatch.
"""
import re
import sys


def read(path):
    ticks, desync, begun = {}, [], False
    with open(path, errors='replace') as f:
        for line in f:
            m = re.search(r'net: hash tick (\d+) crc ([0-9A-F]{8})', line)
            if m:
                ticks[int(m.group(1))] = m.group(2)
            if 'net: desync' in line:
                desync.append(line.strip())
            if 'lockstep begins' in line:
                begun = True
    return ticks, desync, begun


def main(argv):
    a, da, ba = read(argv[1])
    b, db, bb = read(argv[2])
    common = sorted(set(a) & set(b))
    bad = [t for t in common if a[t] != b[t]]
    print('hash ticks: first %d, second %d, common %d (last %s), mismatches %d, desync lines %d' %
          (len(a), len(b), len(common), common[-1] if common else '-', len(bad), len(da) + len(db)))
    if bad:
        print('first mismatch at tick %d: %s vs %s' % (bad[0], a[bad[0]], b[bad[0]]))
    return 0 if ba and bb and common and not bad and not da and not db else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
