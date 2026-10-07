"""lift_parse.py -- asm listing / operand parser for tools/lift.py.

Reads build/reasm/<bin>.asm (IRA/vasm-Devpac syntax), <bin>.lst (addresses and
byte lengths) and <bin>.symbols.json (label -> hunk/offset).  Everything that
cannot be understood raises Unsupported with the asm line number; nothing is
ever guessed.  The instruction byte length computed from the parsed operands is
checked against the listing for every instruction, so a parser/operand-size
mistake shows up as a loud error instead of wrong code.
"""
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REASM = ROOT / "build" / "reasm"
INVENTORY = ROOT / "build" / "inventory"


class Unsupported(Exception):
    """key = opcode/pattern used for the frequency ranking."""

    def __init__(self, key, line, msg):
        super().__init__("asm line %s: %s" % (line, msg))
        self.key, self.line, self.msg = key, line, msg


CONDS = ["T", "F", "HI", "LS", "CC", "CS", "NE", "EQ", "VC", "VS", "PL", "MI",
         "GE", "LT", "GT", "LE"]
COND_ALIAS = {"HS": "CC", "LO": "CS", "RA": "T"}
BASE_MNEMS = set("""ABCD ADD ADDA ADDI ADDQ ADDX AND ANDI ASL ASR BCHG BCLR BSET BTST BRA BSR CHK
CLR CMP CMPA CMPI CMPM DIVS DIVU EOR EORI EXG EXT JMP JSR LEA LINK LSL LSR MOVE MOVEA MOVEM MOVEP
MOVEQ MULS MULU NBCD NEG NEGX NOP NOT OR ORI PEA RESET ROL ROR ROXL ROXR RTD RTE RTR RTS SBCD
STOP SUB SUBA SUBI SUBQ SUBX SWAP TAS TRAP TRAPV TST UNLK ILLEGAL DC DS""".split())
NO_FALLTHROUGH = {"BRA", "JMP", "RTS", "RTE", "RTR", "RTD"}   # data may follow these


# --------------------------------------------------------------------------- expressions
@dataclass(frozen=True)
class Expr:
    sym: str          # label name or "" for a pure number
    add: int          # addend / the number itself

    def is_num(self):
        return not self.sym


_NUM = re.compile(r"^(?:\$([0-9A-Fa-f]+)|(\d+))")
_IDENT = re.compile(r"^[A-Za-z_]\w*")


def parse_expr(text, prog, line):
    """number | label | label+N | label-N | sum of those.  EQU constants fold to numbers."""
    s = text.strip().replace(" ", "")
    if not s:
        raise Unsupported("expr", line, "empty expression")
    pos, sign, sym, total = 0, 1, "", 0
    first = True
    while pos < len(s):
        c = s[pos]
        if c in "+-" and True:
            sign = -1 if c == "-" else 1
            pos += 1
            if pos >= len(s):
                raise Unsupported("expr", line, "bad expression %r" % text)
            c = s[pos]
        elif not first:
            raise Unsupported("expr", line, "bad expression %r" % text)
        else:
            sign = 1
        first = False
        m = _NUM.match(s[pos:])
        if m:
            val = int(m.group(1), 16) if m.group(1) is not None else int(m.group(2))
            total += sign * val
            pos += m.end()
            continue
        m = _IDENT.match(s[pos:])
        if m:
            name = m.group(0)
            pos += m.end()
            if name in prog.equs:
                total += sign * prog.equs[name]
            elif name in prog.labels:
                if sym or sign < 0:
                    raise Unsupported("expr", line, "unsupported symbol arithmetic %r" % text)
                sym = name
            else:
                raise Unsupported("expr", line, "unknown symbol %r" % name)
            continue
        raise Unsupported("expr", line, "cannot parse expression %r" % text)
    return Expr(sym, total)


# --------------------------------------------------------------------------- operands
@dataclass
class Op:
    kind: str                     # dn an ind post pre disp idx absw absl pcd pcidx imm reglist ccr sr usp
    reg: int = -1                 # register number (An for memory kinds, Dn/An for dn/an)
    expr: Expr = None             # displacement / absolute / immediate value
    idx: tuple = None             # (is_addr_reg, num, 'W'|'L')
    regs: tuple = ()              # reglist: tuple of ('D'|'A', n)
    text: str = ""

    def is_mem(self):
        return self.kind in ("ind", "post", "pre", "disp", "idx", "absw", "absl", "pcd", "pcidx")


