#!/usr/bin/env python3
"""Differential harness core: run one routine of the reassembled Moonstone
hunk executables in unicorn (M68K, CPU model 68000) and record the result.

    from harness import Harness
    h = Harness("mog")                       # build/reasm/mog + .symbols.json
    out = h.run("LAB_03CA", regs_in, mem_patches=[(addr, b"..")])
    out.regs, out.ccr, out.writes            # writes = [(addr, bytes)] coalesced

Conventions (shared with include/ms/regs.hpp):
  * The routine is entered as if by JSR: a sentinel return address sits on the
    stack and execution stops when RTS pops it.  regs_in["a"][7] is the SP the
    routine sees *after* that pop (default STACK_TOP); regs_out a[7] is the SP
    after the RTS, so a balanced routine has a[7] unchanged.
  * "ccr" is SR & 0x1F.  The CPU runs supervisor, IPL 7 (SR=0x2700|ccr).
  * Memory map (all zero-filled at every run): the relocated hunk image at
    IMAGE_BASE, stack, a scratch RAM window, plus caller extra_regions.
  * Any access to $DFFxxx / $BFxxxx (custom chips / CIAs), any unmapped
    read/write/fetch, an instruction limit overrun, a CPU exception or fetching
    an instruction this run has written (self-modifying code, which the lifted
    C++ cannot follow; e.g. mog LAB_0E85 indexing LAB_0E8C with a negative
    channel) raises HarnessFault; fuzz generation skips such inputs.

CLI:  py harness.py gen <binary> <LABEL> [--n 2000] [--seed 1] [--ptr-aregs]
      py harness.py list-hunks <binary>
"""
from __future__ import annotations

import argparse
import hashlib
import json
import random
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

from unicorn import Uc, UcError, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
from unicorn import (UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED,
                     UC_HOOK_MEM_FETCH_UNMAPPED, UC_HOOK_MEM_WRITE,
                     UC_HOOK_MEM_READ, UC_HOOK_INTR, UC_HOOK_INSN_INVALID,
                     UC_HOOK_CODE)
from unicorn import m68k_const as M

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'reference/moonshard/tools'))
from moghunks import parse_hunk_file  # noqa: E402

# Memory map ----------------------------------------------------------------
IMAGE_BASE = 0x00100000
STACK_BASE = 0x00F00000
STACK_SIZE = 0x00010000
STACK_TOP = STACK_BASE + STACK_SIZE - 0x100        # SP seen by the routine
SCRATCH_BASE = 0x00800000
SCRATCH_SIZE = 0x00001000
SENTINEL = 0x00F80000          # RTS target: a 3-word stub, see Harness.run
STUB_CODE = bytes.fromhex("40F9") + (SENTINEL + 0x10).to_bytes(4, "big") + bytes.fromhex("4E71")
STUB_END = SENTINEL + 6        # emulation stops here (after MOVE SR,(slot).L)
STUB_SLOT = SENTINEL + 0x10
CPU_MODEL = M.UC_CPU_M68K_M68000
PAGE = 0x1000

DREG = [M.UC_M68K_REG_D0 + i for i in range(8)]
AREG = [M.UC_M68K_REG_A0 + i for i in range(8)]
EDGE = [0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000,
        0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF]


class HarnessFault(Exception):
    """Original code did something the harness forbids (hardware, unmapped...)."""

    def __init__(self, kind, detail):
        super().__init__("%s: %s" % (kind, detail))
        self.kind, self.detail = kind, detail


@dataclass
class Result:
    regs: dict            # {"d": [8], "a": [8]}
    ccr: int
    writes: list          # [(addr, bytes)] coalesced dirty memory, final values


def _round(n):
    return (n + PAGE - 1) & ~(PAGE - 1)


def load_image(exe_path, base=IMAGE_BASE):
    """Relocate a hunk exe to `base`. Returns (image bytes, hunk_bases list)."""
    hf = parse_hunk_file(Path(exe_path).read_bytes())
    blocks = {b.index: b for b in hf.hunks}
    bases, total = [], 0
    for i in range(hf.table_size):
        bases.append(base + total)
        total += blocks[i].size_bytes
    img = bytearray(total)
    for i in range(hf.table_size):
        b = blocks[i]
        off = bases[i] - base
        if b.data is not None:
            img[off:off + len(b.data)] = b.data
        for grp in b.reloc32:
            tb = bases[grp.target_hunk]
            for o in grp.offsets:
                p = off + o
                v = struct.unpack(">I", img[p:p + 4])[0]
                img[p:p + 4] = struct.pack(">I", (v + tb) & 0xFFFFFFFF)
    return bytes(img), bases


