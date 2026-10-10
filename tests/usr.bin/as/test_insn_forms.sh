#!/bin/sh
# Instructions that were assembled to the bytes of another, or of
# another form of themselves, with no complaint: each is held here to
# what GNU as 2.46 writes for it, or to GNU's refusal.
#
# A line is the modes it is tried in, the bytes (or `refused`), and the
# source.  The cases are grouped by the fault that each group had.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# t MODES WANT SOURCE
t() {
    for mode in $1; do
        cases=$((cases + 1))
        printf '\t.text\n\t%s\n' "$3" > t.s
        rm -f t.o
        if "$AS" "-$mode" -o t.o t.s > out 2>&1; then
            objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
            got=$(od -An -v -tx1 text.bin | tr -d ' \n')
        else
            got=refused
        fi
        if [ "$got" != "$2" ]; then
            echo "FAIL $mode [$3]: $got, and GNU as: $2"
            fail=1
        fi
    done
}

# ti MODES WANT SOURCE: the same, the source being in Intel syntax.
ti() {
    for mode in $1; do
        cases=$((cases + 1))
        printf '\t.text\n\t.intel_syntax noprefix\n\t%s\n' "$3" > t.s
        rm -f t.o
        if "$AS" "-$mode" -o t.o t.s > out 2>&1; then
            objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
            got=$(od -An -v -tx1 text.bin | tr -d ' \n')
        else
            got=refused
        fi
        if [ "$got" != "$2" ]; then
            echo "FAIL $mode Intel [$3]: $got, and GNU as: $2"
            fail=1
        fi
    done
}

# enter: the frame's size and then the nesting level, in that order in
# both syntaxes -- it has no destination to be written last.  The two
# had been exchanged, so `enter $16, $0` made a frame of no bytes at
# nesting level sixteen.
t "32 64" c8100000    'enter $16, $0'
t "32 64" c8341203    'enter $0x1234, $3'
t "32 64" c8000000    'enter $0, $0'
t "32 64" c8ffff1f    'enter $0xffff, $31'
t "32 64" c8ffff00    'enter $-1, $0'
t "32 64" 66c8080001  'enterw $8, $1'
t "32"    c8080001    'enterl $8, $1'
t "64"    c8080001    'enterq $8, $1'
t "32 64" refused     'enter $0x10000, $0'
t "32 64" refused     'enter $8, $256'
t "32 64" refused     'enter $8'

# The same in Intel syntax, which was right and must stay so.
for mode in 32 64; do
    cases=$((cases + 1))
    printf '\t.intel_syntax noprefix\n\t.text\n\tenter 16, 0\n\tenter 0x1234, 3\n' > t.s
    "$AS" "-$mode" -o t.o t.s > out 2>&1 || { echo "FAIL $mode Intel enter: refused: $(head -1 out)"; fail=1; continue; }
    objcopy -O binary --only-section=.text t.o text.bin
    got=$(od -An -v -tx1 text.bin | tr -d ' \n')
    [ "$got" = c8100000c8341203 ] || { echo "FAIL $mode Intel enter: $got"; fail=1; }
done

