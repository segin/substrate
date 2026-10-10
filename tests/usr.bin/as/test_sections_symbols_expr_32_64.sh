#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-sems-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/semantics.s" <<'SRC'
.text
.globl glob_fn
.type glob_fn,@function
glob_fn:
    mov $0, %eax
    ret
.size glob_fn, .-glob_fn

.data
.weak weak_sym
.type weak_sym,@object
.size weak_sym,4
weak_sym:
    .long 0

.local local_only
local_only:
    .long 5

.globl hidden_sym
.hidden hidden_sym
.type hidden_sym,@object
.size hidden_sym, 4
hidden_sym:
    .long 0x12345678

.globl obj_sym
.type obj_sym,@object
.size obj_sym, 8
obj_sym:
    .long (3 + 4 * 2)
    .long ext_sym + 4

.section .mysec,"aw",@progbits
mysec_label:
    .byte 1

.section .alignsec,"aw",@progbits
a0:
    .byte 0
.align 16
a1:
    .byte 1
.p2align 5
a2:
    .byte 2
.balign 64
a3:
    .byte 3

.bss
.lcomm lcommsym,8
.comm commsym,16,8
SRC

cat > "$TMP/overflow.s" <<'SRC'
.text
.globl ovf
ovf:
    mov $0x1ff, %al
    ret
SRC

cat > "$TMP/redef.s" <<'SRC'
.text
again:
    nop
again:
    ret
SRC

sym_exists() {
    readelf --wide -s "$1" | awk -v sym="$2" '$8 == sym { ok = 1 } END { exit ok ? 0 : 1 }'
}

sym_check() {
    readelf --wide -s "$1" | awk -v sym="$2" -v type="$3" -v bind="$4" -v vis="$5" '$8 == sym && $4 == type && $5 == bind && $6 == vis { ok = 1 } END { exit ok ? 0 : 1 }'
}

sym_get_hex() {
    readelf --wide -s "$1" | awk -v sym="$2" '$8 == sym { print $2; exit }'
}

check_obj() {
    OBJ="$1"

    readelf -S "$OBJ" | grep -q "\\.text"
    readelf -S "$OBJ" | grep -q "\\.data"
    readelf -S "$OBJ" | grep -q "\\.bss"
    readelf -S "$OBJ" | grep -q "\\.mysec"
    readelf -S "$OBJ" | grep -q "\\.alignsec"
    readelf -S "$OBJ" | grep -Eq "\\.mysec.*PROGBITS"

    sym_check "$OBJ" glob_fn FUNC GLOBAL DEFAULT
    sym_check "$OBJ" obj_sym OBJECT GLOBAL DEFAULT
    sym_check "$OBJ" hidden_sym OBJECT GLOBAL HIDDEN
    readelf --wide -s "$OBJ" | awk '$8 == "weak_sym" && $5 == "WEAK" { ok = 1 } END { exit ok ? 0 : 1 }'
    readelf --wide -s "$OBJ" | awk '$8 == "commsym" && $7 == "COM" { ok = 1 } END { exit ok ? 0 : 1 }'
    readelf --wide -s "$OBJ" | awk '$8 == "lcommsym" && $5 == "LOCAL" { ok = 1 } END { exit ok ? 0 : 1 }'

    # relocatable expression should create relocation for ext_sym
    readelf -r "$OBJ" | grep -q "ext_sym"

    # folded constant 11 must be present in .data (0b 00 00 00 little-endian)
    objdump -s -j .data "$OBJ" | grep -Eiq "0b000000"

    A1_HEX=$(sym_get_hex "$OBJ" a1)
    A2_HEX=$(sym_get_hex "$OBJ" a2)
    A3_HEX=$(sym_get_hex "$OBJ" a3)
    [ -n "$A1_HEX" ] && [ -n "$A2_HEX" ] && [ -n "$A3_HEX" ]

    A1=$((16#$A1_HEX))
    A2=$((16#$A2_HEX))
    A3=$((16#$A3_HEX))

    [ $((A1 % 16)) -eq 0 ]
    [ $((A2 % 32)) -eq 0 ]
    [ $((A3 % 64)) -eq 0 ]
}

"$AS" -32 -o "$TMP/sem32.o" "$TMP/semantics.s"
"$AS" -64 -o "$TMP/sem64.o" "$TMP/semantics.s"
check_obj "$TMP/sem32.o"
check_obj "$TMP/sem64.o"

"$AS" -32 -o "$TMP/overflow.o" "$TMP/overflow.s" >"$TMP/ovf.out" 2>"$TMP/ovf.err"
grep -Eqi "warning|error|overflow|shortened" "$TMP/ovf.err"

if "$AS" -64 -o "$TMP/redef.o" "$TMP/redef.s" >"$TMP/redef.out" 2>"$TMP/redef.err"; then
    echo "expected symbol redefinition input to fail"
    exit 1
fi
grep -qi "error" "$TMP/redef.err"

echo "ok: section/symbol/expression semantics"
