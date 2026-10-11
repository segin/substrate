#!/bin/sh
# Subsections: a section is its numbered parts in ascending order of
# number, whatever order they were written in.
#
# `.text 1`, `.subsection 1` and `.pushsection name, 1` are how code is
# put out of line: written in the middle of a function, assembled after
# it.  They were ignored, so that code was assembled in the path of what
# came before it; then they were refused.  Now the parts are gathered.
#
# And `.pushsection .text, 1` -- a section and a number -- had its 1
# read as a string of flags, which is no flags, and those written over
# the section's: .text lost "ax" and every instruction in it.
#
# The bytes are GNU as 2.46's.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0
cases=0

# sec WHAT SECTION FLAGS BYTES SOURCE: the section holds BYTES, has FLAGS
# and is one section, in 32-bit code and in 64-bit.
sec() {
    for mode in 32 64; do
        cases=$((cases + 1))
        printf "$5" > t.s
        rm -f t.o
        if ! "$AS" --$mode -o t.o t.s 2> err; then
            echo "FAIL $mode $1: $(head -1 err | sed 's/.*: //')"; fail=1; continue
        fi
        objcopy -O binary -j "$2" t.o t.bin 2>/dev/null
        got=$(od -An -v -tx1 t.bin | tr -d ' \n')
        [ "$got" = "$4" ] || { echo "FAIL $mode $1: $2 is $got, not $4"; fail=1; }
        readelf -SW t.o | grep " $2 " | grep -q " $3 " || { echo "FAIL $mode $1: $2 is not $3"; fail=1; }
        [ "$(readelf -SW t.o | grep -c " $2 ")" = 1 ] || { echo "FAIL $mode $1: more than one $2"; fail=1; }
    done
}

# refused WHAT SOURCE: an error, and no object.
refused() {
    cases=$((cases + 1))
    printf "$2" > t.s
    rm -f t.o
    if "$AS" --32 -o t.o t.s 2> err; then echo "FAIL $1: assembled"; fail=1; return; fi
    [ ! -e t.o ] || { echo "FAIL $1: an object was written"; fail=1; }
}

# Subsection 0 is the section.
sec ".pushsection name, 0" .text AX 90c390 '.text\nnop\n.pushsection .text, 0\nret\n.popsection\nnop\n'
sec ".pushsection name"    .text AX 90c390 '.text\nnop\n.pushsection .text\nret\n.popsection\nnop\n'
sec ".subsection 0"        .text AX 90c3   '.text\nnop\n.subsection 0\nret\n'
sec ".text 0"              .text AX 90     '.text 0\nnop\n'
sec "flags are still flags" .text AX 90c3 \
    '.text\nnop\n.pushsection .rodata.str1.1, "aMS", @progbits, 1\n.string "a"\n.popsection\nret\n'

# Each way of writing a number, and the section keeps what it had.
sec ".pushsection name, 1" .text AX 9090c3 '.text\nnop\n.pushsection .text, 1\nret\n.popsection\nnop\n'
sec ".subsection 1"        .text AX 9090c3 '.text\nnop\n.subsection 1\nret\n.subsection 0\nnop\n'
sec ".text 1"              .text AX 9090c3 '.text\nnop\n.text 1\nret\n.text\nnop\n'
sec ".data 2"              .data WA 000102 '.data\n.byte 0\n.data 2\n.byte 2\n.data 1\n.byte 1\n'
sec "a number that is an expression" .text AX 9090c3 '.text\nnop\n.subsection 2-1\nret\n.text 0\nnop\n'

# Ascending order, whatever the source's; the same number twice is one
# part, in the order written.
sec "3, 2, 0 written; 0, 2, 3 assembled" .foo WA 01002203 \
    '.section .foo,"aw",@progbits\n.byte 1\n.subsection 3\n.byte 3\n.subsection 2\n.byte 0x22\n.subsection 0\n.byte 0\n'
