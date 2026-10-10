#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-arm-system-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_arm_system.c" \
   "$ROOT/tests/usr.bin/as/test_arm_system_core.c" \
   -o "$TMP/test_arm_system_core"

"$TMP/test_arm_system_core"
echo "ok: arm system core"
