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

# BSD -P: overwrite-and-unlink for regular files.
echo "secret-data-here" > "$WORK/scrub_me"
"$RM_BIN" -P "$WORK/scrub_me"
[ ! -e "$WORK/scrub_me" ] || fail "-P did not unlink the file"

# BSD -P combined with -r over a directory tree.
mkdir -p "$WORK/scrub_tree/sub"
echo "leaf" > "$WORK/scrub_tree/sub/leaf"
echo "top"  > "$WORK/scrub_tree/top"
"$RM_BIN" -Pr "$WORK/scrub_tree"
[ ! -e "$WORK/scrub_tree" ] || fail "-Pr did not remove the tree"

# BSD -x alias for --one-file-system (smoke: just accept the flag).
mkdir "$WORK/x_alias"
echo "x" > "$WORK/x_alias/file"
"$RM_BIN" -rx "$WORK/x_alias"
[ ! -e "$WORK/x_alias" ] || fail "-x alias did not allow removal"

# GNU --preserve-root=all takes 'all' arg without error.
mkdir "$WORK/pr_all"
echo "x" > "$WORK/pr_all/file"
"$RM_BIN" --preserve-root=all -r "$WORK/pr_all"
[ ! -e "$WORK/pr_all" ] || fail "--preserve-root=all blocked non-/ removal"

# GNU --preserve-root rejects unknown argument.
if "$RM_BIN" --preserve-root=bogus dummy 2>/dev/null; then
    fail "--preserve-root=bogus should fail"
fi

echo "integration: PASS"