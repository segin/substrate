#!/bin/sh
set -eu

CP_BIN=${1:-./cp_host}
WORK=$(mktemp -d /tmp/cp_stress.XXXXXX)
trap 'rm -rf "$WORK"' EXIT INT TERM

src="$WORK/src"
dst="$WORK/dst"
mkdir -p "$src"

# Large fanout + depth
for d in $(seq 1 40); do
    mkdir -p "$src/dir$d/sub"
    for f in $(seq 1 100); do
        printf 'file-%s-%s\n' "$d" "$f" > "$src/dir$d/sub/file$f"
    done
done

"$CP_BIN" -R "$src" "$dst"

# Spot-check counts and a sample hash
src_count=$(find "$src" -type f | wc -l | tr -d ' ')
dst_count=$(find "$dst" -type f | wc -l | tr -d ' ')
[ "$src_count" = "$dst_count" ] || {
    echo "stress: FAIL: file count mismatch" >&2
    exit 1
}
cmp -s "$src/dir20/sub/file20" "$dst/dir20/sub/file20" || {
    echo "stress: FAIL: sample mismatch" >&2
    exit 1
}

echo "stress: PASS"
