#!/bin/sh
# The stack of sections, what a section may be said to be a second
# time, groups of several sections, and the sections every object has
# (AS-T-241, 242, 244, 247, 252, 253).
#
# - `.pushsection` keeps the section it is written in and the one before
#   it, and `.popsection` brings both back: a `.previous` after a pop
#   went to the section that was popped, so what followed was put with
#   the out-of-line text it had just left.
# - A `.popsection` with nothing pushed was an error; it is a warning.
# - Two sections of one group were one group with the first in it.
# - A section said again with another type, or with entries of another
#   size, was taken; it is refused, as other flags were.
# - .text, .data and .bss were aligned to 16, 4 and 4 whatever the
#   source asked for, and an empty .rodata was in every object.
#
# The layouts here are GNU as's, and GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# The sections of t.o that hold what the source said, each as
# name:type:size:flags:alignment.
layout() {
    readelf -SW t.o | awk '/^ *\[ *[0-9]+\] \./ {
        sub(/^ *\[ *[0-9]+\] /, "")
        if ($1 !~ /^\.(symtab|strtab|shstrtab|group|note\.GNU-stack|note\.gnu\.property|rela?\.)/)
            printf "%s:%s:%d:%s:%s ", $1, $2, strtonum("0x" $5), ($7 ~ /^[A-Za-z]+$/ ? $7 : "-"), $NF
    }' | sed 's/ $//'
}

# is SOURCE LAYOUT: `|` in SOURCE is a newline
is() {
    cases=$((cases + 1))
    printf '%s\n' "$1" | tr '|' '\n' > t.s
    rm -f t.o
    if ! "$AS" --64 -o t.o t.s 2> err; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    got=$(layout)
    [ "$got" = "$2" ] || { echo "FAIL [$1]: $got"; echo "  wanted: $2"; fail=1; }
}

refused() {
    cases=$((cases + 1))
    printf '%s\n' "$1" | tr '|' '\n' > t.s
    if "$AS" --64 -o t.o t.s > /dev/null 2> err; then
        echo "FAIL [$1]: assembled"; fail=1
    elif ! grep -q '^t\.s:[0-9]' err; then
        echo "FAIL [$1]: refused without a line: $(tail -1 err)"; fail=1
    fi
}

# What every object has, and how it is aligned.
is '' '.text:PROGBITS:0:AX:1 .data:PROGBITS:0:WA:1 .bss:NOBITS:0:WA:1'
is '.text|nop' '.text:PROGBITS:1:AX:1 .data:PROGBITS:0:WA:1 .bss:NOBITS:0:WA:1'
is '.data|.balign 8|.long 1' '.text:PROGBITS:0:AX:1 .data:PROGBITS:4:WA:8 .bss:NOBITS:0:WA:1'
is '.text|.p2align 4|nop|.bss|.zero 3' '.text:PROGBITS:1:AX:16 .data:PROGBITS:0:WA:1 .bss:NOBITS:3:WA:1'
is '.text|.p2align 2|nop|.p2align 5|nop|.p2align 3' '.text:PROGBITS:40:AX:32 .data:PROGBITS:0:WA:1 .bss:NOBITS:0:WA:1'
is '.section .rodata|.byte 1' '.text:PROGBITS:0:AX:1 .data:PROGBITS:0:WA:1 .bss:NOBITS:0:WA:1 .rodata:PROGBITS:1:A:1'

# A pop brings back the section before the push as well.
is '.data|.long 1|.pushsection .foo|.byte 1|.popsection|.previous|.byte 2' \
    '.text:PROGBITS:1:AX:1 .data:PROGBITS:4:WA:1 .bss:NOBITS:0:WA:1 .foo:PROGBITS:1:-:1'
is '.text|.data|.pushsection .foo|.byte 1|.popsection|.byte 3|.previous|.byte 2' \
    '.text:PROGBITS:1:AX:1 .data:PROGBITS:1:WA:1 .bss:NOBITS:0:WA:1 .foo:PROGBITS:1:-:1'
is '.data|.pushsection .foo|.pushsection .bar|.byte 1|.popsection|.byte 2|.previous|.byte 3,3|.popsection|.byte 4,4,4|.previous|nop' \
    '.text:PROGBITS:1:AX:1 .data:PROGBITS:5:WA:1 .bss:NOBITS:0:WA:1 .foo:PROGBITS:1:-:1 .bar:PROGBITS:1:-:1'
