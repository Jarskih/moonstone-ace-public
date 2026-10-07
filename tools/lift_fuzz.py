"""lift_fuzz.py -- pointer-graph fuzzing for routines that chase pointers.

harness.Harness.fuzz(ptr_aregs=True) points A0-A6 into random bytes, which is enough for
one level of dereference.  Most game routines instead load a struct pointer from a global
(`LEA LAB_05E4,A0 / MOVEA.L 0(A0),A0 / MOVE.W 80(A0),D1`) or follow pointers inside the
struct.  This fuzzer seeds

  * a scratch window whose aligned longs are pointers back into the window half the time,
  * the 32 bytes at every global the routine names (pointer / small value / random), but only
    in non-CODE hunks so the original's instruction stream is never patched,
  * address registers that mostly point into the window (even addresses),

and records through Harness.record, so faulting inputs (unmapped, hardware, odd address) are
discarded exactly like the stock fuzzer.  Seeded cells travel in the case's `mem_in`, so the
host replay applies the same patches.
"""
import random

import harness
from harness import EDGE, HarnessFault, SCRATCH_BASE, STACK_TOP

WIN = SCRATCH_BASE + 0x100
WIN_SIZE = 0x400


def _ptr(rng):
    return (WIN + 0x40 + rng.randrange(0x2C0)) & ~1


def _value(rng):
    p = rng.random()
    if p < 0.35:
        return rng.choice(EDGE)
    if p < 0.6:
        return rng.getrandbits(32)
    if p < 0.8:
        return rng.randrange(256)
    return (rng.choice(EDGE) + rng.choice((-1, 1))) & 0xFFFFFFFF


def _window(rng):
    out = bytearray()
    for _ in range(WIN_SIZE // 4):
        p = rng.random()
        if p < 0.45:
            out += _ptr(rng).to_bytes(4, "big")
        elif p < 0.75:
            out += rng.randrange(0x200).to_bytes(4, "big")
        else:
            out += rng.getrandbits(32).to_bytes(4, "big")
    return bytes(out)


def fuzz(h, label, n, seed, sym_addrs, max_tries_factor=10, stuck_after=300):
    """-> (cases, skipped).  sym_addrs: addresses of globals the routine references."""
    rng = random.Random(seed * 7919 + 13)
    cases, skipped, tries = [], {}, 0
    while len(cases) < n and tries < n * max_tries_factor:
        if not cases and tries >= stuck_after:
            break
        tries += 1
        a = [(_ptr(rng) if rng.random() < 0.8 else _value(rng)) for _ in range(7)] + [STACK_TOP]
        regs = {"d": [_value(rng) for _ in range(8)], "a": a, "ccr": rng.randrange(32)}
        mem = [(WIN, _window(rng))]
        for addr in sym_addrs:
            cell = bytearray()
            for _ in range(8):
                p = rng.random()
                cell += (_ptr(rng) if p < 0.6 else rng.randrange(0x100) if p < 0.8 else rng.getrandbits(32)).to_bytes(4, "big")
            mem.append((addr, bytes(cell)))
        try:
            cases.append(h.record(label, regs, mem))
        except HarnessFault as e:
            skipped[e.kind] = skipped.get(e.kind, 0) + 1
    return cases, skipped


def guard(h):
    """unicorn's DIVS helper dies with a host 'integer overflow' (OSError) on INT_MIN / -1 instead of
    raising the CPU's overflow flag.  Turn that into a skipped (faulting) input so one bad
    case does not abort fuzzing.  Idempotent; returns h."""
    if getattr(h, "_lift_guarded", False):
        return h
    orig = h.record

    def record(label, regs_in, mem_in=()):
        try:
            return orig(label, regs_in, mem_in)
        except OSError as e:
            raise HarnessFault("unicorn", str(e))

    h.record = record
    # Two harness-only regions exist in unicorn but not in the host replay: the 3-word RTS stub
    # page at SENTINEL and the zero padding between the image end and its page boundary.  Code
    # that reads/writes there is poking harness internals, not the program -- make it a fault.
    from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE

    def on_artifact(uc, access, addr, size, value, _):
        if addr == harness.STUB_SLOT:       # the stub's own MOVE SR,(slot)
            return
        h._fail("unmapped", "access to harness-only memory $%08X" % addr)

    end = harness.IMAGE_BASE + len(h.image)
    padded = (end + harness.PAGE - 1) & ~(harness.PAGE - 1)
    h.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, on_artifact,
                  begin=harness.SENTINEL, end=harness.SENTINEL + harness.PAGE - 1)
    if padded > end:
        h.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, on_artifact, begin=end, end=padded - 1)
    h._lift_guarded = True
    return h
