#!/bin/sh
# Tests for bin/paste — POSIX.1-2024 + GNU + BSD.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC="$REPO/bin/paste/paste.c"
BIN="$(mktemp)"
WORK="$(mktemp -d)"
trap 'rm -f "$BIN"; rm -rf "$WORK"' EXIT

cc -O2 -std=c2x -o "$BIN" "$SRC" || { echo "FAIL: compile"; exit 1; }
cd "$WORK"

pass=0 fail=0
check() {
    if [ "$2" = "$3" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL: %s\n  expected: %s\n  actual:   %s\n' "$1" "$2" "$3"
    fi
}

printf '1\n2\n3\n' > p1
printf 'a\nb\n'    > p2
printf 'x\n'       > p3

check "parallel"       "1	a|2	b|3	" \
      "$("$BIN" p1 p2 | tr '\n' '|' | sed 's/|$//')"
check "-d,"            "1,a|2,b|3," \
      "$("$BIN" -d, p1 p2 | tr '\n' '|' | sed 's/|$//')"
check "-s serial"      "1	2	3" "$("$BIN" -s p1)"
check "-s -d, per-file" "1,2,3|a,b" \
      "$("$BIN" -s -d, p1 p2 | tr '\n' '|' | sed 's/|$//')"
check "cycled -d"      "1-a_x|2-b_|3-_" \
      "$("$BIN" -d'-_' p1 p2 p3 | tr '\n' '|' | sed 's/|$//')"
check "escape \\t"     "1	a" "$("$BIN" -d'\t' p1 p2 | head -1)"
check "escape \\0 empty" "1a" "$("$BIN" -d'\0' p1 p2 | head -1)"
check "single file == cat" "1|2|3" \
      "$("$BIN" p1 | tr '\n' '|' | sed 's/|$//')"
check "stdin via -"    "1	a" "$(cat p1 | "$BIN" - p2 | head -1)"
check "-z NUL"         "1	a" \
      "$(printf '1\0' | "$BIN" -z - p2 2>/dev/null | tr '\0' '\n' | head -1)"

echo "paste: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
