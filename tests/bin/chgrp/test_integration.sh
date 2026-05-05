#!/bin/sh
set -eu

CHGRP_BIN=${1:-./chgrp_host}

fail() {
    echo "integration: FAIL: $*" >&2
    exit 1
}

group_of() {
    stat -c '%g' "$1" 2>/dev/null || stat -f '%g' "$1"
}

TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/chgrp_int.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

# Basic group change
"$CHGRP_BIN" --help >/dev/null
"$CHGRP_BIN" --version | grep -q "chgrp (Substrate)" || fail "missing version output"

touch "$WORK/file1"
"$CHGRP_BIN" 0 "$WORK/file1"
[ "$(group_of "$WORK/file1")" = "0" ] || fail "chgrp to root group failed"

# Recursive directory change
mkdir -p "$WORK/parent/child"
touch "$WORK/parent/child/grandchild"
"$CHGRP_BIN" -R 3 "$WORK/parent"
[ "$(group_of "$WORK/parent")" = "3" ] || fail "recursive parent failed"
[ "$(group_of "$WORK/parent/child")" = "3" ] || fail "recursive child failed"
[ "$(group_of "$WORK/parent/child/grandchild")" = "3" ] || fail "recursive grandchild failed"

# Error on non-existent file (should not crash)
if "$CHGRP_BIN" 0 "$WORK/nonexistent" 2>/dev/null; then
    fail "missing file should fail"
fi

# -f flag suppresses output but still fails
"$CHGRP_BIN" -f 0 "$WORK/nonexistent2" >/dev/null
[ -f "$WORK/nonexistent2" ] && fail "created nonexistent file with -f"

# --reference option
touch "$WORK/ref"
"$CHGRP_BIN" 5 "$WORK/ref"
"$CHGRP_BIN" --reference="$WORK/ref" "$WORK/file1"
[ "$(group_of "$WORK/file1")" = "5" ] || fail "--reference failed"

# -h flag with symlinks
ln -sf /dev/null "$WORK/link"
"$CHGRP_BIN" -h 6 "$WORK/link"
[ "$(group_of "$WORK/link")" = "6" ] || fail "-h symlink group failed"

# Walk policy tests
"$CHGRP_BIN" -R -P 7 "$WORK/parent"
[ "$(group_of "$WORK/parent")" = "7" ] || fail "-P policy failed"

echo "integration: PASS"
