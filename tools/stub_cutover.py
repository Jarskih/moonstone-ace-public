"""ROADMAP 7.1o: retire one-instruction `JMP rt_x` stubs whose callers are all C++.

  py tools/stub_cutover.py mog:LAB_0303 program:LAB_0390 ...        dry run: what would change
  py tools/stub_cutover.py --apply mog:LAB_0303 ...                 rewrite src/ + drop the patches
  py tools/stub_cutover.py --skip-file src/rt/combat.cpp ...        leave a file alone (its labels stay stubs)

For every `<bin>:<LABEL>` the script reads the generated asm/<bin>.s to find the stub (`LABEL:` followed by the patch's
`JMP rt_x`), refuses it when live asm still reaches the label (call, address, DC.L table, fall-through: tools/asm_remaining
inbound edges), then in src/**/*.cpp,*.hpp (and tests/*.cpp):
  * `extern uint8_t <bin>_LABEL[]` declarations lose the name,
  * `jsr/jmp/bsr <bin>_LABEL` in asm text and quoted asm strings become `rt_x`,
  * every other use (call(...), owCall(...), table rows, `&<bin>_LABEL`) becomes `RT_FN(rt_x)` (include/rt/stubfn.h),
    a byte pointer to the rt_x entry; the register contract is the one the patch had (JSR LABEL == JSR rt_x).
and deletes the patch by id from asm/patches/*.json (a patch file left with no patches keeps its "funcs").
Run `py tools/resource.py` afterwards.  Uses it cannot classify are printed and left for hand editing.
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

PFX = {'mog': 'mog_', 'program': 'prg_'}
SKIP_DIRS = ('src/lifted/', 'src/rt/abs_stubs.cpp', 'src/rt/image_tab.cpp')


def find_stub(binary, label):
    lines = open(os.path.join(ROOT, 'asm', binary + '.s'), encoding='utf-8').read().split('\n')
    pfx = PFX[binary]
    for i, ln in enumerate(lines):
        if ln == '%s%s:' % (pfx, label):
            j = i + 1
            while j < len(lines) and (lines[j].startswith(';') or not lines[j].strip()):
                j += 1
            pid = None
            if lines[j].startswith('ps_'):
                pid = lines[j][3:-1]
                j += 1
            m = re.match(r'\s+JMP\s+(rt_\w+)\s*$', lines[j])
            if m and pid:
                return m.group(1), pid
            return None, None
    return None, None


def source_files(skip):
    out = []
    for pat in ('src/**/*.cpp', 'src/**/*.hpp', 'src/**/*.h', 'include/**/*.hpp', 'tests/*.cpp'):
        for f in glob.glob(os.path.join(ROOT, pat), recursive=True):
            rel = os.path.relpath(f, ROOT).replace('\\', '/')
            if rel.startswith(SKIP_DIRS) or rel in skip:
                continue
            out.append(rel)
    return sorted(out)


def strip_declaration(code, sym):
    """If `code` is a function declaration naming `sym` (`void sym(...);`, or `extern void a(), sym(), b();`), the line without it
    ('' when nothing is left); None when the line is not a declaration.  A declaration must never become `void RT_FN(rt_x)();`."""
    s = re.escape(sym)
    item = r'\s*' + s + r'\s*\([^)]*\)\s*'
    if not re.match(r'\s*(extern|void|ULONG|UBYTE|UWORD|uint\d+_t|int|bool)\b', code) or not re.search(item, code):
        return None
    if re.match(r'\s*(?:extern\s+(?:"C"\s+)?)?\w+\s*\*?\s*' + s + r'\s*\([^)]*\)\s*;\s*$', code):
        return ''
    new = re.sub(item + r',', ' ', code, count=1)          # not the last item: take its comma
    if new == code:
        new = re.sub(r',' + item, ' ', code, count=1)      # the last item: take the comma before it
    if new == code:
        new = re.sub(item, ' ', code, count=1)
    return re.sub(r'[ 	]+', ' ', new).replace(' ;', ';').replace('( ', '(').rstrip() if new.strip() not in ('extern void;', 'extern;') else ''


def rewrite(text, sym, rt, rel, report):
    """Rewrite one file's text for one stub symbol; returns the new text."""
    tok = re.compile(r'(?<![\w])' + re.escape(sym) + r'(?![\w])')
    if not tok.search(text):
        return text
    # 1) array declarations: drop the name from extern lists
    decl = re.compile(r'(?<![\w])' + re.escape(sym) + r'\[\]')
    if decl.search(text):
        text = re.sub(r'(?<![\w])' + re.escape(sym) + r'\[\]\s*,[ \t]*\n?[ \t]*', '', text)
        text = re.sub(r',\s*' + re.escape(sym) + r'\[\]', '', text)
        text = re.sub(r'(?m)^[ \t]*extern\s+(?:"C"\s+)?uint8_t\s+' + re.escape(sym) + r'\[\]\s*;[ \t]*\n', '', text)
        text = re.sub(r'(?m)^[ \t]*extern\s+"C"\s+uint8_t\s+' + re.escape(sym) + r'\[\]\s*;[ \t]*\n', '', text)
    out = []
    in_asm = False
    for ln in text.split('\n'):
        if 'asm(R"' in ln or 'asm volatile(R"' in ln:
            in_asm = True
        if not tok.search(ln):
            out.append(ln)
            if in_asm and re.search(r'\)"\s*(\)|;|:)', ln):
                in_asm = False
            continue
        code, sep, comment = ln.partition('//') if not in_asm else (ln, '', '')
        if not in_asm:
            decl = strip_declaration(code, sym)
            if decl is not None:             # a prototype / extern list naming the stub: the name leaves it, nothing is rewritten
                if decl.strip():
                    out.append(decl + sep + comment)
                elif comment:
                    out.append(comment.strip() and sep + comment)
                continue
        # quoted-string spans on this line (inline asm)
        def quoted(pos):
            return code[:pos].count('"') % 2 == 1
        def sub(m):
            if in_asm or quoted(m.start()):
                return rt
            before = code[:m.start()]
            if re.search(r'&\s*$', before):
                return 'RT_FN(' + rt + ')'          # `&sym` -> the & is removed below
            return 'RT_FN(' + rt + ')'
        new = tok.sub(sub, code)
        new = re.sub(r'&\s*RT_FN\(', 'RT_FN(', new)
        if re.search(r'extern\b[^;]*' + re.escape(rt), new) and 'RT_FN' in new:
            report.append('%s: manual (declaration line): %s' % (rel, ln.strip()))
        out.append(new + sep + comment)
        if in_asm and re.search(r'\)"\s*(\)|;|:)', ln):
            in_asm = False
    return '\n'.join(out)


