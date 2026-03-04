#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-parser-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cat > "$TMP/x86.s" <<'SRC'
lock repne %fs rex.w mov 8(%ebx,%ecx,4), %eax
mov $1+2*3-4/2%2|8&7^~1<<2>>1, %edx
jmp 0f
mov target, %ecx
0: nop
mov 0b, %eax
target:
mov $target+4, %ebx
.intel_syntax noprefix
mov eax, dword ptr [ebx + 8]
mov edx, [ebx + esi*4 + 32]
mov ecx, gs:[ebx + 4]
.att_syntax prefix
mov %gs:4(%ebx), %ecx
SRC

cat > "$TMP/arm.s" <<'SRC'
addeq r0, r1, r2, lsl r3
stmia r4, {r1, r2, r3}
mrc p15, 0, r0, c1, c0, 0
bne label
label: nop
SRC

cc -Wall -Wextra -Werror -D_GNU_SOURCE -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_lexer.c" "$ROOT/usr.bin/as/as_parser.c" \
   "$ROOT/tests/usr.bin/as/test_parser_core.c" \
   -o "$TMP/test_parser_core"

"$TMP/test_parser_core" "$TMP/x86.s" "$TMP/arm.s"
echo "ok: parser core"
