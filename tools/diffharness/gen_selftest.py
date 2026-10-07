#!/usr/bin/env python3
"""Generate cases that validate include/ms/regs.hpp flag helpers against unicorn.

Each label SELF_<OP>_<B|W|L> is a one-instruction routine (opcode + RTS written
into scratch RAM, operands D0 (src/count) and D1 (dst)).  The matching C++ is
src/lifted/nb/selftest_ops.cpp.  Cases land in tests/diff/nb/ and replay
through run_host.py like any lifted routine.

    py tools/diffharness/gen_selftest.py [--n 400] [--seed 7]
"""
import argparse
import random

import harness as H

SZ = {"B": 0, "W": 1, "L": 2}
# name -> opcode builder (ss = size bits).  D0 = source/count, D1 = destination.
OPS = {
    "ADD": lambda ss: 0xD200 | ss << 6,
    "SUB": lambda ss: 0x9200 | ss << 6,
    "CMP": lambda ss: 0xB200 | ss << 6,
    "AND": lambda ss: 0xC200 | ss << 6,
    "OR": lambda ss: 0x8200 | ss << 6,
    "EOR": lambda ss: 0xB101 | ss << 6,
    "ADDX": lambda ss: 0xD300 | ss << 6,
    "SUBX": lambda ss: 0x9300 | ss << 6,
    "NEG": lambda ss: 0x4401 | ss << 6,
    "NEGX": lambda ss: 0x4001 | ss << 6,
    "NOT": lambda ss: 0x4601 | ss << 6,
    "TST": lambda ss: 0x4A01 | ss << 6,
    # register-count shifts: 1110 ccc d ss 1 tt rrr  (count D0, data D1)
    "ASL": lambda ss: 0xE121 | ss << 6 | 0 << 3,
    "ASR": lambda ss: 0xE021 | ss << 6 | 0 << 3,
    "LSL": lambda ss: 0xE121 | ss << 6 | 1 << 3,
    "LSR": lambda ss: 0xE021 | ss << 6 | 1 << 3,
    "ROXL": lambda ss: 0xE121 | ss << 6 | 2 << 3,
    "ROXR": lambda ss: 0xE021 | ss << 6 | 2 << 3,
    "ROL": lambda ss: 0xE121 | ss << 6 | 3 << 3,
    "ROR": lambda ss: 0xE021 | ss << 6 | 3 << 3,
}
WORD_ONLY = {"MULU": 0xC2C0, "MULS": 0xC3C0}   # MULU/MULS D0,D1


def labels():
    for op in OPS:
        for sz in SZ:
            yield "SELF_%s_%s" % (op, sz), OPS[op](SZ[sz])
    for op, opc in WORD_ONLY.items():
        yield "SELF_%s_W" % op, opc


def rand_regs(rng):
    def v():
        p = rng.random()
        if p < 0.4:
            return rng.choice(H.EDGE)
        if p < 0.55:
            return rng.randrange(0, 72)       # shift counts incl. >= width
        return rng.getrandbits(32)
    d = [v() for _ in range(8)]
    return {"d": d, "a": [0] * 7 + [H.STACK_TOP], "ccr": rng.randrange(32)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=120)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()
    h = H.Harness("nb")
    total = 0
    for label, opc in labels():
        rng = random.Random("%s/%d" % (label, args.seed))
        code = opc.to_bytes(2, "big") + bytes.fromhex("4E75")
        cases = []
        for _ in range(args.n):
            regs = rand_regs(rng)
            res = h.run(H.SCRATCH_BASE, regs, [(H.SCRATCH_BASE, code)])
            cases.append({"regs_in": regs,
                          "mem_in": [{"addr": H.SCRATCH_BASE, "data": code.hex()}],
                          "regs_out": dict(res.regs, ccr=res.ccr),
                          "writes": [{"addr": a, "data": d.hex()} for a, d in res.writes]})
        H.write_cases(h, label, cases, args.seed, entry=H.SCRATCH_BASE)
        total += len(cases)
    print("wrote %d self-test cases" % total)


if __name__ == "__main__":
    main()
