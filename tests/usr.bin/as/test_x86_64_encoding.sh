#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-x64-enc-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/x64_valid.s" <<'SRC'
.text
.globl x64_entry
.type x64_entry,@function
x64_entry:
    push %rbp
    mov %rsp, %rbp
    movabs $0x1122334455667788, %rax
    mov %r8, %r9
    lea local_data(%rip), %rax
    mov ext_data@GOTPCREL(%rip), %rax
    mov tls_var@GOTTPOFF(%rip), %rax
    movdqa %xmm0, %xmm1
    cvtsi2sd %rax, %xmm0
    call ext_func@PLT
    call *%r11
    ud2
    leave
    ret
.size x64_entry, .-x64_entry

.data
local_data:
    .quad 0
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

cat > "$TMP/avx512_only.s" <<'SRC'
.text
.globl avx512_only
.type avx512_only,@function
avx512_only:
    vrcp14ps %xmm0, %xmm1
    ret
.size avx512_only, .-avx512_only
SRC

cat > "$TMP/forbidden64.s" <<'SRC'
.text
bad64:
    pushal
SRC

cat > "$TMP/special64.s" <<'SRC'
.text
.globl x64_special
.type x64_special,@function
x64_special:
    movabs $0x1122334455667788, %rax
    mov %es, %eax
    mov %eax, %es
    mov (%rax), %es
    mov %es, (%rax)
    jrcxz .Ldone
    rex.W
    nop
.Ldone:
    ret
.size x64_special, .-x64_special
SRC

"$AS" -64 -march=x86-64-v3 -o "$TMP/x64_valid_a.o" "$TMP/x64_valid.s"
"$AS" -64 -march=x86-64-v3 -o "$TMP/x64_valid_b.o" "$TMP/x64_valid.s"
cmp "$TMP/x64_valid_a.o" "$TMP/x64_valid_b.o"

"$AS" -64 -o "$TMP/special64.o" "$TMP/special64.s"
objcopy -O binary --only-section=.text "$TMP/special64.o" "$TMP/special64.text"
special_actual=$(od -An -tx1 -v "$TMP/special64.text" | tr -s ' \n' ' ' | sed 's/^ //; s/ $//')
special_expected="48 b8 88 77 66 55 44 33 22 11 8c c0 8e c0 8e 00 8c 00 e3 02 48 90 c3"
test "$special_actual" = "$special_expected"
readelf --wide -r "$TMP/special64.o" | grep -q "There are no relocations in this file."

objdump -dr "$TMP/x64_valid_a.o" | grep -q "movabs"
objdump -dr "$TMP/x64_valid_a.o" | grep -Eq "r8|r9"
objdump -dr "$TMP/x64_valid_a.o" | grep -q "(%rip)"
objdump -dr "$TMP/x64_valid_a.o" | grep -q "ud2"
objdump -dr "$TMP/x64_valid_a.o" | grep -Eq "callq?[[:space:]]+\\*%r11"

readelf --wide -r "$TMP/x64_valid_a.o" | grep -Eq "R_X86_64_(PLT32|PC32)"
readelf --wide -r "$TMP/x64_valid_a.o" | grep -Eq "R_X86_64_(GOTPCREL|GOTPCRELX|REX_GOTPCRELX)"
readelf --wide -r "$TMP/x64_valid_a.o" | grep -Eq "R_X86_64_(GOTTPOFF|TLSGD|TLSLD|TPOFF32)"
readelf --wide -s "$TMP/x64_valid_a.o" | grep -Eq "GLOBAL[[:space:]]+DEFAULT[[:space:]]+UND[[:space:]]+ext_func"

if "$AS" -64 -march=x86-64-v2 -o "$TMP/avx2_bad.o" "$TMP/avx2_only.s" >"$TMP/avx_bad.out" 2>"$TMP/avx_bad.err"; then
    echo "expected AVX2 with -march=x86-64-v2 to fail"
    exit 1
fi
grep -qi "error" "$TMP/avx_bad.err"

"$AS" -64 -march=x86-64-v3 -o "$TMP/avx2_ok.o" "$TMP/avx2_only.s"

if "$AS" -64 -march=x86-64-v3 -o "$TMP/avx512_bad.o" "$TMP/avx512_only.s" >"$TMP/avx512_bad.out" 2>"$TMP/avx512_bad.err"; then
    echo "expected AVX-512 with -march=x86-64-v3 to fail"
    exit 1
fi
grep -qi "error" "$TMP/avx512_bad.err"

"$AS" -64 -march=x86-64-v4 -o "$TMP/avx512_ok.o" "$TMP/avx512_only.s"

if "$AS" -64 -o "$TMP/forbidden64.o" "$TMP/forbidden64.s" >"$TMP/forbid.out" 2>"$TMP/forbid.err"; then
    echo "expected forbidden 64-bit instruction to fail"
    exit 1
fi
grep -qi "error" "$TMP/forbid.err"

echo "ok: x86_64 baseline encoding"
