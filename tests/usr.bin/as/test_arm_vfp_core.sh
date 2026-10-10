#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-arm-vfp-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_arm_vfp.c" \
   "$ROOT/tests/usr.bin/as/test_arm_vfp_core.c" \
   -o "$TMP/test_arm_vfp_core"

"$TMP/test_arm_vfp_core"
echo "ok: arm vfp core"
