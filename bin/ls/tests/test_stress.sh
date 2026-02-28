#!/bin/sh
set -eu

LS_BIN=${1:-./ls_host}
COUNT=${LS_STRESS_COUNT:-100000}
TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/ls_stress.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

mkdir -p "$WORK/big"
i=1
while [ "$i" -le "$COUNT" ]; do
    : > "$WORK/big/f$i"
    i=$((i + 1))
done

listed=$("$LS_BIN" -1 "$WORK/big" | wc -l | tr -d ' ')
[ "$listed" = "$COUNT" ] || {
    echo "ls stress: FAIL: expected $COUNT entries, got $listed" >&2
    exit 1
}

echo "ls stress: PASS ($COUNT entries)"