def drop_patch(binary, pid, dry):
    for f in sorted(glob.glob(os.path.join(ROOT, 'asm', 'patches', '*.json'))):
        text = open(f, encoding='utf-8', newline='').read()
        if json.loads(text).get('binary') != binary:
            continue
        m = None
        for mm in re.finditer(r'"id"\s*:\s*"([^"]+)"', text):
            if re.sub(r'\W', '_', mm.group(1)) == pid:
                m = mm
        if not m:
            continue
        start = text.rfind('{', 0, m.start())
        # matching brace (string aware)
        depth = 0
        i = start
        instr = False
        while i < len(text):
            c = text[i]
            if instr:
                if c == '\\':
                    i += 1
                elif c == '"':
                    instr = False
            else:
                if c == '"':
                    instr = True
                elif c == '{':
                    depth += 1
                elif c == '}':
                    depth -= 1
                    if depth == 0:
                        break
            i += 1
        end = i + 1
        # remove the element with its line(s) and one separating comma
        ls = text.rfind('\n', 0, start) + 1
        rest = text[end:]
        m2 = re.match(r'[ \t]*,[ \t]*\r?\n', rest)
        if m2:
            new = text[:ls] + rest[m2.end():]
        else:                                    # last element: also drop the comma after the previous one
            m3 = re.match(r'[ \t]*\r?\n', rest)
            tail = rest[m3.end():] if m3 else rest
            pre = text[:ls].rstrip()
            if pre.endswith('['):                # the only element: leave an empty array
                new = text[:ls] + tail
            else:
                assert pre.endswith(','), pid
                new = pre[:-1] + text[len(pre):ls] + tail
        json.loads(new)
        if not dry:
            open(f, 'w', encoding='utf-8', newline='').write(new)
        return os.path.basename(f)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stubs', nargs='+', help='<bin>:<LABEL>, bin = mog|program')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--skip-file', action='append', default=[])
    ap.add_argument('--force', action='store_true', help='ignore the live-asm-caller check')
    a = ap.parse_args()
    import asm_remaining as AR
    res = AR.analyse()
    files = source_files(set(a.skip_file))
    texts = {f: open(os.path.join(ROOT, f), encoding='utf-8', newline='').read() for f in files}
    done = []
    report = []
    for spec in a.stubs:
        binary, label = spec.split(':')
        sym = PFX[binary] + label
        rt, pid = find_stub(binary, label)
        if not rt:
            report.append('%s: no JMP rt_ stub found (already cut over?)' % spec)
            continue
        inb = res['inbound'].get(sym, set())
        bad = sorted('%s:%s' % (k, l) for k, l in inb if k in ('call', 'ptr', 'data', 'fall'))
        if bad and not a.force:
            report.append('%s (%s): STAYS, live asm reaches it: %s' % (spec, rt, ', '.join(bad)))
            continue
        for f in files:
            new = rewrite(texts[f], sym, rt, f, report)
            if new != texts[f]:
                texts[f] = new
        done.append((spec, rt, pid))
    # includes for RT_FN
    for f in files:
        t = texts[f]
        if 'RT_FN(' in t and '#include "rt/stubfn.h"' not in t:
            m = list(re.finditer(r'(?m)^#include\s+[<"][^\n]*\n', t))
            if m:
                at = m[-1].end()
                t = t[:at] + '#include "rt/stubfn.h"\n' + t[at:]
            else:
                report.append('%s: needs #include "rt/stubfn.h"' % f)
            texts[f] = t
    changed = [f for f in files if texts[f] != open(os.path.join(ROOT, f), encoding='utf-8', newline='').read()]
    for spec, rt, pid in done:
        pf = drop_patch(spec.split(':')[0], pid, not a.apply)
        print('%-22s -> %-30s patch %s in %s' % (spec, rt, pid, pf))
    if a.apply:
        for f in changed:
            open(os.path.join(ROOT, f), 'w', encoding='utf-8', newline='').write(texts[f])
    print('files %s: %s' % ('rewritten' if a.apply else 'that would change', ', '.join(changed)))
    for r in report:
        print('NOTE', r)


if __name__ == '__main__':
    main()