class Harness:
    def __init__(self, binary, exe=None, symbols=None, insn_limit=2_000_000,
                 extra_regions=()):
        self.binary = binary
        self.exe = Path(exe) if exe else ROOT / "build" / "reasm" / binary
        sym = Path(symbols) if symbols else Path(str(self.exe) + ".symbols.json")
        self.symbols = json.loads(sym.read_text()) if sym.exists() else {}
        self.image, self.hunk_bases = load_image(self.exe)
        self.image_sha1 = hashlib.sha1(self.image).hexdigest()
        self.insn_limit = insn_limit
        self.regions = [(IMAGE_BASE, _round(len(self.image))),
                        (STACK_BASE, STACK_SIZE),
                        (SCRATCH_BASE, SCRATCH_SIZE),
                        (SENTINEL, PAGE)] + list(extra_regions)
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(CPU_MODEL)   # M68000 (not the default 'any')
        for base, size in self.regions:
            self.uc.mem_map(base, size)
        self._fault = None
        self._writes = []
        self._img_written = set()       # image byte addresses written this run
        self.uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
                         UC_HOOK_MEM_FETCH_UNMAPPED, self._on_unmapped)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._on_write)
        # Hardware ranges are unmapped, but guard explicitly in case a region
        # is ever mapped over them.
        self.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self._on_hw,
                         begin=0xDFF000, end=0xDFFFFF)
        self.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self._on_hw,
                         begin=0xBF0000, end=0xBFFFFF)
        self.uc.hook_add(UC_HOOK_INTR, self._on_intr)

    # -- hooks ---------------------------------------------------------------
    def _fail(self, kind, detail):
        if self._fault is None:
            self._fault = (kind, detail)
        self.uc.emu_stop()

    @staticmethod
    def _is_hw(addr):
        a = addr & 0xFFFFFF
        return 0xDFF000 <= a <= 0xDFFFFF or 0xBF0000 <= a <= 0xBFFFFF

    def _on_unmapped(self, uc, access, addr, size, value, _):
        what = {19: "read", 20: "write", 21: "fetch"}.get(access, "access %d" % access)
        kind = "hardware" if self._is_hw(addr) else "unmapped"
        self._fail(kind, "%s of %d byte(s) at $%08X (pc=$%08X)" %
                   (what, size, addr, uc.reg_read(M.UC_M68K_REG_PC)))
        return False

    def _on_hw(self, uc, access, addr, size, value, _):
        self._fail("hardware", "access at $%08X size %d (pc=$%08X)" %
                   (addr, size, uc.reg_read(M.UC_M68K_REG_PC)))

    def _on_write(self, uc, access, addr, size, value, _):
        if SENTINEL <= addr < SENTINEL + PAGE:      # harness-internal CCR slot
            return
        self._writes.append((addr & 0xFFFFFFFF, size))
        if IMAGE_BASE <= addr < IMAGE_BASE + len(self.image):
            self._img_written.update(range(addr, addr + size))

    def _on_code(self, uc, addr, size, _):
        if self._img_written and any(b in self._img_written for b in range(addr, addr + size)):
            self._fail("selfmod", "fetch of written code at $%08X" % addr)

    def _on_intr(self, uc, intno, _):
        self._fail("exception", "vector/intno %d at pc=$%08X" %
                   (intno, uc.reg_read(M.UC_M68K_REG_PC)))

    # -- API ---------------------------------------------------------------------
    def address(self, label):
        s = self.symbols[label]
        return self.hunk_bases[s["hunk"]] + s["offset"]

    def run(self, label_or_addr, regs_in, mem_patches=()):
        """Execute until RTS to the sentinel. Returns Result; raises HarnessFault."""
        res = self._run(label_or_addr, regs_in, mem_patches)
        if self._img_written:
            # The run wrote into the image: replay it once with a per-instruction fetch check
            # (selfmod).  Not armed by default: with a Python code hook installed, unicorn's
            # host 'integer overflow' on DIVS INT_MIN/-1 kills the process instead of raising.
            hk = self.uc.hook_add(UC_HOOK_CODE, self._on_code,
                                  begin=IMAGE_BASE, end=IMAGE_BASE + len(self.image) - 1)
            try:
                res = self._run(label_or_addr, regs_in, mem_patches)
            finally:
                self.uc.hook_del(hk)
        return res

    def _run(self, label_or_addr, regs_in, mem_patches):
        entry = (self.address(label_or_addr) if isinstance(label_or_addr, str)
                 else label_or_addr)
        uc = self.uc
        for base, size in self.regions:
            uc.mem_write(base, bytes(size))
        uc.mem_write(IMAGE_BASE, self.image)
        uc.mem_write(SENTINEL, STUB_CODE)
        for addr, data in mem_patches:
            uc.mem_write(addr, bytes(data))
        # unicorn quirk: mem_write() does not invalidate translated blocks, so
        # code patched/restored here would otherwise execute stale from the
        # previous run.  Flush every run.
        uc.ctl_flush_tb()
        # SR first: switching privilege state swaps A7 with ISP/USP, so A7 must
        # be written after SR is already supervisor.
        uc.reg_write(M.UC_M68K_REG_SR, 0x2700 | (regs_in.get("ccr", 0) & 0x1F))
        d = regs_in["d"]
        a = list(regs_in["a"])
        sp = a[7]
        if not (STACK_BASE + 16 <= sp <= STACK_BASE + STACK_SIZE - 16):
            raise HarnessFault("setup", "a7=$%08X outside stack region" % sp)
        for i in range(8):
            uc.reg_write(DREG[i], d[i])
            uc.reg_write(AREG[i], a[i] if i != 7 else sp - 4)
        uc.mem_write(sp - 4, struct.pack(">I", SENTINEL))
        self._fault, self._writes = None, []
        self._img_written.clear()
        err = None
        try:
            uc.emu_start(entry, STUB_END, count=self.insn_limit)
        except UcError as e:
            err = e
        pc = uc.reg_read(M.UC_M68K_REG_PC)
        if self._fault:
            raise HarnessFault(*self._fault)
        if err is not None:
            raise HarnessFault("cpu", "%s at pc=$%08X" % (err, pc))
        if pc != STUB_END:
            raise HarnessFault("limit", "stopped at pc=$%08X after %d instructions "
                               "without returning" % (pc, self.insn_limit))
        regs = {"d": [uc.reg_read(r) for r in DREG],
                "a": [uc.reg_read(r) for r in AREG]}
        # unicorn quirk: reg_read(SR) does not flush the lazily-evaluated N/Z/C
        # flags (CMP/ADD/SUB report only X/V).  The stub at SENTINEL executes a
        # real MOVE SR,(slot) which does, so read the CCR from there.
        ccr = struct.unpack(">H", bytes(uc.mem_read(STUB_SLOT, 2)))[0] & 0x1F
        return Result(regs, ccr, self._coalesce())

    def _coalesce(self):
        addrs = sorted({a + i for a, sz in self._writes for i in range(sz)})
        runs = []
        for x in addrs:
            if runs and runs[-1][1] == x:
                runs[-1][1] = x + 1
            else:
                runs.append([x, x + 1])
        return [(s, bytes(self.uc.mem_read(s, e - s))) for s, e in runs]

    # -- case files ------------------------------------------------------------------
    def header(self, label, entry=None):
        entry = self.address(label) if entry is None else entry
        return {"binary": self.binary, "label": label, "cpu": "m68000",
                "entry": "$%08X" % entry,
                "image": {"file": "build/diff/%s.img" % self.binary,
                          "base": IMAGE_BASE, "size": len(self.image),
                          "sha1": self.image_sha1, "hunk_bases": self.hunk_bases},
                "stack": {"base": STACK_BASE, "size": STACK_SIZE},
                "scratch": {"base": SCRATCH_BASE, "size": SCRATCH_SIZE}}

    def write_image(self):
        p = ROOT / "build" / "diff" / (self.binary + ".img")
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(self.image)
        return p

    def record(self, label, regs_in, mem_in=()):
        res = self.run(label, regs_in, mem_in)
        return {"regs_in": regs_in,
                "mem_in": [{"addr": a, "data": bytes(d).hex()} for a, d in mem_in],
                "regs_out": dict(res.regs, ccr=res.ccr),
                "writes": [{"addr": a, "data": d.hex()} for a, d in res.writes]}

    def fuzz(self, label, n, seed=1, ptr_aregs=False, max_tries_factor=20):
        """Record n seeded random cases; faulting inputs are discarded (counted)."""
        rng = random.Random(seed)
        cases, skipped = [], {}
        tries = 0
        while len(cases) < n and tries < n * max_tries_factor:
            tries += 1
            regs = {"d": [], "a": [], "ccr": rng.randrange(32)}
            vals = []
            for _ in range(15):
                p = rng.random()
                if p < 0.35:
                    v = rng.choice(EDGE)
                elif p < 0.60:
                    v = rng.getrandbits(32)
                elif p < 0.75:
                    v = rng.randrange(256)
                elif p < 0.85 and vals:
                    v = rng.choice(vals)           # equal pairs
                else:
                    v = (rng.choice(EDGE) + rng.choice((-1, 1))) & 0xFFFFFFFF
                vals.append(v)
            regs["d"] = vals[:8]
            regs["a"] = vals[8:15] + [STACK_TOP]
            mem_in = []
            if ptr_aregs:
                mem_in.append((SCRATCH_BASE + 0x100,
                               bytes(rng.getrandbits(8) for _ in range(0x200))))
                regs["a"] = [SCRATCH_BASE + 0x100 + rng.randrange(0x1F0) for _ in range(7)] + [STACK_TOP]
            try:
                cases.append(self.record(label, regs, mem_in))
            except HarnessFault as e:
                skipped[e.kind] = skipped.get(e.kind, 0) + 1
        return cases, skipped


