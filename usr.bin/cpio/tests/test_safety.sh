#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPIO="$ROOT/cpio"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/in"
printf hi > "$TMP/in/file"
printf '%s\n' ../escape /abs src | (cd "$TMP/in" && "$CPIO" -o -H newc -F "$TMP/a.cpio" || true)
mkdir -p "$TMP/out"
(cd "$TMP/out" && "$CPIO" -i -d --safe-extract -F "$TMP/a.cpio" || true)
test ! -e "$TMP/escape"
