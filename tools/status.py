#!/usr/bin/env python3
"""status.py -- the single writer of `status:` in tools/symbols.yaml (ROADMAP H3).

    py tools/status.py                # check only: report drift (exit 1), uses build/diff/host/results.json
    py tools/status.py --sync         # replay all cases (run_host), then rewrite statuses
    py tools/status.py --sync --from-results   # rewrite from the last results.json without replaying
    (same as: py tools/lift.py --sync-status)

Rules (statuses `idiomatic` and `native` are never touched by the tool):
  * lifted file under src/lifted AND case file with >0 cases AND every case PASSes -> `lifted`
  * `lifted` entry whose lifted file vanished, or whose cases are missing/failing   -> `asm`
  * a proven routine without a symbols.yaml entry gets one containing only `status:`
  * lifted but unproven routines (0 cases / failing / stale) stay `asm`; they are listed.
Human fields and comments in symbols.yaml are preserved: the file is edited line by line.
"""
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "diffharness"))

SYMBOLS = ROOT / "tools" / "symbols.yaml"
RESULTS = ROOT / "build" / "diff" / "host" / "results.json"
AUTO = ("asm", "lifted")        # the only statuses this tool sets / resets
SYMBINS = ("mog", "program")    # nb's lifted ops are harness self-tests, not routines


def routine_label(binary, reg_label):
    """Registry label (lab_<x>) -> symbols.yaml label, or None if it is not a routine."""
    if binary == "program" and reg_label.startswith("program_"):
        return reg_label[len("program_"):]
    if binary == "mog" and reg_label.startswith("mog_"):
        return reg_label[len("mog_"):]
    if reg_label.startswith("SELF_"):
        return None
    return reg_label


def lifted_on_disk(found=None):
    """-> {(binary, symbols label): registry label} for routines that have a lifted function."""
    if found is None:
        import run_host
        found = run_host.scan()
    out = {}
    for (b, reg) in found:
        if b not in SYMBINS:
            continue
        lab = routine_label(b, reg)
        if lab:
            out[(b, lab)] = reg
    return out


def classify(disk, results):
    """-> (proven, unproven): proven = set of (bin, label); unproven = {(bin, label): why}.
    results: {(binary, registry label): (result, ncases)}."""
    proven, unproven = set(), {}
    for key, reg in disk.items():
        res = results.get((key[0], reg))
        if res is None:
            unproven[key] = "no case file"
        elif res[1] == 0:
            unproven[key] = "0 cases"
        elif res[0] != "PASS":
            unproven[key] = res[0].lower()
        else:
            proven.add(key)
    return proven, unproven


def load_results(path=RESULTS):
    if not Path(path).exists():
        return None
    raw = json.loads(Path(path).read_text())
    out = {}
    for k, v in raw.items():
        b, lab = k.split("/", 1)
        out[(b, lab)] = (v["result"], v["cases"])
    return out


# ---------------------------------------------------------------- line-based yaml updater
_ENTRY = re.compile(r'^  (?P<q>["\']?)(?P<label>[^"\':\s][^"\':]*)(?P=q):\s*(#.*)?$')
_TOP = re.compile(r'^(?P<bin>[A-Za-z_][\w-]*):\s*(#.*)?$')
_STATUS = re.compile(r'^(?P<pre>    status:\s*)(?P<val>[A-Za-z_]+)(?P<post>\s*(#.*)?)$')


def current_statuses(text):
    """-> {(binary, label): status or None} for every entry in the file."""
    out, binary, entry = {}, None, None
    for ln in text.split("\n"):
        m = _TOP.match(ln)
        if m:
            binary, entry = m.group("bin"), None
            continue
        m = _ENTRY.match(ln)
        if m and binary:
            entry = (binary, m.group("label"))
            out[entry] = None
            continue
        m = _STATUS.match(ln)
        if m and entry:
            out[entry] = m.group("val")
    return out


