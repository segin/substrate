#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-dir-surface-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/surface.s" <<'SRC'
.text
.globl entry
.type entry,@function
entry:
    ret
.size entry, .-entry

.section .alpha,"aw",@progbits
alpha0:
    .byte 1

.section .beta,"aw",@progbits
beta0:
    .byte 2

.previous
alpha1:
    .byte 3

.pushsection .gamma,"aw",@progbits
gamma0:
    .byte 4
.popsection

.section .orgsec,"aw",@progbits
org0:
    .byte 9
    .org 16
org1:
    .byte 10

.section .alignsec,"aw",@progbits
al0:
    .byte 0
.balign 16
al1:
    .byte 1
.p2align 5
al2:
    .byte 2
SRC

sym_hex() {
    readelf --wide -s "$1" | awk -v sym="$2" '$8 == sym { print $2; exit }'
}

check_obj() {
    obj="$1"

    readelf -S "$obj" | grep -q "\\.alpha"
    readelf -S "$obj" | grep -q "\\.beta"
    readelf -S "$obj" | grep -q "\\.gamma"
    readelf -S "$obj" | grep -q "\\.orgsec"
    readelf -S "$obj" | grep -q "\\.alignsec"

    readelf --wide -s "$obj" | awk '$8 == "entry" && $4 == "FUNC" { ok = 1 } END { exit ok ? 0 : 1 }'

    objdump -s -j .alpha "$obj" | grep -Eiq "0103"
    objdump -s -j .beta "$obj" | grep -Eiq "02"
    objdump -s -j .gamma "$obj" | grep -Eiq "04"

    org0_hex=$(sym_hex "$obj" org0)
    org1_hex=$(sym_hex "$obj" org1)
    al1_hex=$(sym_hex "$obj" al1)
    al2_hex=$(sym_hex "$obj" al2)

    [ -n "$org0_hex" ] && [ -n "$org1_hex" ] && [ -n "$al1_hex" ] && [ -n "$al2_hex" ]

    org0=$((16#$org0_hex))
    org1=$((16#$org1_hex))
    al1=$((16#$al1_hex))
    al2=$((16#$al2_hex))

    [ $((org1 - org0)) -eq 16 ]
    [ $((al1 % 16)) -eq 0 ]
    [ $((al2 % 32)) -eq 0 ]
}

"$AS" -32 -o "$TMP/surface32.o" "$TMP/surface.s"
"$AS" -64 -o "$TMP/surface64.o" "$TMP/surface.s"
check_obj "$TMP/surface32.o"
check_obj "$TMP/surface64.o"

cat > "$TMP/if_bad.s" <<'SRC'
.if 1
.text
x:
    nop
.endif
SRC
if "$AS" -32 -o "$TMP/if_bad.o" "$TMP/if_bad.s" >"$TMP/if_bad.out" 2>"$TMP/if_bad.err"; then
    echo "expected unsupported .if directive failure"
    exit 1
fi
grep -q "unsupported directive .if" "$TMP/if_bad.err"

cat > "$TMP/macro_bad.s" <<'SRC'
.macro M
.endm
.text
x:
    nop
SRC
if "$AS" -32 -o "$TMP/macro_bad.o" "$TMP/macro_bad.s" >"$TMP/macro_bad.out" 2>"$TMP/macro_bad.err"; then
    echo "expected unsupported .macro directive failure"
    exit 1
fi
grep -q "unsupported directive .macro" "$TMP/macro_bad.err"

echo "ok: directive surface 32/64"
