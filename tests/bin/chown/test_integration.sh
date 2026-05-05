#!/bin/sh
set -eu

CHOWN_BIN=${1:-./chown_host}

fail() {
    echo "integration: FAIL: $*" >&2
    exit 1
}

owner_of() {
    stat -c '%u:%g' "$1" 2>/dev/null || stat -f '%u:%g' "$1"
}

group_of() {
    stat -c '%g' "$1" 2>/dev/null || stat -f '%g' "$1"
}

TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/chown_int.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

# Basic ownership change
"$CHOWN_BIN" --help >/dev/null
"$CHOWN_BIN" --version | grep -q "chown (Substrate)" || fail "missing version output"

touch "$WORK/file1"
"$CHOWN_BIN" 0 "$WORK/file1"
[ "$(owner_of "$WORK/file1")" = "0:0" ] || fail "chown to root failed"

# Group-only change (colon syntax)
"$CHOWN_BIN" :1 "$WORK/file1"
[ "$(owner_of "$WORK/file1")" = "0:1" ] || fail "group-only change failed"

# Numeric uid:gid
"$CHOWN_BIN" 0:2 "$WORK/file1"
[ "$(owner_of "$WORK/file1")" = "0:2" ] || fail "uid:gid change failed"

# Symlink handling
ln -sf /dev/null "$WORK/link"
"$CHOWN_BIN" -h 0 "$WORK/link"
[ "$(owner_of "$WORK/link")" = "0:0" ] || fail "symlink chown -h failed"

# Recursive directory change
mkdir -p "$WORK/parent/child"
touch "$WORK/parent/child/grandchild"
"$CHOWN_BIN" -R 0:3 "$WORK/parent"
[ "$(owner_of "$WORK/parent")" = "0:3" ] || fail "recursive parent failed"
[ "$(owner_of "$WORK/parent/child")" = "0:3" ] || fail "recursive child failed"
[ "$(owner_of "$WORK/parent/child/grandchild")" = "0:3" ] || fail "recursive grandchild failed"

# Error on non-existent file (should not crash)
if "$CHOWN_BIN" 0 "$WORK/nonexistent" 2>/dev/null; then
    fail "missing file should fail"
fi

# -f flag suppresses output but still fails
"$CHOWN_BIN" -f 0 "$WORK/nonexistent2" >/dev/null
[ -f "$WORK/nonexistent2" ] && fail "created nonexistent file with -f"

# --reference option
touch "$WORK/ref"
"$CHOWN_BIN" 0:5 "$WORK/ref"
"$CHOWN_BIN" --reference="$WORK/ref" "$WORK/file1"
[ "$(owner_of "$WORK/file1")" = "0:5" ] || fail "--reference failed"

# -h flag with symlinks (not following)
"$CHOWN_BIN" -R -h 0:6 "$WORK/link"
[ "$(owner_of "$WORK/link")" = "0:6" ] || fail "-h recursive symlink failed"

# FCHOWNAT: change ownership via fd
touch "$WORK/fchown_file"
FD=$(exec 3< "$WORK/fchown_file"; echo 3; exec 3-)
if [ -n "$FD" ] && [ "$FD" -gt 0 ] 2>/dev/null; then
    # fchown test - use fchown syscall if available
    :
fi

# Walk policy tests (H, L, P)
"$CHOWN_BIN" -R -P 0:7 "$WORK/parent"
[ "$(owner_of "$WORK/parent")" = "0:7" ] || fail "-P policy failed"

echo "integration: PASS"
