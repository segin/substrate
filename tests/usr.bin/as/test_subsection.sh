#!/bin/sh
# Subsections: nothing is lost, and nothing is put in the wrong place.
#
# `.pushsection .text, 1` -- a section and a subsection number -- had its
# 1 read as a string of flags, which is no flags, and those were written
# over the section's: .text lost "ax" and, not being code any more, every
# instruction in it, with exit status 0.  (AS-T-024.)
#
# A subsection other than 0 is refused for now.  Its contents belong after
# those of the lower ones, and the assembler does not gather them: left in
# source order, code written to be out of line would be assembled in the
# path of what precedes it.  When AS-T-240 gathers them, the refusals
# below become checks of the order.
#
# Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# text WHAT SOURCE BYTES: .text holds BYTES and is still code.
text() {
    printf "$2" > t.s
    if ! "$AS" --32 -o t.o t.s 2> err; then
        echo "FAIL $1: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$3" ] || { echo "FAIL $1: .text is $got, not $3"; fail=1; }
    readelf -SW t.o | grep ' \.text ' | grep -q ' AX ' || { echo "FAIL $1: .text is not AX"; fail=1; }
    [ "$(readelf -SW t.o | grep -c ' \.text ')" = 1 ] || { echo "FAIL $1: more than one .text"; fail=1; }
}

# refused WHAT SOURCE: an error that says why, and no object.
refused() {
    printf "$2" > t.s
    rm -f t.o
    if "$AS" --32 -o t.o t.s 2> err; then echo "FAIL $1: assembled"; fail=1; return; fi
    grep -q 'subsections other than 0 are not supported' err ||
        { echo "FAIL $1: $(head -1 err)"; fail=1; }
    [ ! -e t.o ] || { echo "FAIL $1: an object was written"; fail=1; }
}

text ".pushsection name, 0"  '.text\nnop\n.pushsection .text, 0\nret\n.popsection\nnop\n'  90c390
text ".pushsection name"     '.text\nnop\n.pushsection .text\nret\n.popsection\nnop\n'     90c390
text ".subsection 0"         '.text\nnop\n.subsection 0\nret\n'                            90c3
text ".text 0"               '.text 0\nnop\n'                                              90
text "flags are still flags" '.text\nnop\n.pushsection .rodata.str1.1, "aMS", @progbits, 1\n.string "a"\n.popsection\nret\n' 90c3

refused ".pushsection name, 1" '.text\nnop\n.pushsection .text, 1\nret\n.popsection\nnop\n'
refused ".subsection 1"        '.text\nnop\n.subsection 1\nret\n'
refused ".text 1"              '.text 1\nnop\n'
refused ".data 2"              '.text\nnop\n.data 2\n.byte 1\n'

[ "$fail" -eq 0 ] && echo "ok: subsections"
exit "$fail"
