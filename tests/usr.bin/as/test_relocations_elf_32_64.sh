#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-rel-elf-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/rel32.s" <<'SRC'
.text
.globl f32
.type f32,@function
f32:
    call ext32
    mov $ext32, %eax
    ret
.size f32, .-f32

.data
    .long ext32
    .long .text
SRC

cat > "$TMP/rel64.s" <<'SRC'
.text
.globl f64
.type f64,@function
f64:
    call ext64@PLT
    lea ext64(%rip), %rax
    movabs $ext64, %rax
    ret
.size f64, .-f64

.data
    .quad ext64 + 8
    .quad .text + 16
SRC

cat > "$TMP/overflow_rel.s" <<'SRC'
.text
badrel:
    jmpb far_target
    .fill 300,1,0x90
far_target:
    ret
SRC

"$AS" -32 -o "$TMP/rel32.o" "$TMP/rel32.s"
"$AS" -64 -o "$TMP/rel64.o" "$TMP/rel64.s"

# ET_REL and class/machine sanity
readelf -h "$TMP/rel32.o" | grep -q "Type:[[:space:]]*REL"
readelf -h "$TMP/rel32.o" | grep -q "ELF32"
readelf -h "$TMP/rel64.o" | grep -q "Type:[[:space:]]*REL"
readelf -h "$TMP/rel64.o" | grep -q "ELF64"

# i386 REL and required relocation families
readelf -S "$TMP/rel32.o" | grep -q "\\.rel\\.text"
readelf --wide -r "$TMP/rel32.o" | grep -q "R_386_PC32"
readelf --wide -r "$TMP/rel32.o" | grep -q "R_386_32"

# x86_64 RELA and required relocation families
readelf -S "$TMP/rel64.o" | grep -q "\\.rela\\.text"
readelf --wide -r "$TMP/rel64.o" | grep -Eq "R_X86_64_(PLT32|PC32)"
readelf --wide -r "$TMP/rel64.o" | grep -q "R_X86_64_64"

# Addend handling and section/symbol-relative relocations
readelf --wide -r "$TMP/rel64.o" | grep -Eq "ext64.*\+ 8"
readelf --wide -r "$TMP/rel64.o" | grep -Eq "\\.text.*\+ (16|10)"
readelf --wide -r "$TMP/rel32.o" | grep -q "\\.text"

# String/symbol tables and section header layout basics
readelf -S "$TMP/rel32.o" | grep -q "\\.symtab"
readelf -S "$TMP/rel32.o" | grep -q "\\.strtab"
readelf -S "$TMP/rel32.o" | grep -q "\\.shstrtab"
readelf -S "$TMP/rel64.o" | grep -q "\\.symtab"
readelf -S "$TMP/rel64.o" | grep -q "\\.strtab"
readelf -S "$TMP/rel64.o" | grep -q "\\.shstrtab"
readelf -h "$TMP/rel32.o" | grep -Eq "Start of section headers:[[:space:]]*[1-9][0-9]*"
readelf -h "$TMP/rel64.o" | grep -Eq "Start of section headers:[[:space:]]*[1-9][0-9]*"

if "$AS" -64 -o "$TMP/overflow_rel.o" "$TMP/overflow_rel.s" >"$TMP/ov1.out" 2>"$TMP/ov1.err"; then
    echo "expected forced short-jump relocation overflow to fail"
    exit 1
fi
if "$AS" -64 -o "$TMP/overflow_rel2.o" "$TMP/overflow_rel.s" >"$TMP/ov2.out" 2>"$TMP/ov2.err"; then
    echo "expected forced short-jump relocation overflow to fail"
    exit 1
fi
grep -qi "error" "$TMP/ov1.err"
sed -E 's#/tmp/cc[^ :]+#<tmp>#g; s#/tmp/as-rel-elf-[0-9]+#<tmpdir>#g' "$TMP/ov1.err" >"$TMP/ov1.norm"
sed -E 's#/tmp/cc[^ :]+#<tmp>#g; s#/tmp/as-rel-elf-[0-9]+#<tmpdir>#g' "$TMP/ov2.err" >"$TMP/ov2.norm"
cmp "$TMP/ov1.norm" "$TMP/ov2.norm"

echo "ok: relocations and ELF emission"
