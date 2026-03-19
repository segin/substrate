#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-intel-dual-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/dual_att.s" <<'SRC'
.text
.globl dual
.type dual,@function
dual:
    mov 8(%rax), %ecx
    mov 8(%rax,%rbx,4), %edx
    add $5, %edx
    ret
.size dual, .-dual
SRC

cat > "$TMP/dual_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl dual
.type dual,@function
dual:
    mov ecx, [rax + 8]
    mov edx, [rax + rbx*4 + 8]
    add edx, 5
    ret
.size dual, .-dual
.att_syntax prefix
SRC

"$AS" -64 -o "$TMP/dual_att.o" "$TMP/dual_att.s"
"$AS" -64 -o "$TMP/dual_intel.o" "$TMP/dual_intel.s"
objcopy -O binary --only-section=.text "$TMP/dual_att.o" "$TMP/dual_att.text"
objcopy -O binary --only-section=.text "$TMP/dual_intel.o" "$TMP/dual_intel.text"
cmp "$TMP/dual_att.text" "$TMP/dual_intel.text"

cat > "$TMP/mem_perm_att.s" <<'SRC'
.text
.globl mem_perm
.type mem_perm,@function
mem_perm:
    lea (%ebx), %eax
    lea 16(%ebx), %ecx
    lea 32(%ebx,%esi,4), %edx
    ret
.size mem_perm, .-mem_perm
SRC

cat > "$TMP/mem_perm_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl mem_perm
.type mem_perm,@function
mem_perm:
    lea eax, [ebx]
    lea ecx, [ebx + 16]
    lea edx, [ebx + esi*4 + 32]
    ret
.size mem_perm, .-mem_perm
.att_syntax prefix
SRC

"$AS" -32 -o "$TMP/mem_perm_att.o" "$TMP/mem_perm_att.s"
"$AS" -32 -o "$TMP/mem_perm_intel.o" "$TMP/mem_perm_intel.s"
objcopy -O binary --only-section=.text "$TMP/mem_perm_att.o" "$TMP/mem_perm_att.text"
objcopy -O binary --only-section=.text "$TMP/mem_perm_intel.o" "$TMP/mem_perm_intel.text"
cmp "$TMP/mem_perm_att.text" "$TMP/mem_perm_intel.text"

cat > "$TMP/seg_att.s" <<'SRC'
.text
.globl seg_mem
.type seg_mem,@function
seg_mem:
    mov %gs:4(%ebx), %ecx
    ret
.size seg_mem, .-seg_mem
SRC

cat > "$TMP/seg_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl seg_mem
.type seg_mem,@function
seg_mem:
    mov ecx, gs:[ebx + 4]
    ret
.size seg_mem, .-seg_mem
.att_syntax prefix
SRC

"$AS" -32 -o "$TMP/seg_att.o" "$TMP/seg_att.s"
"$AS" -32 -o "$TMP/seg_intel.o" "$TMP/seg_intel.s"
objcopy -O binary --only-section=.text "$TMP/seg_att.o" "$TMP/seg_att.text"
objcopy -O binary --only-section=.text "$TMP/seg_intel.o" "$TMP/seg_intel.text"
cmp "$TMP/seg_att.text" "$TMP/seg_intel.text"

cat > "$TMP/width_att.s" <<'SRC'
.text
.globl width_case
.type width_case,@function
width_case:
    movb (%ebx), %al
    movw (%ebx), %ax
    movl (%ebx), %eax
    addb %al, (%ebx)
    addw %ax, (%ebx)
    addl %eax, (%ebx)
    ret
.size width_case, .-width_case
SRC

cat > "$TMP/width_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl width_case
.type width_case,@function
width_case:
    mov al, byte ptr [ebx]
    mov ax, word ptr [ebx]
    mov eax, dword ptr [ebx]
    add byte ptr [ebx], al
    add word ptr [ebx], ax
    add dword ptr [ebx], eax
    ret
.size width_case, .-width_case
.att_syntax prefix
SRC

"$AS" -32 -o "$TMP/width_att.o" "$TMP/width_att.s"
"$AS" -32 -o "$TMP/width_intel.o" "$TMP/width_intel.s"
objcopy -O binary --only-section=.text "$TMP/width_att.o" "$TMP/width_att.text"
objcopy -O binary --only-section=.text "$TMP/width_intel.o" "$TMP/width_intel.text"
cmp "$TMP/width_att.text" "$TMP/width_intel.text"

cat > "$TMP/qual32_att.s" <<'SRC'
.text
.globl qual32
.type qual32,@function
qual32:
    movq (%ebx), %mm0
    sgdt (%eax)
    fldt (%ebx)
    ret
.size qual32, .-qual32
SRC

cat > "$TMP/qual32_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl qual32
.type qual32,@function
qual32:
    movq mm0, mmword ptr [ebx]
    sgdt fword ptr [eax]
    fld tbyte ptr [ebx]
    ret
.size qual32, .-qual32
.att_syntax prefix
SRC

