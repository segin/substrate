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
    mov 8(%rbx), %edx
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
    mov edx, [rbx + 8]
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
    lea 32(%esi), %edx
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
    lea edx, [esi + 32]
    ret
.size mem_perm, .-mem_perm
.att_syntax prefix
SRC

"$AS" -32 -o "$TMP/mem_perm_att.o" "$TMP/mem_perm_att.s"
"$AS" -32 -o "$TMP/mem_perm_intel.o" "$TMP/mem_perm_intel.s"
objcopy -O binary --only-section=.text "$TMP/mem_perm_att.o" "$TMP/mem_perm_att.text"
objcopy -O binary --only-section=.text "$TMP/mem_perm_intel.o" "$TMP/mem_perm_intel.text"
cmp "$TMP/mem_perm_att.text" "$TMP/mem_perm_intel.text"

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

echo "ok: intel dual-syntax and basic Intel memory-addressing compatibility"
