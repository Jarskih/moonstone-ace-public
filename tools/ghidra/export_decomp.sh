#!/bin/bash
# Usage: tools/ghidra/export_decomp.sh <nb|program|mog> [LABEL ... | ALL]
# Writes build/ghidra/decomp/<name>/<LABEL>.c
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
GH="${GHIDRA_HOME:-/c/Users/Jari/ghidra/ghidra_12.1.2_PUBLIC}"
B="$ROOT/build/ghidra"
n="$1"; shift
"$GH/support/analyzeHeadless.bat" "$(cygpath -w "$B/project")" MoonstoneACE \
  -process "$n" -noanalysis -scriptPath "$(cygpath -w "$HERE")" \
  -postScript ExportDecomp.java "$(cygpath -w "$B/decomp/$n")" "$@"
