"""mutation_gate.py -- one switch for the slow "deliberate bug" tests (dev-loop speed, 2026-10-11).

A mutation test patches a copy of an engine source with a deliberate bug, recompiles a host driver and expects the
comparison to fail.  They prove the tests have teeth but each one recompiles, so they are off by default:

    py tools/check.py --mutations          # or:  set MS_MUTATIONS=1   (any value but "" and "0")
    py tools/testrun.py --mutations tests.test_copper

Which tests are mutation tests (no per-test edits needed):
  * a TestCase class whose name contains "mutat" (e.g. `class Mutations`, `SdlMutations`), or
  * a test method whose name contains "mutat" (`test_every_mutation_is_caught`, `test_mutated_core_is_caught`), or
  * anything decorated with `@mutation_gate.mutation_test` (use this for a new test whose name does not say it).
Cheap, purely textual ones stay on (CHEAP_MODULES: the lints test_layers and test_raii; together ~1 s).

The gate marks the tests as skipped ("mutation test: ...") through the standard unittest skip attributes, so a skipped class does
not even run its setUpClass (which is where the expensive driver build usually lives).  It hooks `TestLoader`, so it works for
`py -m unittest tests.test_x` (tests/__init__.py imports this), for tools/testrun.py and tools/check.py.  A test named
explicitly (`tests.test_x.Class.test_method`) is a deliberate request and always runs.  `unittest discover -s tests` does not
import the package; use tools/testrun.py there.
"""
import os
import re
import unittest

SKIP_WHY = 'mutation test: set MS_MUTATIONS=1 (tools/check.py --mutations) to run'
NAME_RE = re.compile(r'mutat', re.I)
CHEAP_MODULES = {'test_layers', 'test_raii'}      # measured 2026-10-11: their mutation checks rewrite a text snippet in memory, ~1 s all together


def enabled():
    return os.environ.get('MS_MUTATIONS', '') not in ('', '0')


def mutation_test(obj):
    """Decorator for a test method or class that is a mutation test although its name does not say so."""
    obj.__ms_mutation__ = True
    return obj


def _module(cls):
    return cls.__module__.rsplit('.', 1)[-1]


def _skip(obj):
    obj.__unittest_skip__ = True
    obj.__unittest_skip_why__ = SKIP_WHY


def mark(cls):
    """Mark the mutation tests of a TestCase class as skipped (when mutations are off)."""
    if _module(cls) in CHEAP_MODULES:
        return
    if getattr(cls, '__ms_mutation__', False) or NAME_RE.search(cls.__name__):
        _skip(cls)
        return
    for name in dir(cls):
        if not name.startswith('test'):
            continue
        fn = getattr(cls, name, None)
        if callable(fn) and (NAME_RE.search(name) or getattr(fn, '__ms_mutation__', False)):
            _skip(fn)


def install():
    if getattr(unittest.TestLoader, '_ms_gated', False):
        return
    original = unittest.TestLoader.loadTestsFromTestCase

    def loadTestsFromTestCase(self, testCaseClass):
        if not enabled():
            mark(testCaseClass)
        return original(self, testCaseClass)

    unittest.TestLoader.loadTestsFromTestCase = loadTestsFromTestCase
    unittest.TestLoader._ms_gated = True
