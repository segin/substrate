#!/bin/sh
# A relocation is in a field of its instruction, or there is none
# (AS-T-196).
#
# Where a symbol's relocation goes is worked out from the instruction's
# bytes, and where that came to a field as long as the instruction or
# longer, the relocation was cut to the instruction's length and written
# from its first byte: the opcode, and any prefix, were zeros.  So
#
#     .code16
#     movw sym, %bx           was 00 00 00 00 with R_386_32
#     lea  sym(%bx), %ax      likewise
#     jmp  *sym               likewise
#
# every instruction of 16-bit code that names a symbol in an address,
# the address there being of two bytes and taken for one of four.
#
# An address of 16-bit code has its two bytes and R_386_16 now.  And a
# relocation that would cover its whole instruction is refused, on its
# line, where it had been cut to fit: what is still not known how to
# place says so and writes nothing.
#
# The bytes and relocations are GNU as's, and GNU as passes this test.
# The cases under `placed` are ones this assembler may yet refuse, and
# must not get wrong.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is SOURCE BYTES RELOCATIONS, in a 32-bit object: `|` in SOURCE is a
# newline; the relocations are type@offset:symbol, sorted.
is() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$1" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" --32 -o t.o t.s 2> err; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    relocs=$(readelf -rW t.o | awk '/ R_/ { printf "%s@%x:%s\n", $3, strtonum("0x" $1), $5 }' |
             sort | tr '\n' ' ' | sed 's/ $//')
    [ "$bytes" = "$2" ] || { echo "FAIL [$1]: bytes $bytes, not $2"; fail=1; }
    [ "$relocs" = "$3" ] || { echo "FAIL [$1]: relocations [$relocs], not [$3]"; fail=1; }
}

# placed SOURCE BYTES RELOCATIONS: the same, or refused on its line
placed() {
    { echo .text; printf '%s\n' "$1" | tr '|' '\n'; } > t.s
    if "$AS" --32 -o t.o t.s > /dev/null 2> err; then
        is "$1" "$2" "$3"
        return
    fi
    cases=$((cases + 1))
    grep -q '^t\.s:[0-9]' err || { echo "FAIL [$1]: refused without a line: $(tail -1 err)"; fail=1; }
}

# An address of 16-bit code.
is '.code16|lea sym,%ax' 8d060000 'R_386_16@2:sym'
is '.code16|leaw sym,%bx' 8d1e0000 'R_386_16@2:sym'
is '.code16|movw sym,%bx' 8b1e0000 'R_386_16@2:sym'
is '.code16|lea sym(%bx),%ax' 8d870000 'R_386_16@2:sym'
is '.code16|leal sym,%eax' 668d060000 'R_386_16@3:sym'
is '.code16|jmp *sym' ff260000 'R_386_16@2:sym'
is '.code16|call *sym' ff160000 'R_386_16@2:sym'
is '.code16|addw $1,sym' 8306000001 'R_386_16@2:sym'
is '.code16|incw sym' ff060000 'R_386_16@2:sym'
is '.code16|lgdt sym' 0f01160000 'R_386_16@3:sym'
is '.code16|movb $1,sym' c606000001 'R_386_16@2:sym'
is '.code16|movw $1,sym' c70600000100 'R_386_16@2:sym'
is '.code16|movw sym+4(%bx,%si),%ax' 8b800400 'R_386_16@2:sym'
# And one of 32 bits within it, behind the prefix.
is '.code16|movw sym(%eax),%ax' 678b8000000000 'R_386_32@3:sym'

# In 32-bit code: two bytes of distance or of size, behind what is
# before them.
is 'callw sym' 66e8feff 'R_386_PC16@2:sym'
is 'enter $sym,$1' c8000001 'R_386_16@1:sym'
is 'movl sym(%bx),%ecx' 678b8f0000 'R_386_16@3:sym'

# What may be refused, and may not be wrong.
placed '.code16|movw $sym,%bx' bb0000 'R_386_16@1:sym'
placed '.code16|pushw $sym' 680000 'R_386_16@1:sym'
placed 'ret $sym' c20000 'R_386_16@1:sym'
placed 'int $sym' cd00 'R_386_8@1:sym'
placed 'aam $sym' d400 'R_386_8@1:sym'

# No instruction of these is all zeros where its opcode should be.
for src in '.code16|movw sym,%bx' '.code16|jmp *sym' 'callw sym' 'enter $sym,$1' '.code16|movw $sym,%bx' 'ret $sym'; do
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$src" | tr '|' '\n'; } > t.s
    rm -f t.o
    if "$AS" --32 -o t.o t.s > /dev/null 2>&1; then
        first=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 -N1 | tr -d ' \n')
        at=$(readelf -rW t.o | awk '/ R_/ { print strtonum("0x" $1) }' | sort -n | head -1)
        if [ "$first" = 00 ] || [ "${at:-1}" = 0 ]; then
            echo "FAIL [$src]: a relocation is written over the opcode"; fail=1
        fi
    fi
done

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
