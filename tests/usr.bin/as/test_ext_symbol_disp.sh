#!/bin/sh
# A symbol in the displacement of a memory operand that has a base or an
# index register, on an instruction that one of the extension encoders
# writes -- SSE3 to SSE4.2, popcnt and its kin, cmpxchg16b, BMI, AVX,
# FMA.  The symbol's value stands as 0 until the linker has it, and each
# of those encoders sized the displacement by that 0: one byte.  The
# relocation is four, and was written over the end of the instruction as
# it then stood, the opcode with it: `pshufb sym(%ebx), %xmm0` came out
# as 66 0f 00 00 00 00, and `vaddps table(%rbx), %ymm0, %ymm1` as c5 00
# 00 00 00.  No message was given.
#
# Each line is held to GNU as 2.46: the bytes, and the place and type of
# the relocation (`-` for none).  The 64-bit cases are assembled with
# -march=x86-64-v3, without which this assembler refuses AVX and BMI.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# s MODE BYTES RELOCATION SOURCE
s() {
    cases=$((cases + 1))
    printf '\t.text\n\t%s\n' "$4" > t.s
    rm -f t.o
    if [ "$1" = 64 ]; then
        "$AS" -64 -march=x86-64-v3 -o t.o t.s > out 2>&1
    else
        "$AS" -32 -o t.o t.s > out 2>&1
    fi
    if [ $? -ne 0 ]; then
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

# A base register, 64-bit code.
s 64 660f38008300000000     5:R_X86_64_32S 'pshufb sym(%rbx), %xmm0'
s 64 660f38408300000000     5:R_X86_64_32S 'pmulld sym(%rbx), %xmm0'
s 64 f20f7c8300000000       4:R_X86_64_32S 'haddps sym(%rbx), %xmm0'
s 64 f20f128300000000       4:R_X86_64_32S 'movddup sym(%rbx), %xmm0'
s 64 660f38378300000000     5:R_X86_64_32S 'pcmpgtq sym(%rbx), %xmm0'
s 64 f30fb88300000000       4:R_X86_64_32S 'popcnt sym(%rbx), %eax'
s 64 f30fbd8300000000       4:R_X86_64_32S 'lzcnt sym(%rbx), %eax'
s 64 f30fbc8300000000       4:R_X86_64_32S 'tzcnt sym(%rbx), %eax'
s 64 0f38f08300000000       4:R_X86_64_32S 'movbe sym(%rbx), %eax'
s 64 480fc78b00000000       4:R_X86_64_32S 'cmpxchg16b sym(%rbx)'
s 64 f20f38f08300000000     5:R_X86_64_32S 'crc32b sym(%rbx), %eax'
s 64 c4e278f28b00000000     5:R_X86_64_32S 'andn sym(%rbx), %eax, %ecx'
s 64 c4e278f39b00000000     5:R_X86_64_32S 'blsi sym(%rbx), %eax'
s 64 c4e278f58b00000000     5:R_X86_64_32S 'bzhi %eax, sym(%rbx), %ecx'
s 64 c5fc588b00000000       4:R_X86_64_32S 'vaddps sym(%rbx), %ymm0, %ymm1'
s 64 c5fdfe8b00000000       4:R_X86_64_32S 'vpaddd sym(%rbx), %ymm0, %ymm1'
s 64 c4e27db88b00000000     5:R_X86_64_32S 'vfmadd231ps sym(%rbx), %ymm0, %ymm1'

# With an immediate after the displacement.
s 64 660f3a16830000000001   5:R_X86_64_32S 'pextrd $1, %xmm0, sym(%rbx)'
s 64 660f3a08830000000001   5:R_X86_64_32S 'roundps $1, sym(%rbx), %xmm0'
s 64 660f3a209b0000000001   5:R_X86_64_32S 'pinsrb $1, sym(%rbx), %xmm3'
s 64 660f3a63830000000001   5:R_X86_64_32S 'pcmpistri $1, sym(%rbx), %xmm0'

# An index, an addend, the registers that need SIB or REX.
s 64 660f3800848b00000000   6:R_X86_64_32S 'pshufb sym+8(%rbx,%rcx,4), %xmm0'
s 64 c5fdfe0cc500000000     5:R_X86_64_32S 'vpaddd sym(,%rax,8), %ymm0, %ymm1'
s 64 c4413c588c2400000000   6:R_X86_64_32S 'vaddps sym+16(%r12), %ymm8, %ymm9'
s 64 660f381c8500000000     5:R_X86_64_32S 'pabsb sym(%rbp), %xmm0'
s 64 660f3800842400000000   6:R_X86_64_32S 'pshufb sym(%rsp), %xmm0'

# 32-bit code.
s 32 660f38008300000000     5:R_386_32 'pshufb sym(%ebx), %xmm0'
s 32 660f38408300000000     5:R_386_32 'pmulld sym(%ebx), %xmm0'
s 32 f20f7c8300000000       4:R_386_32 'haddps sym(%ebx), %xmm0'
s 32 f20ff08300000000       4:R_386_32 'lddqu sym(%ebx), %xmm0'
s 32 660f38378300000000     5:R_386_32 'pcmpgtq sym(%ebx), %xmm0'
s 32 f30fb88300000000       4:R_386_32 'popcnt sym(%ebx), %eax'
s 32 f20f38f08300000000     5:R_386_32 'crc32b sym(%ebx), %eax'
s 32 c4e278f28b00000000     5:R_386_32 'andn sym(%ebx), %eax, %ecx'
s 32 c4e278f58b00000000     5:R_386_32 'bzhi %eax, sym(%ebx), %ecx'
s 32 660f3a16830000000001   5:R_386_32 'pextrd $1, %xmm0, sym(%ebx)'
s 32 660f3a08830000000001   5:R_386_32 'roundps $1, sym(%ebx), %xmm0'
s 32 660f3a209b0000000001   5:R_386_32 'pinsrb $1, sym(%ebx), %xmm3'
s 32 660f3800844b04000000   6:R_386_32 'pshufb sym+4(%ebx,%ecx,2), %xmm0'
s 32 660f38008500000000     5:R_386_32 'pshufb sym(%ebp), %xmm0'
s 32 660f3800842400000000   6:R_386_32 'pshufb sym(%esp), %xmm0'

# A displacement that is a number is still as short as it can be, and
# one from %rip was right and stays so.
s 64 660f380003             -              'pshufb (%rbx), %xmm0'
s 64 660f38004308           -              'pshufb 8(%rbx), %xmm0'
s 64 660f38008300100000     -              'pshufb 0x1000(%rbx), %xmm0'
s 32 660f38004308           -              'pshufb 8(%ebx), %xmm0'
s 64 c5fc580d00000000       4:R_X86_64_PC32 'vaddps sym(%rip), %ymm0, %ymm1'

# An absolute address.  32-bit code writes it with ModRM alone; 64-bit
# code gives that form to %rip-relative addressing and needs a SIB byte.
# These encoders wrote the SIB form in both: right in 32-bit code too,
# and a byte longer than GNU's.
s 32 660f38000500000000     5:R_386_32     'pshufb sym, %xmm0'
s 32 660f38000534120000     -              'pshufb 0x1234, %xmm0'
s 32 64660f38000500000000   6:R_386_32     'pshufb %fs:sym, %xmm0'
s 32 f20f7c0504000000       4:R_386_32     'haddps sym+4, %xmm0'
s 32 660f38370500000000     5:R_386_32     'pcmpgtq sym, %xmm0'
s 32 660f3a16050000000001   5:R_386_32     'pextrd $1, %xmm0, sym'
s 32 660f3a08050000000001   5:R_386_32     'roundps $1, sym, %xmm0'
s 32 f30fb80500000000       4:R_386_32     'popcnt sym, %eax'
s 32 f30fbd0500000000       4:R_386_32     'lzcnt sym, %eax'
s 32 f20f38f00500000000     5:R_386_32     'crc32b sym, %eax'
s 32 c4e278f20d00000000     5:R_386_32     'andn sym, %eax, %ecx'
s 32 c5f0580500000000       4:R_386_32     'vaddps sym, %xmm1, %xmm0'
s 32 c5f4580590909090       -              'vaddps 0x90909090, %ymm1, %ymm0'
s 32 c4e27db80d00000000     5:R_386_32     'vfmadd231ps sym, %ymm0, %ymm1'
s 32 62f17448580500000000   6:R_386_32     'vaddps sym, %zmm1, %zmm0'
s 64 660f3800042500000000   6:R_X86_64_32S 'pshufb sym, %xmm0'
s 64 c5f458042500000000     5:R_X86_64_32S 'vaddps sym, %ymm1, %ymm0'
s 64 f30fb8042500000000     5:R_X86_64_32S 'popcnt sym, %eax'

[ "$fail" -eq 0 ] && echo "ok: a symbol's displacement in the extension encoders ($cases cases)"
exit "$fail"
