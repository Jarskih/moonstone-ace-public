"""moghunks.py -- stand-in for moonshard's tools/moghunks.py (ROADMAP 10.2).

The tools import `from moghunks import parse_hunk_file` after putting reference/moonshard/tools first on sys.path (a developer
checkout).  The public tree has no moonshard: this module answers instead with the project's own reader, tools/hunkfile.py
(same data model; tests/test_origload.py and the tools that parse the executables use either)."""
from hunkfile import (HunkBlock, HunkFile, HunkParseError, RelocGroup, hunk_base_offsets,  # noqa: F401
                      parse_hunk_file)
