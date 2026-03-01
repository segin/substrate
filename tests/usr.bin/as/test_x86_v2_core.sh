#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-x86-v2-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_x86_v2.c" \
   "$ROOT/tests/usr.bin/as/test_x86_v2_core.c" \
   -o "$TMP/test_x86_v2_core"

"$TMP/test_x86_v2_core"
echo "ok: x86 v2 core"