is '.data|.pushsection .foo|.section .bar|.byte 1|.popsection|.byte 2|.previous|nop' \
    '.text:PROGBITS:1:AX:1 .data:PROGBITS:1:WA:1 .bss:NOBITS:0:WA:1 .foo:PROGBITS:0:-:1 .bar:PROGBITS:1:-:1'

# A pop of nothing is said and the rest is assembled.
cases=$((cases + 1))
printf '\t.text\n\tnop\n\t.popsection\n\tnop\n' > t.s
rm -f t.o
"$AS" --64 -o t.o t.s 2> err
if [ ! -f t.o ] || ! grep -q '^t\.s:3: Warning: \.popsection without corresponding \.pushsection; ignored' err; then
    echo "FAIL a .popsection of nothing: $(tr '\n' '|' < err)"; fail=1
elif [ "$(layout)" != '.text:PROGBITS:2:AX:1 .data:PROGBITS:0:WA:1 .bss:NOBITS:0:WA:1' ]; then
    echo "FAIL a .popsection of nothing: $(layout)"; fail=1
fi

# Sections of one group are one group.
members() {
    readelf -gW t.o | awk '/group section/ { n++ } /^ *\[ *[0-9]+\] +\./ { m = m " " $NF } END { printf "%d:%s", n, m }'
}
group() {
    cases=$((cases + 1))
    printf '%s\n' "$1" | tr '|' '\n' > t.s
    rm -f t.o
    if ! "$AS" --64 -o t.o t.s 2> err; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    got=$(members)
    [ "$got" = "$2" ] || { echo "FAIL [$1]: groups $got, not $2"; fail=1; }
}
group '.section .a,"axG",@progbits,grp,comdat|nop' '1: .a'
group '.section .a,"axG",@progbits,grp,comdat|nop|.section .b,"aG",@progbits,grp,comdat|.byte 1' '1: .a .b'
group '.section .a,"axG",@progbits,g1,comdat|nop|.section .b,"aG",@progbits,g2,comdat|.byte 1|.section .c,"awG",@progbits,g1,comdat|.byte 2' \
    '2: .a .c .b'
group '.section .a,"aMSG",@progbits,1,grp,comdat|.byte 0|.section .b,"aG",@progbits,grp,comdat|.byte 0' '1: .a .b'
for src in '.section .a,"axG",@progbits,grp,comdat|nop|.section .b,"aG",@progbits,grp,comdat|.byte 1'; do
    cases=$((cases + 1))
    printf '%s\n' "$src" | tr '|' '\n' > t.s
    "$AS" --64 -o t.o t.s 2> err
    n=$(readelf -sW t.o | awk '$8 == "grp"' | wc -l)
    [ "$n" = 1 ] || { echo "FAIL the group's symbol is there $n times"; fail=1; }
done

# A section is what it was first said to be.
refused '.section .a,"aw"|.byte 0|.section .a,"a"|.byte 1'
refused '.section .a,"aw"|.byte 0|.section .a,"aw",@nobits|.byte 1'
refused '.section .a,"aw",@note|.byte 0|.section .a,"aw",@progbits|.byte 1'
refused '.section .a,"aM",@progbits,1|.byte 0|.section .a,"aM",@progbits,2|.byte 1'
is '.section .a,"aM",@progbits,1|.byte 0|.section .a,"aM",@progbits,1|.byte 1' \
    '.text:PROGBITS:0:AX:1 .data:PROGBITS:0:WA:1 .bss:NOBITS:0:WA:1 .a:PROGBITS:2:AM:1'
is '.section .a,"aw"|.byte 0|.text|.section .a|.byte 1' \
    '.text:PROGBITS:0:AX:1 .data:PROGBITS:0:WA:1 .bss:NOBITS:0:WA:1 .a:PROGBITS:2:WA:1'
is '.section .a,"aw"|.byte 0|.section .a,"aw",@progbits|.byte 1' \
    '.text:PROGBITS:0:AX:1 .data:PROGBITS:0:WA:1 .bss:NOBITS:0:WA:1 .a:PROGBITS:2:WA:1'

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
