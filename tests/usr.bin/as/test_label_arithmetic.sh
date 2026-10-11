#!/bin/sh
# Labels subtracted in an instruction, `.` in one, and two more of the
# short forms (AS-T-186).
#
# - One label of a section less another of it is a number.  In a data
#   directive it was; in an instruction it was a relocation to the first
#   label, and the second was forgotten:
#
#       movl $end-start, %eax     loaded the address of `end`
#
# - A symbol of elsewhere less a label of this section is the symbol's
#   distance from the field, R_386_PC32, and the label was forgotten
#   there too: `movl $ext-start, %eax` loaded the address of `ext`.
#   Less anything else there is no relocation, and it is refused.
# - `.` in an instruction was a relocation to a symbol named `.`, which
#   no linker has.  It is where the instruction begins.
# - A move between the accumulator and an address with no register has
#   in 32-bit code a form with no ModRM (a0 to a3), and the accumulator
#   with a symbol for its number has the forms that name no register.
#
# Each case is the bytes of .text and its relocations, which are GNU
# as's; GNU as passes this test.  (The numbers here are in instructions
# whose field GNU as and this assembler make the same size.)
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

# refused MODE SOURCE
refused() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    if "$AS" "--$1" -o t.o t.s > /dev/null 2> err; then
        echo "FAIL --$1 [$2]: assembled"; fail=1
    fi
}

# A label less a label: behind, ahead, local, numbered and global.
is 32 'foo:|nop|bar:|movl $bar-foo,%eax|movl $foo-bar,%ecx|movl $bar-foo+4,%edx' 90b801000000b9ffffffffba05000000 ''
is 64 'foo:|nop|bar:|movl $bar-foo,%eax|movl $foo-bar,%ecx|movl $bar-foo+4,%edx' 90b801000000b9ffffffffba05000000 ''
is 32 'movl $bar-foo,%eax|foo:|nop|nop|bar:' b8020000009090 ''
is 64 'movl $bar-foo,%eax|foo:|nop|nop|bar:' b8020000009090 ''
is 32 '.L1:|nop|.L2:|movl $.L2-.L1,%eax|movl $.L2-.L1,sym' 90b801000000c7050000000001000000 'R_386_32@8:sym'
is 64 '.L1:|nop|.L2:|movl $.L2-.L1,%eax|movl $.L2-.L1,sym' 90b801000000c704250000000001000000 'R_X86_64_32S@9:sym+0'
is 32 '1:|nop|2:|movl $2b-1b,%eax|movl $3f-1b,%ecx|3:' 90b801000000b90b000000 ''
is 64 '1:|nop|2:|movl $2b-1b,%eax|movl $3f-1b,%ecx|3:' 90b801000000b90b000000 ''
is 32 '.globl foo|foo:|nop|.globl bar|bar:|movl $bar-foo,%eax' 90b801000000 ''
is 64 '.globl foo|foo:|nop|.globl bar|bar:|movl $bar-foo,%eax' 90b801000000 ''

# A symbol of elsewhere less a label of this section.
is 32 'foo:|nop|bar:|movl $ext-foo,%eax|movl $ext-bar+8,%ebx' 90b802000000bb0e000000 'R_386_PC32@2:ext R_386_PC32@7:ext'
is 64 'foo:|nop|bar:|movl $ext-foo,%eax|movl $ext-bar+8,%ebx' 90b800000000bb00000000 \
    'R_X86_64_PC32@2:ext+2 R_X86_64_PC32@7:ext+e'
is 32 '1:|nop|movl $ext-1b,%eax' 90b802000000 'R_386_PC32@2:ext'
is 64 '1:|nop|movl $ext-1b,%eax' 90b800000000 'R_X86_64_PC32@2:ext+2'

# `.`, which is where the instruction begins.
is 32 'foo:|nop|movl $.-foo,%ebx|movl $foo-.,%ebx|movl $.-foo+8,%ecx|movl $.-.,%edx' \
    90bb01000000bbfaffffffb913000000ba00000000 ''
is 64 'foo:|nop|movl $.-foo,%ebx|movl $foo-.,%ebx|movl $.-foo+8,%ecx|movl $.-.,%edx' \
    90bb01000000bbfaffffffb913000000ba00000000 ''
is 32 'nop|movl $ext-.,%eax|movl $ext-.+4,%ebx' 90b801000000bb05000000 'R_386_PC32@2:ext R_386_PC32@7:ext'
is 64 'nop|movl $ext-.,%eax|movl $ext-.+4,%ebx' 90b800000000bb00000000 'R_X86_64_PC32@2:ext+1 R_X86_64_PC32@7:ext+5'
is 32 'nop|movl $.,%eax|movl $.+4,%eax' 90b801000000b80a000000 'R_386_32@2:.text R_386_32@7:.text'
is 64 'nop|movl $.,%eax|movl $.+4,%eax' 90b800000000b800000000 'R_X86_64_32@2:.text+1 R_X86_64_32@7:.text+a'

# What has no relocation.
for mode in 32 64; do
    refused $mode '.data|bar:|.text|movl $bar-ext,%eax'
    refused $mode 'movl $a-b,%eax'
    refused $mode 'foo:|.data|bar:|.long 0|.text|movl $foo-bar,%eax'
    refused $mode 'foo:|.section .t2,"ax"|movl $ext-foo,%eax'
    refused $mode 'nop|movl $.-ext,%eax'
