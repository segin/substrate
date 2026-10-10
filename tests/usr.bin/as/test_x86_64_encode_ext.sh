#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-x86-64-encode-ext-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_x86_encode.c" \
   "$ROOT/usr.bin/as/as_x86_vex.c" \
   "$ROOT/usr.bin/as/as_x86_avx.c" \
   "$ROOT/usr.bin/as/as_x86_avx2.c" \
   "$ROOT/usr.bin/as/as_x86_fma.c" \
   "$ROOT/usr.bin/as/as_x86_sse3.c" \
   "$ROOT/usr.bin/as/as_x86_ssse3.c" \
   "$ROOT/usr.bin/as/as_x86_sse41.c" \
   "$ROOT/usr.bin/as/as_x86_sse42.c" \
   "$ROOT/usr.bin/as/as_x86_v2.c" \
   "$ROOT/usr.bin/as/as_x86_v3_misc.c" \
   "$ROOT/usr.bin/as/as_x86_bmi1.c" \
   "$ROOT/usr.bin/as/as_x86_bmi2.c" \
   "$ROOT/tests/usr.bin/as/test_x86_64_encode_ext.c" \
   -o "$TMP/test_x86_64_encode_ext"

"$TMP/test_x86_64_encode_ext"
echo "ok: x86_64 encode ext"
