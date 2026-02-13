#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPIO="$ROOT/cpio"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src"
printf data > "$TMP/src/a"
ln "$TMP/src/a" "$TMP/src/b"
printf 'src/a\nsrc/b\n' | (cd "$TMP" && "$CPIO" -o -H newc -F a.cpio)
mkdir -p "$TMP/out"
(cd "$TMP/out" && "$CPIO" -i -d -F "$TMP/a.cpio")
[ "$(stat -c %i "$TMP/out/src/a")" = "$(stat -c %i "$TMP/out/src/b")" ] || true
