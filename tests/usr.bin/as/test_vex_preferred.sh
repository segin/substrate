#!/bin/sh
# An instruction that has both a VEX and an EVEX encoding is VEX, unless
# something written can only be EVEX's: a %zmm register, a register past
# 15, a mask, zeroing, a broadcast.  The EVEX form of a 128- or 256-bit
# instruction is AVX-512VL, and a processor that has AVX and no AVX-512
# faults on it.
#
# The EVEX encoders were tried first.  So in 32-bit code, and in 64-bit
# code at -march=x86-64-v4, `vaddps %ymm0, %ymm1, %ymm2` was 62 f1 74 28
# 58 d0 and not c5 f4 58 d0: every AVX, AVX2 and FMA instruction came out
# as its AVX-512 one, with no message.
#
# Each line is held to the bytes GNU as 2.46 writes.  The 64-bit cases
# are assembled at x86-64-v4, where both encodings are allowed.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# v MODES WANT SOURCE
v() {
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
            got="refused: $(head -1 out)"
        fi
        if [ "$got" != "$2" ]; then
            echo "FAIL $mode [$3]: $got, and GNU as: $2"
            fail=1
        fi
    done
}

# AVX.
v "32 64" c5f058d0         'vaddps %xmm0, %xmm1, %xmm2'
v "32 64" c5f458d0         'vaddps %ymm0, %ymm1, %ymm2'
v "32"    c5f55810         'vaddpd (%eax), %ymm1, %ymm2'
v "64"    c5f55810         'vaddpd (%rax), %ymm1, %ymm2'
v "32 64" c5f25cd0         'vsubss %xmm0, %xmm1, %xmm2'
v "32"    c5f3595008       'vmulsd 8(%eax), %xmm1, %xmm2'
v "64"    c5f3595008       'vmulsd 8(%rax), %xmm1, %xmm2'
v "32 64" c5e457db         'vxorps %ymm3, %ymm3, %ymm3'
v "32 64" c5f155d0         'vandnpd %xmm0, %xmm1, %xmm2'
v "32 64" c4e37506d001     'vperm2f128 $1, %ymm0, %ymm1, %ymm2'
v "32 64" c4e37518d001     'vinsertf128 $1, %xmm0, %ymm1, %ymm2'
v "32"    c4e27d1808       'vbroadcastss (%eax), %ymm1'
v "64"    c4e27d1808       'vbroadcastss (%rax), %ymm1'
v "32 64" c5f877           'vzeroupper'

# AVX2.
v "32 64" c5f5fed0         'vpaddd %ymm0, %ymm1, %ymm2'
v "32 64" c5f1d4d0         'vpaddq %xmm0, %xmm1, %xmm2'
v "32 64" c5f5efd0         'vpxor %ymm0, %ymm1, %ymm2'
v "32 64" c5f1dbd0         'vpand %xmm0, %xmm1, %xmm2'
v "32 64" c4e27500d0       'vpshufb %ymm0, %ymm1, %ymm2'
v "32 64" c4e27140d0       'vpmulld %xmm0, %xmm1, %xmm2'
v "32 64" c5f576d0         'vpcmpeqd %ymm0, %ymm1, %ymm2'
v "32 64" c4e27d58c8       'vpbroadcastd %xmm0, %ymm1'

# FMA, and the VEX forms of AES, PCLMULQDQ and GFNI.
v "32 64" c4e275b8d0       'vfmadd231ps %ymm0, %ymm1, %ymm2'
v "32 64" c4e2f199d0       'vfmadd132sd %xmm0, %xmm1, %xmm2'
v "32"    c4e2f5ae10       'vfnmsub213pd (%eax), %ymm1, %ymm2'
v "64"    c4e2f5ae10       'vfnmsub213pd (%rax), %ymm1, %ymm2'
v "32 64" c4e271dcd0       'vaesenc %xmm0, %xmm1, %xmm2'
v "32 64" c4e37144d001     'vpclmulqdq $1, %xmm0, %xmm1, %xmm2'
v "32 64" c4e271cfd0       'vgf2p8mulb %xmm0, %xmm1, %xmm2'

# Registers 8 to 15 are VEX's too.
v "64"    c4413458d0       'vaddps %ymm8, %ymm9, %ymm10'
v "64"    c5f45810         'vaddps (%rax), %ymm1, %ymm2'
v "64"    c44175fe7c2408   'vpaddd 8(%r12), %ymm1, %ymm15'
v "64"    c4c275b8d0       'vfmadd231ps %ymm8, %ymm1, %ymm2'
v "64"    c44101efff       'vpxor %xmm15, %xmm15, %xmm15'

# What only EVEX can say is EVEX still: 512 bits, a mask, zeroing, a
# broadcast, an instruction with no VEX form.
v "32 64" 62f1744858d0     'vaddps %zmm0, %zmm1, %zmm2'
v "32 64" 62f17548fed0     'vpaddd %zmm0, %zmm1, %zmm2'
v "32 64" 62f1742958d0     'vaddps %ymm0, %ymm1, %ymm2{%k1}'
v "32 64" 62f1758afed0     'vpaddd %xmm0, %xmm1, %xmm2{%k2}{z}'
v "64"    62f1f5385810     'vaddpd (%rax){1to4}, %ymm1, %ymm2'
v "32 64" 62f3752825d001   'vpternlogd $1, %ymm0, %ymm1, %ymm2'

# And these were AVX-512 first, with a VEX form added later that GNU
# writes only when asked: they stay EVEX.
v "32 64" 62f2750850d0     'vpdpbusd %xmm0, %xmm1, %xmm2'
v "32 64" 62f2f528b4d0     'vpmadd52luq %ymm0, %ymm1, %ymm2'

[ "$fail" -eq 0 ] && echo "ok: VEX where an instruction has both encodings ($cases cases)"
exit "$fail"
