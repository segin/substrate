#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPIO="$ROOT/cpio"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src/dir"
printf 'hello\n' > "$TMP/src/file.txt"
ln "$TMP/src/file.txt" "$TMP/src/hard"
ln -s file.txt "$TMP/src/link"
mkfifo "$TMP/src/pipe"
printf '%s\n' src src/dir src/file.txt src/hard src/link src/pipe | (cd "$TMP" && "$CPIO" -o -H newc -F arch.cpio)
mkdir -p "$TMP/out"
(cd "$TMP/out" && "$CPIO" -i -d -m -F "$TMP/arch.cpio")
test -f "$TMP/out/src/file.txt"
test -L "$TMP/out/src/link"
test -p "$TMP/out/src/pipe"
cmp "$TMP/src/file.txt" "$TMP/out/src/file.txt"
