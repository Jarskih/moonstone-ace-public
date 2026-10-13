"""Put on PYTHONPATH by tools/testrun.py: every Python process then routes its clang calls through tools/hostcache.py."""
import os
import sys

if os.environ.get("MS_HOSTCACHE", "1") != "0":
    _tools = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if _tools not in sys.path:
        sys.path.append(_tools)
    try:
        import hostcache
        hostcache.install()
    except Exception:       # noqa: BLE001 - never break a Python start-up over a cache
        pass
