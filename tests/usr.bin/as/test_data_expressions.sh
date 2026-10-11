#!/bin/sh
# What an expression in a data directive comes to (AS-T-210 to 214).
#
# Every name in one is a number, or so far into a section, or the
# address of a symbol of another file; and the whole is a number, or a
# number and one place (a relocation of the directive's width), or that
# less a place of the directive's own section (a relocation relative to
# the field).  Whatever else it is has no relocation and is refused.
#
# This assembler had a rule for each spelling it had met, and outside
# them:
#
#   .word ext, .byte ext            were refused; in 64-bit code .long ext
#   .long ext+4 - .                 was four zeros and no relocation
#   .word ext - ., .quad ext - .    likewise
#   .long x - undefined             was zero, and said nothing
#   .long x+y, .long 2*x            were the sums of two offsets
#   .long .                         was a relocation to a symbol named `.`
#   movl $x+y, %eax                 had the relocation of x alone
#
# Each case is the bytes of .text and its relocations, which are GNU
# as's, and GNU as passes this test.  The cases keep to names of another
# file and to `.`: a label of the source that is not a .L one has its
# own symbol here, where GNU as names its section.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is MODE SOURCE BYTES RELOCATIONS: `|` in SOURCE is a newline; the
# relocations are type@offset:symbol+addend, sorted.
is() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    relocs=$(readelf -rW t.o | awk '/ R_/ { printf "%s@%x:%s%s\n", $3, strtonum("0x" $1), $5, $6 $7 }' |
             sort | tr '\n' ' ' | sed 's/ $//')
    [ "$bytes" = "$3" ] || { echo "FAIL --$1 [$2]: bytes $bytes, not $3"; fail=1; }
    [ "$relocs" = "$4" ] || { echo "FAIL --$1 [$2]: relocations [$relocs], not [$4]"; fail=1; }
}

# refused MODE SOURCE, and on a line that names the file
refused() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    if "$AS" "--$1" -o t.o t.s > /dev/null 2> err; then
        echo "FAIL --$1 [$2]: assembled"; fail=1
    elif ! grep -q '^t\.s:[0-9]' err; then
        echo "FAIL --$1 [$2]: refused without a line: $(tail -1 err)"; fail=1
    fi
}

# A symbol of another file, in each width.
is 32 '.long ext' 00000000 'R_386_32@0:ext'
is 64 '.long ext' 00000000 'R_X86_64_32@0:ext+0'
is 32 '.word ext' 0000 'R_386_16@0:ext'
is 64 '.word ext' 0000 'R_X86_64_16@0:ext+0'
is 32 '.byte ext' 00 'R_386_8@0:ext'
is 64 '.byte ext' 00 'R_X86_64_8@0:ext+0'
refused 32 '.quad ext'
is 64 '.quad ext' 0000000000000000 'R_X86_64_64@0:ext+0'
is 32 '.long ext+4' 04000000 'R_386_32@0:ext'
is 64 '.long ext+4' 00000000 'R_X86_64_32@0:ext+4'
is 32 '.word ext-2' feff 'R_386_16@0:ext'
is 64 '.word ext-2' 0000 'R_X86_64_16@0:ext-2'
is 32 '.byte ext+1' 01 'R_386_8@0:ext'
is 64 '.byte ext+1' 00 'R_X86_64_8@0:ext+1'
is 32 '.long ext+(1<<4)' 10000000 'R_386_32@0:ext'
is 64 '.long ext+(1<<4)' 00000000 'R_X86_64_32@0:ext+10'
is 32 '.set k,8|.long ext+k|.long k*2' 0800000010000000 'R_386_32@0:ext'
is 64 '.set k,8|.long ext+k|.long k*2' 0000000010000000 'R_X86_64_32@0:ext+8'
is 32 '.long ext|.long ext2 - .|.byte 1|.word ext3' 0000000000000000010000 'R_386_16@9:ext3 R_386_32@0:ext R_386_PC32@4:ext2'
is 64 '.long ext|.long ext2 - .|.byte 1|.word ext3' 0000000000000000010000 'R_X86_64_16@9:ext3+0 R_X86_64_32@0:ext+0 R_X86_64_PC32@4:ext2+0'

