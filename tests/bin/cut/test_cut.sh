#!/bin/sh
# Tests for bin/cut — POSIX.1-2024 + GNU + BSD.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC="$REPO/bin/cut/cut.c"
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

printf 'one:two:three:four\n' > f
printf 'a\tb\tc\n'            > t
printf 'hi  there\tworld\n'   > w

check "-f2 -d:"        "two"             "$("$BIN" -f2 -d: f)"
check "-f2,4 -d:"      "two:four"        "$("$BIN" -f2,4 -d: f)"
check "-f2- -d:"       "two:three:four"  "$("$BIN" -f2- -d: f)"
check "-f-2 -d:"       "one:two"         "$("$BIN" -f-2 -d: f)"
check "-c1-3"          "one"             "$("$BIN" -c1-3 f)"
check "-c1-3,5"        "onet"            "$("$BIN" -c1-3,5 f)"
check "-b1-3 == -c"    "one"             "$("$BIN" -b1-3 f)"
check "-f default tab" "a	c"        "$("$BIN" -f1,3 t)"
check "--complement"   "two:four"        "$("$BIN" -f1,3 -d: --complement f)"
check "--output-delim" "one/two"         "$("$BIN" -f1,2 -d: --output-delimiter=/ f)"
check "-w whitespace"  "hi	world"    "$("$BIN" -w -f1,3 w)"
check "no-delim passthru" "plain"        "$(printf 'plain\n' | "$BIN" -f2 -d:)"
check "-s drops no-delim" "x"            "$(printf 'plain\nq:x\n' | "$BIN" -s -f2 -d:)"
check "-z NUL"         "two"             "$(printf 'one:two\0' | "$BIN" -z -f2 -d: | tr -d '\0')"
check "ranges merge"   "one"             "$("$BIN" -c1-2,2-3 f)"

"$BIN" f 2>/dev/null;        check "no mode exits !=0" "ne0" \
      "$( [ $? -ne 0 ] && echo ne0 || echo zero )"
"$BIN" -f0 -d: f 2>/dev/null; check "zero index exits !=0" "ne0" \
      "$( [ $? -ne 0 ] && echo ne0 || echo zero )"

echo "cut: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
