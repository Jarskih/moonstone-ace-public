#!/usr/bin/env python3
"""emulock.py -- the machine-wide emulator lock: one WinUAE job at a time (dev-loop stability, 2026-10-11).

Several agents / worktrees share this machine and its one WinUAE: two boot batches at once starve each other (screenshots go late,
timeouts, SHOT-LATE).  Every job that starts an emulator takes this lock first; the others wait and say who holds it.

    py tools/emulock.py status                 # who holds it, since when (or "free")
    py tools/emulock.py run [--who TEXT] -- CMD ARGS...   # run a command while holding the lock (e.g. a hand-made uaeshot call)
    import emulock; with emulock.EmuLock('integrate --boot'): ...      # what integrate.py --boot and netpair.py do

The lock file is <main checkout root>/.emu.lock (D:\\Amiga\\.emu.lock: the parent of `git rev-parse --git-common-dir`, so every
worktree finds the same file; MS_EMU_LOCK=<path> overrides, MS_EMU_LOCK=off disables the lock).  It holds a line of JSON for people
(pid, who, started, host cwd) and a Windows byte-range lock far beyond its end: the OS drops that lock the moment the holder's
process dies, so a crashed job can never wedge the queue; a file left behind by a dead holder is "stale" and is simply
overwritten by the next taker (status shows it as stale).  uaeshot.ps1 takes the same lock in PowerShell (same byte range) unless a parent
already holds it: a holder exports MS_EMU_LOCK_HELD=<its pid>, and a child that sees its live pid in the file does not lock again, so
integrate.py --boot holds it once for the whole batch while each uaeshot.ps1 call inside runs on it.

Waiting is a poll every 2 s with a message every 30 s ("waiting for <who> (pid, since)"); `--lock-timeout` / MS_EMU_LOCK_TIMEOUT
(seconds, default 0 = for ever) makes it give up with exit status 3.  Not reentrant across unrelated processes by design.
"""
import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
LOCK_OFFSET = 1 << 20            # the byte that is locked; the JSON info stays readable below it (uaeshot.ps1 uses the same)
POLL = 2.0
REPORT_EVERY = 30.0


def lock_path():
    env = os.environ.get("MS_EMU_LOCK", "")
    if env.lower() == "off":
        return None
    if env:
        return Path(env)
    p = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT, stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, text=True)
    common = p.stdout.strip()
    if p.returncode == 0 and common:
        return Path(common).resolve().parent / ".emu.lock"
    return ROOT / "build" / ".emu.lock"


def pid_alive(pid):
    if pid <= 0:
        return False
    if os.name == "nt":
        import ctypes
        k = ctypes.windll.kernel32
        h = k.OpenProcess(0x1000, False, int(pid))      # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return False
        code = ctypes.c_ulong()
        ok = k.GetExitCodeProcess(h, ctypes.byref(code))
        k.CloseHandle(h)
        return bool(ok) and code.value == 259           # STILL_ACTIVE
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def read_info(path):
    """-> dict (pid, who, started, cwd) of whoever wrote the file last, or {}."""
    try:
        text = Path(path).read_bytes()[:4096].split(b"\0")[0].decode("utf-8", "replace").strip()
        return json.loads(text.splitlines()[0]) if text else {}
    except (OSError, ValueError, IndexError):
        return {}


def describe(info):
    if not info:
        return "someone (no info in the lock file)"
    return "%s (pid %s, since %s, in %s)" % (info.get("who", "?"), info.get("pid", "?"), info.get("started", "?"), info.get("cwd", "?"))


def _try_lock(f):
    if os.name == "nt":
        import msvcrt
        f.seek(LOCK_OFFSET)
        try:
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False
    import fcntl
    try:
        fcntl.lockf(f, fcntl.LOCK_EX | fcntl.LOCK_NB, 1, LOCK_OFFSET)
        return True
    except OSError:
        return False