# Less the place of the field: relative to it.
is 32 '.long ext - .' 00000000 'R_386_PC32@0:ext'
is 64 '.long ext - .' 00000000 'R_X86_64_PC32@0:ext+0'
is 32 '.long ext+4 - .' 04000000 'R_386_PC32@0:ext'
is 64 '.long ext+4 - .' 00000000 'R_X86_64_PC32@0:ext+4'
is 32 '.long ext - . + 8' 08000000 'R_386_PC32@0:ext'
is 64 '.long ext - . + 8' 00000000 'R_X86_64_PC32@0:ext+8'
refused 32 '.quad ext - .'
is 64 '.quad ext - .' 0000000000000000 'R_X86_64_PC64@0:ext+0'
is 32 '.word ext - .' 0000 'R_386_PC16@0:ext'
is 64 '.word ext - .' 0000 'R_X86_64_PC16@0:ext+0'
is 32 '.byte ext - .' 00 'R_386_PC8@0:ext'
is 64 '.byte ext - .' 00 'R_X86_64_PC8@0:ext+0'
is 32 'nop|.long 1, ext - ., ext - .' 90010000000000000000000000 'R_386_PC32@5:ext R_386_PC32@9:ext'
is 64 'nop|.long 1, ext - ., ext - .' 90010000000000000000000000 'R_X86_64_PC32@5:ext+0 R_X86_64_PC32@9:ext+0'

# Less a label of the section the directive is in.
is 32 'a: nop|.long ext - a|.long ext - a + 4' 900100000009000000 'R_386_PC32@1:ext R_386_PC32@5:ext'
is 64 'a: nop|.long ext - a|.long ext - a + 4' 900000000000000000 'R_X86_64_PC32@1:ext+1 R_X86_64_PC32@5:ext+9'
is 32 'nop|.long ext - a|a: nop' 90fcffffff90 'R_386_PC32@1:ext'
is 64 'nop|.long ext - a|a: nop' 900000000090 'R_X86_64_PC32@1:ext-4'
is 32 '.long b-a+ext|a: nop|nop|b: nop' 02000000909090 'R_386_32@0:ext'
is 64 '.long b-a+ext|a: nop|nop|b: nop' 00000000909090 'R_X86_64_32@0:ext+2'
is 32 '.long ext-(a-b)|a: nop|nop|b: nop' 02000000909090 'R_386_32@0:ext'
is 64 '.long ext-(a-b)|a: nop|nop|b: nop' 00000000909090 'R_X86_64_32@0:ext+2'
is 32 '.long undefined-x|x: nop' fcffffff90 'R_386_PC32@0:undefined'
is 64 '.long undefined-x|x: nop' 0000000090 'R_X86_64_PC32@0:undefined-4'

# Two labels of one section: a number.
is 32 'a: nop|nop|b: nop|.long b-a' 90909002000000 ''
is 64 'a: nop|nop|b: nop|.long b-a' 90909002000000 ''
is 32 'a: nop|nop|b: nop|.long b-a+4|.quad b-a|.word b-a|.byte b-a' 909090060000000200000000000000020002 ''
is 64 'a: nop|nop|b: nop|.long b-a+4|.quad b-a|.word b-a|.byte b-a' 909090060000000200000000000000020002 ''
is 32 '.long b-a|a: nop|nop|b: nop' 02000000909090 ''
is 64 '.long b-a|a: nop|nop|b: nop' 02000000909090 ''
is 32 'a: nop|nop|b: nop|.data|.long b-a, a-b' 909090 ''
is 64 'a: nop|nop|b: nop|.data|.long b-a, a-b' 909090 ''
is 32 '.data|x: .long 1|y: .long 2|.text|.long x-y+4|.quad y-x' 000000000400000000000000 ''
is 64 '.data|x: .long 1|y: .long 2|.text|.long x-y+4|.quad y-x' 000000000400000000000000 ''
is 32 '.data|d: .long 0|e: .long 0|.text|a: nop|.long e - d|.byte e-d|.word e-d' 9004000000040400 ''
is 64 '.data|d: .long 0|e: .long 0|.text|a: nop|.long e - d|.byte e-d|.word e-d' 9004000000040400 ''
is 32 '.long (b-a)*2|a: nop|nop|b: nop' 04000000909090 ''
is 64 '.long (b-a)*2|a: nop|nop|b: nop' 04000000909090 ''
is 32 '.long (b-a)/2+1|a: nop|nop|b: nop' 02000000909090 ''
is 64 '.long (b-a)/2+1|a: nop|nop|b: nop' 02000000909090 ''
is 32 'x: nop|.long x - .' 90ffffffff ''
is 64 'x: nop|.long x - .' 90ffffffff ''
is 32 'nop|.long 1, 2, x - .|x: nop' 9001000000020000000400000090 ''
is 64 'nop|.long 1, 2, x - .|x: nop' 9001000000020000000400000090 ''
is 32 '.byte x - .|x: nop' 0190 ''
is 64 '.byte x - .|x: nop' 0190 ''
is 32 '.globl g|g: nop|.long g - .' 90ffffffff ''
is 64 '.globl g|g: nop|.long g - .' 90ffffffff ''
is 32 '.weak g|g: nop|.long g - .' 90ffffffff ''
is 64 '.weak g|g: nop|.long g - .' 90ffffffff ''
is 32 '.globl g|g: nop|x: nop|.long x - g' 909001000000 ''
is 64 '.globl g|g: nop|x: nop|.long x - g' 909001000000 ''
is 32 'nop|1: nop|.long 1b - .|.long . - 1b' 9090ffffffff05000000 ''
is 64 'nop|1: nop|.long 1b - .|.long . - 1b' 9090ffffffff05000000 ''
is 32 'nop|.long 1f - .|nop|1: nop' 90050000009090 ''
is 64 'nop|.long 1f - .|nop|1: nop' 90050000009090 ''
is 32 '.long .-a|a: nop' fcffffff90 ''
is 64 '.long .-a|a: nop' fcffffff90 ''
is 32 'nop|.long . - .' 9000000000 ''
is 64 'nop|.long . - .' 9000000000 ''
is 32 '.long ext-ext' 00000000 ''
is 64 '.long ext-ext' 00000000 ''

