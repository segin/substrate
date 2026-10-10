#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-symver-elf-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cat > "$TMP/symver.s" <<'SRC'
.text
.globl gsym
.type gsym, @function
.symver gsym, gsym@@VERS_1

gsym:
    ret
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
   "$ROOT/tests/usr.bin/as/test_symver_elf.c" "$ROOT/usr.lib/elfobj/libelfobj.a" \
   -o "$TMP/test_symver_elf"

"$TMP/test_symver_elf" "$TMP/symver.s" "$TMP/out.o"
echo "ok: symver elf"
