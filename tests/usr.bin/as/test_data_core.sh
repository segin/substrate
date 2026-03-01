#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-data-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

printf '\x11\x22\x33\x44' > "$TMP/blob.bin"

cat > "$TMP/data.s" <<'SRC'
.byte 1, 2
.short 3
.hword 4
.long 5
.int 6
.quad 7
.8byte 8
.float 1.5
.double 2.5
.ascii "AB"
.asciz "CD"
.string "EF"
.zero 16
.space 8
.fill 3, 2, 0x41
.skip 4
.org 128
.incbin "blob.bin", 1, 2
SRC

cc -Wall -Wextra -Werror -D_GNU_SOURCE -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_lexer.c" "$ROOT/usr.bin/as/as_parser.c" "$ROOT/usr.bin/as/as_data.c" \
   "$ROOT/tests/usr.bin/as/test_data_core.c" \
   -o "$TMP/test_data_core"

"$TMP/test_data_core" "$TMP/data.s"
echo "ok: data core"
