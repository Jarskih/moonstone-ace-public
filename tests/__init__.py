"""Test package.  Importing it (py -m unittest tests.test_x) installs the mutation gate: see tests/mutation_gate.py."""
from . import mutation_gate

mutation_gate.install()
