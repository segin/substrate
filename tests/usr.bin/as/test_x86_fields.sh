#!/bin/sh
# The fields the table's emitter says it wrote, of a displacement, a
# number and both (AS-T-190): see test_x86_fields.c.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

cc -std=gnu17 -Wall -Wextra -Werror -D_GNU_SOURCE -I"$ROOT/usr.bin/as" \
   "$ROOT/tests/usr.bin/as/test_x86_fields.c" "$ROOT/usr.bin/as/as_x86_vec.c" \
   "$ROOT/usr.bin/as/as_x86_vec_table.c" "$ROOT/usr.bin/as/as_x86_regs.c" \
   -o "$TMP/test_x86_fields"

"$TMP/test_x86_fields"
