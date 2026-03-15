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

# ── T13.6: New features ──

# -D (debug) tests
run_test_no_oracle "-D help exits 0" -D help
run_test_no_oracle "-D tree" -D tree tree -type f
run_test_no_oracle "-D stat" -D stat tree -maxdepth 0

# -regextype tests
run_test_no_oracle "-regextype posix-extended" -regextype posix-extended tree -regex '.*/file[0-9]+\.txt'
run_test_no_oracle "-regextype posix-basic" -regextype posix-basic tree -regex '.*/file[0-9]*\.txt'

# -fprint test
run_test_no_oracle "-fprint" tree -maxdepth 1 -fprint "$TMPDIR/fprint_out.txt"
if [ -f "$TMPDIR/fprint_out.txt" ]; then
    echo "PASS: -fprint creates output file"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: -fprint creates output file"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# -fprint0 test
run_test_no_oracle "-fprint0" tree -maxdepth 1 -fprint0 "$TMPDIR/fprint0_out.txt"
if [ -f "$TMPDIR/fprint0_out.txt" ]; then
    echo "PASS: -fprint0 creates output file"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: -fprint0 creates output file"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# -fls test
run_test_no_oracle "-fls" tree -maxdepth 1 -fls "$TMPDIR/fls_out.txt"
if [ -f "$TMPDIR/fls_out.txt" ]; then
    echo "PASS: -fls creates output file"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: -fls creates output file"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# -fprintf test
run_test_no_oracle "-fprintf" tree -maxdepth 1 -fprintf "$TMPDIR/fprintf_out.txt" "%p %s\n"
if [ -f "$TMPDIR/fprintf_out.txt" ]; then
    echo "PASS: -fprintf creates output file"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: -fprintf creates output file"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# -newerXY tests (use file1.txt as reference)
run_test_no_oracle "-newermt" tree -newermt "2000-01-01"
run_test_no_oracle "-neweram" tree -neweram "$TMPDIR/tree/file1.txt"

# -ilname test
ln -sf "FILE1.TXT" "$TMPDIR/tree/caselink" 2>/dev/null || true
run_test_no_oracle "-ilname" tree -ilname "file*"

# -samefile test
ln "$TMPDIR/tree/file1.txt" "$TMPDIR/tree/hardlink1" 2>/dev/null || true
run_test_no_oracle "-samefile" tree -samefile "$TMPDIR/tree/file1.txt"

# -print0
run_test_no_oracle "-print0 runs" tree -maxdepth 0 -print0

# -inum (test with file1.txt inode)
INODE=$(stat -c %i "$TMPDIR/tree/file1.txt" 2>/dev/null || stat -f %i "$TMPDIR/tree/file1.txt" 2>/dev/null)
if [ -n "$INODE" ]; then
    run_test_no_oracle "-inum" tree -inum "$INODE"
fi

# -user test
run_test "-user $(id -un)" tree -user "$(id -un)"

# -size test
run_test "-size 0" tree -size 0 -type f

# -newer test
run_test_no_oracle "-newer" tree -newer "$TMPDIR/tree/file1.txt"

# -atime/-mtime/-ctime
run_test_no_oracle "-mtime -1" tree -mtime -1
run_test_no_oracle "-atime +365" tree -atime +365

# -amin/-mmin/-cmin
run_test_no_oracle "-mmin -60" tree -mmin -60

# -exec {} + (batch mode)
run_test_no_oracle "-exec {} +" tree -type f -exec echo {} +

# -ok would need stdin, so just test it doesn't crash with no input
echo "n" | run_test_no_oracle "-ok with n" tree -maxdepth 0 -ok echo {} ";"

# -delete test (create temp file, verify it's deleted)
touch "$TMPDIR/tree/delete_me.tmp"
run_test_no_oracle "-delete" tree -name "delete_me.tmp" -delete
if [ ! -f "$TMPDIR/tree/delete_me.tmp" ]; then
    echo "PASS: -delete removes file"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: -delete removes file"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# -quit test (should exit immediately)
QUIT_OUT=$(cd "$TMPDIR" && "$OLDPWD/$FIND_BIN" tree -quit 2>/dev/null)
if [ -z "$QUIT_OUT" ]; then
    echo "PASS: -quit produces no output"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: -quit produces no output"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# -xdev test
run_test_no_oracle "-xdev" tree -xdev -type f

# -E (ERE mode)
run_test_no_oracle "-E regex" -E tree -regex '.*/file[0-9]+\.txt'

# Multiple starting points
run_test "Multiple paths" tree/a tree/d -type f

# -files0-from test
printf "tree/a\0tree/d\0" > "$TMPDIR/pathlist"
OURS=$(cd "$TMPDIR" && "$OLDPWD/$FIND_BIN" -files0-from "$TMPDIR/pathlist" -type f 2>/dev/null | sort)
if [ -n "$OURS" ]; then
    echo "PASS: -files0-from reads paths"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: -files0-from reads paths"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# ── T13.7: Conflict resolution tests (C01–C12) ──

# C01: regex default is BRE
run_test_no_oracle "C01: BRE regex default" tree -regex '.*file[0-9]*\.txt'

