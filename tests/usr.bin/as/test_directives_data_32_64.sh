#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-dir-data-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/directives.s" <<'SRC'
.file 1 "directives.s"

.data
data_start:
    .byte 0x12, 0x34
    .short 0x1234
    .long 0x12345678
    .quad 0x1122334455667788
ascii_blob:
    .ascii "AB"
asciz_blob:
    .asciz "CD"
string_blob:
    .string "EF"
fill_blob:
    .space 3,0x41
    .fill 2,1,0x42
    .zero 4

.section .orgsec,"aw",@progbits
org_a:
    .byte 1
    .org 16
org_b:
    .byte 2

.section .note.subs,"a",@note
    .align 4
    .long 5
    .long 4
    .long 1
    .asciz "SUBS"
    .align 4
    .long 0x1234

.section .tdata,"awT",@progbits
tls_init:
    .long 7

.section .tbss,"awT",@nobits
tls_zero:
    .zero 8

.section .text.dirfunc,"ax",@progbits
.globl dir_fn
.type dir_fn,@function
dir_fn:
    .cfi_startproc
    .loc 1 42 0
    nop
    ret
    .cfi_endproc
.size dir_fn, .-dir_fn

.section .text.comdat_fn,"axG",@progbits,comdat_fn,comdat
.globl comdat_fn
.type comdat_fn,@function
comdat_fn:
    ret
.size comdat_fn, .-comdat_fn
SRC

sym_hex() {
    readelf --wide -s "$1" | awk -v sym="$2" '$8 == sym { print $2; exit }'
}

check_obj() {
    obj="$1"

    readelf -S "$obj" | grep -q "\\.data"
    readelf -S "$obj" | grep -q "\\.orgsec"
    readelf -S "$obj" | grep -q "\\.tdata"
    readelf -S "$obj" | grep -q "\\.tbss"
    readelf -S "$obj" | grep -q "\\.note\.subs"
    readelf -S "$obj" | grep -q "\\.group"
    readelf -S "$obj" | grep -q "\\.eh_frame"

    readelf --wide -S "$obj" | grep -Eq "\\.note\\.subs.*NOTE"
    readelf --wide -S "$obj" | grep -Eq "\\.tdata.*WAT"
    readelf --wide -S "$obj" | grep -Eq "\\.tbss.*NOBITS"

    # data directives, strings, and fill/zero patterns in little-endian
    objdump -s -j .data "$obj" | grep -Eiq "12343412"
    objdump -s -j .data "$obj" | grep -Eiq "78563412"
    objdump -s -j .data "$obj" | grep -Eiq "88776655[[:space:]]+44332211"
    objdump -s -j .data "$obj" | grep -Eiq "41424344"
    objdump -s -j .data "$obj" | grep -Eiq "00454600"
    objdump -s -j .data "$obj" | grep -Eiq "41414142"

    # type/size metadata for functions
    readelf --wide -s "$obj" | awk '$8 == "dir_fn" && $4 == "FUNC" && strtonum("0x"$3) > 0 { ok = 1 } END { exit ok ? 0 : 1 }'
    readelf --wide -s "$obj" | awk '$8 == "comdat_fn" && $4 == "FUNC" && strtonum("0x"$3) > 0 { ok = 1 } END { exit ok ? 0 : 1 }'

    # .org should place org_b at offset 16 relative to section start
    a_hex=$(sym_hex "$obj" org_a)
    b_hex=$(sym_hex "$obj" org_b)
    [ -n "$a_hex" ] && [ -n "$b_hex" ]
    a=$((16#$a_hex))
    b=$((16#$b_hex))
    [ $((b - a)) -eq 16 ]
}

"$AS" -32 -g -o "$TMP/dir32.o" "$TMP/directives.s"
"$AS" -64 -g -o "$TMP/dir64.o" "$TMP/directives.s"

check_obj "$TMP/dir32.o"
check_obj "$TMP/dir64.o"

echo "ok: directives and data emission"