def case_path(binary, label):
    return ROOT / "tests" / "diff" / binary / (label + ".json")


def write_cases(h, label, cases, seed=None, entry=None):
    doc = h.header(label, entry)
    doc["seed"] = seed
    doc["cases"] = cases
    p = case_path(h.binary, label)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(doc, indent=0) + "\n")
    h.write_image()
    return p


def cases_to_text(doc):
    """Flatten a case document to the line format read by replay_main.cpp."""
    def regs(r):
        return "d %s a %s ccr %x" % (" ".join("%x" % v for v in r["d"]),
                                     " ".join("%x" % v for v in r["a"]), r["ccr"])
    out = ["image %s %x %x" % (doc["image"]["file"], doc["image"]["base"],
                                doc["image"]["size"]),
           "stack %x %x" % (doc["stack"]["base"], doc["stack"]["size"]),
           "scratch %x %x" % (doc["scratch"]["base"], doc["scratch"]["size"])]
    for i, c in enumerate(doc["cases"]):
        out.append("case %d" % i)
        out.append("in " + regs(c["regs_in"]))
        for m in c["mem_in"]:
            out.append("mem %x %s" % (m["addr"], m["data"]))
        out.append("out " + regs(c["regs_out"]))
        for w in c["writes"]:
            out.append("w %x %s" % (w["addr"], w["data"]))
        out.append("end")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gen")
    g.add_argument("binary"); g.add_argument("label")
    g.add_argument("--n", type=int, default=2000)
    g.add_argument("--seed", type=int, default=1)
    g.add_argument("--ptr-aregs", action="store_true",
                   help="point A0-A6 into a random-filled scratch window")
    lh = sub.add_parser("list-hunks"); lh.add_argument("binary")
    args = ap.parse_args()
    h = Harness(args.binary)
    if args.cmd == "list-hunks":
        for i, b in enumerate(h.hunk_bases):
            print(i, "$%08X" % b)
        return
    cases, skipped = h.fuzz(args.label, args.n, args.seed, args.ptr_aregs)
    p = write_cases(h, args.label, cases, args.seed)
    print("wrote %d cases to %s (skipped faulting inputs: %s)" % (len(cases), p, skipped or "none"))


if __name__ == "__main__":
    main()
