#!/bin/sh
# Symbols that are other names for something (AS-T-260 to 266).
#
# `.set a, expr` and `a = expr`, where expr is no number: another symbol
# and so many bytes on, where the `.set` stands, the distance between
# two labels.  Each part of the assembler made what it could of such a
# name, and they did not agree:
#
#   .set a1,b1 / .set b1,c1 / c1:     a1 was undefined
#   .set a,ext / call a               called a local symbol `a` that no
#                                     file defines
#   .set j1, e1 - c1                  was refused, unless written
#                                     `. - label`
#   .set x,y / .set y,x               assembled
#   .set g,f  (f a function)          g was of no type
#   movl $g2,%eax / .set g2,16        was a relocation against g2
#
# What a name stands for is put where the name is used, names within
# names followed to the end, before anything else reads the source; and
# the symbol table has the name with its target's place, type and size.
#
# Each case is the bytes of .text and .data, the relocations, and the
# symbols as name=value/size/type/binding/section.  They are GNU as's,
# and GNU as passes this test.  (The cases keep clear of a reference to
# a label of the source that no other file sees, which here names the
# label and with GNU its section.)
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is MODE SOURCE BYTES RELOCATIONS SYMBOLS: `|` in SOURCE is a newline
is() {
    cases=$((cases + 1))
    printf '%s\n' "$2" | tr '|' '\n' > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(for s in .text .data; do
                objcopy -O binary -j $s t.o /dev/stdout 2> /dev/null | od -An -v -tx1 | tr -d ' \n'; printf '/'
            done)
    relocs=$(readelf -rW t.o | awk '/ R_/ { printf "%s@%x:%s%s\n", $3, strtonum("0x" $1), $5, $6 $7 }' |
             LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')
    syms=$(readelf -sSW t.o | awk '
        /^ *\[ *[0-9]+\]/ { x = $0; sub(/^ *\[ */, "", x); split(x, a, /[] ]+/); sec[a[1]] = a[2]; next }
        $4 == "NOTYPE" || $4 == "FUNC" || $4 == "OBJECT" {
            if ($8 != "") printf "%s=%x/%d/%s/%s/%s\n", $8, strtonum("0x" $2), $3, $4, $5, (($7 in sec) ? sec[$7] : $7)
        }' | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')
    [ "$bytes" = "$3" ] || { echo "FAIL --$1 [$2]: bytes $bytes, not $3"; fail=1; }
    [ "$relocs" = "$4" ] || { echo "FAIL --$1 [$2]: relocations [$relocs], not [$4]"; fail=1; }
    [ "$syms" = "$5" ] || { echo "FAIL --$1 [$2]: symbols [$syms], not [$5]"; fail=1; }
}

# refused MODE SOURCE NAME: and the message names NAME
refused() {
    cases=$((cases + 1))
    printf '%s\n' "$2" | tr '|' '\n' > t.s
    if "$AS" "--$1" -o t.o t.s > /dev/null 2> err; then
        echo "FAIL --$1 [$2]: assembled"; fail=1
    elif ! grep -q "loop.*$3" err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1
    fi
}

# A number, above where it is used and below.
is 32 '.set g1,2*8|.text|movl $g1,%eax|movl g1(%ebx),%eax' 'b8100000008b4310//' '' 'g1=10/0/NOTYPE/LOCAL/ABS'
is 64 '.set g1,2*8|.text|movl $g1,%eax|movl g1(%ebx),%eax' 'b810000000678b4310//' '' 'g1=10/0/NOTYPE/LOCAL/ABS'
is 32 '.text|movl $g2,%eax|.set g2,16|.data|.long g2' 'b810000000/10000000/' '' 'g2=10/0/NOTYPE/LOCAL/ABS'
is 64 '.text|movl $g2,%eax|.set g2,16|.data|.long g2' 'b810000000/10000000/' '' 'g2=10/0/NOTYPE/LOCAL/ABS'
is 32 '.set big,1<<40|.data|.quad big' '/0000000000010000/' '' 'big=0/0/NOTYPE/LOCAL/ABS'
is 64 '.set big,1<<40|.data|.quad big' '/0000000000010000/' '' 'big=10000000000/0/NOTYPE/LOCAL/ABS'

# The distance between two labels, and from a label to here.
is 32 '.data|c1: .long 0|e1: .long 0|.set j1, e1 - c1|.long j1|.byte j1' '/00000000000000000400000004/' '' 'c1=0/0/NOTYPE/LOCAL/.data e1=4/0/NOTYPE/LOCAL/.data j1=4/0/NOTYPE/LOCAL/ABS'
is 64 '.data|c1: .long 0|e1: .long 0|.set j1, e1 - c1|.long j1|.byte j1' '/00000000000000000400000004/' '' 'c1=0/0/NOTYPE/LOCAL/.data e1=4/0/NOTYPE/LOCAL/.data j1=4/0/NOTYPE/LOCAL/ABS'
is 32 '.text|start: nop|nop|.set len, . - start|.data|.long len|.skip len,7' '9090/020000000707/' '' 'len=2/0/NOTYPE/LOCAL/ABS start=0/0/NOTYPE/LOCAL/.text'
is 64 '.text|start: nop|nop|.set len, . - start|.data|.long len|.skip len,7' '9090/020000000707/' '' 'len=2/0/NOTYPE/LOCAL/ABS start=0/0/NOTYPE/LOCAL/.text'
is 32 '.text|start: nop|nop|.set len, . - start|movl $len,%eax' '9090b802000000//' '' 'len=2/0/NOTYPE/LOCAL/ABS start=0/0/NOTYPE/LOCAL/.text'
is 64 '.text|start: nop|nop|.set len, . - start|movl $len,%eax' '9090b802000000//' '' 'len=2/0/NOTYPE/LOCAL/ABS start=0/0/NOTYPE/LOCAL/.text'
is 32 '.text|l1: nop|l2: nop|d = l2 - l1|e = d + 2|.data|.long e' '9090/03000000/' '' 'd=1/0/NOTYPE/LOCAL/ABS e=3/0/NOTYPE/LOCAL/ABS l1=0/0/NOTYPE/LOCAL/.text l2=1/0/NOTYPE/LOCAL/.text'
is 64 '.text|l1: nop|l2: nop|d = l2 - l1|e = d + 2|.data|.long e' '9090/03000000/' '' 'd=1/0/NOTYPE/LOCAL/ABS e=3/0/NOTYPE/LOCAL/ABS l1=0/0/NOTYPE/LOCAL/.text l2=1/0/NOTYPE/LOCAL/.text'

# Where the `.set` stands.
is 32 '.text|here = .|nop|there = .|.data|.long there - here' '90/01000000/' '' 'here=0/0/NOTYPE/LOCAL/.text there=1/0/NOTYPE/LOCAL/.text'
is 64 '.text|here = .|nop|there = .|.data|.long there - here' '90/01000000/' '' 'here=0/0/NOTYPE/LOCAL/.text there=1/0/NOTYPE/LOCAL/.text'
is 32 '.data|d0: .long 0|.set here, .|.long 0|.set span, . - d0|.long span' '/000000000000000008000000/' '' 'd0=0/0/NOTYPE/LOCAL/.data here=4/0/NOTYPE/LOCAL/.data span=8/0/NOTYPE/LOCAL/ABS'
is 64 '.data|d0: .long 0|.set here, .|.long 0|.set span, . - d0|.long span' '/000000000000000008000000/' '' 'd0=0/0/NOTYPE/LOCAL/.data here=4/0/NOTYPE/LOCAL/.data span=8/0/NOTYPE/LOCAL/ABS'

# A name for a symbol of another file is that symbol, and no symbol of
# its own: through a second name, with a number added, and made weak.
is 32 '.set a,ext|.text|call a|jmp a|movl a,%eax|movl $a,%eax|.data|.long a, a - .' 'e8fcffffffe9fcffffffa100000000b800000000/0000000000000000/' 'R_386_32@0:ext R_386_32@10:ext R_386_32@b:ext R_386_PC32@1:ext R_386_PC32@4:ext R_386_PC32@6:ext' 'ext=0/0/NOTYPE/GLOBAL/UND'
is 64 '.set a,ext|.text|call a|jmp a|movl a,%eax|movl $a,%eax|.data|.long a, a - .' 'e800000000e9000000008b042500000000b800000000/0000000000000000/' 'R_X86_64_32@0:ext+0 R_X86_64_32@12:ext+0 R_X86_64_32S@d:ext+0 R_X86_64_PC32@4:ext+0 R_X86_64_PLT32@1:ext-4 R_X86_64_PLT32@6:ext-4' 'ext=0/0/NOTYPE/GLOBAL/UND'
is 32 '.set a,ext+4|.data|.long a|.text|movl a,%eax' 'a104000000/04000000/' 'R_386_32@0:ext R_386_32@1:ext' 'ext=0/0/NOTYPE/GLOBAL/UND'
is 64 '.set a,ext+4|.data|.long a|.text|movl a,%eax' '8b042500000000/00000000/' 'R_X86_64_32@0:ext+4 R_X86_64_32S@3:ext+4' 'ext=0/0/NOTYPE/GLOBAL/UND'
is 32 '.set w,ext|.weak w|.data|.long w' '/00000000/' 'R_386_32@0:ext' 'ext=0/0/NOTYPE/GLOBAL/UND'
is 64 '.set w,ext|.weak w|.data|.long w' '/00000000/' 'R_X86_64_32@0:ext+0' 'ext=0/0/NOTYPE/GLOBAL/UND'
is 32 '.set a1,b1|.set b1,ext|.data|.long a1, b1' '/0000000000000000/' 'R_386_32@0:ext R_386_32@4:ext' 'ext=0/0/NOTYPE/GLOBAL/UND'
is 64 '.set a1,b1|.set b1,ext|.data|.long a1, b1' '/0000000000000000/' 'R_X86_64_32@0:ext+0 R_X86_64_32@4:ext+0' 'ext=0/0/NOTYPE/GLOBAL/UND'

# A name for a function or an object is one, and as large.
is 32 '.text|.type f,@function|f: ret|.set g,f|.globl g|.data|.type o,@object|o: .long 1|.size o,4|h = o' 'c3/01000000/' '' 'f=0/0/FUNC/LOCAL/.text g=0/0/FUNC/GLOBAL/.text h=0/4/OBJECT/LOCAL/.data o=0/4/OBJECT/LOCAL/.data'
is 64 '.text|.type f,@function|f: ret|.set g,f|.globl g|.data|.type o,@object|o: .long 1|.size o,4|h = o' 'c3/01000000/' '' 'f=0/0/FUNC/LOCAL/.text g=0/0/FUNC/GLOBAL/.text h=0/4/OBJECT/LOCAL/.data o=0/4/OBJECT/LOCAL/.data'
is 32 '.text|.type f,@function|f: ret|.set g,f+1' 'c3//' '' 'f=0/0/FUNC/LOCAL/.text g=1/0/FUNC/LOCAL/.text'
is 64 '.text|.type f,@function|f: ret|.set g,f+1' 'c3//' '' 'f=0/0/FUNC/LOCAL/.text g=1/0/FUNC/LOCAL/.text'
is 32 '.text|.type f,@function|f: ret|.size f,.-f|.set g,f|.globl g' 'c3//' '' 'f=0/1/FUNC/LOCAL/.text g=0/1/FUNC/GLOBAL/.text'
is 64 '.text|.type f,@function|f: ret|.size f,.-f|.set g,f|.globl g' 'c3//' '' 'f=0/1/FUNC/LOCAL/.text g=0/1/FUNC/GLOBAL/.text'

# A name that other files see, for a place in this one, is a symbol in
# its own right: what refers to it goes by the name.
is 32 '.text|.globl f|f: nop|.set g,f|.globl g|call g|.data|.long g' '90e8fcffffff/00000000/' 'R_386_32@0:g R_386_PC32@2:g' 'f=0/0/NOTYPE/GLOBAL/.text g=0/0/NOTYPE/GLOBAL/.text'
is 64 '.text|.globl f|f: nop|.set g,f|.globl g|call g|.data|.long g' '90e800000000/00000000/' 'R_X86_64_32@0:g+0 R_X86_64_PLT32@2:g-4' 'f=0/0/NOTYPE/GLOBAL/.text g=0/0/NOTYPE/GLOBAL/.text'
is 32 '.text|f: nop|.set g,f|.weak g|.data|.long g' '90/00000000/' 'R_386_32@0:g' 'f=0/0/NOTYPE/LOCAL/.text g=0/0/NOTYPE/WEAK/.text'
is 64 '.text|f: nop|.set g,f|.weak g|.data|.long g' '90/00000000/' 'R_X86_64_32@0:g+0' 'f=0/0/NOTYPE/LOCAL/.text g=0/0/NOTYPE/WEAK/.text'

# Whatever the order they are set in, a name for a name is at the place.
for m in 32 64; do
    cases=$((cases + 1))
    printf '\t.set a1,b1\n\t.set b1,c1\n\t.text\n\tnop\nc1:\tnop\n' > t.s
    "$AS" --$m -o t.o t.s 2> err
    got=$(readelf -sW t.o | awk '$8 ~ /^[abc]1$/ { printf "%s=%d/%s ", $8, strtonum("0x" $2), $7 }')
    case "$got" in
    *a1=1/[0-9]*b1=1/[0-9]*c1=1/[0-9]*) ;;
    *) echo "FAIL --$m a name for a name set further down: $got $(tail -1 err)"; fail=1 ;;
    esac
done

# A name that comes back to itself.
refused 32 '.set x,y|.set y,x|.long x' x
refused 64 '.set x,y|.set y,x|.long x' x
refused 32 '.set a,b|.set b,c|.set c,a|.text|call a' a
refused 64 '.set a,b|.set b,c|.set c,a|.text|call a' a

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
