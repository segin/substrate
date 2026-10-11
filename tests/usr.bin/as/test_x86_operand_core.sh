#!/bin/sh
# The operand a register's name is, for every register (AS-T-160): see
# test_x86_operand_core.c.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_x86_regs.c" \
   "$ROOT/tests/usr.bin/as/test_x86_operand_core.c" \
   -o "$TMP/test_x86_operand_core"

"$TMP/test_x86_operand_core"
