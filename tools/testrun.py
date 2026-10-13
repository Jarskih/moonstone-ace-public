#!/usr/bin/env python3
"""testrun.py -- run test modules in parallel, with timing (dev-loop speed, 2026-10-11).

    py tools/testrun.py                         # every tests/test_*.py, jobs = cores/2, mutation tests skipped
    py tools/testrun.py test_copper test_paula  # some modules (tests.test_x, test_x, or a path all work)
    py tools/testrun.py --mutations             # also the mutation tests (tests/mutation_gate.py; same as MS_MUTATIONS=1)
    py tools/testrun.py --jobs 8 --top 20       # 8 workers; print the 20 slowest tests and modules
    py tools/testrun.py --serial-times          # (re)measure: one worker, the numbers are not inflated by contention

Each module runs in its own Python process (`--worker`), so modules cannot disturb each other's globals; the host compile cache
(tools/hostcache.py) is active in every worker and in the processes they start (MS_HOSTCACHE=0 turns it off).  Modules
are scheduled longest first from the durations of the last run (build/testtimes.json), so the pool does not end with one big
module alone.  Modules listed in SERIAL_GROUPS must not overlap with each other (they share a work directory or a port).

Exit status is non-zero if any module fails.  The summary names the failed tests; the full output of each module
is in build/testrun/<module>.log.
"""
import argparse
import io
import json
import os
import subprocess
import sys
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PY = sys.executable or "py"
OUT_DIR = ROOT / "build" / "testrun"
TIMES = ROOT / "build" / "testtimes.json"
DEFAULT_TIMEOUT = 3600

# modules that must not run at the same time (shared work dir / fixed resource); filled from experience
SERIAL_GROUPS = []


def default_jobs():
    return max(1, (os.cpu_count() or 2) // 2)


def all_modules():
    return sorted(p.stem for p in (ROOT / "tests").glob("test_*.py"))


def norm(name):
    """'tests/test_x.py' | 'tests.test_x' | 'test_x' -> 'test_x'"""
    return Path(name).stem if name.endswith(".py") else name.rsplit(".", 1)[-1]


# ---------------------------------------------------------------- worker side
class TimedResult(unittest.TextTestResult):
    def startTest(self, test):
        self._t0 = time.time()
        super().startTest(test)

    def _record(self, test, status):
        self.times.append((test.id(), time.time() - getattr(self, "_t0", time.time()), status))

    times = None

    def addSuccess(self, test):
        super().addSuccess(test)
        self._record(test, "ok")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self._record(test, "FAIL")

    def addError(self, test, err):
        super().addError(test, err)
        self._record(test, "ERROR")

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._record(test, "skip:" + ("mutation" if reason.startswith("mutation test") else "other"))

    def addExpectedFailure(self, test, err):
        super().addExpectedFailure(test, err)
        self._record(test, "ok")

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self._record(test, "FAIL")


def worker(module, out_json):
    sys.path.insert(0, str(ROOT))
    os.chdir(ROOT)
    from tests import mutation_gate           # also what `import tests` does
    mutation_gate.install()
    t0 = time.time()
    stream = io.StringIO()
    TimedResult.times = []
    runner = unittest.TextTestRunner(stream=stream, verbosity=2, resultclass=TimedResult)
    try:
        suite = unittest.defaultTestLoader.loadTestsFromName("tests." + module)
        res = runner.run(suite)
        ok, ran = res.wasSuccessful(), res.testsRun
        failed = [t.id() for t, _ in res.failures + res.errors]
    except BaseException as e:      # import error etc.: loadTestsFromName turns most into a failing test, keep a net anyway
        stream.write("worker crashed: %r\n" % (e,))
        ok, ran, failed = False, 0, ["<worker>"]
    stats = {}
    try:
        import hostcache
        stats = hostcache.stats()
    except Exception:       # noqa: BLE001 - statistics are optional
        pass
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / (module + ".log")).write_text(stream.getvalue(), encoding="utf-8")
    Path(out_json).write_text(json.dumps({
        "module": module, "ok": ok, "ran": ran, "failed": failed, "seconds": time.time() - t0,
        "tests": TimedResult.times, "cache": stats}), encoding="utf-8")
    return 0 if ok else 1


# ---------------------------------------------------------------- parent side
def child_env(mutations, cache=True):
    env = dict(os.environ)
    if mutations:
        env["MS_MUTATIONS"] = "1"
    elif "MS_MUTATIONS" not in env:
        env["MS_MUTATIONS"] = "0"
    if cache and env.get("MS_HOSTCACHE", "1") != "0":
        # only hostcache_site (a lone sitecustomize.py): tools/ itself must not shadow stdlib modules such as `resource`
        env["PYTHONPATH"] = os.pathsep.join([str(HERE / "hostcache_site")] + ([env["PYTHONPATH"]] if env.get("PYTHONPATH") else []))
    else:
        env["MS_HOSTCACHE"] = "0"
    return env