_AREG = r"(A[0-7]|SP)"
_RE_DN = re.compile(r"^D([0-7])$")
_RE_AN = re.compile(r"^" + _AREG + "$")
_RE_IND = re.compile(r"^\(" + _AREG + r"\)$")
_RE_POST = re.compile(r"^\(" + _AREG + r"\)\+$")
_RE_PRE = re.compile(r"^-\(" + _AREG + r"\)$")
_RE_IDX = re.compile(r"^(.*)\(" + _AREG + r",([DA][0-7])(?:\.([WL]))?\)$")
_RE_DISP = re.compile(r"^(.*)\(" + _AREG + r"\)$")
_RE_PCIDX = re.compile(r"^(.*)\(PC,([DA][0-7])(?:\.([WL]))?\)$")
_RE_PC = re.compile(r"^(.*)\(PC\)$")
_RE_ABS = re.compile(r"^(.*?)(?:\.([WL]))?$")
_RE_REGLIST = re.compile(r"^[DA][0-7](-[DA][0-7])?(/[DA][0-7](-[DA][0-7])?)*$")


def _areg(s):
    return 7 if s == "SP" else int(s[1])


def _reglist(s, line):
    out = []
    for part in s.split("/"):
        if "-" in part:
            a, b = part.split("-")
            if a[0] != b[0] or int(b[1]) < int(a[1]):
                raise Unsupported("reglist", line, "unsupported register range %r" % s)
            out += [(a[0], n) for n in range(int(a[1]), int(b[1]) + 1)]
        else:
            out.append((part[0], int(part[1])))
    return tuple(out)