def apply_statuses(text, changes):
    """changes: {(binary, label): status}.  Returns new text; only status lines are touched
    (or added).  Entries/sections that do not exist are appended."""
    lines = text.split("\n")
    pending = dict(changes)
    # pass 1: locate sections/entries
    binary, entry = None, None
    spans = {}        # (bin,label) -> [start, last_field_line]
    sec_end = {}      # bin -> index after the last entry line of the section
    status_at = {}    # (bin,label) -> line index of the status line
    for i, ln in enumerate(lines):
        m = _TOP.match(ln)
        if m:
            binary, entry = m.group("bin"), None
            sec_end[binary] = i + 1
            continue
        m = _ENTRY.match(ln)
        if m and binary:
            entry = (binary, m.group("label"))
            spans[entry] = [i, i]
            sec_end[binary] = i + 1
            continue
        if entry and ln.startswith("    ") and ln.strip() and not ln.lstrip().startswith("#"):
            spans[entry][1] = i
            sec_end[binary] = i + 1
            if _STATUS.match(ln):
                status_at[entry] = i
    inserts = []      # (index to insert before, [lines])
    for key, st in list(pending.items()):
        if key in status_at:
            i = status_at[key]
            m = _STATUS.match(lines[i])
            lines[i] = m.group("pre") + st + m.group("post")
        elif key in spans:
            inserts.append((spans[key][1] + 1, ["    status: " + st]))
        elif key[0] in sec_end:
            inserts.append((sec_end[key[0]], ["  %s:" % key[1], "    status: " + st]))
        else:
            continue
        del pending[key]
    for idx, new in sorted(inserts, key=lambda t: t[0], reverse=True):
        lines[idx:idx] = new
    if pending:      # binary sections that do not exist yet
        while lines and lines[-1] == "":
            lines.pop()
        for b in sorted({k[0] for k in pending}):
            lines += ["", b + ":"]
            for (bb, lab), st in sorted(pending.items()):
                if bb == b:
                    lines += ["  %s:" % lab, "    status: " + st]
        lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------- policy
def plan(current, disk, results):
    """-> (changes, unproven, drift).  current: {(bin,label): status|None}.
    drift: human-readable list of mismatches between the file and reality."""
    proven, unproven = classify(disk, results)
    changes, drift = {}, []
    for key in sorted(proven):
        st = current.get(key)           # None: absent or no status field
        if st in (None, "asm"):
            changes[key] = "lifted"
            drift.append("%s/%s: lifted on disk and replay PASS but not marked (status %s)"
                         % (key[0], key[1], st or "absent"))
    for key, st in sorted(current.items()):
        if st == "lifted" and key not in proven:
            changes[key] = "asm"
            if key not in disk:
                drift.append("%s/%s: marked lifted but its lifted file is gone" % key)
            else:
                drift.append("%s/%s: marked lifted but %s" % (key[0], key[1], unproven[key]))
    return changes, unproven, drift


def check(symbols=SYMBOLS, results=None):
    """-> (drift list, unproven dict, note).  Needs a results.json from run_host (or `results`)."""
    if results is None:
        results = load_results()
    if results is None:
        return [], {}, "no results.json (run tools/diffharness/run_host.py first)"
    cur = current_statuses(Path(symbols).read_text(encoding="utf-8"))
    changes, unproven, drift = plan(cur, lifted_on_disk(), results)
    return drift, unproven, ""


def sync(symbols=SYMBOLS, results=None, disk=None):
    symbols = Path(symbols)
    text = symbols.read_text(encoding="utf-8")
    cur = current_statuses(text)
    changes, unproven, drift = plan(cur, disk if disk is not None else lifted_on_disk(), results)
    new = apply_statuses(text, changes)
    if new != text:
        with open(symbols, "w", encoding="utf-8", newline="\n") as f:
            f.write(new)
    return changes, unproven


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--sync", action="store_true", help="rewrite statuses in symbols.yaml")
    ap.add_argument("--from-results", action="store_true", help="do not replay; use results.json")
    a = ap.parse_args(argv)
    if a.sync:
        if a.from_results:
            results = load_results()
            if results is None:
                print("no results.json; run without --from-results", file=sys.stderr)
                return 2
        else:
            import run_host
            run_host.run()                  # failures are fine here: they just stay asm
            results = load_results()
        changes, unproven = sync(results=results)
        n_l = sum(1 for v in changes.values() if v == "lifted")
        print("status sync: %d set lifted, %d reset to asm" % (n_l, len(changes) - n_l))
        print("unproven (stay asm): %d" % len(unproven))
        for (b, lab), why in sorted(unproven.items()):
            print("  %s/%s: %s" % (b, lab, why))
        return 0
    drift, unproven, note = check()
    if note:
        print(note)
    for d in drift:
        print("DRIFT", d)
    print("status check: %d drift, %d unproven (stay asm)" % (len(drift), len(unproven)))
    return 1 if drift else 0


if __name__ == "__main__":
    sys.exit(main())
