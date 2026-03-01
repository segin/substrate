#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-elf-emit-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cat > "$TMP/emit.s" <<'SRC'
.section .debug_custom, "", @progbits
.ascii "dbg"
.text
.globl main
.type main, @function
.file 1 "emit.s"
main:
.loc 1 1 0
.cfi_startproc
mov $extsym, %eax
.cfi_endproc
.size main, 4
SRC

cc -Wall -Wextra -Werror -D_GNU_SOURCE -I"$ROOT/usr.bin/as" -iquote "$ROOT/include" \
   "$ROOT/usr.bin/as/as_lexer.c" "$ROOT/usr.bin/as/as_parser.c" "$ROOT/usr.bin/as/as_symtab.c" \
   "$ROOT/usr.bin/as/as_sections.c" "$ROOT/usr.bin/as/as_data.c" "$ROOT/usr.bin/as/as_elf_emit.c" \
   "$ROOT/tests/usr.bin/as/test_elf_emit_core.c" "$ROOT/usr.lib/elfobj/libelfobj.a" \
   -o "$TMP/test_elf_emit_core"

"$TMP/test_elf_emit_core" "$TMP/emit.s" "$TMP/out.o"
echo "ok: elf emit core"
