#!/usr/bin/env python3
"""tools/endian_externs.py -- does the C++ view of every owned-data symbol match the generated member type? (ROADMAP 11.3)

tools/gen_data.py cuts the original's data into C++ members: byte runs stay `u8` (big-endian bytes, read with ms::be::), typed cells
(`u16` / `u32`, decided by tools/data_units.yaml) and `DC.L LABEL` pointers are NATIVE.  A C++ reader that treats a u8 run as native
uint16_t / uint32_t (`extern uint16_t x[]` over a u8 member, reinterpret_cast<uint16_t *>) or reads a native cell as big-endian bytes
sees swapped values on a little-endian CPU.  This script lists every such mismatch:

  * the owned-data aliases (`.set _name, _object+off` in <gen>/owned_data.cpp; the names come from tools/cell_names.yaml),
  * the generated member at that offset (<gen>/owned_data.hpp: u8 / u16 / u32 / pointer / struct),
  * every `extern` declaration of that name in src/ and include/ (its element type and array extent),
  * and how the other source lines use the name (be:: / rd16 / ld32 composes, wide casts).

    py tools/endian_externs.py [--gen build-sdl/gen] [--all] [--check]

Exit status 1 with --check (needs --full: `py tools/gen_data.py --full --target pc32 --orig-dir <disk A files> --out-dir X`, then --full X/owned_data.cpp) when a mismatch is found that is not in KNOWN_OK below (symbols the survey judged correct: the reason is
the value).  Reads only generated files and sources; the original game is not needed.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# symbol -> why a flagged declaration is correct (reviewed by hand; see docs/PC_ENDIAN.md 3)
KNOWN_OK = {
    "mogLootPaletteTail": "declared uint32_t[] but only its address is cast to uint16_t * (curated u16 colour words, LAB_09F1)",
    "mogSnatchFlags": "arena.cpp clears the first word (bytes 0, 0), fighters.cpp reads the flag bytes: the same on both byte orders",
    "mogLootPalette": "declared uint8_t[] but cast to uint16_t * (curated u16 colour words, LAB_09F0)",
    "mogSmallFont": "the uint32_t[5] declarations are cast to uint8_t * / uint32_t * consistently (a table of cel addresses)",
    "prgFontRecord": "the byte declaration only adds an offset and casts to uint32_t * (+16 = the font cel table address)",
    "mogHandlerTable": "every access is a big-endian long through be::ld32 / st32 / wr32 on a byte pointer (the uint32_t[] declarations only give the address)",
}

U8 = {"uint8_t", "UBYTE", "BYTE", "char", "int8_t", "bool", "u8", "unsigned char", "signed char"}
U16 = {"uint16_t", "int16_t", "UWORD", "WORD", "u16", "unsigned short", "short", "unsigned short int"}
U32 = {"uint32_t", "int32_t", "ULONG", "LONG", "u32", "unsigned", "unsigned int", "int", "unsigned long", "long", "uintptr_t", "float"}


def _read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def elem_size(ctype):
    """Width in bytes of the C++ element type, or None for a struct / unknown type."""
    t = re.sub(r"\b(const|volatile|static|inline|ms::|rt::|game::|engine::)\b", " ", ctype)
    t = re.sub(r"\b\w+::", "", t)
    t = " ".join(t.replace("&", " ").split())
    if "*" in t:
        return 4
    return 1 if t in U8 else 2 if t in U16 else 4 if t in U32 else None


# ---- generated side ---------------------------------------------------------------------------------------------------
def load_generated(gen):
    hpp, cpp = (_read(os.path.join(gen, n)) for n in ("owned_data.hpp", "owned_data.cpp"))
    # struct fields of the row structs: name -> set of field widths
    structs = {}
    for m in re.finditer(r"struct\s+(?:__attribute__\(\(packed\)\)\s+)?(\w+)\s*\{(.*?)\n\};", hpp, flags=re.S):
        widths = set()
        for fm in re.finditer(r"^\s*([\w:\* ]+?)\s+\**\w+(?:\[\w*\])?\s*;", m.group(2), flags=re.M):
            w = elem_size(fm.group(1) + (" *" if "*" in fm.group(0).split(";")[0] and "*" not in fm.group(1) else ""))
            widths.add(w)
        structs[m.group(1)] = widths
    # object -> struct name
    obj_struct = {}
    for m in re.finditer(r'extern "C"\s+(?:ms::owned::)?(\w+)\s+(g_\w+)\s*;', hpp):
        obj_struct[m.group(2)] = m.group(1)
    # struct bodies of the owned objects: (offset, size, unit, type, member)
    members = {}
    for sname in set(obj_struct.values()):
        m = re.search(r"struct\s+(?:__attribute__\(\(packed\)\)\s+)?" + sname + r"\s*\{(.*?)\n\};", hpp, flags=re.S)
        if not m:
            continue
        lst = []
        for line in m.group(1).splitlines():
            lm = re.match(r"\s*(.+?)\s+(\*?)(\w+)(?:\[(\d+)\])?\s*;\s*//\s*\+(\d+)", line)
            if not lm:
                continue
            ctype, star, name, count, off = lm.group(1), lm.group(2), lm.group(3), lm.group(4), int(lm.group(5))
            count = int(count) if count else 1
            base = ctype.replace("const ", "")
            if star or "*" in ctype:
                unit, kind = 4, "ptr"
            elif base in ("u8", "char"):
                unit, kind = 1, base
            elif base == "u16":
                unit, kind = 2, "u16"
            elif base == "u32":
                unit, kind = 4, "u32"
            elif base in structs:
                w = structs[base] - {None}
                unit, kind = (max(w) if w else 1), "struct " + base
            else:
                unit, kind = 1, base
            size = count * unit if not kind.startswith("struct") else None
            lst.append((off, size, unit, kind, name, count))
        members[sname] = sorted(lst)
    # aliases: C name -> (object, offset)
    aliases = {}
    for m in re.finditer(r"\.set\s+_(\w+),\s*_(g_\w+)\s*(?:\+\s*(\d+))?", cpp):
        aliases[m.group(1)] = (m.group(2), int(m.group(3) or 0))
    return aliases, obj_struct, members


def member_at(members, obj_struct, obj, off):
    lst = members.get(obj_struct.get(obj), [])
    best = None
    for ent in lst:
        if ent[0] <= off:
            best = ent
        else:
            break
    return best, lst


# ---- C++ side ---------------------------------------------------------------------------------------------------------
EXTERN_RE = re.compile(r'\bextern\s+(?:"C"\s+)?([^;{}()]*?);', flags=re.S)


def parse_externs(path, text):
    """[(ctype, name, count_or_None, is_array, line)] for every extern declaration (comma lists split)."""
    out = []
    clean = strip_comments(text)
    for m in EXTERN_RE.finditer(clean):
        stmt = " ".join(m.group(1).split())
        if "=" in stmt:
            stmt = stmt.split("=")[0]
        if not stmt or stmt.startswith("template") or stmt.startswith('"'):
            continue
        line = clean.count("\n", 0, m.start()) + 1
        parts = [p.strip() for p in stmt.split(",")]
        first = re.match(r"^(.*?)(\*?)\s*(\w+)\s*((?:\[[^\]]*\]\s*)*)$", parts[0])
        if not first:
            continue
        base, star, name, dims = first.groups()
        if base.strip() == "":
            continue
        base = base.strip()
        # pointer stars attached to the base type (`Foo *x`) end up in `base`
        ctype0 = base
        out.append((ctype0 + (" *" if star else ""), name, dims, line))
        for p in parts[1:]:
            pm = re.match(r"^(\*?)\s*(\w+)\s*((?:\[[^\]]*\]\s*)*)$", p)
            if pm:
                out.append((ctype0 + (" *" if pm.group(1) else ""), pm.group(2), pm.group(3), line))
    return out


def array_count(dims):
    ms = re.findall(r"\[([^\]]*)\]", dims)
    if not ms:
        return 1
    n = 1
    for d in ms:
        d = d.strip()
        if not d.isdigit():
            return None
        n *= int(d)
    return n


STRUCT_RE = re.compile(r"\bstruct\s+(?:alignas\(\d+\)\s+)?(?:__attribute__\(\(\w+\)\)\s+)?(\w+)\s*(?::[^{;]*)?\{([^{}]*)\}", flags=re.S)


def struct_widths(texts):
    """C++ struct name -> set of field widths (None = a nested struct / unknown)."""
    out = {}
    for text in texts.values():
        for m in STRUCT_RE.finditer(strip_comments(text)):
            ws = set()
            for stmt in m.group(2).split(";"):
                stmt = " ".join(stmt.split())
                if not stmt or "(" in stmt or stmt.startswith(("using", "static", "typedef", "enum")):
                    continue
                fm = re.match(r"^(.*?)(\*?)\s*\w+(?:\s*\[[^\]]*\])*(?:\s*:\s*\d+)?(?:\s*=.*)?$", stmt)
                if fm:
                    ws.add(elem_size(fm.group(1) + (" *" if fm.group(2) else "")))
            out.setdefault(m.group(1), ws)
    return out


def load_nonzero(path):
    """(object, member name) -> True when the original image gives it a non-zero initial value (a `--full` generation of gen_data)."""
    res = {}
    if not path or not os.path.exists(path):
        return res
    buf = []
    obj = None
    for line in _read(path).splitlines():
        om = re.search(r"(?<![\w])(g_\w+)[^=;]*=\s*\{\s*$", line)
        if om:
            obj, buf = om.group(1), []
            continue
        m = re.search(r"//\s*(\w+)\s*$", line)
        buf.append(line.split("//")[0])
        if m:
            body = "".join(buf)
            res[(obj, m.group(1))] = bool(re.search(r'0x[0-9a-fA-F]*[1-9a-fA-F]|"|(?<![\w.])[1-9]\d*(?![\w.])', body))
            buf = []
    return res


def source_files(root=ROOT):
    for sub in ("src", "include"):
        for dp, _dn, fn in os.walk(os.path.join(root, sub)):
            for f in fn:
                if f.endswith((".cpp", ".hpp", ".h", ".c", ".inc")):
                    yield os.path.join(dp, f)


BE_USE = re.compile(r"\b(be::\w+|ld16|ld32|rd16|rd32|readBe\w*|be16|be32|beWord|beLong|ld\(|ms::addr::ld32)")
CAST_RE = re.compile(r"reinterpret_cast<\s*(?:const\s+)?(?:volatile\s+)?(\w+)\s*\*|\(\s*(?:const\s+)?(uint16_t|uint32_t|UWORD|ULONG|WORD|LONG)\s*\*\s*\)")


def read_sites(name, is_array, line_text):
    """Does this source line READ / WRITE the value of `name` (not just take its address)?"""
    pat = re.escape(name)
    if is_array:
        return bool(re.search(r"\b" + pat + r"\s*\[|\*\s*\(?\s*(?:\w+\s*\*\s*\)\s*)?" + pat + r"\b|\b" + pat + r"\s*\+", line_text)) \
            and not re.search(r"&\s*" + pat + r"\s*\[", line_text)
    return bool(re.search(r"(?<!&)(?<![\w:.])" + pat + r"\b", line_text))


def scan(gen, full=None, root=ROOT):
    aliases, obj_struct, members = load_generated(gen)
    nonzero = load_nonzero(full)
    decls = {}      # name -> [(file, line, ctype, dims)]
    texts = {}
    for path in source_files(root):
        texts[path] = _read(path)
    cstructs = struct_widths(texts)
    for path, text in texts.items():
        for ctype, name, dims, line in parse_externs(path, text):
            if name in aliases:
                decls.setdefault(name, []).append((os.path.relpath(path, root).replace("\\", "/"), line, ctype, dims))
    cleaned = {p: strip_comments(t).splitlines() for p, t in texts.items()}
    rows = []
    for name in sorted(decls):
        obj, off = aliases[name]
        mem, lst = member_at(members, obj_struct, obj, off)
        if mem is None:
            continue
        moff, msize, unit, kind, mname, mcount = mem
        pat = re.compile(r"\b" + re.escape(name) + r"\b")
        lines_of = []     # (rel, lineno, text) of every non-declaration use
        for path, ls in cleaned.items():
            rel = os.path.relpath(path, root).replace("\\", "/")
            for i, ln in enumerate(ls, 1):
                if pat.search(ln) and "extern" not in ln:
                    lines_of.append((rel, i, ln))
        def n_reads(d):
            return sum(1 for (r, _i, t) in lines_of if r == d[0] and read_sites(name, bool(d[3].strip()), t))
        widths = {elem_size(d[2]) for d in decls[name] if n_reads(d)}   # a declaration that only takes the address has no width
        for (f, line, ctype, dims) in decls[name]:
            es = elem_size(ctype)
            cnt = array_count(dims)
            is_array = bool(dims.strip())
            mine = [(r, i, t) for (r, i, t) in lines_of if r == f]
            reads = [(r, i, t) for (r, i, t) in mine if read_sites(name, is_array, t)]
            nbe = sum(1 for (_r, _i, t) in mine if BE_USE.search(t))
            ncast = 0
            for (_r, _i, t) in mine:
                cm = CAST_RE.search(t)
                if cm and (cm.group(1) or cm.group(2)) in ("uint16_t", "uint32_t", "UWORD", "ULONG", "WORD", "LONG", "u16", "u32"):
                    ncast += 1
            verdict, why = "ok", ""
            if es is None:
                tname = re.sub(r"\b(const|ms::|game::)\b", "", ctype).replace("::", " ").split()
                tn = tname[-1] if tname else ""
                ws = cstructs.get(tn, set())
                if kind in ("u8", "char") and ws and ws != {1}:
                    verdict, why = "review", "struct %s (field widths %s) over u8 bytes" % (tn, sorted(w for w in ws if w))
            else:
                if kind.startswith("struct"):
                    pass
                elif es != unit and not (es == 4 and kind == "ptr") and not (es == 1 and kind in ("u8", "char")):
                    if unit == 1:
                        verdict, why = "MISMATCH", "native %d-byte reads of big-endian bytes" % es
                    elif es == 1:
                        verdict, why = "MISMATCH", "byte view of a native %d-byte cell" % unit
                    else:
                        verdict, why = "MISMATCH", "%d-byte reads over %d-byte cells" % (es, unit)
                    if not reads and verdict == "MISMATCH":
                        verdict, why = "addr-only", why + " but only the address is used here"
                span = (cnt * es) if cnt is not None else None
                if verdict == "ok" and span and msize and off + span > moff + msize:
                    end = off + span
                    for ent in lst:
                        if ent[0] >= moff + msize and ent[0] < end and ent[2] != es and not ent[3].startswith("struct"):
                            verdict, why = "MISMATCH", "array runs into %s (%d-byte unit)" % (ent[4], ent[2])
                            break
                if verdict == "ok" and kind in ("u8", "char") and es == 1 and ncast:
                    verdict, why = "review", "byte member used through %d wide cast(s)" % ncast
                if verdict == "ok" and kind in ("u16", "u32") and nbe and es == unit:
                    verdict, why = "review", "native cell also read with be:: (%d site(s))" % nbe
            if verdict == "ok" and len(widths - {None}) > 1 and kind != "ptr" and not kind.startswith("struct"):
                verdict, why = "review", "declared with different widths %s in different files" % sorted(widths - {None})
            if name in KNOWN_OK and verdict not in ("ok", "addr-only"):
                verdict, why = "known-ok", KNOWN_OK[name]
            nz = nonzero.get((obj, mname))
            if verdict == "MISMATCH" and nonzero and nz is not True:
                verdict = "state"          # zero in the original image: the fill leaves it alone, only the C++ writes and reads it
            rows.append(dict(name=name, obj=obj, off=off, file=f, line=line, ctype=(ctype + dims.replace(" ", "")).strip(),
                             member="%s %s%s +%d" % (kind, mname, ("[%d]" % mcount) if mcount > 1 else "", moff),
                             unit=unit, verdict=verdict, why=why, nbe=nbe, ncast=ncast, nreads=len(reads),
                             init=("data" if nz else "zero") if nz is not None else "bss",
                             sample=(reads[0][2].strip()[:110] if reads else "")))
    return rows


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", default=os.path.join(ROOT, "build-sdl", "gen"))
    ap.add_argument("--full", help="owned_data.cpp of `gen_data.py --full --target pc32` (initial values: tells data-bearing cells from zero-initialised ones)")
    ap.add_argument("--all", action="store_true", help="list the matching declarations too")
    ap.add_argument("--check", action="store_true", help="exit 1 on a MISMATCH")
    ap.add_argument("--samples", action="store_true", help="print the first reading line of each flagged declaration")
    a = ap.parse_args(argv)
    if not os.path.exists(os.path.join(a.gen, "owned_data.hpp")):
        print("endian_externs: skipped: %s/owned_data.hpp not generated (build the PC target first)" % a.gen)
        return 0
    rows = scan(a.gen, a.full)
    bad = 0
    for r in rows:
        if r["verdict"] in ("ok", "addr-only", "known-ok", "state") and not a.all:
            continue
        if r["verdict"] == "MISMATCH":
            bad += 1
        print("%-8s %-26s %-22s <- %-28s %s [%s:%d] init=%s reads=%d be=%d cast=%d" % (r["verdict"], r["name"], r["ctype"], r["member"], r["why"],
              r["file"], r["line"], r["init"], r["nreads"], r["nbe"], r["ncast"]))
        if a.samples and r["sample"]:
            print("           | " + r["sample"])
    cnt = lambda v: sum(1 for r in rows if r["verdict"] == v)
    print("endian_externs: %d declaration(s) of owned-data symbols: %d MISMATCH, %d review, %d state (zero-initialised), %d address-only, %d known-ok" % (
        len(rows), bad, cnt("review"), cnt("state"), cnt("addr-only"), cnt("known-ok")))
    if a.check and not a.full:
        print("endian_externs: --check needs --full (without the initial values every wide cell looks like a mismatch)")
        return 0
    return 1 if (a.check and bad) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
