#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-a64-simd-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_a64_encode.c" \
   "$ROOT/usr.bin/as/as_a64_simd.c" \
   "$ROOT/tests/usr.bin/as/test_a64_simd_core.c" \
   -o "$TMP/test_a64_simd_core"

"$TMP/test_a64_simd_core"
echo "ok: a64 simd core"
