#!/bin/sh
# Memory operands of the instructions outside the core set (AS-T-177,
# 178, 194, 198, 199).
#
# - A symbol's displacement is a field of 32 bits, whatever the
#   instruction: the SIMD, x87, VEX and BMI encoders each had their own
#   ModRM code, and some wrote a byte for it.
# - An absolute address in 64-bit code has R_X86_64_32S on its
#   displacement.
# - A segment override is written before any of them.
# - A vector register is the index of a gather, which was refused for
#   having "mixed addressing widths", and of nothing else.
# - The base and the index of an address are of one width.
# - A subtraction or division that pops leaves its result in %st(i):
#   `fsubp %st(1), %st` was assembled.
#
# Each case is the bytes of .text and its relocations, which are GNU
# as's; GNU as passes this test.  In 64-bit mode this assembler is asked
# for -march=x86-64-v4, without which it refuses AVX-512.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
gnu=0
"$AS" --version 2> /dev/null | grep -q '^GNU assembler' && gnu=1

assemble() {
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    march=
    [ "$1" = 64 ] && [ "$gnu" = 0 ] && march=-march=x86-64-v4
    # shellcheck disable=SC2086
    "$AS" "--$1" $march -o t.o t.s 2> err
}

# is MODE SOURCE BYTES RELOCATIONS
is() {
    cases=$((cases + 1))
    if ! assemble "$1" "$2"; then
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
    if assemble "$1" "$2"; then
        echo "FAIL --$1 [$2]: assembled"; fail=1
    fi
}

# A symbol's displacement.
is 32 'movaps sym(%ebx),%xmm0|movdqa sym(%ebp),%xmm1|flds sym(%ebx)|fnstcw sym(%ebx)' \
    0f288300000000660f6f8d00000000d98300000000d9bb00000000 \
    'R_386_32@11:sym R_386_32@17:sym R_386_32@3:sym R_386_32@b:sym'
is 64 'haddps foo(%rdx),%xmm2|vmulsd foo+4(%rdx),%xmm2,%xmm3|andn foo+4(%rdx),%ebx,%ecx' \
    f20f7c9200000000c5eb599a00000000c4e260f28a00000000 \
    'R_X86_64_32S@15:foo+4 R_X86_64_32S@4:foo+0 R_X86_64_32S@c:foo+4'
# An absolute address.
is 64 'pandn foo,%xmm2|popcnt foo,%ebx|xsave foo|vpunpckldq foo,%ymm2,%ymm3' \
    660fdf142500000000f30fb81c25000000000fae242500000000c5ed621c2500000000 \
    'R_X86_64_32S@16:foo+0 R_X86_64_32S@1f:foo+0 R_X86_64_32S@5:foo+0 R_X86_64_32S@e:foo+0'
# A segment override, on one instruction of each extension.
is 64 'pshufb %fs:(%rax),%xmm2|haddps %fs:(%rax),%xmm1|pabsb %gs:(%rax),%xmm1|pmulld %fs:(%rax),%xmm1|pcmpgtq %gs:(%rax),%xmm1' \
    64660f38001064f20f7c0865660f381c0864660f38400865660f383708 ''
is 64 'andn %fs:(%rax),%ebx,%ecx|bzhi %eax,%gs:(%rbx),%ecx|vaddps %fs:(%rax),%ymm1,%ymm2|vpaddd %gs:(%rax),%ymm1,%ymm2|vfmadd132ps %fs:(%rax),%ymm1,%ymm2' \
    64c4e260f20865c4e278f50b64c5f4581065c5f5fe1064c4e2759810 ''
is 64 'vaddps %fs:(%rax),%zmm1,%zmm2|vpaddd %gs:8(%rax),%zmm1,%zmm2{%k1}' \
    6462f1744858106562f17549fe9008000000 ''

# A vector register for an index: in a gather.
is 64 'vgatherdps %ymm1,(%rax,%ymm2,4),%ymm3|vpgatherdd %xmm1,(%rax,%xmm2,2),%xmm3|vgatherqpd %ymm1,8(%rax,%ymm2,8),%ymm3' \
    c4e275921c90c4e271901c50c4e2f5935cd008 ''
is 64 'vgatherdps (%rax,%zmm2,4),%zmm3{%k1}' 62f27d49921c90 ''
is 32 'vgatherdps %ymm1,(%eax,%ymm2,4),%ymm3' c4e275921c90 ''
# And in nothing else; nor a base of one width with an index of another.
refused 64 'movl (%rax,%xmm1),%ecx'
refused 64 'vaddps (%rax,%ymm2,4),%ymm1,%ymm3'
refused 64 'movl (%eax,%rbx),%ecx'
refused 64 'movl sym(%rip,%rax,2),%eax'

# The x87 arithmetic that pops: the orders GNU takes, and those it does
# not.
is 32 'faddp %st,%st(1)|faddp|fsubp %st,%st(2)|fsubrp|fmulp|fdivp|fdivrp|faddp %st(1),%st|fmulp %st(2),%st' \
    dec1dec1dee2dee9dec9def1def9dec1deca ''
refused 32 'fsubp %st(1),%st'
refused 32 'fsubp %st(2),%st'
refused 32 'fsubrp %st(1),%st'
refused 32 'fdivp %st(1),%st'
refused 32 'fdivrp %st(2),%st'

[ "$fail" -eq 0 ] && echo "ok: addressing forms ($cases cases)"
exit "$fail"
