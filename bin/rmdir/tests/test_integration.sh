#!/bin/sh
set -eu

RMDIR_BIN=${1:-./rmdir_host}

fail() {
    echo "integration: FAIL: $*" >&2
    exit 1
}

TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/rmdir_int.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

"$RMDIR_BIN" --help >/dev/null
"$RMDIR_BIN" --version | grep -q "rmdir (Substrate)" || fail "missing version output"

mkdir "$WORK/one"
"$RMDIR_BIN" "$WORK/one"
[ ! -e "$WORK/one" ] || fail "single directory not removed"

mkdir -p "$WORK/a/b/c"
"$RMDIR_BIN" -p "$WORK/a/b/c"
[ ! -e "$WORK/a" ] || fail "-p did not remove ancestors"

mkdir -p "$WORK/nonempty/sub"
if "$RMDIR_BIN" "$WORK/nonempty" 2>/dev/null; then
    fail "non-empty directory should fail"
fi

mkdir -p "$WORK/keep/sub/child"
mkdir "$WORK/keep/other"
"$RMDIR_BIN" -p --ignore-fail-on-non-empty "$WORK/keep/sub/child"
[ -e "$WORK/keep" ] || fail "ignore-fail-on-non-empty removed too much"
[ ! -e "$WORK/keep/sub" ] || fail "ignore-fail-on-non-empty did not remove emptied path"

touch "$WORK/file"
if "$RMDIR_BIN" "$WORK/file" 2>/dev/null; then
    fail "non-directory should fail"
fi

ln -s "$WORK/keep" "$WORK/linkdir"
if "$RMDIR_BIN" "$WORK/linkdir" 2>/dev/null; then
    fail "symlink should not be followed"
fi

OUT=$(mkdir "$WORK/verbose" && "$RMDIR_BIN" -v "$WORK/verbose" 2>&1)
printf '%s\n' "$OUT" | grep -q "removing directory" || fail "verbose output missing"

if "$RMDIR_BIN" / 2>/dev/null; then
    fail "removing / should fail"
fi

echo "integration: PASS"