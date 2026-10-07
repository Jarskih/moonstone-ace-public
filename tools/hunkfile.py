#!/usr/bin/env python3
"""hunkfile.py -- strict reader for AmigaOS load files (hunk executables), ROADMAP 10.2.

The project's own copy of the reader the tools need for the original binaries (nb, program, mog from disk A): the tools of
the public tree cannot import ../moonshard.  Same data model as moonshard's moghunks.py (HunkFile / HunkBlock / RelocGroup,
`parse_hunk_file`), so callers can use either.

Supported: HUNK_HEADER (resident names skipped), CODE / DATA / BSS (memory flags on the type word or the size longword),
RELOC32, END, SYMBOL / DEBUG / NAME skipped.  Anything else, a truncated payload, an odd or out-of-range relocation or
trailing bytes raise HunkParseError.

    py tools/hunkfile.py FILE      # one line per hunk
"""
import struct
import sys
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

HUNK_NAME, HUNK_CODE, HUNK_DATA, HUNK_BSS, HUNK_RELOC32 = 0x3E8, 0x3E9, 0x3EA, 0x3EB, 0x3EC
HUNK_SYMBOL, HUNK_DEBUG, HUNK_END, HUNK_HEADER = 0x3F0, 0x3F1, 0x3F2, 0x3F3
HUNKF_ADVISORY, HUNKF_CHIP, HUNKF_FAST = 1 << 29, 1 << 30, 1 << 31
HUNKF_ALL = HUNKF_ADVISORY | HUNKF_CHIP | HUNKF_FAST
KIND = {HUNK_CODE: 'CODE', HUNK_DATA: 'DATA', HUNK_BSS: 'BSS'}


class HunkParseError(Exception):
    pass


@dataclass
class RelocGroup:
    target_hunk: int
    offsets: Tuple[int, ...]


@dataclass
class HunkBlock:
    index: int
    kind: str                         # 'CODE' | 'DATA' | 'BSS'
    mem_flag: str                     # '' | 'chip' | 'fast' | 'advisory'
    size_bytes: int
    file_offset: int                  # payload offset in the file (-1 for BSS)
    data: Optional[bytes]             # None for BSS
    reloc32: List[RelocGroup] = field(default_factory=list)
    end_file_offset: int = -1


@dataclass
class HunkFile:
    table_size: int
    first_hunk: int
    last_hunk: int
    size_words: Tuple[int, ...]
    hunks: List[HunkBlock]
    file_size: int
    consumed: int


def _flag(word):
    if word & HUNKF_CHIP:
        return 'chip'
    if word & HUNKF_FAST:
        return 'fast'
    if word & HUNKF_ADVISORY:
        return 'advisory'
    return ''


def parse_hunk_file(data: bytes) -> HunkFile:
    pos = 0

    def long():
        nonlocal pos
        if pos + 4 > len(data):
            raise HunkParseError(f'unexpected end of file at {pos:#x}')
        v = struct.unpack_from('>I', data, pos)[0]
        pos += 4
        return v

    if long() != HUNK_HEADER:
        raise HunkParseError('not an Amiga load file (no HUNK_HEADER)')
    while True:
        n = long()
        if n == 0:
            break
        pos += 4 * n
    table_size, first, last = long(), long(), long()
    if table_size != last - first + 1:
        raise HunkParseError('hunk table size does not match first/last')
    sizes = tuple(long() for _ in range(table_size))
    hunks, cur, index = [], None, first
    while pos < len(data):
        at = pos
        raw = long()
        typ = raw & 0x3FFFFFFF
        if typ in KIND:
            sw = long()
            size = (sw & ~HUNKF_ALL & 0xFFFFFFFF) * 4
            if typ != HUNK_BSS and pos + size > len(data):
                raise HunkParseError(f'{KIND[typ]} hunk {index} overruns the file')
            cur = HunkBlock(index, KIND[typ], _flag(raw) or _flag(sw), size, pos if typ != HUNK_BSS else -1,
                            None if typ == HUNK_BSS else bytes(data[pos:pos + size]))
            hunks.append(cur)
            index += 1
            if typ != HUNK_BSS:
                pos += size
        elif typ == HUNK_RELOC32:
            if cur is None:
                raise HunkParseError(f'RELOC32 before any hunk at {at:#x}')
            groups = []
            while True:
                count = long()
                if count == 0:
                    break
                target = long()
                if target >= table_size:
                    raise HunkParseError(f'RELOC32 target {target} out of range at {at:#x}')
                if pos + 4 * count > len(data):
                    raise HunkParseError('RELOC32 offsets overrun the file')
                offs = struct.unpack_from(f'>{count}I', data, pos)
                pos += 4 * count
                for o in offs:
                    if o & 1 or (cur.size_bytes and o >= cur.size_bytes):
                        raise HunkParseError(f'bad RELOC32 offset {o:#x} in hunk {cur.index}')
                groups.append(RelocGroup(target, tuple(offs)))
            cur.reloc32 = groups
        elif typ == HUNK_END:
            if cur is not None:
                cur.end_file_offset = pos
        elif typ == HUNK_SYMBOL:
            while True:
                n = long()
                if n == 0:
                    break
                pos += 4 * n + 4
        elif typ in (HUNK_DEBUG, HUNK_NAME):
            pos += 4 * long()
        else:
            raise HunkParseError(f'unsupported hunk type {typ:#x} at {at:#x}')
    if pos != len(data):
        raise HunkParseError('trailing bytes after the last hunk')
    return HunkFile(table_size, first, last, sizes, hunks, len(data), pos)


def hunk_base_offsets(hf: HunkFile) -> List[int]:
    """Image offset of each hunk when the hunks are packed back to back in hunk order."""
    out, total = [], 0
    by = {b.index: b for b in hf.hunks}
    for i in range(hf.table_size):
        out.append(total)
        total += by[i].size_bytes if i in by else 0
    return out


if __name__ == '__main__':
    hf = parse_hunk_file(open(sys.argv[1], 'rb').read())
    for b in hf.hunks:
        print(f'{b.index:3} {b.kind:4} {b.mem_flag or "-":4} {b.size_bytes:7} relocs {sum(len(g.offsets) for g in b.reloc32)}')
