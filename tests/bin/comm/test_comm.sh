#!/bin/sh
# Tests for bin/comm — POSIX.1-2024 + GNU + BSD.
# Compiles comm.c directly with the host cc so the suite is
# self-contained and does not depend on the target toolchain.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC="$REPO/bin/comm/comm.c"
BIN="$(mktemp)"
WORK="$(mktemp -d)"
trap 'rm -f "$BIN"; rm -rf "$WORK"' EXIT

cc -O2 -std=c2x -o "$BIN" "$SRC" || { echo "FAIL: compile"; exit 1; }
cd "$WORK"

pass=0 fail=0
check() { # desc expected actual
    if [ "$2" = "$3" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL: %s\n  expected: %s\n  actual:   %s\n' "$1" "$2" "$3"
    fi
}

printf 'apple\nbanana\ncherry\n' > a
printf 'banana\ncherry\ndate\n'  > b

check "3-column"      "apple|		banana|		cherry|	date" \
      "$("$BIN" a b | tr '\n' '|' | sed 's/|$//')"
check "-12 intersect" "banana
cherry"            "$("$BIN" -12 a b)"
check "-23 a-minus-b" "apple"        "$("$BIN" -23 a b)"
check "-123 empty"    ""             "$("$BIN" -123 a b)"
check "--total"       "1	1	2	total" \
      "$("$BIN" --total a b | tail -1)"
check "--output-delim" "banana,cherry" \
      "$("$BIN" -12 --output-delimiter=, a b | tr '\n' , | sed 's/,$//')"

printf 'APPLE\n' > c; printf 'apple\n' > d
# A case-folded match emits file1's spelling of the common line.
check "-i case-fold" "APPLE" "$("$BIN" -12 -i c d)"

check "stdin via -"  "banana
cherry"            "$(cat a | "$BIN" -12 - b)"

printf 'x\0y\0' > za; printf 'y\0z\0' > zb
check "-z NUL"       "y" "$("$BIN" -z -12 za zb | tr -d '\0')"

"$BIN" a 2>/dev/null; check "wrong argc exits !=0" "ne0" \
      "$( [ $? -ne 0 ] && echo ne0 || echo zero )"

echo "comm: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
