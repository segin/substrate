#!/bin/sh
# The width of an address.  It is the mode's, or the other one that the
# 67 prefix selects: 32 bits in 64-bit code, 16 in 32-bit code.
#
# 64-bit code had no 67 at all: `movl (%eax), %ecx` was assembled as
# `movl (%rax), %ecx` and read through all of %rax.  And once it had,
# the relocation of a symbol's displacement, which was not looked for
# behind a 67, went on the last four bytes of the instruction whatever
# they were.
#
# 32-bit code had the prefix and the 16-bit ModRM table, in the main
# encoder; but a symbol there has a two-byte displacement and a 16-bit
# relocation, and four bytes were written over the end of the
# instruction: `movl %eax, sym(%bx)` came out as 67 00 00 00 00.  The
# other emitters have only the 32-bit table and wrote `fldl (%bx)` as
# `fldl (%ebx)`; they refuse it now.
#
# Each line is held to GNU as 2.46: the bytes, and the place and type of
# the relocation (`-` for none).  The 64-bit cases are assembled with
# -march=x86-64-v3, without which this assembler refuses AVX.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

assemble() {
    printf '\t.text\n\t%s\n' "$2" > t.s
    rm -f t.o
    if [ "$1" = 64 ]; then
        "$AS" -64 -march=x86-64-v3 -o t.o t.s > out 2>&1
    else
        "$AS" -32 -o t.o t.s > out 2>&1
    fi
}

# s MODE BYTES RELOCATION SOURCE
s() {
    cases=$((cases + 1))
    if ! assemble "$1" "$4"; then
        echo "FAIL $1 [$4]: refused: $(head -1 out)"
        fail=1
        return
    fi
    objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
    got=$(od -An -v -tx1 text.bin | tr -d ' \n')
    rel=$(readelf -rW t.o | awk '$1 ~ /^[0-9a-f]+$/ && $3 ~ /^R_/ { sub(/^0+/, "", $1); print $1 ":" $3; exit }')
    if [ "$got" != "$2" ] || [ "${rel:--}" != "$3" ]; then
        echo "FAIL $1 [$4]: $got ${rel:--}, and GNU as: $2 $3"
        fail=1
    fi
}

# x MODE SOURCE: refused, as GNU as refuses it.
x() {
    cases=$((cases + 1))
    if assemble "$1" "$2"; then
        echo "FAIL $1 [$2]: assembled, and GNU as refuses it"
        fail=1
    fi
}

# 32-bit addresses in 64-bit code.
s 64 678b08                 -              'movl (%eax), %ecx'
s 64 67894cb308             -              'movl %ecx, 8(%ebx,%esi,4)'
s 64 6741ff00               -              'incl (%r8d)'
s 64 67438b0411             -              'movl (%r9d,%r10d), %eax'
s 64 6788348500000000       -              'movb %dh, (,%eax,4)'
s 64 67003c5d10000000       -              'addb %bh, 0x10(,%ebx,2)'
s 64 670fb608               -              'movzbl (%eax), %ecx'
s 64 678d0c18               -              'leal (%eax,%ebx), %ecx'
s 64 67488d08               -              'leaq (%eax), %rcx'
s 64 67668d0b               -              'leaw (%ebx), %cx'
s 64 67ff23                 -              'jmp *(%ebx)'
s 64 678608                 -              'xchgb %cl, (%eax)'
s 64 67d323                 -              'shll %cl, (%ebx)'
s 64 67f60301               -              'testb $1, (%ebx)'
s 64 670fb108               -              'cmpxchg %ecx, (%eax)'
s 64 670f1f00               -              'nopl (%eax)'
s 64 67660f1f0400           -              'nopw 0(%eax,%eax,1)'
s 64 670f0110               -              'lgdt (%eax)'
s 64 670fae00               -              'fxsave (%eax)'
s 64 670f1800               -              'prefetchnta (%eax)'
s 64 8b08                   -              'movl (%rax), %ecx'

# After a segment override and before lock, where GNU as has it.
s 64 64678b08               -              'movl %fs:(%eax), %ecx'
s 64 67f0ff00               -              'lock incl (%eax)'

