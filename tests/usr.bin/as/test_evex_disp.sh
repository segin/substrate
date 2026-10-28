#!/bin/sh
# The displacement of an AVX-512 memory operand (the part of AS-T-319
# that was wrong and not merely long), and the EVEX shifts by a number.
#
# An EVEX instruction's 8-bit displacement is multiplied by N, the size
# of what the instruction reads at its memory operand.  It was written
# as a number of bytes: `vpaddd 64(%rax), %zmm1, %zmm2` was assembled
# with the byte 0x40, which a processor reads as 64 * 64, and every
# AVX-512 instruction with a small displacement read or wrote memory
# 4,032 bytes from where the source said.  Nothing was reported.
#
# With a broadcast N is the element's size and the compressed form is
# used, as GNU as uses it.  Without, this assembler does not yet know N
# for each instruction and writes a 32-bit displacement: longer than
# GNU's, and the address the source names.
#
# So two kinds of case.  What the disassembler reads back from the
# object must be the displacement that was written, whichever form it
# is in; and the forms that are GNU's are held to GNU's bytes.
#
# And the shifts by a number, `vpsllq $1, %zmm2, %zmm1`: the register
# the result goes to and the digit that says which shift were in each
# other's fields, and the result was no instruction there is.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
march=-march=x86-64-v4
"$AS" --version 2> /dev/null | grep '^GNU assembler' | grep -qv '(Substrate)' && march=

assemble() {
    printf '\t.text\n\t%s\n' "$1" > t.s
    rm -f t.o
    # shellcheck disable=SC2086
    "$AS" --64 $march -o t.o t.s 2> err
}

# reads SOURCE WHAT: the disassembly of SOURCE has WHAT in it.
reads() {
    cases=$((cases + 1))
    if ! assemble "$1"; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    objdump -d t.o | tail -n +8 > dis
    grep -qF -- "$2" dis || { echo "FAIL [$1]: disassembled as: $(sed 's/^[^\t]*\t[^\t]*\t//' dis | tr '\n' ' ')"; fail=1; }
}

# is SOURCE BYTES
is() {
    cases=$((cases + 1))
    if ! assemble "$1"; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    got=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$got" = "$2" ] || { echo "FAIL [$1]: $got, and GNU as: $2"; fail=1; }
}

# The address is the one that was written.
reads 'vpaddd 64(%rax),%zmm1,%zmm2'      '0x40(%rax)'
reads 'vpaddd 8(%rax),%zmm1,%zmm2'       '0x8(%rax)'
reads 'vpaddd 128(%rax),%zmm1,%zmm2'     '0x80(%rax)'
reads 'vpaddd -64(%rax),%zmm1,%zmm2'     '-0x40(%rax)'
reads 'vpaddd 8128(%rax),%zmm1,%zmm2'    '0x1fc0(%rax)'
reads 'vaddps 4(%rax),%zmm1,%zmm2'       '0x4(%rax)'
reads 'vaddpd 127(%rbx,%rcx,8),%zmm1,%zmm2' '0x7f(%rbx,%rcx,8)'
reads 'vmovdqa64 64(%rax),%zmm1'         '0x40(%rax)'
reads 'vmovdqu8 1(%rax),%zmm1'           '0x1(%rax)'
reads 'vpaddd 8(%rax),%zmm1,%zmm2{%k1}'  '0x8(%rax)'
reads 'vextractf64x4 $1,%zmm1,32(%rax)'  '0x20(%rax)'
reads 'vbroadcastss 4(%rax),%zmm1'       '0x4(%rax)'
reads 'vpsllq $1,64(%rax),%zmm1'         '0x40(%rax)'
reads 'vaddps 4(%rax){1to16},%zmm1,%zmm2'  '0x4(%rax)'
reads 'vaddps 64(%rax){1to16},%zmm1,%zmm2' '0x40(%rax)'
reads 'vaddpd 8(%rax){1to8},%zmm1,%zmm2'   '0x8(%rax)'
reads 'vaddps 6(%rax){1to16},%zmm1,%zmm2'  '0x6(%rax)'

# Where the form is GNU's, the bytes are.
is 'vaddps 4(%rax){1to16},%zmm1,%zmm2'   62f17458585001
is 'vaddps 64(%rax){1to16},%zmm1,%zmm2'  62f17458585010
is 'vaddpd 8(%rax){1to8},%zmm1,%zmm2'    62f1f558585001
is 'vpaddd 8(%rax),%zmm1,%zmm2'          62f17548fe9008000000
is 'vpaddd 8192(%rax),%zmm1,%zmm2'       62f17548fe9000200000
is 'vaddps 4(%rax),%zmm1,%zmm2'          62f17448589004000000
is 'vmovdqu8 1(%rax),%zmm1'              62f17f486f8801000000
is 'vpaddd 0(%rbp),%zmm1,%zmm2'          62f17548fe5500
is 'vpaddd (%rax),%zmm1,%zmm2'           62f17548fe10

# The shifts by a number.
is 'vpsllq $1,%zmm2,%zmm1'               62f1f54873f201
is 'vpsrlq $2,%zmm3,%zmm4'               62f1dd4873d302
is 'vpslldq $1,%zmm2,%zmm1'              62f1754873fa01
is 'vpsrldq $3,%zmm5,%zmm6'              62f14d4873dd03
is 'vpsllq $1,(%rax),%zmm1'              62f1f548733001
is 'vpsrlq $7,(%rbx),%zmm9'              62f1b548731307
reads 'vpsllq $1,%zmm2,%zmm1'            'vpsllq $0x1,%zmm2,%zmm1'

[ "$fail" -eq 0 ] && echo "ok: EVEX displacements and shifts ($cases cases)"
exit "$fail"
