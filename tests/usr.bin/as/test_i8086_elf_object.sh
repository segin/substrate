#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-i8086-elf-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/code16_obj.s" <<'SRC'
.text
.code16
.arch i8086
.globl realmode_obj
.type realmode_obj,@function
realmode_obj:
    ljmp $0x1234,$0x5678
    lcall $0x4321,$0x1111
    mov %es:4(%bx,%si), %ax
    mov %ax, %ss:6(%bp,%di)
    jne 1f
    nop
1:
    jcxz 2f
    loop 2f
2:  nop
.size realmode_obj, .-realmode_obj
SRC

"$AS" --32 -o "$TMP/code16_obj.o" "$TMP/code16_obj.s"

readelf -h "$TMP/code16_obj.o" | grep -q "Type:[[:space:]]*REL"
readelf -S "$TMP/code16_obj.o" | grep -q "[[:space:]]\\.text[[:space:]]"
readelf -r "$TMP/code16_obj.o" | grep -q "There are no relocations in this file."

objdump -dr -mi8086 "$TMP/code16_obj.o" > "$TMP/code16_obj.dump"
grep -Eq 'ljmp[[:space:]]+\$0x1234,\$0x5678' "$TMP/code16_obj.dump"
grep -Eq 'lcall[[:space:]]+\$0x4321,\$0x1111' "$TMP/code16_obj.dump"
grep -Eq 'mov[[:space:]]+%es:0x4\(%bx,%si\),%ax' "$TMP/code16_obj.dump"
grep -Eq 'mov[[:space:]]+%ax,%ss:0x6\(%bp,%di\)' "$TMP/code16_obj.dump"
grep -Eq 'jne|jnz' "$TMP/code16_obj.dump"
grep -Eq 'jcxz' "$TMP/code16_obj.dump"
grep -Eq 'loop' "$TMP/code16_obj.dump"

echo "ok: i8086 relocatable object intent"