def run_module(module, env, timeout):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_json = OUT_DIR / (module + ".json")
    if out_json.exists():
        out_json.unlink()
    t0 = time.time()
    try:
        p = subprocess.run([PY, str(Path(__file__).resolve()), "--worker", module, "--out", str(out_json)], cwd=ROOT, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace",
                           timeout=timeout)
        rc, extra = p.returncode, p.stdout
    except subprocess.TimeoutExpired:
        rc, extra = 124, "timeout after %ds\n" % timeout
    try:
        r = json.loads(out_json.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        r = {"module": module, "ok": False, "ran": 0, "failed": ["<no result>"], "tests": [], "cache": {},
             "seconds": time.time() - t0}
        (OUT_DIR / (module + ".log")).write_text(extra, encoding="utf-8")
    r["rc"] = rc
    r["wall"] = time.time() - t0
    return r


def load_times():
    try:
        return json.loads(TIMES.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_times(old, results):
    new = dict(old)
    for r in results:
        if r["ok"]:
            new[r["module"]] = round(r["seconds"], 1)
    TIMES.parent.mkdir(parents=True, exist_ok=True)
    TIMES.write_text(json.dumps(new, indent=1, sort_keys=True), encoding="utf-8")


def run_modules(modules, jobs=None, mutations=False, cache=True, timeout=DEFAULT_TIMEOUT, quiet=False, top=0):
    """Run the modules in a pool.  -> (ok, results)"""
    jobs = jobs or default_jobs()
    env = child_env(mutations, cache)
    times = load_times()
    order = sorted(modules, key=lambda m: -times.get(m, 30.0))
    lock = threading.Lock()
    group_locks = [threading.Lock() for _ in SERIAL_GROUPS]
    results = []
    t_start = time.time()

    def one(m):
        held = [gl for g, gl in zip(SERIAL_GROUPS, group_locks) if m in g]
        for gl in held:
            gl.acquire()
        try:
            r = run_module(m, env, timeout)
        finally:
            for gl in held:
                gl.release()
        with lock:
            results.append(r)
            if not quiet:
                skipped = sum(1 for _, _, s in r["tests"] if s.startswith("skip:mutation"))
                print("%-4s %-28s %3d tests %7.1fs%s%s" % ("ok" if r["ok"] else "FAIL", r["module"], r["ran"], r["wall"],
                      ("  (%d mutation skipped)" % skipped) if skipped else "",
                      ("  cache %d/%d" % (r["cache"].get("hits", 0), r["cache"].get("hits", 0) + r["cache"].get("misses", 0)))
                      if r.get("cache", {}).get("hits", 0) + r.get("cache", {}).get("misses", 0) else ""), flush=True)
        return r

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        list(ex.map(one, order))
    wall = time.time() - t_start
    results.sort(key=lambda r: r["module"])
    if not quiet:
        report(results, wall, top, jobs)
    if cache:
        try:
            import hostcache
            hostcache.prune()
        except Exception:       # noqa: BLE001
            pass
    if all(r["ok"] for r in results):
        save_times(times, results)
    else:
        save_times(times, [r for r in results if r["ok"]])
    return all(r["ok"] for r in results), results


def report(results, wall, top, jobs):
    failed = [r for r in results if not r["ok"]]
    total = sum(r["seconds"] for r in results)
    hits = sum(r["cache"].get("hits", 0) for r in results)
    miss = sum(r["cache"].get("misses", 0) for r in results)
    skipped = sum(1 for r in results for _, _, s in r["tests"] if s.startswith("skip:mutation"))
    if top:
        print("\nslowest modules (seconds in the worker):")
        for r in sorted(results, key=lambda r: -r["seconds"])[:top]:
            print("  %7.1f  %s" % (r["seconds"], r["module"]))
        print("slowest tests:")
        every = [(s, i, st) for r in results for i, s, st in r["tests"]]
        for s, i, st in sorted(every, reverse=True)[:top]:
            print("  %7.1f  %s  [%s]" % (s, i, st))
    for r in failed:
        print("\n---- FAILED %s: %s" % (r["module"], ", ".join(r["failed"][:8])))
        log = OUT_DIR / (r["module"] + ".log")
        try:
            print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-40:]))
        except OSError:
            pass
    print("\ntestrun: %d module(s), %d test(s), %d failed module(s); wall %.1fs, sum of modules %.1fs (jobs %d)%s%s"
          % (len(results), sum(r["ran"] for r in results), len(failed), wall, total, jobs,
             ("; host compile cache %d hit / %d miss (%.0fs computing keys)" % (hits, miss, sum(r["cache"].get("key_seconds", 0.0) for r in results)))
             if hits + miss else "",
             ("; %d mutation test(s) skipped (--mutations)" % skipped) if skipped else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("modules", nargs="*", help="test modules (default: all tests/test_*.py)")
    ap.add_argument("--jobs", "-j", type=int, default=0, help="parallel workers (default cores/2 = %d)" % default_jobs())
    ap.add_argument("--mutations", action="store_true", help="also run the mutation tests (MS_MUTATIONS=1)")
    ap.add_argument("--no-cache", action="store_true", help="do not use the host compile cache")
    ap.add_argument("--serial-times", action="store_true", help="one worker (clean timings)")
    ap.add_argument("--top", type=int, default=15, help="print the N slowest modules and tests (0 = off)")
    ap.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT, help="seconds per module")
    ap.add_argument("--worker", help=argparse.SUPPRESS)
    ap.add_argument("--out", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.worker:
        return worker(a.worker, a.out)
    mods = [norm(m) for m in a.modules] or all_modules()
    ok, _ = run_modules(mods, 1 if a.serial_times else a.jobs, a.mutations, not a.no_cache, a.timeout, top=a.top)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
