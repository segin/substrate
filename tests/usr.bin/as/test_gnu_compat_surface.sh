#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
LD="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/as-compat-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/compat64.s" <<'SRC'
.text
.globl compat64
.type compat64,@function
compat64:
    .cfi_startproc
1:
    nop
    jne 1b
    jmp 2f
2:
    ret
    .cfi_endproc
.size compat64, .-compat64

.section .rodata.str1.1,"aMS",@progbits,1
msg64:
    .asciz "compat64"

.intel_syntax noprefix
.section .text.intel64,"ax",@progbits
.globl intel64
.type intel64,@function
intel64:
    mov eax, 7
    ret
.size intel64, .-intel64
.att_syntax prefix
SRC

cat > "$TMP/compat32.s" <<'SRC'
.text
.globl compat32
.type compat32,@function
compat32:
    .cfi_startproc
1:
    nop
    jne 1b
    jmp 2f
2:
    ret
    .cfi_endproc
.size compat32, .-compat32

.section .rodata.str1.1,"aMS",@progbits,1
msg32:
    .asciz "compat32"
SRC

cat > "$TMP/inline_asm.c" <<'SRC'
int g;
int compat_inline(int x) {
    asm volatile("addl $3, %0" : "+r"(x) :: "cc");
    asm volatile("movl %1, %0" : "=r"(g) : "r"(x) : "memory");
    return x;
}
SRC

cat > "$TMP/avx2_only.s" <<'SRC'
.text
.globl avx2_only
.type avx2_only,@function
avx2_only:
    vpbroadcastd %xmm0, %ymm1
    ret
.size avx2_only, .-avx2_only
SRC

cat > "$TMP/multi_a.s" <<'SRC'
.text
.globl a
.type a,@function
a:
    ret
.size a, .-a
SRC

cat > "$TMP/multi_b.s" <<'SRC'
.text
.globl b
.type b,@function
b:
    ret
.size b, .-b
SRC

# Core compatibility sources should assemble in both modes.
"$AS" -64 -g -o "$TMP/compat64.o" "$TMP/compat64.s"
"$AS" -32 -g -o "$TMP/compat32.o" "$TMP/compat32.s"

# Compare wrapper output against direct backend on a representative input.
gcc -c -x assembler-with-cpp -m64 -g -o "$TMP/compat64.ref.o" "$TMP/compat64.s"
cmp "$TMP/compat64.o" "$TMP/compat64.ref.o"

# Compiler-emitted .cfi/.section and inline-asm patterns.
gcc -m64 -O2 -fno-asynchronous-unwind-tables -S -o "$TMP/inline64.s" "$TMP/inline_asm.c"
gcc -m32 -O2 -fno-asynchronous-unwind-tables -S -o "$TMP/inline32.s" "$TMP/inline_asm.c"
"$AS" -64 -o "$TMP/inline64.o" "$TMP/inline64.s"
"$AS" -32 -o "$TMP/inline32.o" "$TMP/inline32.s"

# Driver output remains consumable by ld/objdump.
"$LD" -m64 -r -o "$TMP/compat64.r.o" "$TMP/compat64.o" "$TMP/inline64.o"
"$LD" -m32 -r -o "$TMP/compat32.r.o" "$TMP/compat32.o" "$TMP/inline32.o"
objdump -dr "$TMP/compat64.o" >/dev/null
objdump -dr "$TMP/compat32.o" >/dev/null

# Feature-guarded compatibility matrix by -march.
if "$AS" -64 -march=x86-64-v2 -o "$TMP/avx2_bad.o" "$TMP/avx2_only.s" >"$TMP/march_bad.out" 2>"$TMP/march_bad.err"; then
    echo "expected AVX2 with -march=x86-64-v2 to fail"
    exit 1
fi
grep -qi "error" "$TMP/march_bad.err"
"$AS" -64 -march=x86-64-v3 -o "$TMP/avx2_ok.o" "$TMP/avx2_only.s"

# Intentional incompatibility: multiple input files are rejected.
if "$AS" -64 -o "$TMP/multi.o" "$TMP/multi_a.s" "$TMP/multi_b.s" >"$TMP/multi.out" 2>"$TMP/multi.err"; then
    echo "expected multi-input invocation to fail"
    exit 1
fi
grep -q "multiple input files are not supported" "$TMP/multi.err"

echo "ok: GNU/toolchain compatibility surface"
