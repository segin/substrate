#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-x86-avx512cd-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_x86_evex.c" \
   "$ROOT/usr.bin/as/as_x86_avx512cd.c" \
   "$ROOT/tests/usr.bin/as/test_x86_avx512cd_core.c" \
   -o "$TMP/test_x86_avx512cd_core"

"$TMP/test_x86_avx512cd_core"
echo "ok: x86 avx512cd core"
