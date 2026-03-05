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
    movw (%ebx), %ax
    movl (%ebx), %eax
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
    mov ax, word ptr [ebx]
    mov eax, dword ptr [ebx]
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
