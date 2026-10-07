"""RAII lint (ROADMAP 9.2b, docs/RAII.md, CLAUDE.md "Resources use RAII").

A static scan of src/ (not src/lifted, not src/rt/gen) for NEW raw acquire/release calls that belong in a guard:
  rt_file_open / rt_file_open_exact      -> rt::FileHandle        systemUse / systemUnuse            -> rt::SystemAccess
  systemReleaseBlitterToOs / ...FromOs   -> rt::OsAccess          dos Open / Close, Lock / UnLock    -> rt::DosHandle / DosLock
  memAlloc* / memFree                    -> rt::MemBlock          exec Disable / Enable              -> rt::IrqOff
  pr_WindowPtr                           -> rt::NoRequesters      AllocDosObject, OpenLibrary        -> ms::scopeExit
  memStackMark / memStackRelease         -> ms::MemMark
The guards themselves (src/rt/guards.hpp, include/engine/guard.hpp) are the only places that may call them directly; everything
else is either converted or listed in tests/raii_allow.json with a reason (the documented exceptions of docs/RAII.md: handles
that outlive a call, the overlay switches, never-returning crash code, script-driven ops, the C ABI of rt/files).

Also: every guard type is non-copyable (compile-time MS_GUARD_PINNED / MS_GUARD_MOVABLE static_asserts, compiled on the host for
the engine guards, plus mutation checks) and the rule-of-five spelling of the guard classes is checked textually.

The list can only shrink: an entry whose raw call occurs fewer times than listed fails the test.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOW_FILE = os.path.join(ROOT, 'tests', 'raii_allow.json')
SCAN_DIRS = ('src/game', 'src/engine', 'src/rt', 'src/main.cpp')
SKIP_DIRS = ('src/lifted', 'src/rt/gen')
GUARD_FILES = ('src/rt/guards.hpp', 'include/engine/guard.hpp')
SCAN_EXT = ('.cpp', '.hpp', '.h', '.c')

# kind -> (regex on a comment/string-stripped line, the guard to use)
RULES = {
    'file-open': (re.compile(r'\brt_file_open(?:_exact)?\s*\('), 'rt::FileHandle'),
    'system-use': (re.compile(r'\bsystem(?:Use|Unuse)\s*\('), 'rt::SystemAccess'),
    'blitter-to-os': (re.compile(r'\bsystem(?:ReleaseBlitterToOs|GetBlitterFromOs)\s*\('), 'rt::OsAccess'),
    'dos-open': (re.compile(r'(?<![\w.:>])(?:Open|Close)\s*\('), 'rt::DosHandle'),
    'dos-lock': (re.compile(r'(?<![\w.:>])(?:Lock|UnLock)\s*\('), 'rt::DosLock'),
    'mem-alloc': (re.compile(r'\b(?:memAlloc\w*|memFree)\s*\('), 'rt::MemBlock'),
    'irq-off': (re.compile(r'(?<![\w.:>])(?:Disable|Enable)\s*\(\s*\)'), 'rt::IrqOff'),
    'window-ptr': (re.compile(r'\bpr_WindowPtr\b'), 'rt::NoRequesters'),
    'dos-object': (re.compile(r'\b(?:AllocDosObject|FreeDosObject|OpenLibrary|CloseLibrary)\s*\('), 'ms::scopeExit'),
    'mem-mark': (re.compile(r'\bmemStack(?:Mark|Release)\s*\('), 'ms::MemMark'),
}


def rel(p):
    return os.path.relpath(p, ROOT).replace('\\', '/')


def strip(line, inblock):
    """Remove comments and string literals; -> (code, still inside a block comment)."""
    out = []
    i = 0
    n = len(line)
    while i < n:
        if inblock:
            j = line.find('*/', i)
            if j < 0:
                return ''.join(out), True
            i = j + 2
            inblock = False
            continue
        c = line[i]
        if line.startswith('//', i):
            break
        if line.startswith('/*', i):
            inblock = True
            i += 2
            continue
        if c in '"\'':
            q = c
            i += 1
            while i < n and line[i] != q:
                i += 2 if line[i] == '\\' else 1
            i += 1
            out.append('""')
            continue
        out.append(c)
        i += 1
    return ''.join(out), inblock


def source_files():
    for d in SCAN_DIRS:
        base = os.path.join(ROOT, d)
        if os.path.isfile(base):
            yield base
            continue
        for dp, dn, fn in os.walk(base):
            r = rel(dp)
            if any(r == s or r.startswith(s + '/') for s in SKIP_DIRS):
                dn[:] = []
                continue
            for f in sorted(fn):
                if f.endswith(SCAN_EXT):
                    yield os.path.join(dp, f)


def scan_text(path, text):
    """-> {(file, kind): [line numbers]} of raw acquire/release calls in one file."""
    found = {}
    inblock = False
    for no, line in enumerate(text.splitlines(), 1):
        code, inblock = strip(line, inblock)
        if not code.strip() or code.lstrip().startswith('#') or 'scopeExit(' in code:   # a scopeExit lambda is the release itself
            continue
        for kind, (rx, _) in RULES.items():
            for _m in rx.finditer(code):
                found.setdefault((path, kind), []).append(no)
    return found


def lint():
    found = {}
    for p in source_files():
        r = rel(p)
        if r in GUARD_FILES:
            continue
        with open(p, encoding='utf-8', errors='replace') as f:
            found.update(scan_text(r, f.read()))
    return found


def load_allow():
    with open(ALLOW_FILE, encoding='utf-8') as f:
        return json.load(f)['allow']


def check(found, allow):
    """-> (new, stale): uses not covered by the allow-list / entries that cover more than occurs."""
    new, stale = [], []
    allowed = {(e['file'], e['kind']): e['count'] for e in allow}
    for k, lines in sorted(found.items()):
        if allowed.get(k, 0) < len(lines):
            new.append('%s [%s] %d use(s) at lines %s, allowed %d: use %s'
                       % (k[0], k[1], len(lines), lines, allowed.get(k, 0), RULES[k[1]][1]))
    for k, n in sorted(allowed.items()):
        if len(found.get(k, ())) < n:
            stale.append('%s [%s] allowed %d, found %d' % (k[0], k[1], n, len(found.get(k, ()))))
    return new, stale


# ---- compile-time guard checks ---------------------------------------------------------------------------------------------
def host_cxx():
    return shutil.which('clang++') or shutil.which('g++')


GUARD_PROBE = r'''
#include "engine/guard.hpp"
using namespace ms;
struct Rec { int a; };                       // a plain data struct: rule of zero, copyable
static_assert(GuardTraits<Rec>::kCopyConstructible && GuardTraits<Rec>::kCopyAssignable, "traits see copyable types");
MS_GUARD_PINNED(MemMark);
static int g_n;
struct Bump { void operator()() const { ++g_n; } };
MS_GUARD_PINNED(ScopeExit<Bump>);
int main() {
	{ auto s = scopeExit([] { ++g_n; }); }
	{ auto s = scopeExit([] { ++g_n; }); s.dismiss(); }
	MemStack st; memStackInit(st, 0x1000, 256);
	uint32_t a;
	{ MemMark m(st); memStackAlloc(st, 16, a); if(st.ulTop != 16) return 1; }
	if(st.ulTop != 0) return 2;
	{ MemMark m(st); memStackAlloc(st, 16, a); m.keep(); }
	if(st.ulTop != 16) return 3;
	return g_n == 1 ? 0 : 4;
}
'''

BAD_COPYABLE = '#include "engine/guard.hpp"\nstruct Bad { Bad() {} ~Bad() {} };\nMS_GUARD_PINNED(Bad);\nint main() { return 0; }\n'
GOOD_MOVABLE = ('#include "engine/guard.hpp"\nstruct Mv { Mv() {} ~Mv() {} Mv(const Mv &) = delete; Mv &operator=(const Mv &) = delete;'
                ' Mv(Mv &&) {} Mv &operator=(Mv &&) { return *this; } };\nMS_GUARD_MOVABLE(Mv);\nint main() { return 0; }\n')


def compile_probe(src, run=False):
    cxx = host_cxx()
    with tempfile.TemporaryDirectory() as d:
        cpp = os.path.join(d, 'p.cpp')
        with open(cpp, 'w') as f:
            f.write(src)
        exe = os.path.join(d, 'p.exe')
        cmd = [cxx, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'),
               cpp, os.path.join(ROOT, 'src', 'engine', 'memstack.cpp'), '-o', exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            return False, r.stderr
        if run:
            r = subprocess.run([exe], capture_output=True, text=True)
            return r.returncode == 0, 'exit %d' % r.returncode
        return True, ''


class RaiiLint(unittest.TestCase):
    def test_allow_list_entries_are_documented(self):
        for e in load_allow():
            self.assertTrue(e.get('reason', '').strip(), 'entry without reason: %r' % e)
            self.assertIn(e['kind'], RULES)
            self.assertGreater(e['count'], 0)
            self.assertTrue(os.path.exists(os.path.join(ROOT, e['file'])), e['file'])

    def test_tree_has_no_new_raw_pairs_and_no_stale_entries(self):
        new, stale = check(lint(), load_allow())
        msg = []
        if new:
            msg.append('NEW raw acquire/release (use the guard, or add a documented entry to tests/raii_allow.json and docs/RAII.md):')
            msg += ['  ' + n for n in new]
        if stale:
            msg.append('STALE allow-list entries (the raw call is gone: lower the count or delete the entry):')
            msg += ['  ' + s for s in stale]
        self.assertFalse(msg, '\n'.join(msg))

    def test_mutation_raw_file_open_is_flagged(self):
        f = scan_text('src/rt/x.cpp', 'void f() {\n\tif(rt_file_open("a") != 0) return;\n\trt_file_close();\n}\n')
        self.assertIn(('src/rt/x.cpp', 'file-open'), f)

    def test_mutation_raw_system_use_is_flagged(self):
        f = scan_text('src/rt/x.cpp', 'void f() { systemUse(); g(); systemUnuse(); }\n')
        self.assertEqual(f[('src/rt/x.cpp', 'system-use')], [1, 1])

    def test_comments_strings_and_other_names_are_not_flagged(self):
        txt = ('// systemUse() in a comment\n/* memAlloc(1) */\nconst char *s = "Open(x)";\n'
               'void g() { iniOpen(a); joyOpen(); enhancedEnable(); rt::enhancedEnable(); sFile.close(); h.Close(); }\n')
        self.assertEqual(scan_text('src/rt/y.cpp', txt), {})

    def test_guards_are_used_not_flagged(self):
        txt = 'void f() { rt::SystemAccess sOs; rt::FileHandle sF("a"); rt::MemBlock b(4, MEMF_ANY); }\n'
        self.assertEqual(scan_text('src/rt/y.cpp', txt), {})

    def test_guard_classes_spell_the_rule_of_five(self):
        """Every guard class declares its destructor and deletes copy; moves are deleted too, except FileHandle's real move."""
        seen = 0
        for fn in GUARD_FILES:
            with open(os.path.join(ROOT, fn), encoding='utf-8') as f:
                txt = f.read()
            for m in re.finditer(r'^class (\w+) \{(.*?)^\};', txt, re.S | re.M):
                name, body = m.group(1), m.group(2)
                seen += 1
                self.assertRegex(body, r'~%s\(' % name, name + ': destructor')
                self.assertRegex(body, r'%s\(const %s(?:<\w+>)? &\) = delete;' % (name, name), name + ': copy ctor')
                self.assertRegex(body, r'%s &operator=\(const %s(?:<\w+>)? &\) = delete;' % (name, name), name + ': copy assign')
                if name == 'FileHandle':   # the one guard that leaves its scope: a real move that empties the source
                    self.assertIn('FileHandle(FileHandle &&o)', body)
                else:
                    self.assertRegex(body, r'%s\(%s(?:<\w+>)? &&\) = delete;' % (name, name), name + ': move ctor')
                    self.assertRegex(body, r'%s &operator=\(%s(?:<\w+>)? &&\) = delete;' % (name, name), name + ': move assign')
        self.assertGreaterEqual(seen, 10)

    def test_every_guard_class_has_a_static_assert(self):
        for fn in GUARD_FILES:
            with open(os.path.join(ROOT, fn), encoding='utf-8') as f:
                txt = f.read()
            for m in re.finditer(r'^\s*class (\w+) \{', txt, re.M):
                name = m.group(1)
                self.assertRegex(txt, r'MS_GUARD_(?:PINNED|MOVABLE)\(%s\b' % name, name + ': no MS_GUARD_* static_assert')


@unittest.skipUnless(host_cxx(), 'needs a host clang++/g++')
class GuardCompile(unittest.TestCase):
    def test_engine_guards_compile_run_and_are_non_copyable(self):
        ok, out = compile_probe(GUARD_PROBE, run=True)
        self.assertTrue(ok, out)

    def test_mutation_copyable_guard_fails_the_static_assert(self):
        ok, out = compile_probe(BAD_COPYABLE)
        self.assertFalse(ok)
        self.assertIn('pinned guard', out)

    def test_move_only_guard_is_accepted_as_movable(self):
        ok, out = compile_probe(GOOD_MOVABLE)
        self.assertTrue(ok, out)


if __name__ == '__main__':
    unittest.main()
