#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-sections-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cat > "$TMP/sections.s" <<'SRC'
.text
.subsection 1
push_label:
.pushsection .foo, "ax", @progbits
.balign 32
.group grp1, comdat
.popsection
.previous
.section .bar, "aw", @nobits
.p2align 4
.data
.align 8
.rodata
.bss
SRC

cc -Wall -Wextra -Werror -D_GNU_SOURCE -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_lexer.c" "$ROOT/usr.bin/as/as_parser.c" "$ROOT/usr.bin/as/as_sections.c" \
   "$ROOT/tests/usr.bin/as/test_sections_core.c" \
   -o "$TMP/test_sections_core"

"$TMP/test_sections_core" "$TMP/sections.s"
echo "ok: sections core"
