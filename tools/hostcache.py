"""hostcache.py -- content-addressed cache for the host clang++ test drivers (dev-loop speed, 2026-10-11).

The unit tests compile small host drivers (`clang++ -std=c++17 -O1 ... -o driver.exe`) and run them.  Most runs recompile
identical sources.  This module intercepts `subprocess.run` / `check_output` / `check_call` calls whose program is clang or
clang++ and answers a repeat of an identical compile or link from `build/hostcache/v<N>/`:

  key = sha256(CACHE_FORMAT,
               the compiler identity: resolved path + the full `--version` text + `-dumpmachine`   (one run per process),
               the flags, with every path-to-an-include-dir removed (their effect is in the preprocessed text),
               the PREPROCESSED text of each source (`clang -E -P` with the same flags: every header's content, -D, -include),
               the bytes of each object / library given to a link, LIB / LIBRARY_PATH)

so any change of compiler, of a flag, of a source or of any header it includes is a miss; the cache can never serve a binary
made by another compiler (the host clang was upgraded 11 -> 23 on 2026-10-11).  Only the plain forms are cached
(`-c a.cpp -o a.o`, `a.o b.o -o x.exe`, `a.cpp b.cpp -o x.exe`); anything unusual (-E, -S, -M*, -g*, response files, no -o, a
failing preprocess) runs as before.  A failed compile is never stored.  On a hit the warnings of the original compile are not
replayed (tests only look at the exit status).

    MS_HOSTCACHE=0          turn it off (also tools/testrun.py --no-cache)
    MS_HOSTCACHE_DIR=<dir>  another cache directory (default build/hostcache)
    MS_HOSTCACHE_MAX_MB=n   size limit for prune() (default 3000; least recently used entries go first)

CACHE_FORMAT is part of the directory name (v<N>): bump it to invalidate every entry; prune() deletes the directories of other
formats.  Format 2 is the first with a compiler identity in the key (an identity-less format 1 never shipped, but a stale
directory of it would be swept all the same).

Used through tools/hostcache_site/sitecustomize.py (put on PYTHONPATH by tools/testrun.py, so also every Python child of a
test is covered) or by `hostcache.install()`.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

CACHE_FORMAT = 2
PREPROCESS_THREADS = int(os.environ.get("MS_HOSTCACHE_THREADS", "8"))      # parallel `clang -E` runs of one key computation
ROOT = Path(__file__).resolve().parent.parent

SRC_EXT = (".c", ".cc", ".cpp", ".cxx")
OBJ_EXT = (".o", ".obj", ".a", ".lib")
# options followed by a separate value; the include-like ones leave the key (the preprocessed text carries their effect)
INCLUDE_OPTS = ("-I", "-isystem", "-iquote", "-idirafter", "-include", "-imsvc")
VALUE_OPTS = ("-x", "-target", "--target", "-Xclang", "-Xlinker", "-Xpreprocessor", "-mllvm", "-arch", "-L")
NO_CACHE_EXACT = ("-E", "-S", "-fsyntax-only", "--version", "-###", "-emit-llvm", "-dumpmachine", "-v", "--help", "-pg")
NO_CACHE_PREFIX = ("-save-temps", "-print", "--print", "-fprofile", "-fcoverage")
COMPILERS = ("clang", "clang++")

_real_run = subprocess.run
_stats = {"hits": 0, "misses": 0, "uncacheable": 0, "key_seconds": 0.0}
_identity_memo = {}
_hash_memo = {}


def enabled():
    return os.environ.get("MS_HOSTCACHE", "1") != "0"


def cache_root():
    return Path(os.environ.get("MS_HOSTCACHE_DIR") or (ROOT / "build" / "hostcache"))


def cache_dir():
    return cache_root() / ("v%d" % CACHE_FORMAT)


def stats():
    return dict(_stats)


# ---------------------------------------------------------------- compiler identity
def compiler_identity(program):
    """-> (resolved path, full `--version` text, `-dumpmachine` text) of the compiler `program` names; run once per process."""
    exe = shutil.which(program) or program
    path = os.path.realpath(exe)
    if path not in _identity_memo:
        def ask(flag):
            r = _real_run([exe, flag], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
            return r.stdout.strip() if r.returncode == 0 else "unknown(%d)" % r.returncode
        _identity_memo[path] = (path, ask("--version"), ask("-dumpmachine"))
    return _identity_memo[path]


def make_key(identity, key_args, source_texts, input_hashes, extra_env=()):
    """The cache key (hex).  identity = (path, version text, machine); key_args: the flags that matter, in order;
    source_texts: the preprocessed text (bytes) of each source; input_hashes: the digests of object / library files."""
    h = hashlib.sha256()

    def put(tag, data):
        if isinstance(data, str):
            data = data.encode("utf-8", "surrogateescape")
        h.update(tag.encode() + b"\0" + str(len(data)).encode() + b"\0" + data + b"\0")

    put("format", str(CACHE_FORMAT))
    for i, part in enumerate(identity):
        put("compiler%d" % i, part)
    for a in key_args:
        put("arg", a)
    for t in source_texts:
        put("source", t if isinstance(t, bytes) else t.encode("utf-8", "surrogateescape"))
    for d in input_hashes:
        put("input", d)
    for e in extra_env:
        put("env", e)
    return h.hexdigest()


# ---------------------------------------------------------------- command line analysis
class Plan:
    """What a cacheable clang command line consists of."""
    def __init__(self):
        self.out = None
        self.compile_only = False
        self.sources = []
        self.inputs = []
        self.key_args = []       # flags that belong in the key
        self.pp_args = []        # flags for the preprocess run (all but -c, -o, sources, link inputs)


def analyse(args, cwd):
    """-> Plan, or None when the command line is not one of the plain cacheable forms."""
    p = Plan()
    i = 1
    n = len(args)
    while i < n:
        a = args[i]
        if a.startswith("@") or a == "-" or a.startswith("-M") or a.startswith("-g") or a.startswith("-Z"):
            return None
        if a == "-c":
            p.compile_only = True
        elif a == "-o":
            if i + 1 >= n:
                return None
            p.out = args[i + 1]
            i += 1
        elif a.startswith("-o") and len(a) > 2:
            p.out = a[2:]
        elif a in INCLUDE_OPTS:
            if i + 1 >= n:
                return None
            p.pp_args += [a, args[i + 1]]
            i += 1
        elif any(a.startswith(o) and len(a) > len(o) for o in INCLUDE_OPTS if len(o) == 2):      # -Idir
            p.pp_args.append(a)
        elif a.startswith("-"):
            if a in NO_CACHE_EXACT or a.startswith(NO_CACHE_PREFIX):
                return None
            if a in VALUE_OPTS:
                if i + 1 >= n:
                    return None
                p.key_args += [a, args[i + 1]]
                p.pp_args += [a, args[i + 1]]
                i += 1
            else:
                p.key_args.append(a)
                p.pp_args.append(a)
        else:
            full = os.path.join(cwd, a) if not os.path.isabs(a) else a
            low = a.lower()
            if low.endswith(SRC_EXT) and os.path.isfile(full):
                p.sources.append(full)
            elif low.endswith(OBJ_EXT) and os.path.isfile(full):
                p.inputs.append(full)
            else:
                return None            # an argument this module does not understand
        i += 1
    if not p.out or (not p.sources and not p.inputs):
        return None
    if p.compile_only and (p.inputs or len(p.sources) != 1):
        return None
    return p


def file_digest(path):
    st = os.stat(path)
    tag = (path, st.st_mtime_ns, st.st_size)
    if tag not in _hash_memo:
        _hash_memo[tag] = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    return _hash_memo[tag]


def preprocess(exe, plan, source, cwd):
    """The preprocessed text of one source with the command's flags, or None if that fails."""
    r = _real_run([exe, "-E", "-P", "-Wno-unused-command-line-argument"] + plan.pp_args + [source], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return r.stdout if r.returncode == 0 else None


def key_for(args, cwd):
    """-> (key, Plan) for a cacheable clang command line, else (None, None)."""
    plan = analyse(args, cwd)
    if plan is None:
        return None, None
    exe = args[0]
    t0 = time.time()
    compiler_identity(exe)                  # asked once, before the threads below start
    # one `clang -E` per source is a process start each (~0.1 s on Windows, and a driver can have 150 sources): run them side by side
    if len(plan.sources) > 1:
        with ThreadPoolExecutor(max_workers=min(PREPROCESS_THREADS, len(plan.sources))) as pool:
            texts = list(pool.map(lambda src: preprocess(exe, plan, src, cwd), plan.sources))
    else:
        texts = [preprocess(exe, plan, src, cwd) for src in plan.sources]
    _stats["key_seconds"] += time.time() - t0
    if any(t is None for t in texts):
        return None, None
    inputs = [file_digest(f) for f in plan.inputs]
    env = tuple("%s=%s" % (k, os.environ.get(k, "")) for k in ("LIB", "LIBRARY_PATH"))
    key = make_key(compiler_identity(exe), ["compile" if plan.compile_only else "link"] + plan.key_args, texts, inputs, env)
    return key, plan


# ---------------------------------------------------------------- the interception
def _is_clang(args):
    if isinstance(args, (str, bytes)) or not args:
        return False
    try:
        first = os.fspath(args[0])
    except TypeError:
        return False
    if not isinstance(first, str):
        return False
    base = os.path.basename(first).lower()
    if base.endswith(".exe"):
        base = base[:-4]
    return base in COMPILERS


def _completed(args, kw):
    capture = kw.get("capture_output") or kw.get("stdout") == subprocess.PIPE
    text = kw.get("text") or kw.get("universal_newlines") or kw.get("encoding") or kw.get("errors")
    out = ("" if text else b"") if capture else None
    err = ("" if text else b"") if (kw.get("capture_output") or kw.get("stderr") == subprocess.PIPE) else None
    return subprocess.CompletedProcess(args, 0, out, err)


def cached_run(*popenargs, **kw):
    """Replacement for subprocess.run."""
    args = popenargs[0] if popenargs else kw.get("args")
    if not enabled() or kw.get("shell") or not _is_clang(args) or kw.get("input") is not None:
        return _real_run(*popenargs, **kw)
    args = [os.fspath(a) for a in args]
    cwd = os.fspath(kw.get("cwd") or os.getcwd())
    try:
        key, plan = key_for(args, cwd)
    except OSError:
        key = None
    if key is None:
        _stats["uncacheable"] += 1
        return _real_run(*popenargs, **kw)
    entry = cache_dir() / key[:2] / key
    out = plan.out if os.path.isabs(plan.out) else os.path.join(cwd, plan.out)
    if entry.is_file():
        try:
            os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
            shutil.copy(entry, out)
            os.utime(entry, None)
            _stats["hits"] += 1
            return _completed(args, kw)
        except OSError:
            pass                        # fall through: compile for real
    _stats["misses"] += 1
    result = _real_run(*popenargs, **kw)
    if result.returncode == 0 and os.path.isfile(out):
        try:
            entry.parent.mkdir(parents=True, exist_ok=True)
            fd, tmp = tempfile.mkstemp(dir=entry.parent, prefix="tmp-")
            os.close(fd)
            shutil.copy(out, tmp)
            os.replace(tmp, entry)       # atomic: parallel workers may store the same key
        except OSError:
            pass
    return result


def cached_check_call(*popenargs, **kw):
    cached_run(*popenargs, check=True, **kw)
    return 0


def install():
    """Route clang compiles of this process through the cache (idempotent)."""
    if subprocess.run is cached_run or not enabled():
        return
    subprocess.run = cached_run
    subprocess.check_call = cached_check_call


def prune(max_bytes=None):
    """Delete cache directories of other formats, then the least recently used entries beyond the size limit."""
    root = cache_root()
    if not root.is_dir():
        return
    mine = cache_dir().name
    for d in root.iterdir():
        if d.name != mine:
            if d.is_dir():
                shutil.rmtree(d, ignore_errors=True)
            else:
                d.unlink()
    limit = max_bytes if max_bytes is not None else int(os.environ.get("MS_HOSTCACHE_MAX_MB", "3000")) * 1024 * 1024
    files = []
    total = 0
    for dp, _, fns in os.walk(cache_dir()):
        for fn in fns:
            f = os.path.join(dp, fn)
            try:
                st = os.stat(f)
            except OSError:
                continue
            files.append((st.st_mtime, st.st_size, f))
            total += st.st_size
    for _, size, f in sorted(files):
        if total <= limit:
            break
        try:
            os.remove(f)
            total -= size
        except OSError:
            pass


def clear():
    shutil.rmtree(cache_root(), ignore_errors=True)


if __name__ == "__main__":
    if "--clear" in sys.argv:
        clear()
        print("cleared", cache_root())
    elif "--prune" in sys.argv:
        prune()
    else:
        n = size = 0
        for dp, _, fns in os.walk(cache_root()):
            for fn in fns:
                n += 1
                size += os.path.getsize(os.path.join(dp, fn))
        print("%s: %d entries, %.1f MB (format %d)" % (cache_root(), n, size / 1e6, CACHE_FORMAT))
