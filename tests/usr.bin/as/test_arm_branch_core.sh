#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-arm-branch-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_arm_encode.c" \
   "$ROOT/usr.bin/as/as_arm_branch.c" \
   "$ROOT/tests/usr.bin/as/test_arm_branch_core.c" \
   -o "$TMP/test_arm_branch_core"

"$TMP/test_arm_branch_core"
echo "ok: arm branch core"
