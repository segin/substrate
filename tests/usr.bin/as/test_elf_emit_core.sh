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
   "$ROOT/usr.bin/as/as_util.c" \
   "$ROOT/usr.bin/as/as_x86_encode.c" \
   "$ROOT/usr.bin/as/as_cfi.c" \
   "$ROOT/usr.bin/as/as_dwline.c" \
   "$ROOT/usr.bin/as/as_arm.c" \
   "$ROOT/usr.bin/as/as_arm_simd.c" \
   "$ROOT/usr.bin/as/as_arm_thumb.c" \
   "$ROOT/usr.bin/as/as_a64.c" \
   "$ROOT/usr.bin/as/as_a64_simd.c" \
   "$ROOT/usr.bin/as/as_x86_vec.c" \
   "$ROOT/usr.bin/as/as_x86_vec_table.c" \
   "$ROOT/usr.bin/as/as_x86_vex.c" \
   "$ROOT/usr.bin/as/as_x86_sse3.c" \
   "$ROOT/usr.bin/as/as_expr.c" "$ROOT/usr.bin/as/as_lexer.c" "$ROOT/usr.bin/as/as_x86_regs.c" "$ROOT/usr.bin/as/as_parser.c" "$ROOT/usr.bin/as/as_symtab.c" "$ROOT/usr.bin/as/as_reloc_op.c" \
   "$ROOT/usr.bin/as/as_sections.c" "$ROOT/usr.bin/as/as_data.c" "$ROOT/usr.bin/as/as_elf_emit.c" \
   "$ROOT/tests/usr.bin/as/test_elf_emit_core.c" "${ELFOBJ_A:-$ROOT/usr.lib/elfobj/libelfobj.a}" \
   -o "$TMP/test_elf_emit_core"

"$TMP/test_elf_emit_core" "$TMP/emit.s" "$TMP/out.o"
echo "ok: elf emit core"
