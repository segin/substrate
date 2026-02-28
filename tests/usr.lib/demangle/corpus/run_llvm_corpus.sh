#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CLI="$ROOT/tools/demangle_cli"

if [ ! -x "$CLI" ]; then
    echo "missing demangle_cli at $CLI" >&2
    exit 1
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

LLVM_ARCHIVE=""
for cand in /usr/lib/libLLVM*.a /usr/lib/llvm*/lib/libLLVM*.a; do
    if [ -f "$cand" ]; then
        LLVM_ARCHIVE="$cand"
        break
    fi
done

if [ -n "$LLVM_ARCHIVE" ]; then
    nm -g "$LLVM_ARCHIVE" 2>/dev/null | awk '{print $3}' | grep '^_Z' | head -n 5000 > "$TMPDIR/syms.txt"
else
    cat > "$TMPDIR/fallback.cpp" <<'CPP'
#include <vector>
#include <string>
template <typename T> static T add(T a, T b) { return a + b; }
int main() { std::vector<std::string> v; v.push_back("x"); return add(1, 2); }
CPP
    c++ -c -O2 "$TMPDIR/fallback.cpp" -o "$TMPDIR/fallback.o"
    nm "$TMPDIR/fallback.o" 2>/dev/null | awk '{print $3}' | grep '^_Z' > "$TMPDIR/syms.txt"
fi

TOTAL=$(wc -l < "$TMPDIR/syms.txt" | tr -d ' ')
if [ "$TOTAL" -eq 0 ]; then
    echo "no LLVM/fallback symbols found" >&2
    exit 0
fi

"$CLI" < "$TMPDIR/syms.txt" > "$TMPDIR/ours.txt"
OURS_NONEMPTY=$(grep -cve '^$' "$TMPDIR/ours.txt" || true)

echo "llvm corpus: total=$TOTAL ours_nonempty=$OURS_NONEMPTY"
exit 0
