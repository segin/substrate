#!/bin/sh
# The prefixes of an instruction that one of the extension encoders
# writes -- SSE3 to SSE4.2, popcnt and its kin, BMI, AVX, FMA, AVX-512.
# Those encoders write from the mandatory prefix on and are not told of
# a segment override or of lock, and their callers did not write either:
# `lock cmpxchg16b (%rbx)` was assembled with no lock, and so was not
# atomic, and `popcnt %fs:(%rbx), %eax` and `vaddps %gs:(%rbx), ...`
# read from %ds.  No message was given.
#
# Each line is held to the bytes GNU as 2.46 writes.  The 64-bit cases
# are assembled with -march=x86-64-v3, without which this assembler
# refuses AVX and BMI.
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

# e MODE WANT SOURCE
e() {
    cases=$((cases + 1))
    if assemble "$1" "$3"; then
        objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
        got=$(od -An -v -tx1 text.bin | tr -d ' \n')
    else
        got="refused: $(head -1 out)"
    fi
    if [ "$got" != "$2" ]; then
        echo "FAIL $1 [$3]: $got, and GNU as: $2"
        fail=1
    fi
}

# r MODE OFFSET SOURCE: where the one relocation is, which must move
# with the bytes when a prefix goes in front of them.
r() {
    cases=$((cases + 1))
    if ! assemble "$1" "$3"; then
        echo "FAIL $1 [$3]: refused: $(head -1 out)"
        fail=1
        return
    fi
    got=$(readelf -rW t.o | awk '$1 ~ /^[0-9a-f]+$/ && $3 ~ /^R_/ { print $1; exit }' | sed 's/^0*//')
    if [ "$got" != "$2" ]; then
        echo "FAIL $1 [$3]: relocation at ${got:-nowhere}, and GNU as: $2"
        fail=1
    fi
}

# lock.
e 64 f0480fc70b         'lock cmpxchg16b (%rbx)'
e 64 65f0480fc74b08     'lock cmpxchg16b %gs:8(%rbx)'
e 64 64480fc70b         'cmpxchg16b %fs:(%rbx)'
e 64 65f04b0fc74c6c08   'lock cmpxchg16b %gs:8(%r12,%r13,2)'

# A segment, 64-bit code.
e 64 64f30fb803         'popcnt %fs:(%rbx), %eax'
e 64 65f34d0fb84810     'popcnt %gs:16(%r8), %r9'
e 64 64f30fbd03         'lzcnt %fs:(%rbx), %eax'
e 64 64f30fbc03         'tzcnt %fs:(%rbx), %eax'
e 64 640f38f003         'movbe %fs:(%rbx), %eax'
e 64 64f20f7c03         'haddps %fs:(%rbx), %xmm0'
e 64 64f20f1203         'movddup %fs:(%rbx), %xmm0'
e 64 64f20ff003         'lddqu %fs:(%rbx), %xmm0'
e 64 64660f380003       'pshufb %fs:(%rbx), %xmm0'
e 64 64660f381c03       'pabsb %fs:(%rbx), %xmm0'
e 64 65660f384003       'pmulld %gs:(%rbx), %xmm0'
e 64 64660f3a160301     'pextrd $1, %xmm0, %fs:(%rbx)'
e 64 65660f3a201b01     'pinsrb $1, %gs:(%rbx), %xmm3'
e 64 64660f3a080301     'roundps $1, %fs:(%rbx), %xmm0'
e 64 64660f383703       'pcmpgtq %fs:(%rbx), %xmm0'
e 64 65660f38374b08     'pcmpgtq %gs:8(%rbx), %xmm1'
e 64 64660f3a630301     'pcmpistri $1, %fs:(%rbx), %xmm0'
e 64 64c4e278f31b       'blsi %fs:(%rbx), %eax'
e 64 64c4e278f20b       'andn %fs:(%rbx), %eax, %ecx'
e 64 64c4e278f50b       'bzhi %eax, %fs:(%rbx), %ecx'
e 64 65c5fc580b         'vaddps %gs:(%rbx), %ymm0, %ymm1'
e 64 26c5fc580b         'vaddps %es:(%rbx), %ymm0, %ymm1'
e 64 64c5fdfe0b         'vpaddd %fs:(%rbx), %ymm0, %ymm1'
e 64 64c4e27db80b       'vfmadd231ps %fs:(%rbx), %ymm0, %ymm1'

# The same, 32-bit code.
e 32 64f20f7c03         'haddps %fs:(%ebx), %xmm0'
e 32 64f20ff003         'lddqu %fs:(%ebx), %xmm0'
e 32 64660f380003       'pshufb %fs:(%ebx), %xmm0'
e 32 65660f381c4304     'pabsb %gs:4(%ebx), %xmm0'
e 32 65660f384003       'pmulld %gs:(%ebx), %xmm0'
e 32 64660f3a160301     'pextrd $1, %xmm0, %fs:(%ebx)'
e 32 64660f3a080301     'roundps $1, %fs:(%ebx), %xmm0'
e 32 64660f383703       'pcmpgtq %fs:(%ebx), %xmm0'
e 32 64660f3a630301     'pcmpistri $1, %fs:(%ebx), %xmm0'
e 32 64f30fb803         'popcnt %fs:(%ebx), %eax'
e 32 64c4e278f20b       'andn %fs:(%ebx), %eax, %ecx'
e 32 65c4e278f50b       'bzhi %eax, %gs:(%ebx), %ecx'

# And none where none is written.
e 64 660f383703         'pcmpgtq (%rbx), %xmm0'
e 64 f30fb8d8           'popcnt %eax, %ebx'
e 64 c5fc580b           'vaddps (%rbx), %ymm0, %ymm1'
e 32 660f380003         'pshufb (%ebx), %xmm0'

# The relocation follows the bytes.
r 64 5 'popcnt %fs:sym(%rip), %eax'
r 64 5 'lock cmpxchg16b sym(%rip)'
r 64 6 'pshufb %gs:sym(%rip), %xmm0'
r 64 5 'vaddps %fs:sym(%rip), %ymm0, %ymm1'
r 64 6 'pinsrb $1, %gs:sym(%rip), %xmm3'
r 32 5 'popcnt %fs:sym, %eax'

[ "$fail" -eq 0 ] && echo "ok: prefixes of the extension encoders ($cases cases)"
exit "$fail"
