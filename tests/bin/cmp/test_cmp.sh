#!/bin/sh
# Tests for bin/cmp — POSIX.1-2024 + GNU + BSD.
# Compiles cmp.c with the host cc so the suite is self-contained.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC="$REPO/bin/cmp/cmp.c"
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

printf 'abc\n' > a
printf 'abd\n' > b
printf 'abc\n' > a2

# identical -> no output, exit 0
out="$("$BIN" a a2)"; rc=$?
check "identical output" "" "$out"
check "identical rc"     "0" "$rc"

# differ -> first-diff message, exit 1
out="$("$BIN" a b)"; rc=$?
check "differ message" "a b differ: char 3, line 1" "$out"
check "differ rc"      "1" "$rc"

# -s silent
out="$("$BIN" -s a b)"; rc=$?
check "-s no output" "" "$out"
check "-s rc"        "1" "$rc"

# -l octal listing
printf 'abc' > x; printf 'abd' > y
check "-l listing" "3 143 144" "$("$BIN" -l x y)"

# -x hex listing
check "-x listing" "3 63 64" "$("$BIN" -x x y)"

# -i skip makes them equal
printf 'Xbc' > p; printf 'Ybc' > q
out="$("$BIN" -i 1 p q)"; rc=$?
check "-i skip equal rc" "0" "$rc"

# skip1:skip2 form
printf 'XXbc' > p2; printf 'Ybc' > q2
out="$("$BIN" -i 2:1 p2 q2)"; rc=$?
check "-i skip1:skip2 rc" "0" "$rc"

# positional skip operands
out="$("$BIN" p q 1 1)"; rc=$?
check "skip operands rc" "0" "$rc"

# -n limit
printf 'abZ' > n1; printf 'abY' > n2
out="$("$BIN" -n 2 n1 n2)"; rc=$?
check "-n within-limit rc" "0" "$rc"
out="$("$BIN" -n 3 n1 n2)"; rc=$?
check "-n past-limit rc"   "1" "$rc"

# -z size mismatch (a=4 bytes "abc\n", x=3 bytes "abc")
out="$("$BIN" -z a x 2>/dev/null)"; rc=$?
check "-z size mismatch rc" "1" "$rc"

# EOF on shorter file
err="$("$BIN" x a 2>&1 1>/dev/null)";
check "EOF diagnostic" "cmp: EOF on x" "$err"

# stdin via -
out="$(printf 'abc' | "$BIN" - x)"; rc=$?
check "stdin identical rc" "0" "$rc"

# bad argc
"$BIN" a 2>/dev/null; check "argc<2 exits 2" "2" "$?"

echo "cmp: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
