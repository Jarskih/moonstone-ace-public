#!/usr/bin/env python3
"""adfx.py -- extract the Moonstone OFS floppies and checksum their files.

Strict AmigaDOS OFS reader: walks directory hash chains from the root block
and reads each file by following its data-block chain (T_DATA blocks:
+12 data size, +16 next data block, payload at +24). Fails loudly on a
broken chain or a size mismatch instead of truncating.

Usage: py tools/adfx.py OUTDIR disk.adf [disk.adf ...]
       writes OUTDIR/<disk stem>/<path> and OUTDIR/SHA1SUMS
"""
import hashlib, os, struct, sys

BLK, T_HEADER, T_DATA, ST_DIR, ST_FILE = 512, 2, 8, 2, -3


def u32(b, o): return struct.unpack_from('>I', b, o)[0]
def s32(b, o): return struct.unpack_from('>i', b, o)[0]


class Adf:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.img = f.read()
        if self.img[:3] != b'DOS' or self.img[3] & 1:
            raise ValueError(f'{path}: not an OFS disk (dostype {self.img[:4]!r})')
        self.root = len(self.img) // BLK // 2

    def blk(self, n):
        if not 2 <= n < len(self.img) // BLK:
            raise ValueError(f'block {n} out of range')
        return self.img[n * BLK:(n + 1) * BLK]

    def entries(self, dir_blk, prefix=''):
        b = self.blk(dir_blk)
        for slot in range(72):
            n = u32(b, 24 + slot * 4)
            while n:
                h = self.blk(n)
                name = h[0x1B1:0x1B1 + h[0x1B0]].decode('latin-1')
                kind = s32(h, 0x1FC)
                if kind == ST_DIR:
                    yield from self.entries(n, prefix + name + '/')
                elif kind == ST_FILE:
                    yield prefix + name, self.read(n)
                n = u32(h, 0x1F0)

    def read(self, hdr):
        h = self.blk(hdr)
        size, n, out = u32(h, 0x144), u32(h, 0x10), bytearray()
        while n:
            d = self.blk(n)
            if u32(d, 0) != T_DATA or u32(d, 4) != hdr:
                raise ValueError(f'header {hdr}: block {n} is not its data block')
            out += d[24:24 + u32(d, 12)]
            n = u32(d, 16)
        if len(out) != size:
            raise ValueError(f'header {hdr}: chain gave {len(out)} bytes, header says {size}')
        return bytes(out)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, sums = sys.argv[1], []
    for path in sys.argv[2:]:
        disk = os.path.splitext(os.path.basename(path))[0].split()[-1]  # "...) A.adf" -> "A"
        for name, data in Adf(path).entries(Adf(path).root):
            dst = os.path.join(out, disk, name)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, 'wb') as f:
                f.write(data)
            sums.append(f'{hashlib.sha1(data).hexdigest()}  {disk}/{name}')
        print(f'{disk}: {sum(1 for s in sums if s.split()[1].startswith(disk + "/"))} files')
    with open(os.path.join(out, 'SHA1SUMS'), 'w', newline='\n') as f:
        f.write('\n'.join(sorted(sums, key=lambda s: s.split()[1])) + '\n')


if __name__ == '__main__':
    main()
