#!/bin/sh
# Names of any length: of sections, of symbols and of files (AS-T-254).
#
# A section's name was carried about the assembler in arrays of 128
# bytes, and taken out of its quotes into one of 256: a section with a
# longer name was "unknown section", or was the section whose name its
# first 127 bytes were.  The names are kept whole now, however long.
#
# Sections of 301 and of 5,000 characters, a label in each, and what
# reaches them: a relocation into each from an instruction and from
# data, a jump to a symbol of 5,000 characters, a `.pushsection` of the
# long name.  The bytes of .text and the sizes of the sections are GNU
# as's, and GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

n301=$(printf '.s%0299d' 1)
n5000=$(printf '.s%04998d' 1)
# Two names that are the same for their first 300 characters.
twin_a=$(printf '.t%0299da' 1)
twin_b=$(printf '.t%0299db' 1)
sym=$(printf 'y%04999d' 1)

cat > t.s <<EOF
	.section $n301,"aw"
x1:	.long 1
	.section $n5000,"aw"
x2:	.long 2
.Lq:	.long 3
	.section $twin_a,"aw"
ta:	.byte 1
	.section $twin_b,"aw"
tb:	.byte 2, 2
	.section $twin_a
	.byte 1, 1
	.text
	movl x1, %eax
	movl x2, %eax
	movl .Lq, %eax
	jmp $sym
	.globl g$sym
g$sym:	nop
$sym:	.long x2 - ., .Lq - ., ta - ., tb - .
	.file "$sym.c"
	.pushsection $n301
	.byte 9
	.popsection
	ret
EOF

for m in 32 64; do
    cases=$((cases + 1))
    rm -f t.o
    if ! "$AS" --$m -o t.o t.s 2> err; then
        echo "FAIL --$m: $(tail -1 err | cut -c1-200)"; fail=1
        continue
    fi
    # Each section is there under its whole name, and as long as it
    # should be.
    # (readelf shortens a long name; the names are looked for in the
    # file, and the sections taken in the order they were first named.)
    for n in "$n301" "$n5000" "$twin_a" "$twin_b"; do
        strings -n 300 t.o | grep -qx -e "$n" || { echo "FAIL --$m: a section's name is not whole in the object"; fail=1; }
    done
    sizes=$(readelf -SW t.o | awk '
        { for (i = 1; i <= NF; i++) if ($i == "PROGBITS" && $(i + 4) == "00" && $(i + 5) == "WA")
              printf "%d ", strtonum("0x" $(i + 3)) }')
    [ "$sizes" = "0 5 8 3 2 " ] || { echo "FAIL --$m: the sections are of [$sizes] bytes"; fail=1; }
    # The jump reaches the long symbol, in two bytes: it is one byte on.
    text=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ $m = 32 ] && at=31 || at=43
    [ "$(echo "$text" | cut -c$at-$((at + 5)))" = eb0190 ] || { echo "FAIL --$m: .text is $text"; fail=1; }
    # A relocation into each long section from the instructions and
    # from the data: seven in .text, whatever they are made against.
    relocs=$(readelf -rW t.o | grep -c ' R_')
    [ "$relocs" = 7 ] || { echo "FAIL --$m: $relocs relocations, not 7"; fail=1; }
    # The long symbol is in the symbol table whole.
    longest=$(readelf -sW t.o | awk '$4 == "NOTYPE" { n = length($8); if (n > m) m = n } END { print m + 0 }')
    [ "$longest" = 5001 ] || { echo "FAIL --$m: the longest symbol is $longest characters"; fail=1; }
done

# Linked, the relocations into the long sections come to the places of
# the labels in them.
if command -v ld > /dev/null 2>&1; then
    cases=$((cases + 1))
    cat > u.s <<EOF
	.section $n5000,"aw"
	.long 0x11111111
x2:	.long 0x22222222
	.section $n301,"aw"
x1:	.long 0x33333333
	.text
	.globl _start
_start:	.long x1, x2
EOF
    if "$AS" --32 -o u.o u.s 2> err && ld -m elf_i386 -e _start -o u u.o 2>> err; then
        a1=$(nm u | awk '$3 == "x1" { print $1 }')
        a2=$(nm u | awk '$3 == "x2" { print $1 }')
        got=$(objcopy -O binary -j .text u /dev/stdout | od -An -v -tx4 | tr -s ' ' | sed 's/^ //')
        [ "$got" = "$a1 $a2" ] || { echo "FAIL linked: .text is [$got], x1 and x2 are [$a1 $a2]"; fail=1; }
    else
        echo "FAIL linked: $(tail -1 err | cut -c1-200)"; fail=1
    fi
fi

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
