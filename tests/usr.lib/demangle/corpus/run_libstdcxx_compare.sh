#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CLI="$ROOT/tools/demangle_cli"

if [ ! -x "$CLI" ]; then
    echo "missing demangle_cli at $CLI" >&2
    exit 1
fi

LIBSTD="$(cc -print-file-name=libstdc++.a 2>/dev/null || true)"
if [ -z "$LIBSTD" ] || [ ! -f "$LIBSTD" ]; then
    echo "libstdc++.a not found; skipping" >&2
    exit 0
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

nm -g "$LIBSTD" 2>/dev/null | awk '{print $3}' | grep '^_Z' | head -n 5000 > "$TMPDIR/syms.txt"
TOTAL=$(wc -l < "$TMPDIR/syms.txt" | tr -d ' ')
if [ "$TOTAL" -eq 0 ]; then
    echo "no Itanium symbols in libstdc++.a" >&2
    exit 0
fi

"$CLI" < "$TMPDIR/syms.txt" > "$TMPDIR/ours.txt"
c++filt < "$TMPDIR/syms.txt" > "$TMPDIR/cxxfilt.txt"

OURS_NONEMPTY=$(grep -cve '^$' "$TMPDIR/ours.txt" || true)
CXX_NONEMPTY=$(grep -cve '^$' "$TMPDIR/cxxfilt.txt" || true)

echo "libstdc++ corpus: total=$TOTAL ours_nonempty=$OURS_NONEMPTY cxxfilt_nonempty=$CXX_NONEMPTY"
exit 0
