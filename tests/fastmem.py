"""Sequential bytes.replace over a big buffer without one full scan per pair (test speed, 2026-10-11).

The emulator tests (test_fighters_emu, test_arena_emu) normalise two memory snapshots of ~1 MB each, for every case, with a loop of
`part = part.replace(old_i, new_i)` over a few dozen 4-byte addresses: dozens of full scans per snapshot, almost all of them finding
nothing.  `xlat` gives the same result: a pair whose first bytes do not occur in the buffer cannot match, so it is skipped; the
presence of each distinct prefix is re-checked only after a replacement actually changed the buffer (a replacement could create a
new occurrence).  Same order, same non-overlapping left-to-right matching as the plain loop.
"""

PREFIX = 3


def xlat(part, pairs):
    """-> part after `for old, new in pairs: part = part.replace(old, new)` (pairs: (bytes, bytes), in that order)."""
    prefixes = {old[:PREFIX] for old, _ in pairs}
    live = None
    for old, new in pairs:
        if live is None:
            live = {p for p in prefixes if p in part}
        if old[:PREFIX] not in live:
            continue
        replaced = part.replace(old, new)
        if replaced is not part:             # bytes.replace hands back the same object when there was no match
            part = replaced
            live = None
    return part