sec "a part written in pieces" .text AX 90f4cccdc3 \
    '.text\nnop\n.text 2\nret\n.text 1\nint3\n.text\nhlt\n.text 1\n.byte 0xcd\n'
sec "a part never left" .text AX 90c3 '.text\nnop\n.subsection 5\nret\n'
sec "numbers far apart" .text AX 900102 '.text\nnop\n.subsection 100000\n.byte 2\n.subsection 9000\n.byte 1\n'
# GNU as orders them as signed numbers: -1 is before 0.
sec "a negative number" .text AX c390cc '.text\nnop\n.subsection -1\nret\n.subsection 1\nint3\n'

# .previous and .popsection go back to the part that was left, not only
# to the section.
sec ".previous after .subsection" .text AX 90f4c3 '.text\nnop\n.subsection 1\nret\n.previous\nhlt\n'
sec ".previous twice" .text AX 90f4c3cc '.text\nnop\n.subsection 1\nret\n.previous\nhlt\n.previous\nint3\n'
sec ".popsection to a numbered part" .text AX 90c3cc \
    '.text\nnop\n.text 1\nret\n.pushsection .data\n.byte 7\n.popsection\nint3\n'
sec "pushes nested, each with a number" .data WA 000102 \
    '.text\n.pushsection .data, 1\n.byte 1\n.pushsection .text, 2\nret\n.popsection\n.byte 2\n.popsection\n.data\n.byte 0\n'
sec "pushes nested: the other section" .text AX f4c3 \
    '.text\n.pushsection .data, 1\n.byte 1\n.pushsection .text, 2\nret\n.popsection\n.byte 2\n.popsection\nhlt\n'
sec "a section entered by name is entered at 0" .foo WA 000901 \
    '.section .foo,"aw"\n.byte 0\n.subsection 1\n.byte 1\n.text\nnop\n.section .foo\n.byte 9\n'

# Out-of-line code: the branches reach across, and are the short ones.
sec "out of line and back" .text AX eb01c3ebfd \
    '.text\nf: jmp .Lslow\n.Lback: ret\n.pushsection .text, 1\n.Lslow: jmp .Lback\n.popsection\n'
sec "a label on the directive that leaves" .text AX 90eb01cc \
    '.text\nnop\n.subsection 1\nint3\n.Lx: .previous\njmp .Lx\n.subsection 1\n'
sec "a difference across parts" .data WA 0005000000aabbcc00 \
    '.data\na: .byte 0\n.long b - a - 3\n.data 1\n.byte 0xaa, 0xbb, 0xcc\nb: .byte 0\n'
# A numeric label is the nearest in the source, not in the object.
sec "1b and 2f across parts" .text AX 90eb01c3ebfeebfb \
    '.text\n1: nop\n.subsection 1\n1: jmp 1b\njmp 2f\n.previous\njmp 1b\n2: ret\n'

# An alignment in a part is the section's.
sec "alignment in a part" .data WA 010000000200000003 \
    '.data\n.byte 1\n.data 1\n.balign 4\n.long 2\n.data 2\n.byte 3\n'

# The mode is what it was where the statement was written.
cases=$((cases + 1))
printf '.text\n.code16\nmovw %%ax,%%bx\n.subsection 1\nmovw %%ax,%%bx\n.code32\nmovw %%ax,%%bx\n.previous\nmovw %%ax,%%bx\n.subsection 1\nmovl %%eax,%%ebx\n' > t.s
if "$AS" --32 -o t.o t.s 2> err; then
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = 89c36689c389c36689c389c3 ] || { echo "FAIL the mode in a part: $got"; fail=1; }
else
    echo "FAIL the mode in a part: $(head -1 err)"; fail=1
fi

refused "a name for a number"            '.text\n.subsection foo\nnop\n'
refused ".pushsection, a negative number" '.text\n.pushsection .text,-2\nnop\n'

[ "$fail" -eq 0 ] && echo "ok: subsections ($cases cases)"
exit "$fail"
