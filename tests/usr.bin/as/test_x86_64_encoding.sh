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

cat > "$TMP/forbidden64.s" <<'SRC'
.text
bad64:
    pushal
SRC

"$AS" -64 -march=x86-64-v3 -o "$TMP/x64_valid_a.o" "$TMP/x64_valid.s"
"$AS" -64 -march=x86-64-v3 -o "$TMP/x64_valid_b.o" "$TMP/x64_valid.s"
cmp "$TMP/x64_valid_a.o" "$TMP/x64_valid_b.o"

objdump -dr "$TMP/x64_valid_a.o" | grep -q "movabs"
objdump -dr "$TMP/x64_valid_a.o" | grep -Eq "r8|r9"
objdump -dr "$TMP/x64_valid_a.o" | grep -q "(%rip)"

readelf --wide -r "$TMP/x64_valid_a.o" | grep -Eq "R_X86_64_(PLT32|PC32)"
readelf --wide -r "$TMP/x64_valid_a.o" | grep -Eq "R_X86_64_(GOTPCREL|GOTPCRELX|REX_GOTPCRELX)"
readelf --wide -r "$TMP/x64_valid_a.o" | grep -Eq "R_X86_64_(GOTTPOFF|TLSGD|TLSLD|TPOFF32)"

if "$AS" -64 -march=x86-64-v2 -o "$TMP/avx2_bad.o" "$TMP/avx2_only.s" >"$TMP/avx_bad.out" 2>"$TMP/avx_bad.err"; then
    echo "expected AVX2 with -march=x86-64-v2 to fail"
    exit 1
fi
grep -qi "error" "$TMP/avx_bad.err"

"$AS" -64 -march=x86-64-v3 -o "$TMP/avx2_ok.o" "$TMP/avx2_only.s"

if "$AS" -64 -o "$TMP/forbidden64.o" "$TMP/forbidden64.s" >"$TMP/forbid.out" 2>"$TMP/forbid.err"; then
    echo "expected forbidden 64-bit instruction to fail"
    exit 1
fi
grep -qi "error" "$TMP/forbid.err"

echo "ok: x86_64 baseline encoding"