def _unlock(f):
    if os.name == "nt":
        import msvcrt
        f.seek(LOCK_OFFSET)
        try:
            msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
    else:
        import fcntl
        try:
            fcntl.lockf(f, fcntl.LOCK_UN, 1, LOCK_OFFSET)
        except OSError:
            pass


class EmuLock:
    """Context manager: holds the emulator lock for the block.  A no-op when a parent holds it (MS_EMU_LOCK_HELD) or it is off."""

    def __init__(self, who, timeout=None, quiet=False):
        self.who = who
        self.timeout = float(os.environ.get("MS_EMU_LOCK_TIMEOUT", "0")) if timeout is None else timeout
        self.quiet = quiet
        self.f = None
        self.path = lock_path()
        self.owner = False

    def _held_by_parent(self):
        held = os.environ.get("MS_EMU_LOCK_HELD", "")
        return held.isdigit() and pid_alive(int(held)) and read_info(self.path).get("pid") == int(held)

    def acquire(self):
        if self.path is None or self._held_by_parent():
            return self
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.f = open(self.path, "a+b")
        t0 = last = time.time()
        while not _try_lock(self.f):
            now = time.time()
            if self.timeout and now - t0 > self.timeout:
                self.f.close()
                self.f = None
                raise TimeoutError("emulator lock held by " + describe(read_info(self.path)))
            if now - last >= REPORT_EVERY or last == t0:
                if not self.quiet:
                    print("emulock: waiting for the emulator, held by %s (waited %ds)" % (describe(read_info(self.path)), now - t0),
                          flush=True)
                last = now
            time.sleep(POLL)
        info = {"pid": os.getpid(), "who": self.who, "started": datetime.now().strftime("%Y-%m-%d %H:%M:%S"), "cwd": os.getcwd()}
        self.f.seek(0)
        self.f.truncate(0)
        self.f.write((json.dumps(info) + "\n").encode("utf-8"))
        self.f.flush()
        self.owner = True
        os.environ["MS_EMU_LOCK_HELD"] = str(os.getpid())
        if not self.quiet and time.time() - t0 > 1:
            print("emulock: got the emulator after %ds" % (time.time() - t0), flush=True)
        return self

    def release(self):
        if self.f is not None:
            if self.owner:
                try:
                    self.f.seek(0)
                    self.f.truncate(0)
                except OSError:
                    pass
                os.environ.pop("MS_EMU_LOCK_HELD", None)
            _unlock(self.f)
            self.f.close()
            self.f = None
            self.owner = False

    def __enter__(self):
        return self.acquire()

    def __exit__(self, *exc):
        self.release()
        return False


def status():
    path = lock_path()
    if path is None:
        print("emulock: off (MS_EMU_LOCK=off)")
        return 0
    if not path.exists():
        print("emulock: free (%s does not exist yet)" % path)
        return 0
    with open(path, "a+b") as f:
        if _try_lock(f):
            info = read_info(path)
            _unlock(f)
            print("emulock: free (%s)%s" % (path, "; stale info of a dead holder: " + describe(info) if info else ""))
            return 0
    info = read_info(path)
    print("emulock: HELD by %s%s [%s]" % (describe(info), "" if pid_alive(int(info.get("pid", 0) or 0)) else " -- pid is gone?", path))
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    r = sub.add_parser("run", help="run CMD while holding the lock")
    r.add_argument("--who", default="")
    r.add_argument("--lock-timeout", type=float, default=None)
    r.add_argument("argv", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    if a.cmd == "status":
        return status()
    argv = a.argv[1:] if a.argv[:1] == ["--"] else a.argv
    if not argv:
        ap.error("run needs a command after --")
    try:
        with EmuLock(a.who or "emulock run " + " ".join(argv)[:60], a.lock_timeout):
            return subprocess.call(argv)
    except TimeoutError as e:
        print("emulock: " + str(e))
        return 3


if __name__ == "__main__":
    sys.exit(main())
