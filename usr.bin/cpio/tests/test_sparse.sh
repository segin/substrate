#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPIO="$ROOT/cpio"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src"
truncate -s 0 "$TMP/src/sparse"
dd if=/dev/zero of="$TMP/src/sparse" bs=1 count=1 seek=$((2*1024*1024)) status=none
printf 'src/sparse\n' | (cd "$TMP" && "$CPIO" -o -H newc -F a.cpio)
mkdir -p "$TMP/out"
(cd "$TMP/out" && "$CPIO" -i -d -F "$TMP/a.cpio")
[ "$(stat -c %s "$TMP/out/src/sparse")" -eq $((2*1024*1024+1)) ]