# `.` is the place of the element, and no symbol.
is 32 '.long .+4' 04000000 'R_386_32@0:.text'
is 64 '.long .+4' 00000000 'R_X86_64_32@0:.text+4'
is 32 '.long ., .' 0000000004000000 'R_386_32@0:.text R_386_32@4:.text'
is 64 '.long ., .' 0000000000000000 'R_X86_64_32@0:.text+0 R_X86_64_32@4:.text+4'
is 32 'nop|.long ., .' 900100000005000000 'R_386_32@1:.text R_386_32@5:.text'
is 64 'nop|.long ., .' 900000000000000000 'R_X86_64_32@1:.text+1 R_X86_64_32@5:.text+5'
refused 32 'nop|.quad .'
is 64 'nop|.quad .' 900000000000000000 'R_X86_64_64@1:.text+1'
is 32 'nop|.word .|.byte .' 90010003 'R_386_16@1:.text R_386_8@3:.text'
is 64 'nop|.word .|.byte .' 90000000 'R_X86_64_16@1:.text+1 R_X86_64_8@3:.text+3'
is 32 'nop|.long 1, .' 900100000005000000 'R_386_32@5:.text'
is 64 'nop|.long 1, .' 900100000000000000 'R_X86_64_32@5:.text+5'
for m in 32 64; do
    cases=$((cases + 1))
    printf '\t.text\n\tnop\n\t.long ., .+4\n\tmovl $., %%eax\n' > t.s
    "$AS" --$m -o t.o t.s 2> err
    if readelf -sW t.o | awk '$8 == "." { found = 1 } END { exit !found }'; then
        echo "FAIL --$m: a symbol named '.' is in the symbol table"; fail=1
    fi
done

# What has no relocation.
refused 32 '.long x-undefined'
refused 64 '.long x-undefined'
refused 32 '.long u1-u2'
refused 64 '.long u1-u2'
refused 32 'x: nop|.long x+y|y: nop'
refused 64 'x: nop|.long x+y|y: nop'
refused 32 '.long 2*x|x: nop'
refused 64 '.long 2*x|x: nop'
refused 32 '.long ext - h|.data|h: .long 0'
refused 64 '.long ext - h|.data|h: .long 0'
refused 32 '.data|h: .long 0|.text|.long h - ext'
refused 64 '.data|h: .long 0|.text|.long h - ext'
refused 32 '.long x/2|x: nop'
refused 64 '.long x/2|x: nop'
refused 32 '.long x-y|x: nop|.data|y: .long 0'
refused 64 '.long x-y|x: nop|.data|y: .long 0'
refused 32 '.long ~ext'
refused 64 '.long ~ext'
refused 32 '.long . - ext'
refused 64 '.long . - ext'
refused 32 '.long x&1|x: nop'
refused 64 '.long x&1|x: nop'
refused 32 '.long ext+ext'
refused 64 '.long ext+ext'
refused 32 '.long ext1-ext2'
refused 64 '.long ext1-ext2'
refused 32 'x: nop|y: nop|movl $x+y,%eax'
refused 64 'x: nop|y: nop|movl $x+y,%eax'
refused 32 'x: nop|movl $2*x,%eax'
refused 64 'x: nop|movl $2*x,%eax'
refused 32 'movl $ext1+ext2,%eax'
refused 64 'movl $ext1+ext2,%eax'
refused 32 'movl $ext*2,%eax'
refused 64 'movl $ext*2,%eax'
refused 32 'movl ext1+ext2(%ebx),%eax'
refused 64 'movl ext1+ext2(%ebx),%eax'

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
