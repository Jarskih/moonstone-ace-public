"""lift_emit.py -- per-opcode C++ emission for tools/lift.py.

One method per opcode family, each producing a single C++ statement (or a braced
block) that follows the 68000 semantics exactly.  The ALU/flag maths is the
hand-written ms::add/sub/cmp/... in include/ms/regs.hpp plus ms::lift::* from the
generated lift_ops.hpp.  Operands are evaluated in hardware order: the source EA
(with its (An)+ / -(An) side effect) and value first, then the destination EA.
Unknown opcodes/operand forms raise Unsupported -- no guessed code.
"""
from lift_parse import Unsupported, CONDS

def suffix(binary, label):
    """Registry suffix: text after `lab_` in the lifted function name (see tools/lift.py)."""
    label = label.replace("+", "_p")          # routine entered at LABEL+2 (callgraph name 'LAB_0426+2')
    return label if binary == "mog" else "%s_%s" % (binary, label)


MASK = {"B": "0xFFu", "W": "0xFFFFu"}
BITS = {"B": 8, "W": 16, "L": 32}
SETR = {"B": "setB", "W": "setW", "L": "setL"}
BIN_FN = {"ADD": "add", "ADDI": "add", "ADDQ": "add", "SUB": "sub", "SUBI": "sub", "SUBQ": "sub",
          "AND": "and_", "ANDI": "and_", "OR": "or_", "ORI": "or_", "EOR": "eor", "EORI": "eor"}
SHIFT_FN = {"ASL": "asl", "ASR": "asr", "LSL": "lsl", "LSR": "lsr", "ROL": "rol", "ROR": "ror",
            "ROXL": "roxl", "ROXR": "roxr"}
UNSUPPORTED_OPS = {"TRAP", "TRAPV", "CHK", "RTE", "RTR", "RTD", "STOP", "RESET", "ILLEGAL", "MOVEP",
                   "ABCD", "SBCD", "NBCD", "DC", "DS"}


