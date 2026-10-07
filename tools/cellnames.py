"""cellnames.py -- the C++ names of the original data cells (ROADMAP 7.1s), read from tools/cell_names.yaml.

    import cellnames
    cellnames.load()            -> {('mog', 'LAB_0633'): 'mogCurKnight', ...}
    cellnames.symbol('mog', 'LAB_0633')   -> 'mogCurKnight' (or 'mog_LAB_0633' when the cell has no C++ name yet)
    cellnames.label_of(name)    -> ('mog', 'LAB_0633') | None

The mapping file is HUMAN-owned; tools/rename_cells.py applies it to the sources, tools/gen_data.py names the link-time
alias of the cell with it, tests/emu_lib.py resolves the names in the unicorn blobs through it.  Format (one `LABEL: name` per line,
`#` comments, grouped by the binary the label belongs to):

    mog:
      LAB_0633: mogCurKnight        # Knight* of the current knight
    program:
      LAB_05B8: prgWipeState
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, 'tools', 'cell_names.yaml')
PREFIX = {'program': 'prg', 'mog': 'mog'}
BINARY = {'prg': 'program', 'mog': 'mog'}


def load(path=None):
    import yaml
    with open(path or PATH, encoding='utf-8') as f:
        doc = yaml.safe_load(f) or {}
    out = {}
    for binary, cells in doc.items():
        if binary not in PREFIX:
            raise ValueError(f'{PATH}: unknown binary {binary!r}')
        for label, name in (cells or {}).items():
            out[(binary, str(label))] = str(name)
    return out


_NAMES = None


def names():
    global _NAMES
    if _NAMES is None:
        _NAMES = load()
    return _NAMES


def symbol(binary, label):
    """The C symbol of a cell: its C++ name, else the label alias `<prefix>_<label>`."""
    return names().get((binary, label)) or f'{PREFIX[binary]}_{label}'


def label_of(name):
    for key, n in names().items():
        if n == name:
            return key
    return None


_BY_NAME = None


def legacy(sym):
    """The `mog_LAB_0633` / `prg_SECSTRT_30` spelling of an undefined symbol of a C++ blob: tests/ link the sources against the ORIGINAL image
    (the oracle), whose cells they find by label; a renamed cell (`mogCurKnight`) maps back to its label, anything else is returned as is."""
    global _BY_NAME
    if _BY_NAME is None:
        _BY_NAME = {n: f'{PREFIX[b]}_{lab}' for (b, lab), n in names().items()}
    return _BY_NAME.get(sym, sym)


def legacy_set(syms):
    """`legacy` over a set of undefined symbols (a test's `und - defs`)."""
    return {legacy(s) for s in syms}


def defsym_aliases(cmd, syms):
    """Linker arguments that give every C++ cell name of `syms` the address its legacy spelling got in `cmd` (a list holding
    `--defsym=<legacy>=<value>` for the symbols of legacy_set(syms)): the tests resolve the cells by label, the sources spell the names."""
    have = {}
    for a in cmd:
        if a.startswith('--defsym='):
            k, _, v = a[len('--defsym='):].partition('=')
            have[k] = v
    out = []
    for s in sorted(syms):
        old = legacy(s)
        if old != s and old in have:
            out.append(f'--defsym={s}={have[old]}')
    return out
