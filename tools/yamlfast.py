"""yamlfast.py -- PyYAML's safe_load through libyaml (test speed, 2026-10-11).

`yaml.safe_load` is the pure-Python parser: ~60 ms for tools/data_types.yaml, and the generators / checkers / tests parse the same few
yaml files hundreds of times per run.  `safe_load` here uses the C parser (`yaml.CSafeLoader`) when PyYAML was built with libyaml and
the pure one otherwise; the objects are the same.  `load_path` additionally remembers the parsed document per (path, mtime, size): the
caller must not modify what it returns.
"""
import os

import yaml

SafeLoader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)

_memo = {}


def safe_load(stream):
    return yaml.load(stream, Loader=SafeLoader)


def load_path(path, encoding="utf-8"):
    """The parsed yaml document of a file, parsed once per (path, mtime, size).  READ-ONLY: the same object comes back each time."""
    st = os.stat(path)
    key = (os.path.abspath(path), st.st_mtime_ns, st.st_size)
    if key not in _memo:
        with open(path, encoding=encoding) as f:
            _memo[key] = safe_load(f)
    return _memo[key]
