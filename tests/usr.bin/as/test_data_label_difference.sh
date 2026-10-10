#!/bin/sh
# `x - label` in data, with the label in the directive's own section
# (AS-T-217).
#
# A position-independent jump table is `.long .L17 - .L19` over and over:
# a label in the code less the table's own.  That is the code label's
# address less the field's own, plus the distance from the table's label
# to the field -- a PC-relative relocation against the code label with
# that distance for its addend.  The assembler knew `x - .` and nothing
# else of the kind; these were assembled as zeros with no relocation, and
# every switch gcc compiled for x86-64 jumped to its own table.
#
# The bytes and relocations are GNU as's.  Run by run-suite.sh, which sets
# $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

cat > t.s <<'EOF'
	.text
f:	nop
.L1:	nop
.L2:	ret
	.section .rodata
	.long 0
.Lt:	.long .L1-.Lt, .L2-.Lt
	.long .L1-.Lt+8
	.long ext-.Lt
	.long .Lt-.Lt
EOF

# check MODE DATA RELOCATIONS
check() {
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .rodata t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$2" ] || { echo "FAIL --$1: .rodata is $got, not $2"; fail=1; }
    rel=$(readelf -rW t.o | awk '/ R_/ { print $1, $3, $5, $6, $7 }' | sed 's/^0*//; s/ *$//' | tr '\n' ';')
    [ "$rel" = "$3" ] || { echo "FAIL --$1: relocations '$rel', not '$3'"; fail=1; }
}

# i386 keeps the addend in the field; x86-64 in the relocation.  The last
# entry is a label less itself: a number, 0, and no relocation.
check 32 000000000100000006000000110000000c00000000000000 \
    '4 R_386_PC32 .text;8 R_386_PC32 .text;c R_386_PC32 .text;10 R_386_PC32 ext;'
check 64 000000000000000000000000000000000000000000000000 \
    '4 R_X86_64_PC32 .text + 1;8 R_X86_64_PC32 .text + 6;c R_X86_64_PC32 .text + 11;10 R_X86_64_PC32 ext + c;'

# Two labels of one section are still a number.
printf '\t.data\na:\t.long 1\nb:\t.long 2\n\t.long b-a\n' > t.s
"$AS" --64 -o t.o t.s 2> err || { echo "FAIL b-a: $(head -1 err)"; fail=1; }
objcopy -O binary -j .data t.o t.bin
[ "$(od -An -v -tx1 t.bin | tr -d ' \n')" = 010000000200000004000000 ] ||
    { echo "FAIL b-a: $(od -An -v -tx1 t.bin | tr -d ' \n')"; fail=1; }
[ "$(readelf -rW t.o | grep -c ' R_')" = 0 ] || { echo "FAIL b-a: a relocation"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: label differences in data"
exit "$fail"
