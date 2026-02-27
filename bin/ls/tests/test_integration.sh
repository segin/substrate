#!/bin/sh
set -eu

LS_BIN=${1:-./ls_host}

fail() {
    echo "ls integration: FAIL: $*" >&2
    exit 1
}

TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/ls_int.XXXXXX")
trap 'chmod -R u+rwx "$WORK" >/dev/null 2>&1 || true; rm -rf "$WORK"' EXIT INT TERM

# empty directory
mkdir "$WORK/empty"
out=$("$LS_BIN" "$WORK/empty")
[ -z "$out" ] || fail "empty directory should produce empty output"

# hidden handling
mkdir "$WORK/hid"
touch "$WORK/hid/.hidden" "$WORK/hid/visible"
"$LS_BIN" "$WORK/hid" | grep -q "visible" || fail "visible missing"
if "$LS_BIN" "$WORK/hid" | grep -q ".hidden"; then
    fail ".hidden should not appear without -a"
fi
"$LS_BIN" -a "$WORK/hid" | grep -q "^\.$" || fail "-a missing dot"
"$LS_BIN" -a "$WORK/hid" | grep -q "^\.\.$" || fail "-a missing dotdot"

# special files
mkdir "$WORK/special"
mkfifo "$WORK/special/pipe"
python3 - "$WORK/special/sock" <<'PY'
import os
import socket
import sys
path = sys.argv[1]
try:
    os.unlink(path)
except FileNotFoundError:
    pass
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.bind(path)
s.close()
PY
"$LS_BIN" -l "$WORK/special" | grep -q " pipe$" || fail "fifo missing from long listing"
"$LS_BIN" -l "$WORK/special" | grep -q " sock$" || fail "socket missing from long listing"

# recursive and permission denied
mkdir -p "$WORK/rec/sub"
touch "$WORK/rec/sub/file"
mkdir "$WORK/rec/denied"
touch "$WORK/rec/denied/x"
chmod 000 "$WORK/rec/denied"
if "$LS_BIN" -R "$WORK/rec" >"$WORK/ls_int_out" 2>"$WORK/ls_int_err"; then
    rc=0
else
    rc=$?
fi
chmod 700 "$WORK/rec/denied"
[ "$rc" -eq 1 ] || fail "-R with denied dir should return 1"
grep -q "cannot open directory" "$WORK/ls_int_err" || fail "permission error missing"
rm -f "$WORK/ls_int_out" "$WORK/ls_int_err"

# large directory (1000+)
mkdir "$WORK/big"
i=1
while [ "$i" -le 1200 ]; do
    : > "$WORK/big/f$i"
    i=$((i + 1))
done
count=$("$LS_BIN" -1 "$WORK/big" | wc -l | tr -d ' ')
[ "$count" = "1200" ] || fail "large directory count mismatch ($count)"

# weird names and machine parsing
mkdir "$WORK/weird"
touch "$WORK/weird/space name"
touch "$WORK/weird/quote\"name"
python3 - "$WORK/weird" <<'PY'
import os, sys
open(os.path.join(sys.argv[1], "line\nbreak"), "wb").close()
PY
parsed=$("$LS_BIN" -1 --quoting-style=escape "$WORK/weird" | awk 'NF {print $1}' | wc -l | tr -d ' ')
[ "$parsed" = "3" ] || fail "awk parsing should return 3 entries"
"$LS_BIN" -1 --quoting-style=escape "$WORK/weird" | grep -q "line\\\\nbreak" || fail "newline name must be escaped"

# mixed sorting criteria
mkdir "$WORK/mix"
printf '123456' > "$WORK/mix/big"
printf '1' > "$WORK/mix/small"
sleep 1
printf '2' > "$WORK/mix/newer"
"$LS_BIN" -1S "$WORK/mix" | head -n 1 | grep -q "^big$" || fail "-S ordering incorrect"
"$LS_BIN" -1t "$WORK/mix" | head -n 1 | grep -q "^newer$" || fail "-t ordering incorrect"

echo "ls integration: PASS"
