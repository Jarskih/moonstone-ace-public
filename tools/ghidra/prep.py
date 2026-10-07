#!/usr/bin/env python3
"""prep.py -- build relocated hunk images + layout/labels for the Ghidra import.

Load base choice: hunks are PACKED sequentially (hunk i follows hunk i-1,
aligned up to 0x1000) starting at 0x00100000.  Packed (not 1 MB-spaced) so that
mog's 46 hunks stay far below $BF0000 and never touch $000-$400 vectors, the
CIA ($BFxxxx) or custom chips ($DFFxxx).  RELOC32 is applied byte-wise.

Outputs per binary in build/ghidra/<name>/ :
  hunk_NN.bin   relocated image (CODE/DATA only; BSS has no file)
  layout.json   [{hunk,name,base,size,kind,chip,bss,file}]
  layout.tsv    same, tab separated (read by the Ghidra script)
  labels.tsv    name addr hunk offset is_code is_func   (+ HW rows: hunk=-1)
  <name>        4-byte stub that analyzeHeadless imports (names the program)
"""
import json, re, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'reference/moonshard/tools'))
from moghunks import parse_hunk_file  # noqa: E402

SRC = ROOT / 'reference/moonshard/moonstone-main/amiga_asm'
REASM = ROOT / "build" / "reasm"
OUT = ROOT / "build" / "ghidra"
BASE0, ALIGN = 0x00100000, 0x1000
NAMES = ["nb", "program", "mog"]

def prep(name):
    hf = parse_hunk_file((SRC / name).read_bytes())
    syms = json.loads((REASM / f"{name}.symbols.json").read_text())
    out = OUT / name
    out.mkdir(parents=True, exist_ok=True)
    bases, cur = [], BASE0
    for h in hf.hunks:
        bases.append(cur)
        cur += (h.size_bytes + ALIGN - 1) // ALIGN * ALIGN
    assert cur < 0xBF0000
    layout = []
    for h in hf.hunks:
        bss = h.kind == "BSS"
        fn = None
        if not bss:
            img = bytearray(h.data)
            for g in h.reloc32:
                for off in g.offsets:
                    v = struct.unpack(">I", img[off:off+4])[0]
                    img[off:off+4] = struct.pack(">I", (v + bases[g.target_hunk]) & 0xFFFFFFFF)
            fn = f"hunk_{h.index:02d}.bin"
            (out / fn).write_bytes(bytes(img))
        layout.append(dict(hunk=h.index, name=f"S_{h.index}_{h.kind}", base=bases[h.index],
                           size=h.size_bytes, kind=h.kind, chip=h.mem_flag == "chip",
                           bss=bss, file=fn))
    (out / "layout.json").write_text(json.dumps(layout, indent=1))
    (out / "layout.tsv").write_text("".join(
        f"{l['hunk']}\t{l['name']}\t{l['base']:#x}\t{l['size']}\t{l['kind']}\t{int(l['chip'])}\t{l['file'] or '-'}\n"
        for l in layout))
    # JSR/BSR targets from the listing
    asm = (REASM / f"{name}.asm").read_text(errors="replace")
    targets = set(re.findall(r"^\s+(?:JSR|BSR)(?:\.[A-Z])?\s+([A-Za-z_][A-Za-z0-9_]*)\s*$", asm, re.M))
    rows, nfunc = [], 0
    for lab, s in sorted(syms.items(), key=lambda kv: (kv[1]["hunk"], kv[1]["offset"])):
        h = s["hunk"]
        code = layout[h]["kind"] == "CODE"
        fnc = code and lab in targets
        nfunc += fnc
        rows.append(f"{lab}\t{bases[h] + s['offset']:#x}\t{h}\t{s['offset']}\t{int(code)}\t{int(fnc)}")
    # hardware EQUs ($DFFxxx custom, $BFxxxx CIA)
    hw = 0
    for m in re.finditer(r"^(\w+)\s+EQU\s+\$([0-9A-Fa-f]+)\s*$", asm, re.M):
        a = int(m.group(2), 16)
        if 0xBF0000 <= a < 0xC00000 or 0xDFF000 <= a < 0xE00000:
            rows.append(f"{m.group(1)}\t{a:#x}\t-1\t0\t0\t0"); hw += 1
    (out / "labels.tsv").write_text("\n".join(rows) + "\n")
    (out / name).write_bytes(b"\0\0\0\0")
    print(f"{name}: {len(layout)} hunks, {len(syms)} labels, {nfunc} func candidates, {hw} hw equs")

if __name__ == "__main__":
    for n in (sys.argv[1:] or NAMES):
        prep(n)
