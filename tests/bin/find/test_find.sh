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
mkdir -p "$TMPDIR/tree/emptydir"
touch "$TMPDIR/tree/file1.txt"
touch "$TMPDIR/tree/file2.c"
touch "$TMPDIR/tree/a/file3.txt"
touch "$TMPDIR/tree/a/b/file4.h"
touch "$TMPDIR/tree/a/b/c/deep.txt"
touch "$TMPDIR/tree/d/file5.c"
ln -s "$TMPDIR/tree/file1.txt" "$TMPDIR/tree/link1" 2>/dev/null || true
ln -s "/nonexistent/target" "$TMPDIR/tree/broken_link" 2>/dev/null || true
mkfifo "$TMPDIR/tree/fifo1" 2>/dev/null || true
echo "content" > "$TMPDIR/tree/file1.txt"
chmod 755 "$TMPDIR/tree/file2.c"
chmod 644 "$TMPDIR/tree/a/file3.txt"
chmod 600 "$TMPDIR/tree/a/b/file4.h"

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

# Run a test that only checks our find exits 0
run_test_no_oracle() {
    local name="$1"
    shift
    if cd "$TMPDIR" && "$OLDPWD/$FIND_BIN" "$@" >/dev/null 2>&1; then
        echo "PASS: $name"
        TEST_PASS=$((TEST_PASS+1))
    else
        echo "FAIL: $name (non-zero exit)"
        TEST_FAIL=$((TEST_FAIL+1))
    fi
    cd "$OLDPWD"
}

OLDPWD=$(pwd)

# ── T13.2: POSIX core tests ──
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
run_test "-name OR" tree -name "*.txt" -o -name "*.c"
run_test "NOT name" tree ! -name "*.txt"
run_test "Parentheses" tree "(" -name "*.txt" -o -name "*.c" ")" -type f
run_test "-depth" tree -depth
run_test "-iname" tree -iname "*.TXT"
run_test "-path" tree -path "*/a/*"

# -perm tests
run_test "-perm exact 755" tree -perm 755
run_test "-perm -644" tree -perm -644

# -links
run_test "-links 1" tree -links 1

# -true/-false
run_test "-true" tree -true
run_test "-false (no output)" tree -false -print

# -exec
run_test "-exec echo" tree -name "*.txt" -exec echo {} ";"
run_test "-prune" tree -name a -prune -o -print

# ── T13.3: BSD extension tests ──
run_test "-empty" tree -empty
run_test "-delete setup" tree -empty -type f -print
run_test_no_oracle "-ls runs" tree -maxdepth 1 -ls
run_test "-not alias" tree -not -name "*.txt"
run_test "-and/-or aliases" tree -name "*.c" -or -name "*.h"

# ── T13.4: GNU extension tests ──
run_test "-readable" tree -readable -type f
run_test_no_oracle "-printf %p" tree -maxdepth 1 -printf "%p\n"
run_test "-xtype f" tree -xtype f

# ── T13.5: Edge cases ──
# Broken symlinks
run_test "Broken symlink not in -type f" tree -type f
run_test "Broken symlink in -type l" tree -type l

# Symlink loop (create one)
ln -sf "$TMPDIR/tree/a" "$TMPDIR/tree/a/self_loop" 2>/dev/null || true
run_test_no_oracle "Symlink loop handled" tree

# Deeply nested maxdepth
run_test "-maxdepth 2" tree -maxdepth 2

# Empty -name with wildcard
run_test "-name * (all)" tree -name "*"

# Implicit print with action
run_test "Action inhibits implicit print" tree -name "*.txt" -print

echo ""
echo "$TEST_PASS passed, $TEST_FAIL failed out of $((TEST_PASS+TEST_FAIL)) tests."
exit $((TEST_FAIL > 0 ? 1 : 0))
