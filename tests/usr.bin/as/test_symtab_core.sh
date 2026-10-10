#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-symtab-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cat > "$TMP/symtab.s" <<'SRC'
.global gsym
.weak wsym
.local lsym
.hidden gsym
.protected wsym
.internal isym
.type gsym, @function
.size gsym, 64
.comm csym, 16, 4
.lcomm lcsym, 8, 2
.symver gsym, gsym@@VERS_1

gsym:
mov $fsym+4, %eax
mov wsym, %ebx
mov ext_missing, %ecx
lsym:
isym:
wsym:
fsym:
SRC

cc -Wall -Wextra -Werror -D_GNU_SOURCE -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_lexer.c" "$ROOT/usr.bin/as/as_parser.c" "$ROOT/usr.bin/as/as_symtab.c" \
   "$ROOT/tests/usr.bin/as/test_symtab_core.c" \
   -o "$TMP/test_symtab_core"

"$TMP/test_symtab_core" "$TMP/symtab.s"
echo "ok: symtab core"