"$AS" -32 -o "$TMP/qual32_att.o" "$TMP/qual32_att.s"
"$AS" -32 -o "$TMP/qual32_intel.o" "$TMP/qual32_intel.s"
objcopy -O binary --only-section=.text "$TMP/qual32_att.o" "$TMP/qual32_att.text"
objcopy -O binary --only-section=.text "$TMP/qual32_intel.o" "$TMP/qual32_intel.text"
cmp "$TMP/qual32_att.text" "$TMP/qual32_intel.text"

cat > "$TMP/x87stack_att.s" <<'SRC'
.text
.globl x87stack32
.type x87stack32,@function
x87stack32:
    fld %st(1)
    fxch %st(2)
    fadd %st(1), %st
    fucomi %st(1), %st
    fcomi %st(1), %st
    fucomip %st(1), %st
    ret
.size x87stack32, .-x87stack32
SRC

cat > "$TMP/x87stack_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl x87stack32
.type x87stack32,@function
x87stack32:
    fld st(1)
    fxch st(2)
    fadd st, st(1)
    fucomi st, st(1)
    fcomi st, st(1)
    fucomip st, st(1)
    ret
.size x87stack32, .-x87stack32
.att_syntax prefix
SRC

"$AS" -32 -o "$TMP/x87stack_att.o" "$TMP/x87stack_att.s"
"$AS" -32 -o "$TMP/x87stack_intel.o" "$TMP/x87stack_intel.s"
objcopy -O binary --only-section=.text "$TMP/x87stack_att.o" "$TMP/x87stack_att.text"
objcopy -O binary --only-section=.text "$TMP/x87stack_intel.o" "$TMP/x87stack_intel.text"
cmp "$TMP/x87stack_att.text" "$TMP/x87stack_intel.text"

cat > "$TMP/mmxbridge_att.s" <<'SRC'
.text
.globl mmxbridge32
.type mmxbridge32,@function
mmxbridge32:
    movdq2q %xmm0, %mm1
    movq2dq %mm0, %xmm1
    ret
.size mmxbridge32, .-mmxbridge32
SRC

cat > "$TMP/mmxbridge_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl mmxbridge32
.type mmxbridge32,@function
mmxbridge32:
    movdq2q mm1, xmm0
    movq2dq xmm1, mm0
    ret
.size mmxbridge32, .-mmxbridge32
.att_syntax prefix
SRC

"$AS" -32 -o "$TMP/mmxbridge_att.o" "$TMP/mmxbridge_att.s"
"$AS" -32 -o "$TMP/mmxbridge_intel.o" "$TMP/mmxbridge_intel.s"
objcopy -O binary --only-section=.text "$TMP/mmxbridge_att.o" "$TMP/mmxbridge_att.text"
objcopy -O binary --only-section=.text "$TMP/mmxbridge_intel.o" "$TMP/mmxbridge_intel.text"
cmp "$TMP/mmxbridge_att.text" "$TMP/mmxbridge_intel.text"

cat > "$TMP/order32_att.s" <<'SRC'
.text
.globl order32
.type order32,@function
order32:
    movbe (%ebx), %eax
    movbe %eax, (%ebx)
    in (%dx), %al
    in (%dx), %eax
    out %al, (%dx)
    out %eax, (%dx)
    vmread %ebx, %eax
    vmwrite %ebx, %eax
    movdir64b (%ebx), %eax
    enqcmd (%eax), %ebx
    enqcmds (%eax), %ebx
    ret
.size order32, .-order32
SRC

cat > "$TMP/order32_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl order32
.type order32,@function
order32:
    movbe eax, [ebx]
    movbe [ebx], eax
    in al, dx
    in eax, dx
    out dx, al
    out dx, eax
    vmread eax, ebx
    vmwrite eax, ebx
    movdir64b eax, [ebx]
    enqcmd ebx, [eax]
    enqcmds ebx, [eax]
    ret
.size order32, .-order32
.att_syntax prefix
SRC

"$AS" -32 -o "$TMP/order32_att.o" "$TMP/order32_att.s"
"$AS" -32 -o "$TMP/order32_intel.o" "$TMP/order32_intel.s"
objcopy -O binary --only-section=.text "$TMP/order32_att.o" "$TMP/order32_att.text"
objcopy -O binary --only-section=.text "$TMP/order32_intel.o" "$TMP/order32_intel.text"
cmp "$TMP/order32_att.text" "$TMP/order32_intel.text"

cat > "$TMP/qual64_att.s" <<'SRC'
.text
.globl qual64
.type qual64,@function
qual64:
    movq (%rbx), %rax
    movdqa (%rax), %xmm0
    movdqa (%rax), %xmm1
    vpaddd (%rax), %ymm1, %ymm0
    vaddps (%rax), %zmm1, %zmm0
    ret
.size qual64, .-qual64
SRC

