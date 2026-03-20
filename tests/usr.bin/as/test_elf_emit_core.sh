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
   "$ROOT/usr.bin/as/as_x86_encode.c" \
   "$ROOT/usr.bin/as/as_x86_vex.c" \
   "$ROOT/usr.bin/as/as_x86_evex.c" \
   "$ROOT/usr.bin/as/as_x86_avx.c" \
   "$ROOT/usr.bin/as/as_x86_avx2.c" \
   "$ROOT/usr.bin/as/as_x86_avx512f.c" \
   "$ROOT/usr.bin/as/as_x86_avx512bw.c" \
   "$ROOT/usr.bin/as/as_x86_avx512cd.c" \
   "$ROOT/usr.bin/as/as_x86_avx512dq.c" \
   "$ROOT/usr.bin/as/as_x86_f16c.c" \
   "$ROOT/usr.bin/as/as_x86_fma.c" \
   "$ROOT/usr.bin/as/as_x86_sse3.c" \
   "$ROOT/usr.bin/as/as_x86_ssse3.c" \
   "$ROOT/usr.bin/as/as_x86_sse41.c" \
   "$ROOT/usr.bin/as/as_x86_sse42.c" \
   "$ROOT/usr.bin/as/as_x86_v2.c" \
   "$ROOT/usr.bin/as/as_x86_v3_misc.c" \
   "$ROOT/usr.bin/as/as_x86_bmi1.c" \
   "$ROOT/usr.bin/as/as_x86_bmi2.c" \
   "$ROOT/usr.bin/as/as_x86_reloc.c" \
   "$ROOT/usr.bin/as/as_lexer.c" "$ROOT/usr.bin/as/as_parser.c" "$ROOT/usr.bin/as/as_symtab.c" \
   "$ROOT/usr.bin/as/as_sections.c" "$ROOT/usr.bin/as/as_data.c" "$ROOT/usr.bin/as/as_elf_emit.c" \
   "$ROOT/tests/usr.bin/as/test_elf_emit_core.c" "$ROOT/usr.lib/elfobj/libelfobj.a" \
   -o "$TMP/test_elf_emit_core"

"$TMP/test_elf_emit_core" "$TMP/emit.s" "$TMP/out.o"
echo "ok: elf emit core"