# With no suffix, the registers say what size the instruction is.  None
# of these had the size of its registers: every one was assembled as the
# 32-bit instruction -- `mov %al, %cl` as `mov %eax, %ecx`, `neg %ax` as
# `neg %eax` -- and ran as that.
#
# A byte register makes it the byte form.
t "32 64" fec0      'inc %al'
t "32"    f6d8      'neg %al'
t "32"    f6d3      'not %bl'
t "32"    f6e1      'mul %cl'
t "32"    f6f2      'div %dl'
t "32"    f6e9      'imul %cl'
t "32"    84c3      'test %al, %bl'
t "32"    08c4      'or %al, %ah'
t "32 64" 18c2      'sbb %al, %dl'
t "32"    0008      'add %cl, (%eax)'
t "64"    0008      'add %cl, (%rax)'
t "32 64" 0fc0c3    'xadd %al, %bl'
t "64"    d0e1      'shl $1, %cl'
t "32 64" d2c0      'rol %cl, %al'
t "32 64" c0cb02    'ror $2, %bl'
t "32 64" ec        'in %dx, %al'
t "32 64" ee        'out %al, %dx'
t "32 64" e460      'in $0x60, %al'
t "32 64" e680      'out %al, $0x80'
# A 16-bit register makes it the 16-bit form, with the prefix 66.
t "32 64" 66f7d8    'neg %ax'
t "32 64" 66f7d3    'not %bx'
t "32 64" 66f7e1    'mul %cx'
t "32 64" 66f7f2    'div %dx'
t "32"    6650      'push %ax'
t "32"    665b      'pop %bx'
t "32"    6640      'inc %ax'
t "64"    66ffc0    'inc %ax'
t "32"    6648      'dec %ax'
t "64"    66ffc8    'dec %ax'
t "32"    6601c3    'add %ax, %bx'
t "32"    6639c3    'cmp %ax, %bx'
t "32"    6621d9    'and %bx, %cx'
t "32"    6629d6    'sub %dx, %si'
t "32 64" 6685c3    'test %ax, %bx'
t "32 64" 660fc1c3  'xadd %ax, %bx'
t "32 64" 660f44d8  'cmove %ax, %bx'
t "32 64" 660fafd8  'imul %ax, %bx'
t "32 64" 66b83412  'mov $0x1234, %ax'
t "64"    6683c301  'add $1, %bx'
t "64"    6683d001  'adc $1, %ax'
t "32 64" 66d1e8    'shr %ax'
t "32 64" 66d1e0    'shl %ax'
t "64"    66d1fa    'sar $1, %dx'
t "32 64" 66c1fb03  'sar $3, %bx'
t "32 64" 66d3e3    'shl %cl, %bx'
t "32 64" 66d3c2    'rol %cl, %dx'
t "32 64" 66ffd0    'call *%ax'
t "32 64" 66ffe3    'jmp *%bx'
t "32 64" 66ed      'in %dx, %ax'
t "32 64" 66ef      'out %ax, %dx'
t "32 64" 660f01e0  'smsw %ax'
t "32 64" 660f02d8  'lar %ax, %bx'
t "64"    660fa3c3  'bt %ax, %bx'
t "64"    660fbcd8  'bsf %ax, %bx'
t "64"    660fb1c3  'cmpxchg %ax, %bx'
t "64"    660fa5c3  'shld %cl, %ax, %bx'
t "64"    660fbae803 'bts $3, %ax'
# The register that says is the destination; where that is memory, the
# source.
t "32"    668903    'mov %ax, (%ebx)'
t "32"    668b03    'mov (%ebx), %ax'
t "32"    668d03    'lea (%ebx), %ax'
t "32 64" 660fbed8  'movsx %al, %bx'
t "32 64" 0fb6c0    'movzx %al, %eax'
t "32 64" 0fbec0    'movsx %al, %eax'
# And the registers that do not say: the count of a shift, the port of
# in and out, a segment register that is moved to, the byte of setcc,
# and the 16-bit registers of instructions that are not 16-bit.
t "32"    d323      'shl %cl, (%ebx)'
t "64"    d323      'shl %cl, (%rbx)'
t "32 64" d3e0      'shl %cl, %eax'
t "32 64" 0fa5c3    'shld %cl, %eax, %ebx'
t "32 64" ed        'in %dx, %eax'
t "32 64" 8ed8      'mov %ax, %ds'
t "32 64" 0f95c0    'setne %al'
t "32 64" dfe0      'fnstsw %ax'
t "32 64" 0f00d0    'lldt %ax'
t "32 64" 0f00d9    'ltr %cx'
t "32 64" 0f02d8    'lar %ax, %ebx'

