#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-i386-enc-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/i386_valid.s" <<'SRC'
.text
.globl i386_entry
.type i386_entry,@function
i386_entry:
    push %ebp
    mov %esp, %ebp
    sub $32, %esp

    mov $5, %eax
    add $3, %eax
    sub $1, %eax
    imul $7, %eax, %edx
    and $0xff, %eax
    or $0x10, %eax
    xor %edx, %eax
    cmp $0, %eax
    je .Lzero

    movsx %al, %ecx
    movzx %al, %edx
    shl $2, %eax
    sar $1, %eax
    ror $1, %eax
    bt $3, %eax
    bts $1, %eax

    fld1
    fstp %st(0)

    pxor %xmm0, %xmm0
    movdqa %xmm0, %xmm1
    cvtsi2sd %eax, %xmm0

    mov 8(%ebx,%ecx,4), %eax
    mov abs_label, %edx
    mov (%eax), %ecx
    mov %gs:0, %eax

    lock addl $1, counter
    rep movsb

    call helper
    jmp .Ldone
.Lzero:
    mov $0, %eax
.Ldone:
    leave
    ret
.size i386_entry, .-i386_entry

.type helper,@function
helper:
    ret
.size helper, .-helper

.data
counter:
    .long 0
abs_label:
    .long 0x11223344
SRC

cat > "$TMP/i386_invalid.s" <<'SRC'
.text
bad_i386:
    add %eax, %xmm0
SRC

"$AS" -32 -march=pentium4 -o "$TMP/i386_valid.o" "$TMP/i386_valid.s"
objdump -dr "$TMP/i386_valid.o" | grep -q "lock"
objdump -dr "$TMP/i386_valid.o" | grep -q "rep"
objdump -dr "$TMP/i386_valid.o" | grep -q "fld1"
objdump -dr "$TMP/i386_valid.o" | grep -q "movdqa"
objdump -dr "$TMP/i386_valid.o" | grep -q "cvtsi2sd"

if "$AS" -32 -o "$TMP/i386_invalid.o" "$TMP/i386_invalid.s" >"$TMP/invalid.out" 2>"$TMP/invalid.err"; then
    echo "expected invalid operand form to fail"
    exit 1
fi
grep -Eq "i386_invalid\.s:3|i386_invalid\.s" "$TMP/invalid.err"
grep -qi "error" "$TMP/invalid.err"

echo "ok: i386 baseline encoding"
