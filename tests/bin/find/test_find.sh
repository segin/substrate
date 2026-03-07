#!/bin/bash
# test_find.sh — Oracle-comparison test harness for our find vs system find
set -e

FIND_BIN="./bin/find/find"
SYS_FIND="$(command -v find)"
make -C bin/find NATIVE_BUILD=1 >/dev/null 2>&1

TEST_PASS=0
TEST_FAIL=0

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Build a test tree
mkdir -p "$TMPDIR/tree/a/b/c"
mkdir -p "$TMPDIR/tree/d"
touch "$TMPDIR/tree/file1.txt"
touch "$TMPDIR/tree/file2.c"
touch "$TMPDIR/tree/a/file3.txt"
touch "$TMPDIR/tree/a/b/file4.h"
touch "$TMPDIR/tree/a/b/c/deep.txt"
touch "$TMPDIR/tree/d/file5.c"
ln -s "$TMPDIR/tree/file1.txt" "$TMPDIR/tree/link1" 2>/dev/null || true
mkfifo "$TMPDIR/tree/fifo1" 2>/dev/null || true
echo "content" > "$TMPDIR/tree/file1.txt"
chmod 755 "$TMPDIR/tree/file2.c"

run_test() {
    local name="$1"
    shift
    local out_ours=$(cd "$TMPDIR" && "$OLDPWD/$FIND_BIN" "$@" 2>/dev/null | sort)
    local out_sys=$(cd "$TMPDIR" && "$SYS_FIND" "$@" 2>/dev/null | sort)

    if [ "$out_ours" = "$out_sys" ]; then
        echo "PASS: $name"
        TEST_PASS=$((TEST_PASS+1))
    else
        echo "FAIL: $name"
        echo "  EXPECTED (system find):"
        echo "$out_sys" | head -20 | sed 's/^/    /'
        echo "  GOT (our find):"
        echo "$out_ours" | head -20 | sed 's/^/    /'
        TEST_FAIL=$((TEST_FAIL+1))
    fi
}

OLDPWD=$(pwd)

# ── POSIX core tests ──
run_test "Default (no expression)" tree
run_test "-name *.txt" tree -name "*.txt"
run_test "-name *.c" tree -name "*.c"
run_test "-type f" tree -type f
run_test "-type d" tree -type d
run_test "-type l" tree -type l
run_test "-maxdepth 1" tree -maxdepth 1
run_test "-maxdepth 0" tree -maxdepth 0
run_test "-mindepth 2" tree -mindepth 2
run_test "-name AND -type" tree -name "*.txt" -type f

# Boolean operators
run_test "-name OR" tree -name "*.txt" -o -name "*.c"
run_test "NOT name" tree ! -name "*.txt"
run_test "Parentheses" tree "(" -name "*.txt" -o -name "*.c" ")" -type f

# Actions
run_test "-print0" tree -print0
run_test "-empty" tree -empty

# Depth
run_test "-depth" tree -depth

echo ""
echo "$TEST_PASS passed, $TEST_FAIL failed out of $((TEST_PASS+TEST_FAIL)) tests."
exit $((TEST_FAIL > 0 ? 1 : 0))