def split_operands(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [o.strip() for o in out]


def parse_operand(text, prog, line):
    t = text.strip()
    u = t.upper() if re.match(r"^[dDaApPsScCrR0-9()+\-,/.]+$", t) else t
    if _RE_DN.match(u):
        return Op("dn", int(u[1]), text=t)
    if _RE_AN.match(u):
        return Op("an", _areg(u), text=t)
    if u in ("CCR", "SR", "USP"):
        return Op(u.lower(), text=t)
    m = _RE_IND.match(u)
    if m:
        return Op("ind", _areg(m.group(1)), text=t)
    m = _RE_POST.match(u)
    if m:
        return Op("post", _areg(m.group(1)), text=t)
    m = _RE_PRE.match(u)
    if m:
        return Op("pre", _areg(m.group(1)), text=t)
    if t.startswith("#"):
        return Op("imm", expr=parse_expr(t[1:], prog, line), text=t)
    if _RE_REGLIST.match(u) and ("/" in u or "-" in u):
        return Op("reglist", regs=_reglist(u, line), text=t)
    m = _RE_PCIDX.match(u)
    if m:
        x = m.group(2)
        return Op("pcidx", expr=parse_expr(t[:m.end(1)], prog, line),
                  idx=(x[0] == "A", int(x[1]), m.group(3) or "W"), text=t)
    m = _RE_PC.match(t.replace("pc", "PC"))
    if m:
        return Op("pcd", expr=parse_expr(m.group(1), prog, line), text=t)
    m = _RE_IDX.match(u if "LAB" not in t else t.replace("sp", "SP"))
    if m:
        x = m.group(3)
        return Op("idx", _areg(m.group(2)), expr=parse_expr(m.group(1) or "0", prog, line),
                  idx=(x[0] == "A", int(x[1]), m.group(4) or "W"), text=t)
    m = _RE_DISP.match(t)
    if m and not t.startswith("("):
        return Op("disp", _areg(m.group(2).upper()), expr=parse_expr(m.group(1), prog, line), text=t)
    if t.startswith("(") and "PC" not in t.upper():
        raise Unsupported("operand", line, "unsupported operand %r" % t)
    m = _RE_ABS.match(t)
    if m and not any(c in t for c in "()*"):
        e = parse_expr(m.group(1), prog, line)
        kind = "absl" if m.group(2) != "W" else "absw"
        o = Op(kind, expr=e, text=t)
        o.explicit = m.group(2)
        return o
    raise Unsupported("operand", line, "unsupported operand %r" % t)


# --------------------------------------------------------------------------- instructions
@dataclass
class Insn:
    line: int
    mnem: str           # normalised base mnemonic (ADD, BCC, DBCC, SCC, ...)
    size: str           # 'B' 'W' 'L' 'S' or ''
    ops: list
    text: str
    hunk: int = 0
    off: int = 0
    length: int = 0
    cond: str = ""      # for Bcc/DBcc/Scc: one of CONDS
    labels: list = field(default_factory=list)


def split_mnemonic(raw, line):
    base, _, suf = raw.upper().partition(".")
    if base in BASE_MNEMS:
        return base, suf, ""
    if base == "DBRA":
        return "DBCC", suf, "F"
    if base.startswith("DB") and (base[2:] in CONDS or base[2:] in COND_ALIAS):
        c = base[2:]
        return "DBCC", suf, COND_ALIAS.get(c, c)
    if base.startswith("B") and (base[1:] in CONDS[2:] or base[1:] in COND_ALIAS):
        c = base[1:]
        return "BCC", suf, COND_ALIAS.get(c, c)
    if base.startswith("S") and (base[1:] in CONDS or base[1:] in COND_ALIAS):
        c = base[1:]
        return "SCC", suf, COND_ALIAS.get(c, c)
    raise Unsupported(base, line, "unknown mnemonic %r" % raw)


_SZ_BYTES = {"B": 2, "W": 2, "L": 4, "": 2}


def ext_bytes(op, size, quick=False):
    k = op.kind
    if k in ("dn", "an", "ind", "post", "pre", "ccr", "sr", "usp"):
        return 0
    if k in ("disp", "idx", "pcd", "pcidx", "absw", "reglist"):
        return 2
    if k == "absl":
        return 4
    if k == "imm":
        return 0 if quick else _SZ_BYTES[size]
    raise Unsupported("operand", 0, "length of %s" % k)


def expected_lengths(mnem, size, ops):
    """Candidate instruction lengths (set) for ops; abs operands tried as .W or .L."""
    quick = mnem in ("ADDQ", "SUBQ", "MOVEQ") or (mnem in ("ASL", "ASR", "LSL", "LSR", "ROL", "ROR", "ROXL", "ROXR"))
    szimm = size
    if mnem in ("BTST", "BCHG", "BCLR", "BSET"):
        szimm = "B"
    if mnem in ("BCC", "BRA", "BSR"):
        return {2 if size == "S" else 4}
    if mnem == "DBCC":
        return {4}
    if mnem == "LINK":
        return {4}
    if mnem == "TRAP":
        return {2}
    opts = {2}
    if mnem == "MOVEM":                       # register mask word; a single register has no reglist operand
        opts = {4}
        ops = [op for op in ops if op.is_mem()]
    for op in ops:
        if op.kind == "absw":
            opts = {o + 2 for o in opts}
        elif op.kind == "absl":
            opts = {o + 4 for o in opts}
        else:
            opts = {o + ext_bytes(op, szimm, quick) for o in opts}
    return opts


# --------------------------------------------------------------------------- program database
LST_ITEM = re.compile(r"^([0-9A-F]{2}):([0-9A-F]{8}) ([0-9A-F]*)\s*\t\s*(\d+): (.*)$")
EQU = re.compile(r"^(\w+)\s+EQU\s+\$([0-9A-Fa-f]+)")
LABEL_LINE = re.compile(r"^([A-Za-z_]\w*):")


class Program:
    def __init__(self, binary):
        self.binary = binary
        self.lines = (REASM / (binary + ".asm")).read_text(errors="replace").split("\n")
        self.symbols = json.loads((REASM / (binary + ".symbols.json")).read_text())
        self.labels = {k: (v["hunk"], v["offset"]) for k, v in self.symbols.items()}
        self.equs = {}
        for l in self.lines:
            m = EQU.match(l)
            if m:
                self.equs[m.group(1)] = int(m.group(2), 16)
        self.addr_of_line = {}      # asm line -> (hunk, off)
        for l in (REASM / (binary + ".lst")).read_text(errors="replace").split("\n"):
            m = LST_ITEM.match(l)
            if m:
                self.addr_of_line[int(m.group(4))] = (int(m.group(1), 16), int(m.group(2), 16))
        cg = json.loads((INVENTORY / (binary + ".callgraph.json")).read_text())
        self.hunk_kind = {int(k): v["kind"] for k, v in cg["hunks"].items()}
        self.routines = {r["name"]: r for r in cg["routines"]}
        self.edges = cg["edges"]
        self.indirect = cg["indirect_sites"]
        self.entry_at = {}          # (hunk, off) -> routine name
        for r in cg["routines"]:
            self.entry_at[(r["hunk"], r["start"])] = r["name"]
            for a in r.get("aliases", []):
                if a in self.labels:
                    self.entry_at[self.labels[a]] = r["name"]
        self.indirect_lines = {s.get("line") for s in cg["indirect_sites"]}
        self.indirect_lines |= {s.get("line") if isinstance(s, dict) else s
                                for s in cg["unresolved_indirect_lines"]}

    def label_addr(self, expr):
        h, o = self.labels[expr.sym]
        return h, o + expr.add

    def parse_insn_text(self, mnemonic, operand_text, ln):
        """'MOVE.W', 'D0,-(A7)' -> (base mnemonic, size suffix, cond, [Op])."""
        base, suf, cond = split_mnemonic(mnemonic, ln)
        return base, suf, cond, [parse_operand(o, self, ln) for o in split_operands(operand_text)]

    def parse_routine(self, name):
        """-> (list[Insn], label_names_by_offset) for the routine body; raises Unsupported."""
        r = self.routines[name]
        # Pass 1: every line of the span becomes an item: an Insn, or the Unsupported it raised
        # (DC/DS data, or text/tables that IRA decoded as nonsense instructions).
        items = []                      # [off or None, Insn | Unsupported]
        pending = []
        for ln in range(r["line_first"], r["line_last"] + 1):
            raw = self.lines[ln - 1]
            raw = raw.split(";", 1)[0].rstrip()
            if not raw.strip():
                continue
            m = LABEL_LINE.match(raw)
            if m:
                pending.append(m.group(1))
                rest = raw[m.end():].strip()
                if not rest:
                    continue
                raw = "\t" + rest
            parts = raw.strip().split(None, 1)
            mn = parts[0]
            off = self.addr_of_line[ln][1] if ln in self.addr_of_line else None
            try:
                if mn.upper().split(".")[0] in ("DC", "DS"):
                    raise Unsupported(mn.upper(), ln, "data in code: %s" % raw.strip())
                base, suf, cond, ops = self.parse_insn_text(mn, parts[1] if len(parts) > 1 else "", ln)
                if off is None:
                    raise Unsupported(base, ln, "no listing address")
                item = Insn(ln, base, suf, ops, re.sub(r"\s+", " ", raw.strip()), self.addr_of_line[ln][0],
                            off, 0, cond, pending)
            except Unsupported as e:
                item = e
            pending = []
            items.append([off, item])
        if not items:
            raise Unsupported("empty", r["line_first"], "no instructions")
        if isinstance(items[0][1], Unsupported):
            raise items[0][1]
        if items[0][0] != r["start"]:
            raise Unsupported("entry inside instruction", items[0][1].line,
                              "routine entry $%X is not an instruction boundary (IRA decoded from $%X: %s)"
                              % (r["start"], items[0][0], items[0][1].text))
        # Pass 2: keep what control flow reaches from the entry (fall-through and branches to labels
        # inside the span). Unreachable items -- padding after a BRA, a CODE-hunk variable such as
        # mog LAB_01EB, an inline string -- are data; reaching an Unsupported item raises it.
        at = {o: i for i, (o, _) in enumerate(items) if o is not None}
        seen, work = set(), [0]
        while work:
            i = work.pop()
            if i in seen or i >= len(items):
                continue
            seen.add(i)
            ins = items[i][1]
            if isinstance(ins, Unsupported):
                raise ins
            if ins.mnem not in NO_FALLTHROUGH:
                work.append(i + 1)
            if ins.mnem in ("BCC", "BRA", "DBCC", "BSR", "JSR", "JMP") and ins.ops:
                t = ins.ops[-1]
                if t.expr is not None and t.expr.sym in self.labels:
                    h, o = self.label_addr(t.expr)
                    if h == r["hunk"] and r["start"] <= o < r["end"] and o in at:
                        work.append(at[o])
        offs = [o for o, _ in items if o is not None] + [r["end"]]
        insns, labels = [], {}
        for i in sorted(seen):
            off, ins = items[i]
            ins.length = min(x for x in offs if x > off) - off
            for p in ins.labels:
                labels[p] = off
            insns.append(ins)
        for ins in insns:
            self._check_length(ins)
        # labels that sit after the last instruction (none expected) are ignored
        return insns, labels

    def _check_length(self, ins):
        if ins.length <= 0:
            raise Unsupported(ins.mnem, ins.line, "non-positive length")
        # decide abs.W vs abs.L for numeric absolute operands from the listing length
        absn = [op for op in ins.ops if op.kind in ("absw", "absl") and not op.expr.sym]
        sz = ins.size if ins.size in ("B", "W", "L", "S") else ""
        if absn:
            good = []
            for combo in range(1 << len(absn)):
                for i, op in enumerate(absn):
                    op.kind = "absl" if (combo >> i) & 1 else "absw"
                if ins.length in expected_lengths(ins.mnem, sz, ins.ops):
                    good.append(combo)
            if len(good) != 1:
                raise Unsupported(ins.mnem, ins.line, "cannot decide abs.W/abs.L from length %d (%s)"
                                  % (ins.length, ins.text))
            for i, op in enumerate(absn):
                op.kind = "absl" if (good[0] >> i) & 1 else "absw"
            return
        for op in ins.ops:
            if op.kind == "absw" and op.expr.sym:
                op.kind = "absl"
        if ins.length not in expected_lengths(ins.mnem, sz, ins.ops):
            raise Unsupported(ins.mnem, ins.line, "length %d does not match operands (%s)" % (ins.length, ins.text))
