#!/bin/sh
set -eu

MKDIR_BIN=${1:-./mkdir_host}

fail() {
    echo "integration: FAIL: $*" >&2
    exit 1
}

mode_of() {
    stat -c '%a' "$1" 2>/dev/null || stat -f '%Mp%Lp' "$1"
}

TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/mkdir_int.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

"$MKDIR_BIN" --help >/dev/null
"$MKDIR_BIN" --version | grep -q "mkdir (Substrate)" || fail "missing version output"

"$MKDIR_BIN" "$WORK/one"
[ -d "$WORK/one" ] || fail "single directory not created"

"$MKDIR_BIN" -p "$WORK/a/b/c"
[ -d "$WORK/a/b/c" ] || fail "-p deep path not created"

"$MKDIR_BIN" -m 700 "$WORK/mode700"
[ "$(mode_of "$WORK/mode700")" = "700" ] || fail "numeric -m not applied"

"$MKDIR_BIN" -m u=rwx,go= "$WORK/modesym"
[ "$(mode_of "$WORK/modesym")" = "700" ] || fail "symbolic -m not applied"

if "$MKDIR_BIN" "$WORK/one" 2>/dev/null; then
    fail "existing directory should fail without -p"
fi

"$MKDIR_BIN" -p "$WORK/one"

touch "$WORK/file"
if "$MKDIR_BIN" -p "$WORK/file/sub" 2>/dev/null; then
    fail "file component should fail under -p"
fi

OUT=$("$MKDIR_BIN" -pv "$WORK/pv/a/b" 2>&1)
printf '%s\n' "$OUT" | grep -q "created directory '" || fail "-v output missing"

OUT=$("$MKDIR_BIN" -Z "$WORK/ctxdir" 2>&1)
[ -d "$WORK/ctxdir" ] || fail "-Z should still create directory"
printf '%s\n' "$OUT" | grep -q "SELinux contexts are not supported" || fail "-Z warning missing"

"$MKDIR_BIN" -p "$WORK/trailing/slash/"
[ -d "$WORK/trailing/slash" ] || fail "trailing slash path not created"

("$MKDIR_BIN" -p "$WORK/race/a/b/c" >/dev/null 2>&1) &
pid1=$!
("$MKDIR_BIN" -p "$WORK/race/a/b/c" >/dev/null 2>&1) &
pid2=$!
wait "$pid1" || fail "first concurrent mkdir -p failed"
wait "$pid2" || fail "second concurrent mkdir -p failed"
[ -d "$WORK/race/a/b/c" ] || fail "concurrent mkdir -p path missing"

echo "integration: PASS"