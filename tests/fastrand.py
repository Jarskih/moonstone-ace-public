"""Bulk random bytes with the exact random stream of a loop over rng.randint (test speed, 2026-10-11).

`bytes(rng.randint(lo, hi) for _ in range(n))` costs ~3 us a byte (randint -> randrange -> _randbelow, three python frames); the case
generators of the creature / fighter / arena tests draw millions of bytes.  Python's randint(lo, hi) is lo + _randbelow(hi - lo + 1),
and _randbelow(n) draws getrandbits(n.bit_length()) until the value is below n.  `randint_bytes` does exactly that in one loop, so
it consumes the same words of the generator in the same order and returns the same bytes: a test keeps its seeded cases.  The
equivalence is checked the first time it is used (against randint on a twin generator); on any difference, or for a generator that
is not a plain random.Random, it falls back to the loop over randint.
"""
import random

_checked = None


def _fast(rng, n, lo, hi):
    span = hi - lo + 1
    k = span.bit_length()
    getrandbits = rng.getrandbits
    out = bytearray(n)
    for i in range(n):
        r = getrandbits(k)
        while r >= span:
            r = getrandbits(k)
        out[i] = lo + r
    return bytes(out)


def _slow(rng, n, lo, hi):
    return bytes(rng.randint(lo, hi) for _ in range(n))


def _self_check():
    for lo, hi in ((0, 255), (0, 0), (0, 1), (3, 200), (1, 7)):
        a, b = random.Random(20261011), random.Random(20261011)
        if _fast(a, 300, lo, hi) != _slow(b, 300, lo, hi) or a.getrandbits(32) != b.getrandbits(32):
            return False
    return True


def randint_bytes(rng, n, lo=0, hi=255):
    """n bytes, each rng.randint(lo, hi) (same values, same generator state afterwards as the equivalent loop)."""
    global _checked
    if type(rng) is not random.Random or hi < lo:
        return _slow(rng, n, lo, hi)
    if _checked is None:
        _checked = _self_check()
    return _fast(rng, n, lo, hi) if _checked else _slow(rng, n, lo, hi)
