#!/bin/sh
# Tests for bin/diff — POSIX.1-2024 + GNU + BSD.
# Compiles diff.c with the host cc so the suite is self-contained.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC="$REPO/bin/diff/diff.c"
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
        printf 'FAIL: %s\n  expected: [%s]\n  actual:   [%s]\n' "$1" "$2" "$3"
    fi
}
pipe() { tr '\n' '|' | sed 's/|$//'; }

printf '1\n2\n3\n' > a
printf '1\nX\n3\n' > b
printf '1\n2\n3\n' > a2
printf '1\n3\n'    > del
printf '1\n2\n3\n4\n' > ins

# normal: change
check "normal change" "2c2|< 2|---|> X" "$("$BIN" a b | pipe)"
"$BIN" a b >/dev/null; check "change exit 1" "1" "$?"

# normal: delete
check "normal delete" "2d1|< 2" "$("$BIN" a del | pipe)"

# normal: insert
check "normal insert" "3a4|> 4" "$("$BIN" a ins | pipe)"

# identical: no output, exit 0
out="$("$BIN" a a2)"; rc=$?
check "identical output" "" "$out"
check "identical exit 0" "0" "$rc"

# unified
check "unified" "--- A|+++ B|@@ -1,3 +1,3 @@| 1|-2|+X| 3" \
      "$("$BIN" -u --label A --label B a b | pipe)"

# context
check "context" \
"*** A|--- B|***************|*** 1,3 ****|  1|! 2|  3|--- 1,3 ----|  1|! X|  3" \
      "$("$BIN" -c --label A --label B a b | pipe)"

# ed script
check "ed script" "2c|X|." "$("$BIN" -e a b | pipe)"

# rcs script
check "rcs script" "d2 1|a2 1|X" "$("$BIN" -n a b | pipe)"

# brief
check "brief" "Files a and b differ" "$("$BIN" -q a b)"

# -s report identical
check "-s identical" "Files a and a2 are identical" "$("$BIN" -s a a2)"

# -i case-insensitive
printf 'Hello\n' > c1; printf 'hello\n' > c2
out="$("$BIN" -i c1 c2)"; rc=$?
check "-i case fold exit 0" "0" "$rc"
check "no -i differs"       "1c1|< Hello|---|> hello" "$("$BIN" c1 c2 | pipe)"

# -w ignore all whitespace
printf 'a b c\n' > w1; printf 'a  b  c\n' > w2
out="$("$BIN" -w w1 w2)"; rc=$?
check "-w ignore space exit 0" "0" "$rc"

# missing newline at end of file is a difference
printf 'x'   > nl1
printf 'x\n' > nl2
check "no-newline detected" \
      "1c1|< x|\\ No newline at end of file|---|> x" \
      "$("$BIN" nl1 nl2 | pipe)"

# recursive directory diff
mkdir d1 d2
printf '1\n' > d1/x; printf '2\n' > d2/x
printf 'z\n' > d1/only1
check "-r -q recursive" "Only in d1: only1|Files d1/x and d2/x differ" \
      "$("$BIN" -r -q d1 d2 | pipe)"
check "-r -q recursive with trailing slashes" "Only in d1/: only1|Files d1/x and d2/x differ" \
      "$("$BIN" -r -q d1/ d2/ | pipe)"

# diff files directly inside a directory to ensure path joining behaves correctly
mkdir dir_test_1 dir_test_2
printf "1\n" > dir_test_1/test_file.txt
printf "2\n" > dir_test_2/test_file.txt
check "diff identical named files in different dirs" "1c1|< 1|---|> 2" "$("$BIN" dir_test_1/test_file.txt dir_test_2/test_file.txt | pipe)"

# -U with explicit context count
check "unified -U1" "--- A|+++ B|@@ -1,3 +1,3 @@| 1|-2|+X| 3" \
      "$("$BIN" -U1 --label A --label B a b | pipe)"

# --version
check "--version" "diff (Substrate) 1.0" "$("$BIN" --version)"

echo "diff: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
