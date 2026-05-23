#!/bin/sh
# Tests for bin/df — POSIX.1-2024 + GNU + BSD.
# Host-compile df.c; statvfs() is provided by the host libc.  The
# mount-table is taken from a fixture via DF_MOUNTS_FILE so the test
# is independent of the host's /proc/mounts contents.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC="$REPO/bin/df/df.c"
BIN="$(mktemp)"
WORK="$(mktemp -d)"
trap 'rm -f "$BIN"; rm -rf "$WORK"' EXIT

cc -O2 -std=c2x -Wall -o "$BIN" "$SRC" || { echo "FAIL: compile"; exit 1; }
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

# Two mountpoints that exist on the host: / and the temp dir we made.
cat > "$WORK/mounts" <<EOF
rootfs / ext2 rw 0 0
tmpfs $WORK tmpfs rw 0 0
EOF
export DF_MOUNTS_FILE="$WORK/mounts"

# Header is always present in default and -P modes.
check "header default" "Filesystem" \
      "$("$BIN" | head -1 | awk '{print $1}')"

# Default lists both fixture entries.
check "lists root mount"  "1" \
      "$("$BIN" | grep -c ' / *$')"
check "lists work mount"  "1" \
      "$("$BIN" | grep -c " $WORK *$")"

# -h human-readable produces a "Size" column.
check "-h header Size" "Size" \
      "$("$BIN" -h | head -1 | awk '{print $2}')"

# -T adds the Type column.
check "-T Type column" "Type" \
      "$("$BIN" -T | head -1 | awk '{print $2}')"
check "-T shows ext2 type" "ext2" \
      "$("$BIN" -T | grep ' / *$' | awk '{print $2}')"

# -i switches to inode columns.
check "-i Inodes column" "Inodes" \
      "$("$BIN" -i | head -1 | awk '{print $2}')"

# -t filter matches one mount.
check "-t ext2 (only root)" "1" \
      "$("$BIN" -t ext2 | tail -n +2 | wc -l | tr -d ' ')"

# File operand resolves to the longest-prefix mount.
testfile="$WORK/probe"
echo x > "$testfile"
check "file-operand resolves work mount" "1" \
      "$("$BIN" "$testfile" | grep -c " $WORK *$")"

# -P keeps the POSIX 1024-block default and produces same row count.
check "-P uses 1024-byte blocks" "1024-blocks" \
      "$("$BIN" -P | head -1 | awk '{print $2}')"

# -m switches to 1M block reporting.
check "-m uses 1M blocks" "1M-blocks" \
      "$("$BIN" -m | head -1 | awk '{print $2}')"

# Missing mounts file -> diagnostic + exit 1.
"$BIN" </dev/null 2>/dev/null
rc=$?      # mounts fixture is set; this should succeed
check "default exit success" "0" "$rc"

DF_MOUNTS_FILE=/nonexistent/path "$BIN" 2>/dev/null
check "missing mounts file exits nonzero" "ne0" \
      "$( [ $? -ne 0 ] && echo ne0 || echo zero )"

# --version
check "--version" "df (Substrate) 1.0" "$("$BIN" --version)"

echo "df: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
