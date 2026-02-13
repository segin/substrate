#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPIO="$ROOT/cpio"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src"
printf 'x' > "$TMP/src/f"
printf 'src/f\n' | (cd "$TMP" && "$CPIO" -o -H newc -F a.cpio)
if command -v pax >/dev/null 2>&1; then
  pax -f "$TMP/a.cpio" >/dev/null
elif command -v cpio >/dev/null 2>&1; then
  cpio -it < "$TMP/a.cpio" >/dev/null
else
  echo "no system pax/cpio; skipping"
fi
