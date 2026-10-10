#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-src-model-$$
mkdir -p "$TMP/inc"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/model_att.s" <<'SRC'
.data
vals:
    .byte 10, 012, 0x0a, 0b1010
msg:
    .ascii "A\\nB\\tC\\x44"

.text
.globl model_att
.type model_att,@function
.macro SETRET imm
    mov $\imm, %eax
.endm

model_att:
1:
    nop
    jmp 1b
2:
    SETRET 42
    .if 1
        nop
    .else
        ud2
    .endif
    jmp 3f
3:
    ret
.size model_att, .-model_att
SRC

cat > "$TMP/model_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl model_intel
.type model_intel,@function
model_intel:
    mov eax, 3
    jmp 1f
1:
    ret
.size model_intel, .-model_intel
.att_syntax prefix
SRC

cat > "$TMP/inc/l3.inc" <<'SRC'
.equ INC_VALUE, 9
SRC
cat > "$TMP/inc/l2.inc" <<'SRC'
.include "l3.inc"
SRC
cat > "$TMP/inc/l1.inc" <<'SRC'
.include "l2.inc"
SRC
cat > "$TMP/include_ok.s" <<'SRC'
.text
.globl include_ok
.type include_ok,@function
.include "l1.inc"
include_ok:
    mov $INC_VALUE, %eax
    ret
.size include_ok, .-include_ok
SRC

cat > "$TMP/inc/cycle_a.inc" <<'SRC'
.include "cycle_b.inc"
SRC
cat > "$TMP/inc/cycle_b.inc" <<'SRC'
.include "cycle_a.inc"
SRC
cat > "$TMP/include_cycle.s" <<'SRC'
.text
.globl include_cycle
.include "cycle_a.inc"
include_cycle:
    ret
SRC

cat > "$TMP/invalid_multi.s" <<'SRC'
.text
bad_multi:
    fooop %eax, %ebx
    mov %badreg, %eax
    barrr
SRC

"$AS" -32 -o "$TMP/model_att32.o" "$TMP/model_att.s"
"$AS" -64 -o "$TMP/model_att64.o" "$TMP/model_att.s"
"$AS" -32 -o "$TMP/model_intel32.o" "$TMP/model_intel.s"
"$AS" -64 -o "$TMP/model_intel64.o" "$TMP/model_intel.s"
"$AS" -32 -I "$TMP/inc" -o "$TMP/include_ok32.o" "$TMP/include_ok.s"
"$AS" -64 -I "$TMP/inc" -o "$TMP/include_ok64.o" "$TMP/include_ok.s"

if "$AS" -32 -I "$TMP/inc" -o "$TMP/include_cycle.o" "$TMP/include_cycle.s" >"$TMP/cycle.out" 2>"$TMP/cycle.err"; then
    echo "expected include cycle to fail"
    exit 1
fi

if "$AS" -32 -o "$TMP/invalid_multi.o" "$TMP/invalid_multi.s" >"$TMP/invalid.out" 2>"$TMP/invalid.err"; then
    echo "expected invalid source to fail"
    exit 1
fi

ERRS=$(grep -c "[Ee]rror" "$TMP/invalid.err" || true)
[ "$ERRS" -ge 2 ]

echo "ok: source model compatibility (AT&T/Intel/macros/conditionals/includes)"