done

# The accumulator and an address with no register, in 32-bit code.
is 32 'movl sym,%eax|movl %eax,sym|movb sym,%al|movb %al,sym|movw sym,%ax|movw %ax,sym' \
    a100000000a300000000a000000000a20000000066a10000000066a300000000 \
    'R_386_32@10:sym R_386_32@16:sym R_386_32@1:sym R_386_32@1c:sym R_386_32@6:sym R_386_32@b:sym'
is 32 'mov 0x1000,%eax|mov %eax,0x1000|mov sym+4,%eax|mov %fs:sym,%eax|mov %eax,%gs:0x10|data16 movl sym,%eax' \
    a100100000a300100000a10400000064a10000000065a31000000066a100000000 \
    'R_386_32@11:sym R_386_32@1d:sym R_386_32@b:sym'
# Not another register, not with an index, and not in 64-bit code.
is 32 'movl sym,%ebx|movl %ecx,sym|movl sym(,%eax),%eax|movl (%eax),%eax' \
    8b1d00000000890d000000008b0405000000008b00 'R_386_32@2:sym R_386_32@8:sym R_386_32@f:sym'
is 64 'movl sym,%eax|movl %eax,sym' 8b04250000000089042500000000 'R_X86_64_32S@3:sym+0 R_X86_64_32S@a:sym+0'
# A load through the global offset table stays as it is, for the linker.
is 32 'movl sym@GOT,%eax|movl %eax,sym@GOT|movb sym@GOT,%al|movl sym@GOTOFF,%eax|movl sym@NTPOFF,%eax|movl sym@INDNTPOFF,%eax' \
    8b0500000000a3000000008a0500000000a100000000a100000000a100000000 \
    'R_386_GOT32@7:sym R_386_GOT32@d:sym R_386_GOT32X@2:sym R_386_GOTOFF@12:sym R_386_TLS_IE@1c:sym R_386_TLS_LE@17:sym'

# The accumulator with a symbol for its number.
is 32 'testb $sym,%al|testw $sym,%ax|testl $sym,%eax' a80066a90000a900000000 'R_386_16@4:sym R_386_32@7:sym R_386_8@1:sym'
is 64 'testb $sym,%al|testw $sym,%ax|testl $sym,%eax' a80066a90000a900000000 \
    'R_X86_64_16@4:sym+0 R_X86_64_32@7:sym+0 R_X86_64_8@1:sym+0'
is 32 'addb $sym,%al|orw $sym,%ax|adcl $sym,%eax|sbbl $sym+4,%eax|andl $sym,%eax|subl $sym,%eax|xorl $sym,%eax|cmpl $sym,%eax' \
    0400660d000015000000001d0400000025000000002d0000000035000000003d00000000 \
    'R_386_16@4:sym R_386_32@11:sym R_386_32@16:sym R_386_32@1b:sym R_386_32@20:sym R_386_32@7:sym R_386_32@c:sym R_386_8@1:sym'
is 64 'addb $sym,%al|orw $sym,%ax|adcl $sym,%eax|sbbl $sym+4,%eax|andl $sym,%eax|subl $sym,%eax|xorl $sym,%eax|cmpl $sym,%eax' \
    0400660d000015000000001d0000000025000000002d0000000035000000003d00000000 \
    'R_X86_64_16@4:sym+0 R_X86_64_32@11:sym+0 R_X86_64_32@16:sym+0 R_X86_64_32@1b:sym+0 R_X86_64_32@20:sym+0 R_X86_64_32@7:sym+0 R_X86_64_32@c:sym+4 R_X86_64_8@1:sym+0'
is 64 'addq $sym,%rax|orq $sym,%rax|cmpq $sym,%rax|testq $sym,%rax|andq $sym-8,%rax' \
    480500000000480d00000000483d0000000048a900000000482500000000 \
    'R_X86_64_32S@14:sym+0 R_X86_64_32S@1a:sym-8 R_X86_64_32S@2:sym+0 R_X86_64_32S@8:sym+0 R_X86_64_32S@e:sym+0'
# Not another register.
is 32 'addl $sym,%ebx|testl $sym,%ecx|cmpb $sym,%bl' 81c300000000f7c10000000080fb00 'R_386_32@2:sym R_386_32@8:sym R_386_8@e:sym'
is 64 'addl $sym,%ebx|testl $sym,%ecx|cmpb $sym,%bl' 81c300000000f7c10000000080fb00 \
    'R_X86_64_32@2:sym+0 R_X86_64_32@8:sym+0 R_X86_64_8@e:sym+0'
# The table by name: the addend is the field's place in the instruction.
is 32 'addl $_GLOBAL_OFFSET_TABLE_,%eax|.L1:|addl $_GLOBAL_OFFSET_TABLE_+[.-.L1],%eax' 05010000000501000000 \
    'R_386_GOTPC@1:_GLOBAL_OFFSET_TABLE_ R_386_GOTPC@6:_GLOBAL_OFFSET_TABLE_'

[ "$fail" -eq 0 ] && echo "ok: label arithmetic and short forms ($cases cases)"
exit "$fail"