cat > "$TMP/qual64_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl qual64
.type qual64,@function
qual64:
    mov rax, qword ptr [rbx]
    movdqa xmm0, xmmword ptr [rax]
    movdqa xmm1, oword ptr [rax]
    vpaddd ymm0, ymm1, ymmword ptr [rax]
    vaddps zmm0, zmm1, zmmword ptr [rax]
    ret
.size qual64, .-qual64
.att_syntax prefix
SRC

"$AS" -64 -march=x86-64-v4 -o "$TMP/qual64_att.o" "$TMP/qual64_att.s"
"$AS" -64 -march=x86-64-v4 -o "$TMP/qual64_intel.o" "$TMP/qual64_intel.s"
objcopy -O binary --only-section=.text "$TMP/qual64_att.o" "$TMP/qual64_att.text"
objcopy -O binary --only-section=.text "$TMP/qual64_intel.o" "$TMP/qual64_intel.text"
cmp "$TMP/qual64_att.text" "$TMP/qual64_intel.text"

cat > "$TMP/forms_att.s" <<'SRC'
.text
.globl forms64
.type forms64,@function
forms64:
    movabs $0x1122334455667788, %rax
    ret
.size forms64, .-forms64
SRC

cat > "$TMP/forms_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl forms64
.type forms64,@function
forms64:
    movabs rax, 0x1122334455667788
    ret
.size forms64, .-forms64
.att_syntax prefix
SRC

"$AS" -64 -o "$TMP/forms_att.o" "$TMP/forms_att.s"
"$AS" -64 -o "$TMP/forms_intel.o" "$TMP/forms_intel.s"
objcopy -O binary --only-section=.text "$TMP/forms_att.o" "$TMP/forms_att.text"
objcopy -O binary --only-section=.text "$TMP/forms_intel.o" "$TMP/forms_intel.text"
cmp "$TMP/forms_att.text" "$TMP/forms_intel.text"

cat > "$TMP/str_att.s" <<'SRC'
.text
.globl forms32
.type forms32,@function
forms32:
    movsb
    movsd
    lodsb
    lodsd
    stosb
    stosd
    ret
.size forms32, .-forms32
SRC

cat > "$TMP/str_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl forms32
.type forms32,@function
forms32:
    movsb
    movsd
    lodsb
    lodsd
    stosb
    stosd
    ret
.size forms32, .-forms32
.att_syntax prefix
SRC

"$AS" -32 -o "$TMP/str_att.o" "$TMP/str_att.s"
"$AS" -32 -o "$TMP/str_intel.o" "$TMP/str_intel.s"
objcopy -O binary --only-section=.text "$TMP/str_att.o" "$TMP/str_att.text"
objcopy -O binary --only-section=.text "$TMP/str_intel.o" "$TMP/str_intel.text"
cmp "$TMP/str_att.text" "$TMP/str_intel.text"

cat > "$TMP/ambig_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl ambig
.type ambig,@function
ambig:
    mov [rax], 1
    ret
.size ambig, .-ambig
.att_syntax prefix
SRC

if "$AS" -64 -o "$TMP/ambig_intel.o" "$TMP/ambig_intel.s" >"$TMP/ambig.out" 2>"$TMP/ambig.err"; then
    echo "expected ambiguous Intel form to fail"
    exit 1
fi
grep -Eqi "unsupported|malformed|error" "$TMP/ambig.err"

cat > "$TMP/ambig_add_intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl ambig_add
.type ambig_add,@function
ambig_add:
    add [ebx], 1
    ret
.size ambig_add, .-ambig_add
.att_syntax prefix
SRC

if "$AS" -32 -o "$TMP/ambig_add_intel.o" "$TMP/ambig_add_intel.s" >"$TMP/ambig_add.out" 2>"$TMP/ambig_add.err"; then
    echo "expected ambiguous Intel add form to fail"
    exit 1
fi
grep -Eqi "unsupported|malformed|error" "$TMP/ambig_add.err"

cat > "$TMP/unsupported_qual.s" <<'SRC'
.intel_syntax noprefix
.text
bad_qual:
    mov eax, octaword ptr [ebx]
SRC
if "$AS" -32 -o "$TMP/unsupported_qual.o" "$TMP/unsupported_qual.s" >"$TMP/uq.out" 2>"$TMP/uq.err"; then
    echo "expected unsupported Intel qualifier failure"
    exit 1
fi
grep -Eqi "unsupported Intel size qualifier" "$TMP/uq.err"

cat > "$TMP/malformed_qual.s" <<'SRC'
.intel_syntax noprefix
.text
bad_mq:
    mov eax, ptr [ebx]
SRC
if "$AS" -32 -o "$TMP/malformed_qual.o" "$TMP/malformed_qual.s" >"$TMP/mq.out" 2>"$TMP/mq.err"; then
    echo "expected malformed Intel qualifier failure"
    exit 1
fi
grep -Eqi "malformed Intel size qualifier" "$TMP/mq.err"

echo "ok: intel dual-syntax and memory-addressing compatibility"
