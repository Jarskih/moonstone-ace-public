#!/usr/bin/env python3
"""check.py -- the one command behind every milestone gate (ROADMAP H2).

    py tools/check.py                # all steps, missing case files regenerated first
    py tools/check.py --quick        # unit tests, resource verify, routines, survey (no replay, no builds)
    py tools/check.py --builds       # also cmake --build build, build-debug, build-game-debug, build-enh, build-synth (those that exist)
    py tools/check.py --regen        # regenerate ALL replay case files (deterministic: n=500 seed=1; nb self-tests n=120 seed=7)

--regen forgets the "fuzzing gave no cases" marker (build/diff/host/nocases.json).
Steps: 0b RAII lint (tests/test_raii.py, ROADMAP 9.2b)  0 layer lint (tests/test_layers.py, ROADMAP 9.3c)  1 unittest  2 resource --verify  3 routines (symbols validation)  4 run_host (replay of
every lifted routine)  4b status ladder drift (lifted+PASS but not marked / marked but gone;
uses the fresh replay results)  5 lift --survey (fails only if it crashes)  6 builds (optional).
Exit status is non-zero if any step fails.  Needs the PATH from AGENTS.md for --builds and python
deps (pyyaml); clang++ for run_host.
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PY = sys.executable or "py"
sys.path.insert(0, str(HERE))
import origin  # noqa: E402  (ROADMAP 10.2: the listing-based steps skip in a public tree)
NO_LISTING = "skipped: " + origin.NO_LISTING
BUILD_DIRS = ("build", "build-debug", "build-game-debug", "build-enh", "build-synth")   # all the game since 7.1r; an absent dir is skipped


def run(cmd, tail=15):
    """Run a command, stream nothing; print the last lines on failure.  -> (ok, output)"""
    p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, encoding="utf-8", errors="replace")
    if p.returncode != 0:
        print("\n---- FAILED: %s (exit %d) ----" % (" ".join(map(str, cmd)), p.returncode))
        print("\n".join(p.stdout.splitlines()[-tail:]))
    return p.returncode == 0, p.stdout


# ---------------------------------------------------------------- case regeneration
def expected_case_files(found):
    """-> [(binary, registry label, Path)] for every registered lifted function."""
    return [(b, lab, ROOT / "tests" / "diff" / b / (lab + ".json")) for (b, lab) in sorted(found)]


def regen_cases(force=False):
    """Regenerate missing (or all, with force) case files.  -> (ok, note)"""
    sys.path.insert(0, str(HERE))
    sys.path.insert(0, str(HERE / "diffharness"))
    import run_host
    import status
    found = run_host.scan()
    marker = ROOT / "build" / "diff" / "host" / "nocases.json"     # fuzzing yielded no cases: don't retry
    try:
        nocases = set(json.loads(marker.read_text()))
    except (OSError, ValueError):
        nocases = set()
    todo = [(b, lab, p) for b, lab, p in expected_case_files(found)
            if force or (not p.exists() and "%s/%s" % (b, lab) not in nocases)]
    if force:
        nocases = set()
    if not todo:
        return True, "all case files present"
    ok = True
    nb = [t for t in todo if t[0] == "nb"]
    rest = [t for t in todo if t[0] != "nb"]
    if nb:      # selftest generator writes the whole nb set (cheap, seeded)
        good, _ = run([PY, str(HERE / "diffharness" / "gen_selftest.py"), "--n", "120", "--seed", "7"])
        ok &= good
    if rest:
        import lift
        import harness
        progs, harnesses = {}, {}
        for b, lab, p in rest:
            name = status.routine_label(b, lab)
            if name is None:
                continue
            if b not in progs:
                progs[b] = lift.Program(b)
                harnesses[b] = harness.Harness(b)
            prog = progs[b]
            try:
                _, em = lift.lift_routine(prog, name, set(prog.routines), True)
                cnt, mode, _ = lift.gen_cases(harnesses[b], prog, b, name, em, 500, 1)
            except Exception as e:      # noqa: BLE001 - report and keep going
                print("regen %s/%s failed: %s" % (b, lab, e))
                ok = False
                continue
            if cnt == 0:
                print("regen %s/%s: no cases (stays unproven)" % (b, lab))
                nocases.add("%s/%s" % (b, lab))
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps(sorted(nocases)))
    return ok, "regenerated %d case file(s), %d without cases" % (len(todo), len(nocases))


# ---------------------------------------------------------------- steps
def step_unittest():
    return run([PY, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py"])[0]


def step_layers():
    """ROADMAP 9.3c: include/pointer lint of the layers (tests/test_layers.py, allow-list tests/layers_allow.json)."""
    return run([PY, "-m", "unittest", "tests.test_layers"])[0]


def step_raii():
    """ROADMAP 9.2b: no new raw acquire/release pairs (tests/test_raii.py, allow-list tests/raii_allow.json), guards non-copyable."""
    return run([PY, "-m", "unittest", "tests.test_raii"])[0]


def step_resource():
    if not origin.have_listing():
        return True, NO_LISTING
    return run([PY, "tools/resource.py", "--verify"])[0]


def step_routines():
    if not origin.have_listing():
        return True, NO_LISTING
    return run([PY, "tools/routines.py"])[0]


def step_replay(regen, quick_note):
    if not origin.have_listing():
        quick_note.append(NO_LISTING)
        return True
    ok, note = regen_cases(force=regen)
    quick_note.append(note)
    good, _ = run([PY, "tools/diffharness/run_host.py"], tail=25)
    return ok and good


def step_status():
    if not origin.have_listing():
        return True, NO_LISTING
    sys.path.insert(0, str(HERE))
    sys.path.insert(0, str(HERE / "diffharness"))
    import status
    drift, unproven, note = status.check()
    for d in drift:
        print("DRIFT", d)
    if note:
        print("status: " + note)
    if drift:
        print("fix: py tools/status.py --sync --from-results")
    return not drift, "%d unproven (stay asm)" % len(unproven) if not note else note


def step_survey():
    if not origin.have_listing():
        return True, NO_LISTING
    return run([PY, "tools/lift.py", "--survey"])[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quick", action="store_true", help="skip run_host and builds")
    ap.add_argument("--builds", action="store_true", help="also cmake --build the build dirs that exist (build, build-debug, build-game-debug, build-enh, build-synth)")
    ap.add_argument("--regen", action="store_true", help="regenerate all replay case files")
    a = ap.parse_args()

    rows = []       # (step, result, seconds, note)

    def do(name, fn):
        t = time.time()
        r = fn()
        note = ""
        if isinstance(r, tuple):
            r, note = r
        rows.append((name, "PASS" if r else "FAIL", time.time() - t, note))

    do("0 layer lint", step_layers)
    do("0b raii lint", step_raii)
    do("1 unittest", step_unittest)
    do("2 resource --verify", step_resource)
    do("3 routines (symbols)", step_routines)
    if a.quick:
        rows.append(("4 run_host", "SKIP", 0.0, "--quick"))
        sys.path.insert(0, str(HERE))
        sys.path.insert(0, str(HERE / "diffharness"))
        do("4b status ladder", step_status)    # uses the last results.json if any
    else:
        notes = []
        t = time.time()
        ok = step_replay(a.regen, notes)
        rows.append(("4 run_host", "PASS" if ok else "FAIL", time.time() - t, "; ".join(notes)))
        do("4b status ladder", step_status)
    do("5 lift --survey", step_survey)
    if a.builds and not a.quick:
        for d in BUILD_DIRS:
            if not (ROOT / d).is_dir():
                rows.append(("6 build " + d, "SKIP", 0.0, "directory does not exist"))
                continue
            do("6 build " + d, lambda d=d: run(["cmake", "--build", str(ROOT / d)], tail=30)[0])
    elif a.builds:
        rows.append(("6 builds", "SKIP", 0.0, "--quick"))

    w = max(len(r[0]) for r in rows)
    print("\n%-*s  %-6s %8s  %s" % (w, "step", "result", "seconds", "note"))
    print("-" * (w + 40))
    for name, res, sec, note in rows:
        print("%-*s  %-6s %8.1f  %s" % (w, name, res, sec, note))
    failed = [r[0] for r in rows if r[1] == "FAIL"]
    print("\ncheck: %s" % ("GREEN" if not failed else "RED (" + ", ".join(failed) + ")"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
