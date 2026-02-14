#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPIO="$ROOT/cpio"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src"
if [ "$(id -u)" -eq 0 ]; then
  mknod "$TMP/src/nullnode" c 1 3
  printf 'src/nullnode\n' | (cd "$TMP" && "$CPIO" -o -H newc -F a.cpio)
  mkdir -p "$TMP/out"
  (cd "$TMP/out" && "$CPIO" -i -d -F "$TMP/a.cpio")
  test -c "$TMP/out/src/nullnode"
else
  echo "non-root: device-node create test skipped"
fi
