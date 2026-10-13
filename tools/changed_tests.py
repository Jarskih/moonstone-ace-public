#!/usr/bin/env python3
"""changed_tests.py -- which test modules does a change touch?  (behind `py tools/check.py --changed [REV]`)

    py tools/changed_tests.py [REV]            # list the modules and why (REV default: merge-base of HEAD and moonstone-ace)
    py tools/changed_tests.py --files a.cpp b.hpp   # same for named files instead of a git diff

A deliberately simple mapping, no coverage data.  The changed files are `git diff REV` (committed + uncommitted) plus untracked
files, inside moonstone-ace/.  A test module (tests/test_*.py) is chosen when its text MENTIONS one of

  * the changed file's basename ("fighters.cpp", "emu_lib.py", "tables.yaml", also `import emu_lib` for a .py), or
  * the basename of any source that includes a changed header, transitively (include/ms/foo.hpp -> bar.cpp -> test of bar.cpp),

and a changed tests/test_x.py chooses itself.  ALWAYS_RUN (the cheap structural lints) are added to every selection.  A changed file that no test
mentions is listed as "no test mentions": for those the answer is `check.py --quick` before the merge, not a guess here.
Files that cannot change behaviour (*.md, docs/) select nothing.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
ALWAYS_RUN = ("test_layers", "test_raii", "test_hostcache")      # cheap, structural: layers, RAII lint, the test tooling itself
# changes under these paths select the named modules although no test spells the file name (they read the directory)
PREFIX_RULES = (("tests/boot/", "test_bootcheck"), ("tools/hostcache_site/", "test_hostcache"), ("cmake/", "test_sdl_build"),
                ("tests/mutation_gate.py", "test_hostcache"), ("tests/__init__.py", "test_hostcache"))
IGNORE_SUFFIX = (".md", ".png", ".txt.md")
IGNORE_DIRS = ("docs/",)
SRC_DIRS = ("src", "include")
TOKEN_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.+-]*\.(?:cpp|hpp|h|c|py|json|yaml|yml|s|txt|ps1|cmake|ini)\b")
IMPORT_RE = re.compile(r"^\s*(?:from|import)\s+([A-Za-z_][A-Za-z0-9_]*)", re.M)
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


def git(*args):
    p = subprocess.run(["git"] + list(args), cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                       encoding="utf-8", errors="replace")
    return p.returncode, p.stdout.strip()


def default_rev():
    rc, out = git("merge-base", "HEAD", "moonstone-ace")
    return out if rc == 0 and out else "HEAD"


def changed_files(rev):
    """Files (relative to moonstone-ace/) changed since rev, plus untracked ones."""
    files = set()
    for cmd in (["diff", "--name-only", "--relative", rev], ["ls-files", "--others", "--exclude-standard"]):
        rc, out = git(*cmd)
        if rc == 0:
            files.update(l.strip().replace("\\", "/") for l in out.splitlines() if l.strip())
    return sorted(f for f in files if (ROOT / f).exists() or f.startswith("tests/"))


def mentions(text):
    """Basenames a test module refers to: file names it spells out, and python modules it imports."""
    out = {t.lower() for t in TOKEN_RE.findall(text)}
    out.update(m.lower() + ".py" for m in IMPORT_RE.findall(text))
    return out


def include_graph():
    """-> {header basename: set of basenames of the files that include it directly}."""
    users = {}
    for d in SRC_DIRS:
        for p in (ROOT / d).rglob("*"):
            if p.suffix.lower() not in (".cpp", ".hpp", ".h", ".c", ".inc"):
                continue
            try:
                text = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for inc in INCLUDE_RE.findall(text):
                users.setdefault(Path(inc).name.lower(), set()).add(p.name.lower())
    return users


def affected_names(changed):
    """basename -> why, for every changed file and every file that (transitively) includes a changed header."""
    users = include_graph()
    why = {}
    for f in changed:
        name = Path(f).name.lower()
        why.setdefault(name, "changed " + f)
        frontier = [name]
        while frontier:
            cur = frontier.pop()
            for u in users.get(cur, ()):
                if u not in why:
                    why[u] = "includes %s (changed %s)" % (cur, f)
                    frontier.append(u)
    return why


def select(changed, tests_dir=None):
    """-> (modules, reasons {module: [text]}, unmatched [changed file])"""
    tests_dir = Path(tests_dir or ROOT / "tests")
    changed = [f for f in changed if not f.endswith(IGNORE_SUFFIX) and not f.startswith(IGNORE_DIRS)]
    why = affected_names(changed)
    reasons = {}
    used = set()
    for t in sorted(tests_dir.glob("test_*.py")):
        mod = t.stem
        try:
            toks = mentions(t.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            continue
        toks.add(t.name.lower())
        for name, w in why.items():
            if name in toks:
                reasons.setdefault(mod, []).append("%s (%s)" % (name, w))
                used.add(name)
    for f in changed:
        for prefix, mod in PREFIX_RULES:
            if f.startswith(prefix) and (tests_dir / (mod + ".py")).exists():
                reasons.setdefault(mod, []).append("%s is under %s" % (f, prefix))
                used.add(Path(f).name.lower())
        if f.startswith("tests/test_") and f.endswith(".py"):
            reasons.setdefault(Path(f).stem, []).append("the test itself changed")
            used.add(Path(f).name.lower())
    for m in ALWAYS_RUN:
        if (tests_dir / (m + ".py")).exists():
            reasons.setdefault(m, []).append("always (cheap structural test)")
    unmatched = [f for f in changed if Path(f).name.lower() not in used and not any(
        w.endswith(f) and n in used for n, w in why.items())]
    return sorted(reasons), reasons, unmatched


def print_unmatched(unmatched, limit=8):
    for f in unmatched[:limit]:
        print("  no test mentions %s" % f)
    if len(unmatched) > limit:
        print("  ... and %d more files no test mentions" % (len(unmatched) - limit))


def main():
    args = sys.argv[1:]
    if args[:1] == ["--files"]:
        changed, rev = args[1:], "(named files)"
    else:
        rev = args[0] if args else default_rev()
        changed = changed_files(rev)
    mods, reasons, unmatched = select(changed)
    print("changed since %s: %d file(s)" % (rev, len(changed)))
    for m in mods:
        print("  %-26s %s" % (m, "; ".join(reasons[m][:3]) + (" ..." if len(reasons[m]) > 3 else "")))
    print_unmatched(unmatched)
    return 0


if __name__ == "__main__":
    sys.exit(main())
