#!/bin/sh
set -eu

RM_BIN=${1:-./rm_host}

fail() {
    echo "integration: FAIL: $*" >&2
    exit 1
}

TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/rm_int.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

"$RM_BIN" --help >/dev/null
"$RM_BIN" --version | grep -q "rm (Substrate)" || fail "missing version output"

touch "$WORK/file1"
"$RM_BIN" "$WORK/file1"
[ ! -e "$WORK/file1" ] || fail "single file not removed"

touch "$WORK/file2" "$WORK/file3"
"$RM_BIN" "$WORK/file2" "$WORK/file3"
[ ! -e "$WORK/file2" ] || fail "multiple file removal left file2"
[ ! -e "$WORK/file3" ] || fail "multiple file removal left file3"

mkdir "$WORK/empty"
"$RM_BIN" -d "$WORK/empty"
[ ! -e "$WORK/empty" ] || fail "-d did not remove empty directory"

mkdir -p "$WORK/tree/sub"
touch "$WORK/tree/sub/file"
"$RM_BIN" -r "$WORK/tree"
[ ! -e "$WORK/tree" ] || fail "-r did not remove directory tree"

"$RM_BIN" -f "$WORK/nonexistent"

touch "$WORK/verbose"
OUT=$("$RM_BIN" -v "$WORK/verbose" 2>&1)
printf '%s\n' "$OUT" | grep -q "removed" || fail "-v output missing"

mkdir "$WORK/outside"
touch "$WORK/outside/keep"
ln -s "$WORK/outside" "$WORK/linkdir"
"$RM_BIN" -r "$WORK/linkdir"
[ ! -L "$WORK/linkdir" ] || fail "symlink to directory not removed"
[ -e "$WORK/outside/keep" ] || fail "symlink target should not be removed"

if "$RM_BIN" -rf / >/tmp/rm-preserve-root.$$ 2>&1; then
    rm -f /tmp/rm-preserve-root.$$
    fail "preserve-root should refuse rm -rf /"
fi
grep -q "dangerous to operate recursively on '/'" /tmp/rm-preserve-root.$$ || fail "preserve-root message missing"
rm -f /tmp/rm-preserve-root.$$

touch "$WORK/trailing"
if "$RM_BIN" "$WORK/trailing/" 2>/dev/null; then
    fail "trailing slash on non-directory should fail"
fi

echo "integration: PASS"