# xchg of bytes is 86, by its suffix or by its registers; it was 87,
# which exchanges 32 bits.
t "32 64" 86c3      'xchgb %al, %bl'
t "32 64" 86c3      'xchg %al, %bl'
t "32 64" 86ce      'xchg %cl, %dh'
t "32"    8608      'xchgb %cl, (%eax)'
t "32"    8608      'xchg %cl, (%eax)'
t "64"    8608      'xchgb %cl, (%rax)'
t "64"    4486c0    'xchg %r8b, %al'
t "64"    44860f    'xchgb %r9b, (%rdi)'
t "32 64" 6687ca    'xchg %cx, %dx'
t "32 64" 6687ca    'xchgw %cx, %dx'
t "32 64" 87ca      'xchgl %ecx, %edx'
# An exchange is the same with its memory operand written first, which
# was refused.
t "32"    871b      'xchgl (%ebx), %ebx'
t "32"    8613      'xchg (%ebx), %dl'
t "32"    8608      'xchgb (%eax), %cl'
t "32"    66870b    'xchg (%ebx), %cx'
t "64"    48870f    'xchg (%rdi), %rcx'
t "64"    44860f    'xchg (%rdi), %r9b'
t "64"    668718    'xchgw (%rax), %bx'

# Each shift and rotate by %cl, at each width: the count is not what is
# shifted.
for op in rol:0 ror:1 rcl:2 rcr:3 shl:4 sal:4 shr:5 sar:7; do
    name=${op%:*}
    ext=${op#*:}
    t "32 64" "66d3$(printf '%02x' $((0xc0 + ext * 8)))" "${name}w %cl, %ax"
    t "32 64" "d3$(printf '%02x' $((0xc0 + ext * 8)))"   "$name %cl, %eax"
    t "32 64" "d2$(printf '%02x' $((0xc3 + ext * 8)))"   "$name %cl, %bl"
    t "32"    "d2$(printf '%02x' $((ext * 8)))"          "${name}b %cl, (%eax)"
    t "32"    "d3$(printf '%02x' $((ext * 8)))"          "${name}l %cl, (%eax)"
done

# movsx and movzx: the source's width is its register's, or the suffix's
# -- which for these two names the source and not the instruction -- and
# the instruction's size is the destination's.  A 16-bit source was
# taken for a byte (`movzx %ax, %eax` was `movzx %al, %eax`), and a
# suffix was taken for the instruction's size.
t "32 64" 0fbec0     'movsx %al, %eax'
t "32 64" 0fbfc0     'movsx %ax, %eax'
t "32 64" 660fbec0   'movsx %al, %ax'
t "32 64" 0fb6c0     'movzx %al, %eax'
t "32 64" 0fb7c0     'movzx %ax, %eax'
t "32 64" 660fb6c0   'movzx %al, %ax'
t "32 64" 660fb7c0   'movzx %ax, %ax'
t "32"    0fb608     'movzx (%eax), %ecx'
t "32"    660fbe08   'movsx (%eax), %cx'
t "32 64" 0fbfd8     'movsxw %ax, %ebx'
t "32 64" 0fbed8     'movsxb %al, %ebx'
t "32 64" 0fb7d8     'movzxw %ax, %ebx'
t "32 64" 660fb6d8   'movzxb %al, %bx'
t "32"    0fb608     'movzxb (%eax), %ecx'
t "32"    0fb708     'movzxw (%eax), %ecx'
t "32"    0fbf18     'movsxw (%eax), %ebx'
t "32"    0fbe18     'movsxb (%eax), %ebx'
t "32"    660fbe18   'movsxb (%eax), %bx'
# The AT&T names, which were right and stay so.
t "32 64" 0fbec0     'movsbl %al, %eax'
t "32 64" 660fbec0   'movsbw %al, %ax'
t "32 64" 0fbfc0     'movswl %ax, %eax'
t "32 64" 0fb6c0     'movzbl %al, %eax'
t "32 64" 660fb6c0   'movzbw %al, %ax'
t "32 64" 0fb7c0     'movzwl %ax, %eax'
t "32"    0fb608     'movzbl (%eax), %ecx'
t "32"    0fb708     'movzwl (%eax), %ecx'
t "32"    0fbe08     'movsbl (%eax), %ecx'
t "32"    0fbf08     'movswl (%eax), %ecx'
t "32"    660fb608   'movzbw (%eax), %cx'
t "32"    660fbe08   'movsbw (%eax), %cx'
# To 64 bits; and from 32, which is movsxd and has no zero-extending
# twin.
t "64"    480fbec0   'movsx %al, %rax'
t "64"    480fbfc0   'movsx %ax, %rax'
t "64"    480fb6c0   'movzx %al, %rax'
t "64"    480fb7c0   'movzx %ax, %rax'
t "64"    400fbec6   'movsx %sil, %eax'
t "64"    400fb6cf   'movzx %dil, %ecx'
t "64"    480fbec0   'movsbq %al, %rax'
t "64"    480fbfc0   'movswq %ax, %rax'
t "64"    480fb6c0   'movzbq %al, %rax'
t "64"    480fb7c0   'movzwq %ax, %rax'
t "64"    480fbe07   'movsbq (%rdi), %rax'
t "64"    480fb70f   'movzwq (%rdi), %rcx'
t "64"    480fbfd8   'movsxw %ax, %rbx'
t "64"    480fbed8   'movsxb %al, %rbx'
t "64"    480fb618   'movzxb (%rax), %rbx'
t "64"    4863c0     'movslq %eax, %rax'
t "64"    4863c0     'movsxd %eax, %rax'
t "64"    4863d8     'movsxl %eax, %rbx'
t "64"    486318     'movsxl (%rax), %rbx'
t "64"    4863d8     'movsx %eax, %rbx'
t "64"    4963c0     'movsx %r8d, %rax'
t "64"    63c0       'movsx %eax, %eax'
t "32"    refused    'movsx %eax, %eax'
t "32 64" refused    'movzx %eax, %eax'
t "64"    refused    'movzx %eax, %rax'

# ud2b is not ud2: it is the other undefined opcode, 0F B9.
t "32 64" 0f0b  'ud2'
t "32 64" 0f0b  'ud2a'
t "32 64" 0fb9  'ud2b'

# test has no form with two immediates.
t "32 64" refused    'test $1, $2'
t "32 64" refused    'testb $1, $2'
t "32 64" refused    'testb $0, $0'
t "32 64" refused    'testw $1, $2'
t "32 64" refused    'testl $1, $2'
t "32 64" f6c101     'testb $1, %cl'
t "32"    f60301     'testb $1, (%ebx)'
t "64"    f60301     'testb $1, (%rbx)'
t "32 64" f7c102000000 'testl $2, %ecx'

# nop with an operand is the long NOP, 0F 1F /0, of the operand's size.
# `nop (%eax)` and `nop %eax` were the one byte 90 in 32-bit code, and
# nopl and nopw of a register were refused.
t "32 64" 90             'nop'
t "32 64" 0f1fc0         'nop %eax'
t "32 64" 660f1fc0       'nop %ax'
t "32 64" 0f1fc1         'nop %ecx'
t "64"    480f1fc0       'nop %rax'
t "64"    490f1fc1       'nop %r9'
t "64"    410f1fc1       'nop %r9d'
t "32 64" 660f1fc0       'nopw %ax'
t "32 64" 0f1fc0         'nopl %eax'
t "32 64" 660f1fc1       'nopw %cx'
t "32 64" 0f1fc2         'nopl %edx'
t "64"    480f1fc0       'nopq %rax'
t "32"    0f1f00         'nop (%eax)'
t "64"    0f1f00         'nop (%rax)'
t "32"    0f1f00         'nopl (%eax)'
t "32"    660f1f00       'nopw (%eax)'
t "64"    0f1f00         'nopl (%rax)'
t "64"    660f1f00       'nopw (%rax)'
t "64"    480f1f00       'nopq (%rax)'
t "32"    0f1f8078563412 'nopl 0x12345678(%eax)'
t "32"    0f1f445808     'nopl 8(%eax,%ebx,2)'
t "64"    0f1f8078563412 'nopl 0x12345678(%rax)'
t "64"    430f1f444808   'nopl 8(%r8,%r9,2)'
t "32"    0f1f4304       'nop 4(%ebx)'
t "64"    0f1f4304       'nop 4(%rbx)'
t "32 64" refused        'nopb %al'
t "32 64" refused        'nop %al'
t "32 64" refused        'nop $1'
t "32 64" refused        'nop %eax, %ebx'

# maskmovq and maskmovdqu: the mask is ModRM.rm and the register stored
# is ModRM.reg.  In 32-bit AT&T source the two had been exchanged, so
# the mask was stored under the data.
t "32 64" 0ff7d1         'maskmovq %mm1, %mm2'
t "32 64" 0ff7c7         'maskmovq %mm7, %mm0'
t "32 64" 660ff7d1       'maskmovdqu %xmm1, %xmm2'
t "32 64" 660ff7c7       'maskmovdqu %xmm7, %xmm0'
t "64"    66410ff7d1     'maskmovdqu %xmm9, %xmm2'
t "64"    66440ff7d1     'maskmovdqu %xmm1, %xmm10'
ti "32 64" 0ff7d1        'maskmovq mm2, mm1'
ti "32 64" 660ff7d1      'maskmovdqu xmm2, xmm1'
t "32 64" refused        'maskmovq (%eax), %mm2'
t "32 64" refused        'maskmovq %mm1, %xmm2'
t "32 64" refused        'maskmovdqu %mm1, %xmm2'
t "32 64" refused        'maskmovq %mm1'

# extrq and insertq: the length is the first immediate byte and the
# index the second, and AT&T writes the index first.  The bytes had been
# in the order written, extrq's ModRM.reg held the register where it is
# /0, a memory operand was assembled where there is no such form, and
# 64-bit code had neither instruction.
t "32 64" 660f78c10804   'extrq $4, $8, %xmm1'
t "32 64" 660f78c74000   'extrq $0, $64, %xmm7'
t "32 64" 660f78c000ff   'extrq $255, $0, %xmm0'
t "32 64" 660f79d1       'extrq %xmm1, %xmm2'
t "64"    66410f78c10804 'extrq $4, $8, %xmm9'
t "64"    66410f79d1     'extrq %xmm9, %xmm2'
t "64"    66440f79d1     'extrq %xmm1, %xmm10'
t "32 64" f20f78d10804   'insertq $4, $8, %xmm1, %xmm2'
t "32 64" f20f78c7013f   'insertq $63, $1, %xmm7, %xmm0'
t "32 64" f20f79d1       'insertq %xmm1, %xmm2'
t "64"    f2410f78d10804 'insertq $4, $8, %xmm9, %xmm2'
t "64"    f2440f78d10804 'insertq $4, $8, %xmm1, %xmm10'
t "64"    f2450f79d1     'insertq %xmm9, %xmm10'
ti "32 64" 660f78c10804  'extrq xmm1, 8, 4'
ti "32 64" f20f78d10804  'insertq xmm2, xmm1, 8, 4'
ti "32 64" 660f79d1      'extrq xmm2, xmm1'
t "32 64" refused        'extrq $256, $0, %xmm0'
t "32 64" refused        'extrq $-1, $0, %xmm0'
t "32 64" refused        'extrq (%eax), %xmm2'
t "32 64" refused        'extrq $4, %xmm1'
t "32"    refused        'extrq $4, $8, %xmm9'
t "32 64" refused        'insertq $4, $8, (%eax), %xmm2'
t "32 64" refused        'insertq $4, %xmm1, %xmm2'

# crc32: the source is of 8, 16, 32 or 64 bits -- its suffix's width, or
# its register's -- and the destination of 32, or of 64 for a source of
# 8 or 64.  In 32-bit code every source register was taken for 32 bits;
# in 64-bit code a suffix on a memory source was ignored, %ah was
# encoded as %al, %sil as %dh, and a segment override was dropped.
t "32 64" f20f38f0c0     'crc32 %al, %eax'
t "32 64" f20f38f0cb     'crc32 %bl, %ecx'
t "32 64" f20f38f0dc     'crc32 %ah, %ebx'
t "32 64" 66f20f38f1c0   'crc32 %ax, %eax'
t "32 64" 66f20f38f1d1   'crc32 %cx, %edx'
t "32 64" f20f38f1d8     'crc32 %eax, %ebx'
t "32 64" f20f38f0d8     'crc32b %al, %ebx'
t "32 64" 66f20f38f1d8   'crc32w %ax, %ebx'
t "32 64" f20f38f1d8     'crc32l %eax, %ebx'
t "32"    f20f38f018     'crc32b (%eax), %ebx'
t "32"    66f20f38f118   'crc32w (%eax), %ebx'
t "32"    f20f38f118     'crc32l (%eax), %ebx'
t "32"    f20f38f118     'crc32 (%eax), %ebx'
t "32"    64f20f38f018   'crc32b %fs:(%eax), %ebx'
t "32"    f20f38f01d00000000 'crc32b sym, %ebx'
t "64"    f20f38f018     'crc32b (%rax), %ebx'
t "64"    66f20f38f118   'crc32w (%rax), %ebx'
t "64"    f20f38f118     'crc32l (%rax), %ebx'
t "64"    f2480f38f118   'crc32q (%rax), %rbx'
t "64"    f20f38f118     'crc32 (%rax), %ebx'
t "64"    f2480f38f118   'crc32 (%rax), %rbx'
t "64"    66f2410f38f11c24 'crc32w (%r12), %ebx'
t "64"    64f20f38f018   'crc32b %fs:(%rax), %ebx'
t "64"    f20f38f01d00000000 'crc32b sym(%rip), %ebx'
t "64"    f2400f38f0de   'crc32 %sil, %ebx'
t "64"    f2480f38f0c8   'crc32 %al, %rcx'
t "64"    f2480f38f0c8   'crc32b %al, %rcx'
t "64"    f2480f38f008   'crc32b (%rax), %rcx'
t "64"    f2480f38f1d8   'crc32 %rax, %rbx'
t "64"    f2480f38f1d8   'crc32q %rax, %rbx'
t "64"    f2450f38f0c8   'crc32 %r8b, %r9d'
t "64"    66f2450f38f1c8 'crc32 %r8w, %r9d'
t "64"    f2450f38f1c8   'crc32 %r8d, %r9d'
t "64"    f24d0f38f1c8   'crc32 %r8, %r9'
ti "32 64" f20f38f0c3    'crc32 eax, bl'
ti "32 64" 66f20f38f1c1  'crc32 eax, cx'
ti "32"   f20f38f003     'crc32 eax, byte ptr [ebx]'
ti "32"   66f20f38f103   'crc32 eax, word ptr [ebx]'
ti "32"   f20f38f103     'crc32 eax, dword ptr [ebx]'
t "64"    refused        'crc32 %ah, %r9d'
t "32 64" refused        'crc32 %eax, %bx'
t "32 64" refused        'crc32 %eax, %bl'
t "64"    refused        'crc32 %ax, %rbx'
t "64"    refused        'crc32 %eax, %rbx'
t "64"    refused        'crc32q %rax, %ebx'
t "64"    refused        'crc32w (%rax), %rbx'
t "64"    refused        'crc32l (%rax), %rbx'
t "32"    refused        'crc32q (%eax), %ebx'
t "32 64" refused        'crc32b %ax, %ebx'
t "32 64" refused        'crc32 $1, %ebx'
t "32 64" refused        'crc32 %xmm0, %eax'
t "32 64" refused        'crc32 %eax, %xmm0'
t "32"    refused        'crc32 %es, %eax'

[ "$fail" -eq 0 ] && echo "ok: instruction forms ($cases cases)"
exit "$fail"
