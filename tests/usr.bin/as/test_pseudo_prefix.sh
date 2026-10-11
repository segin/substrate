#!/bin/sh
# The pseudo-prefixes: a word in braces before an x86 instruction that
# asks for one of the encodings it has.
#
#   {vex} {vex2} {vex3}   the VEX encoding; {vex3} its three-byte prefix
#   {evex}                the EVEX encoding
#   {disp8} {disp32}      a displacement of that width, zero if none is
#                         written; {disp8} of a value that does not fit
#                         in a byte is four
#   {load} {store}        between two registers, the destination in
#                         ModRM.reg or in ModRM.rm
#   {rex}                 a REX prefix though none is needed
#   {nooptimize}          nothing this assembler would otherwise do
#
# {vex} and {evex} were taken out of the source in five places, two of
# them in the lexer, and the instruction encoded as if they had not been
# written; the others were refused as bad operands.  Each is now done,
# or the statement refused where its instruction has no such encoding.
#
# Each line is held to the bytes GNU as 2.46 writes.  The 64-bit cases
# are assembled at -march=x86-64-v4, where both of VEX and EVEX are
# allowed.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# p MODES WANT SOURCE
p() {
    for mode in $1; do
        cases=$((cases + 1))
        printf '\t.text\n\t%s\n' "$3" > t.s
        rm -f t.o
        if [ "$mode" = 64 ]; then
            "$AS" -64 -march=x86-64-v4 -o t.o t.s > out 2>&1
        else
            "$AS" -32 -o t.o t.s > out 2>&1
        fi
        if [ $? -eq 0 ]; then
            objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
            got=$(od -An -v -tx1 text.bin | tr -d ' \n')
        else
            got=refused
        fi
        if [ "$got" != "$2" ]; then
            [ "$got" = refused ] && got="refused ($(head -1 out))"
            echo "FAIL $mode [$3]: $got, and GNU as: $2"
            fail=1
        fi
    done
}

# VEX and EVEX.
p "32 64" c5e858d9         'vaddps %xmm1,%xmm2,%xmm3'
p "32 64" c5e858d9         '{vex} vaddps %xmm1,%xmm2,%xmm3'
p "32 64" c5e858d9         '{vex2} vaddps %xmm1,%xmm2,%xmm3'
p "32 64" c4e16858d9       '{vex3} vaddps %xmm1,%xmm2,%xmm3'
p "32 64" 62f16c0858d9     '{evex} vaddps %xmm1,%xmm2,%xmm3'
p "32 64" 62f16c2858d9     '{evex} vaddps %ymm1,%ymm2,%ymm3'
p "32 64" c4e26900d9       '{vex3} vpshufb %xmm1,%xmm2,%xmm3'
p "64"    64c4e16c5818     '{vex3} vaddps %fs:(%rax),%ymm2,%ymm3'
p "64"    62f16d08fe18     '{evex} vpaddd (%rax),%xmm2,%xmm3'
# vpdpbusd is EVEX unless asked.
p "32 64" 62f2750850d0     'vpdpbusd %xmm0,%xmm1,%xmm2'
p "32 64" c4e27150d0       '{vex} vpdpbusd %xmm0,%xmm1,%xmm2'
p "32 64" refused          '{vex} vaddps %zmm1,%zmm2,%zmm3'
p "32 64" refused          '{evex} vzeroupper'
p "32 64" refused          '{vex} nop'
p "32 64" refused          '{evex} movl %eax,%ebx'

# The width of a displacement.
p "32"    8b9800000000     '{disp32} movl (%eax),%ebx'
p "32"    8b5800           '{disp8} movl (%eax),%ebx'
p "32"    8b9804000000     '{disp32} movl 4(%eax),%ebx'
p "32"    8b5804           '{disp8} movl 4(%eax),%ebx'
p "32"    8b9800100000     '{disp8} movl 0x1000(%eax),%ebx'
p "32"    8b9c4800000000   '{disp32} movl (%eax,%ecx,2),%ebx'
p "32"    8b9c2400000000   '{disp32} movl (%esp),%ebx'
p "32"    8b5d00           '{disp8} movl (%ebp),%ebx'
p "32"    8b9d00000000     '{disp32} movl (%ebp),%ebx'
p "32"    660f38008000000000 '{disp32} pshufb (%eax),%xmm0'
p "64"    48898700000000   '{disp32} movq %rax,(%rdi)'
p "64"    498344240001     '{disp8} addq $1,(%r12)'
p "64"    c5e8589800000000 '{disp32} vaddps (%rax),%xmm2,%xmm3'
p "64"    c5ec585800       '{disp8} vaddps (%rax),%ymm2,%ymm3'
p "64"    62f16c48589800000000 '{evex} {disp32} vaddps (%rax),%zmm2,%zmm3'

# The direction between two registers.
p "32 64" 89c3             'movl %eax,%ebx'
p "32 64" 89c3             '{store} movl %eax,%ebx'
p "32 64" 8bd8             '{load} movl %eax,%ebx'
p "32 64" 8ad8             '{load} movb %al,%bl'
p "32 64" 01c3             '{store} addl %eax,%ebx'
p "32 64" 03d8             '{load} addl %eax,%ebx'
p "32 64" 33d1             '{load} xorl %ecx,%edx'
p "32 64" 663bd8           '{load} cmpw %ax,%bx'
p "64"    488bd8           '{load} movq %rax,%rbx'
p "64"    4d2bc8           '{load} subq %r8,%r9'
p "32"    8b18             '{load} movl (%eax),%ebx'
p "32"    8918             '{store} movl %ebx,(%eax)'

# A REX that is not needed -- and none with %ah, which cannot have one.
p "64"    4089c3           '{rex} movl %eax,%ebx'
p "64"    4088c3           '{rex} movb %al,%bl'
p "64"    4090             '{rex} nop'
p "64"    40830001         '{rex} addl $1,(%rax)'
p "64"    4489c0           '{rex} movl %r8d,%eax'
p "64"    88e0             '{rex} movb %ah,%al'
p "32"    refused          '{rex} nop'
p "32"    refused          '{rex} movl %eax,%ebx'

# After a label, and the one that asks for nothing.
p "32 64" c5e858d9         'here: {vex} vaddps %xmm1,%xmm2,%xmm3'
p "32 64" 90               '{nooptimize} nop'
p "32 64" 89c3             '{nooptimize} movl %eax,%ebx'

# A name that is none of them.
p "32 64" refused          '{bogus} nop'
p "32 64" refused          '{disp16} movl 4(%eax),%ebx'

[ "$fail" -eq 0 ] && echo "ok: pseudo-prefixes ($cases cases)"
exit "$fail"
