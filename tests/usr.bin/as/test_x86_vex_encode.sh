#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-x86-vex-encode-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_x86_vex.c" \
   "$ROOT/tests/usr.bin/as/test_x86_vex_encode.c" \
   -o "$TMP/test_x86_vex_encode"

"$TMP/test_x86_vex_encode"
echo "ok: x86 vex encode"
