#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-lexer-core-$$
mkdir -p "$TMP/inc"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/att.s" <<'SRC'
start: mov $0x10, %eax # comment
.ascii "A\nB\x43" /* block */
SRC

cat > "$TMP/intel.s" <<'SRC'
mov eax, 42 ; line comment
SRC

cat > "$TMP/inc/defs.inc" <<'SRC'
inc_label: add $1, %eax
SRC

cat > "$TMP/include.s" <<'SRC'
.include "defs.inc"
main_label: nop
SRC

cat > "$TMP/preproc.s" <<'SRC'
.if 1
.else
.endif
.ifdef SYM
.ifndef SYM
.macro M arg
.endm
.rept 2
.endr
.irp x,a,b
.irpc c,XYZ
SRC

cc -O2 -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_lexer.c" "$ROOT/tests/usr.bin/as/test_lexer_core.c" \
   -o "$TMP/test_lexer_core"

"$TMP/test_lexer_core" "$TMP/att.s" "$TMP/intel.s" "$TMP/include.s" "$TMP/preproc.s" "$TMP/inc"

echo "ok: lexer core"
