#!/bin/bash
# Re-runnable: prep + headless Ghidra import/analysis of nb, program, mog.
# Usage: tools/ghidra/import.sh [name ...]      (default: nb program mog)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
GH="${GHIDRA_HOME:-/c/Users/Jari/ghidra/ghidra_12.1.2_PUBLIC}"
B="$ROOT/build/ghidra"
W() { cygpath -w "$1"; }
NAMES=("$@"); [ ${#NAMES[@]} -eq 0 ] && NAMES=(nb program mog)
mkdir -p "$B/project"
py "$HERE/prep.py" "${NAMES[@]}"
for n in "${NAMES[@]}"; do
  echo "=== importing $n"
  "$GH/support/analyzeHeadless.bat" "$(W "$B/project")" MoonstoneACE \
    -import "$(W "$B/$n/$n")" -overwrite -noanalysis \
    -processor 68000:BE:32:default -loader BinaryLoader \
    -scriptPath "$(W "$HERE")" \
    -postScript ImportMoonstone.java "$(W "$B/$n")"
done