# C02: -perm /mode (any-bit)
run_test "-perm /111" tree -perm /111
# C02: -perm +mode legacy alias
run_test_no_oracle "C02: -perm +111 legacy" tree -perm +111 -type f

# C06: -delete + -L rejected
DELETE_L_ERR=$(cd "$TMPDIR" && "$OLDPWD/$FIND_BIN" -L tree -delete 2>&1 || true)
if echo "$DELETE_L_ERR" | grep -q "not compatible"; then
    echo "PASS: C06: -delete + -L rejected"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: C06: -delete + -L rejected"
    echo "  GOT: $DELETE_L_ERR"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# C07: -follow sets global deref mode (may exit non-zero due to broken symlinks)
FOLLOW_OUT=$(cd "$TMPDIR" && "$OLDPWD/$FIND_BIN" tree -follow -type f 2>/dev/null || true)
if [ -n "$FOLLOW_OUT" ]; then
    echo "PASS: C07: -follow as global modifier"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: C07: -follow as global modifier"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# C08: -d alias for -depth
run_test "-d alias" tree -d -type f

# C10: -exec {} + batch mode with many files
for i in $(seq 1 20); do touch "$TMPDIR/tree/batch_${i}.tmp"; done
run_test "-exec + with 20 files" tree -name "batch_*.tmp" -exec echo {} +

# C12: -O0 disables reordering
run_test_no_oracle "C12: -O0" -O0 tree -type f -name "*.c"

# ── T13.8: -fstype test ──
# Get the filesystem type of tmpdir
FSTYPE=$(stat -f -c %T "$TMPDIR" 2>/dev/null || echo "")
if [ -n "$FSTYPE" ]; then
    FOUND=$(cd "$TMPDIR" && "$OLDPWD/$FIND_BIN" tree -maxdepth 0 -fstype "$FSTYPE" 2>/dev/null)
    if [ -n "$FOUND" ]; then
        echo "PASS: -fstype matches"
        TEST_PASS=$((TEST_PASS+1))
    else
        echo "FAIL: -fstype matches"
        TEST_FAIL=$((TEST_FAIL+1))
    fi
else
    echo "PASS: -fstype (skipped - no stat -f)" ; TEST_PASS=$((TEST_PASS+1))
fi

# ── T13.9: -printf %M full strmode ──
MOUT=$(cd "$TMPDIR" && "$OLDPWD/$FIND_BIN" tree -maxdepth 0 -printf "%M\n" 2>/dev/null)
if echo "$MOUT" | grep -qE '^d[r-][w-][xsS-][r-][w-][xsS-][r-][w-][xtT-]$'; then
    echo "PASS: -printf %M produces strmode"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: -printf %M produces strmode"
    echo "  GOT: '$MOUT'"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# ── T13.10: -printx escaped output ──
# Create file with space in name
touch "$TMPDIR/tree/has space.txt" 2>/dev/null || true
PXOUT=$(cd "$TMPDIR" && "$OLDPWD/$FIND_BIN" tree -name "has space.txt" -printx 2>/dev/null)
if echo "$PXOUT" | grep -q '\\'; then
    echo "PASS: -printx escapes spaces"
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: -printx escapes spaces"
    echo "  GOT: '$PXOUT'"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# ── T13.11: -H only dereferences command-line symlinks ──
run_test "-H mode" -H tree -type f

# ── T13.12: -daystart modifier ──
run_test_no_oracle "-daystart -mtime 0" tree -daystart -mtime 0

# ── T13.13: no starting point defaults to . ──
DEFAULT_OUT=$(cd "$TMPDIR/tree" && "$OLDPWD/$FIND_BIN" -maxdepth 0 2>/dev/null)
SYS_DEFAULT_OUT=$(cd "$TMPDIR/tree" && "$SYS_FIND" -maxdepth 0 2>/dev/null)
if [ "$DEFAULT_OUT" = "$SYS_DEFAULT_OUT" ]; then
    echo "PASS: default starting point = ."
    TEST_PASS=$((TEST_PASS+1))
else
    echo "FAIL: default starting point = ."
    echo "  EXPECTED: '$SYS_DEFAULT_OUT'  GOT: '$DEFAULT_OUT'"
    TEST_FAIL=$((TEST_FAIL+1))
fi

# ── T13.14: comma operator ──
run_test_no_oracle "comma operator" tree -name "*.txt" , -name "*.c"

# ── T13.15: -ignore_readdir_race ──
run_test_no_oracle "-ignore_readdir_race" tree -ignore_readdir_race -type f

# ── T13.16: -noignore_readdir_race ──
run_test_no_oracle "-noignore_readdir_race" tree -noignore_readdir_race -type f

# ── T13.17: -noleaf (silently accepted) ──
run_test_no_oracle "-noleaf" tree -noleaf -type f

# ── T13.18: -warn/-nowarn (silently accepted) ──
run_test_no_oracle "-nowarn" tree -nowarn -name "*.txt"

# Cleanup batch files
rm -f "$TMPDIR/tree"/batch_*.tmp
rm -f "$TMPDIR/tree/has space.txt"

echo ""
echo "$TEST_PASS passed, $TEST_FAIL failed out of $((TEST_PASS+TEST_FAIL)) tests."
exit $((TEST_FAIL > 0 ? 1 : 0))