class Emitter:
    def __init__(self, prog, rname, insns, labels, lifted, allow_calls):
        self.prog, self.bin = prog, prog.binary
        self.rname = rname
        self.r = prog.routines[rname]
        self.insns = insns
        self.labels = labels                        # asm label -> offset
        self.lifted = lifted                        # routine names with a lifted function
        self.allow_calls = allow_calls
        self.starts = {i.off for i in insns}
        self.uses_sym = False
        self.sym_refs = set()                       # (label, addend) of every global named
        self.callees = []                           # routine names called via lifted fns
        self.asm_calls = []                         # raw addresses sent through callAsm
        self.used_labels = set()
        self.lab_of = {}                            # offset -> C label
        for name, off in sorted(labels.items(), key=lambda kv: kv[0]):
            self.lab_of.setdefault(off, name)
        self.has_mem_an = any(op.kind in ("ind", "post", "pre", "disp", "idx") for i in insns for op in i.ops)
        self.out = []
        # A routine that touches the stack (A7 operands, PEA/LINK/UNLK, calls) must see the same
        # SP the original sees: regs.hpp passes a[7] *after* the return-address pop, the original
        # runs with a[7] pointing at the return-address slot.  Emit `a7 -= 4` on entry and
        # `a7 += 4` on RTS / before tail calls so every stack address matches the original.
        self.uses_stack = any(
            i.mnem in ("PEA", "LINK", "UNLK", "JSR", "BSR") or
            any(op.reg == 7 and op.kind in ("an", "ind", "post", "pre", "disp", "idx") for op in i.ops)
            for i in insns)

    # ------------------------------------------------------------------ helpers
    def fail(self, ins, key, msg=None):
        raise Unsupported(key, ins.line, msg or "unsupported: %s" % ins.text)

    def sz(self, ins, allowed=("B", "W", "L")):
        if ins.size not in allowed:
            self.fail(ins, ins.mnem, "missing/invalid size .%s: %s" % (ins.size, ins.text))
        return ins.size

    def addr_const(self, e, ins):
        if e.sym:
            self.uses_sym = True
            self.sym_refs.add((e.sym, e.add))
            s = "MS_SYM(%s, %s)" % (self.bin, e.sym)
            if e.add > 0:
                s += " + %du" % e.add
            elif e.add < 0:
                s += " - %du" % -e.add
            return s
        return "0x%08Xu" % (e.add & 0xFFFFFFFF)

    def ret_addr(self, ins):
        self.uses_sym = True
        return "MS_HUNK(%s, %02X) + 0x%Xu" % (self.bin, ins.hunk, ins.off + ins.length)

    @staticmethod
    def sx16(v):
        return v - 0x10000 if v & 0x8000 else v

    def num_disp(self, op, ins, bits):
        e = op.expr
        if e.sym:
            self.fail(ins, "symbolic displacement", "symbolic displacement: %s" % ins.text)
        d = e.add
        lim = 1 << (bits - 1)
        if not -lim <= d < lim:
            self.fail(ins, "displacement range", "displacement out of range: %s" % ins.text)
        return d

    @staticmethod
    def addsub(base, d):
        if d == 0:
            return base
        return "%s %s %du" % (base, "+" if d > 0 else "-", abs(d))

    def idx_expr(self, op):
        isa, n, sz = op.idx
        reg = "R.a[%d]" % n if isa else "R.d[%d]" % n
        return "sext16(%s)" % reg if sz == "W" else reg

    def step(self, op, size):
        return 2 if (size == "B" and op.reg == 7) else (1 if size == "B" else 2 if size == "W" else 4)

    def mem_setup(self, op, ins, size, name):
        """-> (lines, address expression) for a memory operand (side effects included)."""
        k = op.kind
        if k == "ind":
            return ["uint32_t %s = R.a[%d];" % (name, op.reg)], name
        if k == "post":
            return ["uint32_t %s = R.a[%d]; R.a[%d] += %du;" % (name, op.reg, op.reg, self.step(op, size))], name
        if k == "pre":
            return ["R.a[%d] -= %du; uint32_t %s = R.a[%d];" % (op.reg, self.step(op, size), name, op.reg)], name
        if k == "disp":
            d = self.num_disp(op, ins, 16)
            return ["uint32_t %s = %s;" % (name, self.addsub("R.a[%d]" % op.reg, d))], name
        if k == "idx":
            d = self.num_disp(op, ins, 8)
            return ["uint32_t %s = %s;" % (name, self.addsub("R.a[%d] + %s" % (op.reg, self.idx_expr(op)), d))], name
        if k == "absl":
            return [], self.addr_const(op.expr, ins)
        if k == "absw":
            if op.expr.sym:
                self.fail(ins, "abs.W symbol")
            return [], "0x%08Xu" % (self.sx16(op.expr.add & 0xFFFF) & 0xFFFFFFFF)
        if k == "pcd":
            if not op.expr.sym:
                self.fail(ins, "pc-relative numeric")
            return [], self.addr_const(op.expr, ins)
        if k == "pcidx":
            if not op.expr.sym:
                self.fail(ins, "pc-relative numeric")
            return ["uint32_t %s = %s + %s;" % (name, self.addr_const(op.expr, ins), self.idx_expr(op))], name
        self.fail(ins, "operand", "not a memory operand: %s" % op.text)

    def imm(self, op, ins, size):
        e = op.expr
        if e.sym:
            if size != "L":
                self.fail(ins, "symbolic immediate", "symbolic immediate not long: %s" % ins.text)
            return self.addr_const(e, ins)
        v = e.add
        if size == "B":
            return "0x%02Xu" % (v & 0xFF)
        if size == "W":
            return "0x%04Xu" % (v & 0xFFFF)
        return "0x%08Xu" % (v & 0xFFFFFFFF)

    def rd(self, op, ins, size, ea=None):
        k = op.kind
        if k == "dn":
            r = "R.d[%d]" % op.reg
            return r if size == "L" else "(%s & %s)" % (r, MASK[size])
        if k == "an":
            if size == "B":
                self.fail(ins, "byte An", "byte access of address register: %s" % ins.text)
            r = "R.a[%d]" % op.reg
            return r if size == "L" else "(%s & %s)" % (r, MASK[size])
        if k == "imm":
            return self.imm(op, ins, size)
        if op.is_mem():
            return "M.r%d(%s)" % (BITS[size], ea)
        self.fail(ins, "operand", "cannot read operand %s" % op.text)

    def wr(self, op, ins, size, ea, val):
        k = op.kind
        if k == "dn":
            return "%s(R.d[%d], %s);" % (SETR[size], op.reg, val)
        if k == "an":
            if size != "L":
                self.fail(ins, "An write", "sized write to address register: %s" % ins.text)
            return "R.a[%d] = %s;" % (op.reg, val)
        if op.is_mem():
            cast = {"B": "(uint8_t)", "W": "(uint16_t)", "L": ""}[size]
            return "M.w%d(%s, %s(%s));" % (BITS[size], ea, cast, val)
        self.fail(ins, "operand", "cannot write operand %s" % op.text)

    def src_value(self, op, ins, size, lines, name="ea0"):
        """Evaluate a source operand; returns an expression usable after later setups."""
        if op.is_mem():
            su, ea = self.mem_setup(op, ins, size, name)
            lines += su
            lines.append("uint32_t s = %s;" % self.rd(op, ins, size, ea))
            return "s"
        return self.rd(op, ins, size)

    def dst_place(self, op, ins, size, lines, name="ea1"):
        if op.is_mem():
            su, ea = self.mem_setup(op, ins, size, name)
            lines += su
            return ea
        return None

    def put(self, ins, lines, extra=""):
        decl = any("uint32_t " in l for l in lines)
        text = " ".join(lines)
        if decl or len(lines) > 1:
            text = "{ %s }" % text
        com = "// %s%s" % (ins.text, extra)
        if len(text) > 100:
            self.out.append("    {   " + com)
            for l in lines:
                self.out.append("        " + l)
            self.out.append("    }")
        else:
            self.out.append("    %s  %s" % (text, com))

    # ------------------------------------------------------------------ branch targets
    def resolve(self, op, ins):
        if op.kind not in ("absl", "pcd") or not op.expr.sym:
            self.fail(ins, "%s (indirect)" % ins.mnem, "indirect/numeric branch target: %s" % ins.text)
        h, off = self.prog.label_addr(op.expr)
        return h, off

    def local_label(self, off):
        if off in self.lab_of:
            name = self.lab_of[off]
        else:
            name = "X_%X" % off
        self.used_labels.add(off)
        self.lab_of.setdefault(off, name)
        return name

    def call_code(self, h, off, ins, push):
        """Statements that run the code at (h, off) as a subroutine; None entry => hook."""
        name = self.prog.entry_at.get((h, off))
        pre = ("M.w32(R.a[7] - 4u, %s); " % self.ret_addr(ins)) if push else ""
        if name is not None and name in self.lifted:
            if name not in self.callees:
                self.callees.append(name)
            return "%slab_%s(R, M);" % (pre, suffix(self.bin, name))
        if not self.allow_calls:
            self.fail(ins, "call to non-lifted routine",
                      "call to non-lifted %s (use --allow-calls): %s" % (name or "$%X" % off, ins.text))
        self.uses_sym = True
        self.asm_calls.append((h, off))
        return "%slift::callAsm(R, M, MS_HUNK(%s, %02X) + 0x%Xu);" % (pre, self.bin, h, off)

    def pop_ret(self):
        return "R.a[7] += 4u; " if self.uses_stack else ""

    def branch_stmt(self, op, ins):
        """C++ statement transferring control (goto or tail call)."""
        h, off = self.resolve(op, ins)
        r = self.r
        if h == r["hunk"] and r["start"] <= off < r["end"]:
            if off not in self.starts:
                self.fail(ins, "branch into data", "branch target $%X is not an instruction: %s" % (off, ins.text))
            return "goto %s;" % self.local_label(off)
        return "{ %s%s return; }" % (self.pop_ret(), self.call_code(h, off, ins, False))

    # ------------------------------------------------------------------ dispatch
    def emit_insn(self, ins):
        m = ins.mnem
        if m in UNSUPPORTED_OPS:
            self.fail(ins, m)
        fn = getattr(self, "op_" + m, None)
        if fn is None:
            if m in BIN_FN:
                fn = self.op_bin
            elif m in SHIFT_FN:
                fn = self.op_shift
            else:
                self.fail(ins, m)
        fn(ins)

    def emit_all(self):
        for ins in self.insns:
            self.emit_insn(ins)

    # ------------------------------------------------------------------ data movement
    def op_MOVE(self, ins):
        if len(ins.ops) != 2:
            self.fail(ins, "MOVE")
        src, dst = ins.ops
        if src.kind in ("sr", "ccr", "usp") or dst.kind in ("sr", "ccr", "usp"):
            self.fail(ins, "MOVE sr/ccr/usp", "MOVE to/from SR/CCR/USP: %s" % ins.text)
        size = self.sz(ins)
        if dst.kind == "an":
            return self.op_MOVEA(ins)
        lines = []
        sv = self.src_value(src, ins, size, lines)
        ea = self.dst_place(dst, ins, size, lines)
        lines.append(self.wr(dst, ins, size, ea, "move<%s>(R, %s)" % (size, sv)))
        self.put(ins, lines)

    def op_MOVEA(self, ins):
        size = self.sz(ins, ("W", "L"))
        src, dst = ins.ops
        if dst.kind != "an":
            self.fail(ins, "MOVEA")
        lines = []
        sv = self.src_value(src, ins, size, lines)
        lines.append("R.a[%d] = %s;" % (dst.reg, "sext16(%s)" % sv if size == "W" else sv))
        self.put(ins, lines)

    def op_MOVEQ(self, ins):
        src, dst = ins.ops
        if src.kind != "imm" or src.expr.sym or dst.kind != "dn":
            self.fail(ins, "MOVEQ")
        v = src.expr.add & 0xFF
        self.put(ins, ["R.d[%d] = move<L>(R, 0x%08Xu);" % (dst.reg, (v | 0xFFFFFF00) if v & 0x80 else v)])

    def op_LEA(self, ins):
        src, dst = ins.ops
        if dst.kind != "an" or not src.is_mem() or src.kind in ("post", "pre"):
            self.fail(ins, "LEA")
        lines, ea = self.mem_setup(src, ins, "L", "ea0")
        lines.append("R.a[%d] = %s;" % (dst.reg, ea))
        self.put(ins, lines)

    def op_PEA(self, ins):
        (src,) = ins.ops
        if not src.is_mem() or src.kind in ("post", "pre"):
            self.fail(ins, "PEA")
        lines, ea = self.mem_setup(src, ins, "L", "ea0")
        lines.append("M.push32(R, %s);" % ea)
        self.put(ins, lines)

    def op_EXG(self, ins):
        a, b = ins.ops
        if a.kind not in ("dn", "an") or b.kind not in ("dn", "an"):
            self.fail(ins, "EXG")
        ra = "R.%s[%d]" % ("d" if a.kind == "dn" else "a", a.reg)
        rb = "R.%s[%d]" % ("d" if b.kind == "dn" else "a", b.reg)
        self.put(ins, ["uint32_t t = %s; %s = %s; %s = t;" % (ra, ra, rb, rb)])

    def op_SWAP(self, ins):
        (d,) = ins.ops
        if d.kind != "dn":
            self.fail(ins, "SWAP")
        r = "R.d[%d]" % d.reg
        self.put(ins, ["%s = tst<L>(R, (%s << 16) | (%s >> 16));" % (r, r, r)])

    def op_EXT(self, ins):
        (d,) = ins.ops
        size = self.sz(ins, ("W", "L"))
        if d.kind != "dn":
            self.fail(ins, "EXT")
        r = "R.d[%d]" % d.reg
        if size == "W":
            self.put(ins, ["setW(%s, tst<W>(R, sext8(%s)));" % (r, r)])
        else:
            self.put(ins, ["%s = tst<L>(R, sext16(%s));" % (r, r)])

    def op_ADDX(self, ins):
        self._addx(ins, "addx")

    def op_SUBX(self, ins):
        self._addx(ins, "subx")

    def _addx(self, ins, fn):
        size = self.sz(ins)
        src, dst = ins.ops
        if src.kind == "dn" and dst.kind == "dn":
            self.put(ins, ["%s(R.d[%d], %s<%s>(R, R.d[%d], %s));" % (SETR[size], dst.reg, fn, size, dst.reg, self.rd(src, ins, size))])
        elif src.kind == "pre" and dst.kind == "pre":
            l0, e0 = self.mem_setup(src, ins, size, "ea0")
            l1, e1 = self.mem_setup(dst, ins, size, "ea1")
            val = "%s<%s>(R, M.r%d(%s), M.r%d(%s))" % (fn, size, BITS[size], e1, BITS[size], e0)
            self.put(ins, l0 + l1 + [self.wr(dst, ins, size, e1, val)])
        else:
            self.fail(ins, ins.mnem)

    def op_NOP(self, ins):
        self.out.append("    ;  // NOP")

    def op_LINK(self, ins):
        a, imm = ins.ops
        if a.kind != "an" or a.reg == 7 or imm.kind != "imm" or imm.expr.sym:
            self.fail(ins, "LINK")
        d = self.sx16(imm.expr.add & 0xFFFF)
        self.put(ins, ["M.push32(R, R.a[%d]); R.a[%d] = R.a[7]; %s;" %
                       (a.reg, a.reg, "R.a[7] = %s" % self.addsub("R.a[7]", d))])

    def op_UNLK(self, ins):
        (a,) = ins.ops
        if a.kind != "an" or a.reg == 7:
            self.fail(ins, "UNLK")
        self.put(ins, ["R.a[7] = R.a[%d]; R.a[%d] = M.pop32(R);" % (a.reg, a.reg)])

    def op_MOVEM(self, ins):
        size = self.sz(ins, ("W", "L"))
        src, dst = ins.ops
        step = 2 if size == "W" else 4

        def regs_of(op):
            if op.kind == "reglist":
                return list(op.regs)
            if op.kind == "dn":
                return [("D", op.reg)]
            if op.kind == "an":
                return [("A", op.reg)]
            self.fail(ins, "MOVEM", "bad register list: %s" % ins.text)

        def reg(t):
            return "R.%s[%d]" % ("d" if t[0] == "D" else "a", t[1])

        lines = []
        if dst.is_mem():                                   # registers -> memory
            regs = regs_of(src)
            if dst.kind == "pre":
                if any(t == ("A", dst.reg) for t in regs):
                    self.fail(ins, "MOVEM An in own predec list")
                order = sorted(regs, key=lambda t: (0 if t[0] == "A" else 1, -t[1]))   # A7..A0, D7..D0
                a = "R.a[%d]" % dst.reg
                for t in order:
                    val = "(uint16_t)%s" % reg(t) if size == "W" else reg(t)
                    lines.append("%s -= %du; M.w%d(%s, %s);" % (a, step, BITS[size], a, val))
            elif dst.kind in ("post", "absl", "absw", "pcd", "pcidx") and dst.kind in ("post", "pcd", "pcidx"):
                self.fail(ins, "MOVEM dst mode", "unsupported MOVEM destination: %s" % ins.text)
            else:
                su, ea = self.mem_setup(dst, ins, size, "ea")
                if not su:
                    su = ["uint32_t ea = %s;" % ea]
                    ea = "ea"
                lines += su
                order = sorted(regs, key=lambda t: (0 if t[0] == "D" else 1, t[1]))
                for t in order:
                    val = "(uint16_t)%s" % reg(t) if size == "W" else reg(t)
                    lines.append("M.w%d(ea, %s); ea += %du;" % (BITS[size], val, step))
        elif src.is_mem():                                 # memory -> registers
            regs = regs_of(dst)
            order = sorted(regs, key=lambda t: (0 if t[0] == "D" else 1, t[1]))
            if src.kind == "pre":
                self.fail(ins, "MOVEM src mode", "unsupported MOVEM source: %s" % ins.text)
            if src.kind == "post":
                lines.append("uint32_t ea = R.a[%d];" % src.reg)
            else:
                su, ea = self.mem_setup(src, ins, size, "ea")
                if not su:
                    su = ["uint32_t ea = %s;" % ea]
                lines += su
            for t in order:
                rv = "M.r16(ea)" if size == "W" else "M.r32(ea)"
                lines.append("%s = %s; ea += %du;" % (reg(t), "sext16(%s)" % rv if size == "W" else rv, step))
            if src.kind == "post":
                lines.append("R.a[%d] = ea;" % src.reg)
        else:
            self.fail(ins, "MOVEM", "MOVEM without memory operand: %s" % ins.text)
        self.put(ins, lines)

    # ------------------------------------------------------------------ arithmetic / logic
    def op_bin(self, ins):
        m = ins.mnem
        fn = BIN_FN[m]
        size = self.sz(ins)
        if len(ins.ops) != 2:
            self.fail(ins, m)
        src, dst = ins.ops
        if dst.kind == "an":
            if m in ("ADDQ", "SUBQ"):
                v = src.expr.add & 0xFFFFFFFF
                self.put(ins, ["R.a[%d] %s= %du;" % (dst.reg, "+" if m == "ADDQ" else "-", v)])
                return
            self.fail(ins, m, "%s to address register: %s" % (m, ins.text))
        lines = []
        sv = self.src_value(src, ins, size, lines)
        ea = self.dst_place(dst, ins, size, lines)
        lines.append(self.wr(dst, ins, size, ea, "%s<%s>(R, %s, %s)" % (fn, size, self.rd(dst, ins, size, ea), sv)))
        self.put(ins, lines)

    def op_ADDA(self, ins):
        self._adda(ins, "+")

    def op_SUBA(self, ins):
        self._adda(ins, "-")

    def _adda(self, ins, sign):
        size = self.sz(ins, ("W", "L"))
        src, dst = ins.ops
        if dst.kind != "an":
            self.fail(ins, ins.mnem)
        lines = []
        sv = self.src_value(src, ins, size, lines)
        lines.append("R.a[%d] %s= %s;" % (dst.reg, sign, "sext16(%s)" % sv if size == "W" else sv))
        self.put(ins, lines)

    def op_CMP(self, ins):
        size = self.sz(ins)
        src, dst = ins.ops
        if dst.kind != "dn":
            self.fail(ins, "CMP")
        lines = []
        sv = self.src_value(src, ins, size, lines)
        lines.append("cmp<%s>(R, %s, %s);" % (size, self.rd(dst, ins, size), sv))
        self.put(ins, lines)

    def op_CMPI(self, ins):
        size = self.sz(ins)
        src, dst = ins.ops
        lines = []
        sv = self.src_value(src, ins, size, lines)
        ea = self.dst_place(dst, ins, size, lines)
        lines.append("cmp<%s>(R, %s, %s);" % (size, self.rd(dst, ins, size, ea), sv))
        self.put(ins, lines)

    def op_CMPA(self, ins):
        size = self.sz(ins, ("W", "L"))
        src, dst = ins.ops
        if dst.kind != "an":
            self.fail(ins, "CMPA")
        lines = []
        sv = self.src_value(src, ins, size, lines)
        lines.append("cmp<L>(R, R.a[%d], %s);" % (dst.reg, "sext16(%s)" % sv if size == "W" else sv))
        self.put(ins, lines)

    def op_CMPM(self, ins):
        size = self.sz(ins)
        src, dst = ins.ops
        if src.kind != "post" or dst.kind != "post":
            self.fail(ins, "CMPM")
        l0, e0 = self.mem_setup(src, ins, size, "ea0")
        l1, e1 = self.mem_setup(dst, ins, size, "ea1")
        self.put(ins, l0 + l1 + ["cmp<%s>(R, M.r%d(%s), M.r%d(%s));" % (size, BITS[size], e1, BITS[size], e0)])

    def _unary(self, ins, expr_fn, setflags=False):
        size = self.sz(ins)
        (dst,) = ins.ops
        lines = []
        ea = self.dst_place(dst, ins, size, lines)
        lines.append(self.wr(dst, ins, size, ea, expr_fn(size, self.rd(dst, ins, size, ea))))
        self.put(ins, lines)

    def op_NEG(self, ins):
        self._unary(ins, lambda s, v: "neg<%s>(R, %s)" % (s, v))

    def op_NEGX(self, ins):
        self._unary(ins, lambda s, v: "negx<%s>(R, %s)" % (s, v))

    def op_NOT(self, ins):
        self._unary(ins, lambda s, v: "not_<%s>(R, %s)" % (s, v))

    def op_CLR(self, ins):
        size = self.sz(ins)
        (dst,) = ins.ops
        lines = []
        ea = self.dst_place(dst, ins, size, lines)
        lines.append(self.wr(dst, ins, size, ea, "0u"))
        lines.append("R.setNZVC(false, true, false, false);")
        self.put(ins, lines)

    def op_TST(self, ins):
        size = self.sz(ins)
        (src,) = ins.ops
        if src.kind == "an" and size == "B":
            self.fail(ins, "TST An")
        lines = []
        sv = self.src_value(src, ins, size, lines)
        lines.append("tst<%s>(R, %s);" % (size, sv))
        self.put(ins, lines)

    def op_TAS(self, ins):
        (dst,) = ins.ops
        lines = []
        ea = self.dst_place(dst, ins, "B", lines)
        lines.append(self.wr(dst, ins, "B", ea, "lift::tas(R, %s)" % self.rd(dst, ins, "B", ea)))
        self.put(ins, lines)

    def op_SCC(self, ins):
        (dst,) = ins.ops
        lines = []
        ea = self.dst_place(dst, ins, "B", lines)
        lines.append(self.wr(dst, ins, "B", ea, "cond(R, %s) ? 0xFFu : 0u" % ins.cond))
        self.put(ins, lines)

    def op_MULU(self, ins):
        self._mul(ins, "mulu")

    def op_MULS(self, ins):
        self._mul(ins, "muls")

    def _mul(self, ins, fn):
        src, dst = ins.ops
        if dst.kind != "dn":
            self.fail(ins, ins.mnem)
        lines = []
        sv = self.src_value(src, ins, "W", lines)
        lines.append("R.d[%d] = %s(R, R.d[%d], %s);" % (dst.reg, fn, dst.reg, sv))
        self.put(ins, lines)

    def op_DIVU(self, ins):
        self._div(ins, "divu")

    def op_DIVS(self, ins):
        self._div(ins, "divs")

    def _div(self, ins, fn):
        src, dst = ins.ops
        if dst.kind != "dn":
            self.fail(ins, ins.mnem)
        lines = []
        sv = self.src_value(src, ins, "W", lines)
        lines.append("if (!lift::%s(R, R.d[%d], %s)) { lift::zeroDivide(R, M); return; }" % (fn, dst.reg, sv))
        self.put(ins, lines)

    def op_shift(self, ins):
        fn = SHIFT_FN[ins.mnem]
        if len(ins.ops) == 2:
            size = self.sz(ins)
            cnt, dst = ins.ops
            if dst.kind != "dn":
                self.fail(ins, ins.mnem)
            if cnt.kind == "imm" and not cnt.expr.sym:
                c = "%du" % (cnt.expr.add if cnt.expr.add else 8)
            elif cnt.kind == "dn":
                c = "(R.d[%d] & 63u)" % cnt.reg
            else:
                self.fail(ins, ins.mnem, "bad shift count: %s" % ins.text)
            self.put(ins, ["%s(R.d[%d], %s<%s>(R, R.d[%d], %s));" % (SETR[size], dst.reg, fn, size, dst.reg, c)])
        else:
            (dst,) = ins.ops
            if not dst.is_mem():
                self.fail(ins, ins.mnem, "single-operand shift of register: %s" % ins.text)
            lines = []
            ea = self.dst_place(dst, ins, "W", lines)
            lines.append(self.wr(dst, ins, "W", ea, "%s<W>(R, %s, 1)" % (fn, self.rd(dst, ins, "W", ea))))
            self.put(ins, lines)

    def _bit(self, ins, fn, writes):
        bitop, dst = ins.ops
        if dst.kind == "dn":
            bits, size = 32, "L"
        elif dst.is_mem():
            bits, size = 8, "B"
        else:
            self.fail(ins, ins.mnem)
        if bitop.kind == "dn":
            b = "R.d[%d]" % bitop.reg
        elif bitop.kind == "imm" and not bitop.expr.sym:
            b = "%du" % (bitop.expr.add & 0xFF)
        else:
            self.fail(ins, ins.mnem)
        lines = []
        ea = self.dst_place(dst, ins, size, lines)
        cur = self.rd(dst, ins, size, ea)
        if writes:
            lines.append(self.wr(dst, ins, size, ea, "lift::%s<%d>(R, %s, %s)" % (fn, bits, cur, b)))
        else:
            lines.append("lift::btst<%d>(R, %s, %s);" % (bits, cur, b))
        self.put(ins, lines)

    def op_BTST(self, ins):
        self._bit(ins, "btst", False)

    def op_BSET(self, ins):
        self._bit(ins, "bset", True)

    def op_BCLR(self, ins):
        self._bit(ins, "bclr", True)

    def op_BCHG(self, ins):
        self._bit(ins, "bchg", True)

    # ------------------------------------------------------------------ control flow
    def op_RTS(self, ins):
        self.out.append("    %sreturn;  // RTS" % self.pop_ret())

    def op_BRA(self, ins):
        (t,) = ins.ops
        self.out.append("    %s  // %s" % (self.branch_stmt(t, ins), ins.text))

    def op_JMP(self, ins):
        (t,) = ins.ops
        self.out.append("    %s  // %s" % (self.branch_stmt(t, ins), ins.text))

    def op_BCC(self, ins):
        (t,) = ins.ops
        self.out.append("    if (cond(R, %s)) %s  // %s" % (ins.cond, self.branch_stmt(t, ins), ins.text))

    def op_DBCC(self, ins):
        dn, t = ins.ops
        if dn.kind != "dn":
            self.fail(ins, "DBcc")
        body = ("{ uint32_t t = (R.d[%d] - 1u) & 0xFFFFu; setW(R.d[%d], t); if (t != 0xFFFFu) %s }"
                % (dn.reg, dn.reg, self.branch_stmt(t, ins)))
        if ins.cond != "F":
            body = "if (!cond(R, %s)) %s" % (ins.cond, body)
        self.out.append("    %s  // %s" % (body, ins.text))

    def op_BSR(self, ins):
        (t,) = ins.ops
        h, off = self.resolve(t, ins)
        self.out.append("    %s  // %s" % (self.call_code(h, off, ins, True), ins.text))

    def op_JSR(self, ins):
        (t,) = ins.ops
        h, off = self.resolve(t, ins)
        self.out.append("    %s  // %s" % (self.call_code(h, off, ins, True), ins.text))

    # ------------------------------------------------------------------ whole routine
    def routine_body(self):
        """-> list of C++ lines (function body), labels inserted where targeted."""
        body = []
        self.out = body
        last = self.insns[-1]
        for ins in self.insns:
            mark = len(body)
            self.emit_insn(ins)
            body.insert(mark, "@LABEL %d" % ins.off)
        if last.mnem not in ("RTS", "BRA", "JMP"):
            end = self.r["end"]
            name = self.prog.entry_at.get((last.hunk, end))
            if name is None:
                raise Unsupported("fallthrough", last.line, "routine falls through into non-routine code")
            body.append("    %s  // fall through into %s" % (
                self.pop_ret() + self.call_code(last.hunk, end, last, False) + " return;", name))
        final = []
        for l in body:
            if l.startswith("@LABEL "):
                off = int(l.split()[1])
                if off in self.used_labels:
                    final.append("%s:;" % self.lab_of[off])
            else:
                final.append(l)
        return final