# The string instructions, whose address is their index register.
s 64 67ab                   -              'stosl %eax, %es:(%edi)'
s 64 67a4                   -              'movsb (%esi), (%edi)'
s 64 67ac                   -              'lodsb (%esi), %al'
s 64 3667a6                 -              'cmpsb %es:(%edi), %ss:(%esi)'

# The emitters that write no prefix of their own.
s 64 67dd00                 -              'fldl (%eax)'
s 64 670f2800               -              'movaps (%eax), %xmm0'
s 64 67f30f1008             -              'movss (%eax), %xmm1'
s 64 67f20f2a00             -              'cvtsi2sd (%eax), %xmm0'
s 64 67f30f7e00             -              'movq (%eax), %xmm0'
s 64 67660f7e00             -              'movd %xmm0, (%eax)'
s 64 678c00                 -              'movw %es, (%eax)'
s 64 67660f380000           -              'pshufb (%eax), %xmm0'
s 64 67f30fb808             -              'popcnt (%eax), %ecx'
s 64 67f20f38f008           -              'crc32b (%eax), %ecx'

# A symbol's displacement behind the prefix: on the displacement, and
# not sign-extended, the address being of 32 bits.
s 64 678b8800000000         3:R_X86_64_32  'movl sym(%eax), %ecx'
s 64 678d9000000000         3:R_X86_64_32  'leal sym(%eax),%edx'
s 64 674883830000000001     4:R_X86_64_32  'addq $1,sym(%ebx)'
s 64 64678b8800000000       4:R_X86_64_32  'movl %fs:sym(%eax),%ecx'
s 64 67dd8000000000         3:R_X86_64_32  'fldl sym(%eax)'
s 64 67660f38008000000000   6:R_X86_64_32  'pshufb sym(%eax),%xmm0'
s 64 67c5fc588800000000     5:R_X86_64_32  'vaddps sym(%eax),%ymm0,%ymm1'
s 64 8b8800000000           2:R_X86_64_32S 'movl sym(%rax),%ecx'

# lea into less than 64 bits throws the top of the address away, and
# its relocation need not be the sign-extended kind.
s 64 8d9000000000           2:R_X86_64_32  'leal sym(%rax),%edx'
s 64 668d9000000000         3:R_X86_64_32  'leaw sym(%rax),%dx'
s 64 8d142500000000         3:R_X86_64_32  'leal sym,%edx'
s 64 8d149d00000000         3:R_X86_64_32  'leal sym(,%rbx,4),%edx'
s 64 488d9000000000         3:R_X86_64_32S 'leaq sym(%rax),%rdx'
s 64 8d1500000000           2:R_X86_64_PC32 'leal sym(%rip),%edx'

# 16-bit addresses in 32-bit code.
s 32 67668b00               -              'movw (%bx,%si), %ax'
s 32 678b4f04               -              'movl 4(%bx),%ecx'
s 32 670f0117               -              'lgdt (%bx)'
s 32 6789870000             3:R_386_16     'movl %eax,sym(%bx)'
s 32 678b8f0000             3:R_386_16     'movl sym(%bx),%ecx'
s 32 678d970000             3:R_386_16     'leal sym(%bx),%edx'
s 32 67668b800000           4:R_386_16     'movw sym(%bx,%si),%ax'
s 32 67c784000005000000     3:R_386_16     'movl $5,sym(%si)'
s 32 670fb6870000           4:R_386_16     'movzbl sym(%bx),%eax'
s 32 67ff850200             3:R_386_16     'incl sym+2(%di)'
s 32 8b8b00000000           2:R_386_32     'movl sym(%ebx),%ecx'

# The port of in and out is no address.
s 32 ec                     -              'inb (%dx), %al'
s 32 6e                     -              'outsb (%esi), (%dx)'

# No prefix makes these.
x 64 'movl (%ax), %ecx'
x 64 'movl (%eax,%rbx), %ecx'
x 32 'movl (%rax), %ebx'
x 32 'movl (%r8), %eax'

[ "$fail" -eq 0 ] && echo "ok: address size ($cases cases)"
exit "$fail"
