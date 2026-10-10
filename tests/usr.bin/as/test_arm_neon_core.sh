#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-arm-neon-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_arm_neon.c" \
   "$ROOT/tests/usr.bin/as/test_arm_neon_core.c" \
   -o "$TMP/test_arm_neon_core"

"$TMP/test_arm_neon_core"
echo "ok: arm neon core"
