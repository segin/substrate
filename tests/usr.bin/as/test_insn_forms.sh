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

# VIA's PadLock: no operand, a fixed ModRM byte, and a rep prefix that
# is part of each but xstore.  montmul and xstore-rng were two bytes in
# 32-bit code -- 0F A6 and 0F A7, neither the prefix nor the ModRM --
# and unknown in 64-bit; the others were unknown in both.
t "32 64" f30fa6c0       'montmul'
t "32 64" f30fa6c8       'xsha1'
t "32 64" f30fa6d0       'xsha256'
t "32 64" 0fa7c0         'xstore'
t "32 64" 0fa7c0         'xstore-rng'
t "32 64" 0fa7c0         'xstorerng'
t "32 64" f30fa7c8       'xcrypt-ecb'
t "32 64" f30fa7c8       'xcryptecb'
t "32 64" f30fa7d0       'xcrypt-cbc'
t "32 64" f30fa7d0       'xcryptcbc'
t "32 64" f30fa7d8       'xcrypt-ctr'
t "32 64" f30fa7d8       'xcryptctr'
t "32 64" f30fa7e0       'xcrypt-cfb'
t "32 64" f30fa7e0       'xcryptcfb'
t "32 64" f30fa7e8       'xcrypt-ofb'
t "32 64" f30fa7e8       'xcryptofb'
t "32 64" f30fa6c0       'rep montmul'
t "32 64" f30fa7c0       'rep xstore'
t "32 64" f30fa6c8       'repz xsha1'
t "32 64" refused        'montmul %eax'

# The user-mode wait instructions.  tpause and umwait take a 32-bit
# register, and %edx and %eax may be written after it; umonitor takes
# an address in a register, with 67 where its width is not the mode's.
# `umonitor %cx` had no 67 in 32-bit code, so it was `umonitor %ecx`;
# 64-bit code had none of the three, and 32-bit code no umwait and
# tpause of %eax alone.
t "32 64" 660faef1       'tpause %ecx'
t "32 64" 660faef0       'tpause %eax'
t "32 64" 660faef7       'tpause %edi'
t "32 64" 660faef1       'tpause %ecx, %edx, %eax'
t "64"    66410faef1     'tpause %r9d'
t "32 64" f20faef1       'umwait %ecx'
t "32 64" f20faef3       'umwait %ebx'
t "32 64" f20faef1       'umwait %ecx, %edx, %eax'
t "64"    f2410faef2     'umwait %r10d'
t "32"    67f30faef1     'umonitor %cx'
t "32"    f30faef1       'umonitor %ecx'
t "32"    f30faef0       'umonitor %eax'
t "64"    67f30faef1     'umonitor %ecx'
t "64"    67f30faef0     'umonitor %eax'
t "64"    f30faef1       'umonitor %rcx'
t "64"    f3410faef1     'umonitor %r9'
t "64"    67f3410faef1   'umonitor %r9d'
t "32 64" refused        'tpause'
t "32 64" refused        'tpause %cx'
t "64"    refused        'tpause %rcx'
t "32 64" refused        'tpause %ecx, %eax, %edx'
t "64"    refused        'umonitor %cx'
t "32"    refused        'umonitor %rcx'
t "32 64" refused        'umonitor %cl'
t "32 64" refused        'umonitor (%eax)'

# The byte of a shuffle may be written as a negative number, which GNU
# takes for these and for no other SSE immediate.  32-bit code refused
# it, with "unsupported mnemonic".
t "32 64" 660f70d1ff     'pshufd $-1, %xmm1, %xmm2'
t "32 64" 660f70d180     'pshufd $-128, %xmm1, %xmm2'
t "32 64" 660f70d1ff     'pshufd $255, %xmm1, %xmm2'
t "32 64" f30f70d1ff     'pshufhw $-1, %xmm1, %xmm2'
t "32 64" f20f70d1ff     'pshuflw $-1, %xmm1, %xmm2'
t "32 64" 0f70d1ff       'pshufw $-1, %mm1, %mm2'
t "32 64" 0fc6d1ff       'shufps $-1, %xmm1, %xmm2'
t "32 64" 660fc6d1ff     'shufpd $-1, %xmm1, %xmm2'
t "32"    refused        'pshufd $256, %xmm1, %xmm2'
t "32"    refused        'pshufd $-129, %xmm1, %xmm2'
t "32"    refused        'cmpps $-1, %xmm1, %xmm2'

# %ah, %ch, %dh and %bh with an absolute or an index-only address were
# encoded as %al, %cl, %dl and %bl in 32-bit code: `movb %ah, 0x1234`
# stored %al.
t "32"    882534120000   'movb %ah, 0x1234'
t "32"    8a2d34120000   'movb 0x1234, %ch'
t "32"    88348500000000 'movb %dh, (,%eax,4)'
t "32"    003c5d10000000 'addb %bh, 0x10(,%ebx,2)'
t "32"    882500000000   'movb %ah, sym'
t "32"    382c4d00000000 'cmpb %ch, sym(,%ecx,2)'
t "32"    863d34120000   'xchgb %bh, 0x1234'
t "32"    843534120000   'testb %dh, 0x1234'
t "32"    8820           'movb %ah, (%eax)'
t "32"    88644b04       'movb %ah, 4(%ebx,%ecx,2)'
t "32"    881c8500000000 'movb %bl, (,%eax,4)'
t "32"    00ee           'addb %ch, %dh'
t "64"    88242534120000 'movb %ah, 0x1234'
t "64"    88348500000000 'movb %dh, (,%rax,4)'
t "64"    8820           'movb %ah, (%rax)'
t "64"    0038           'addb %bh, (%rax)'
t "64"    0fb6c4         'movzbl %ah, %eax'

# %spl, %bpl, %sil and %dil are the codes of %ah to %bh with a REX
# prefix, which they must have though no bit of it is set.  Most
# instructions had none: `movb $1, %sil` was `movb $1, %dh`.
t "64"    408820         'movb %spl, (%rax)'
t "64"    408a28         'movb (%rax), %bpl'
t "64"    40287308       'subb %sil, 8(%rbx)'
t "64"    40b601         'movb $1, %sil'
t "64"    40f6c701       'testb $1, %dil'
t "64"    4080fc03       'cmpb $3, %spl'
t "64"    400fb6c6       'movzbl %sil, %eax'
t "64"    400fbecf       'movsbl %dil, %ecx'
t "64"    66400fb6c5     'movzbw %bpl, %ax'
t "64"    480fbec6       'movsbq %sil, %rax'
t "64"    400f95c6       'setne %sil'
t "64"    400f94c7       'sete %dil'
t "64"    4086f0         'xchg %sil, %al'
t "64"    408638         'xchgb %dil, (%rax)'
t "64"    40fec7         'incb %dil'
t "64"    40fece         'decb %sil'
t "64"    40f6dc         'negb %spl'
t "64"    40f6d5         'notb %bpl'
t "64"    40f6e6         'mulb %sil'
t "64"    40f6f7         'divb %dil'
t "64"    40d0e6         'shlb $1, %sil'
t "64"    40d2ef         'shrb %cl, %dil'
t "64"    40c0fd03       'sarb $3, %bpl'
t "64"    4080ce01       'orb $1, %sil'
t "64"    4080d400       'adcb $0, %spl'
t "64"    4018eb         'sbbb %bpl, %bl'
t "64"    4084f6         'testb %sil, %sil'
t "64"    400fb030       'cmpxchgb %sil, (%rax)'
t "64"    400fc0f8       'xaddb %dil, %al'
t "64"    8808           'movb %cl, (%rax)'

# And %ah to %bh cannot be in an instruction that has a REX for any
# reason: there they would be the other four.  These were assembled.
t "64"    refused        'movb %ah, %sil'
t "64"    refused        'movb %sil, %ah'
t "64"    refused        'movb %ah, %r8b'
t "64"    refused        'movb %r8b, %ch'
t "64"    refused        'addb %ch, (%r8)'
t "64"    refused        'movb %bh, (%rax,%r9)'
t "64"    refused        'movzbl %ah, %r8d'
t "64"    refused        'movzbq %ah, %rax'
t "64"    refused        'cmpb %dh, %r10b'
t "64"    refused        'testb %bh, %sil'

# 32-bit code has no REX, and so none of the registers that need one.
# They were written as the registers their low three bits name.
t "32"    refused        'movb %sil, %al'
t "32"    refused        'movb %bpl, (%eax)'
t "32"    refused        'setne %dil'
t "32"    refused        'movb %r8b, %al'
t "32"    refused        'movl %r8d, %eax'
t "32"    refused        'addw %r9w, %ax'
t "32"    refused        'movq %rax, %rbx'
t "32"    54             'pushl %esp'
t "32"    0f6fec         'movq %mm4, %mm5'
t "32"    660f6eee       'movd %esi, %xmm5'
t "32"    d9cd           'fxch %st(5)'
t "32"    0f20e6         'movl %cr4, %esi'

# %r12 is an index register like any other: it has %rsp's low three
# bits and a REX.X to tell it apart.  It was refused, as %rsp.  And the
# stack pointer, which is no index, was refused by the main encoders
# and silently left out of the address by the others.
t "64"    42890420       'movl %eax,(%rax,%r12)'
t "64"    428b0c20       'movl (%rax,%r12),%ecx'
t "64"    428b0c60       'movl (%rax,%r12,2),%ecx'
t "64"    4a8b44e708     'movq 8(%rdi,%r12,8),%rax'
t "64"    4a8d0423       'leaq (%rbx,%r12),%rax'
t "64"    428b04a500000000 'movl (,%r12,4),%eax'
t "64"    43884c2500     'movb %cl, (%r13,%r12)'
t "64"    4a0104e500000000 'addq %rax, sym(,%r12,8)'
t "64"    f2430f1004e4   'movsd (%r12,%r12,8), %xmm0'
t "64"    66420f38000420 'pshufb (%rax,%r12), %xmm0'
t "64"    f3420fb804a0   'popcnt (%rax,%r12,4), %eax'
t "64"    42ff442404     'incl 4(%rsp,%r12)'
t "64"    418b0c04       'movl (%r12,%rax),%ecx'
t "64"    428b0c28       'movl (%rax,%r13),%ecx'
t "32"    8d0c04         'leal (%esp,%eax), %ecx'
t "64"    8b442408       'movl 8(%rsp), %eax'
t "64"    refused        'movl (%rax,%rsp),%ecx'
t "64"    refused        'movl (,%rsp,2),%ecx'
t "64"    refused        'pshufb (%rax,%rsp), %xmm0'
t "64"    refused        'popcnt (%rax,%rsp), %eax'
t "32"    refused        'movl (%eax,%esp),%ecx'
t "32"    refused        'pshufb (%eax,%esp), %xmm0'
t "32"    refused        'movw (%bx,%sp), %ax'

# Between two registers a move, and each of the eight arithmetic
# instructions, has two encodings: the destination in ModRM.rm or in
# ModRM.reg.  GNU as writes the first.  The second was written here --
# for every move, and in 64-bit code for the arithmetic too: the same
# instruction, and not the bytes an object is compared with GNU's by.
t "32 64" 89c3           'movl %eax, %ebx'
t "32 64" 88c1           'movb %al, %cl'
t "32 64" 88e3           'movb %ah, %bl'
t "32 64" 6689e0         'movw %sp, %ax'
t "32 64" 89f7           'movl %esi, %edi'
t "64"    4889d7         'movq %rdx, %rdi'
t "64"    4589e7         'movl %r12d, %r15d'
t "64"    4088f0         'movb %sil, %al'
t "64"    4088c7         'movb %al, %dil'
t "64"    664189dd       'movw %bx, %r13w'
t "32 64" 01c3           'addl %eax, %ebx'
t "32 64" 00c0           'addb %al, %al'
t "32 64" 6629ca         'subw %cx, %dx'
t "32 64" 31c0           'xorl %eax, %eax'
t "32 64" 39ca           'cmpl %ecx, %edx'
t "32 64" 08df           'orb %bl, %bh'
t "32 64" 21fe           'andl %edi, %esi'
t "32 64" 11c3           'adcl %eax, %ebx'
t "32 64" 19c3           'sbbl %eax, %ebx'
t "64"    4801c0         'addq %rax, %rax'
t "64"    4d31c1         'xorq %r8, %r9'
t "64"    4038f7         'cmpb %sil, %dil'
t "64"    00ee           'addb %ch, %dh'
t "32 64" 85c3           'testl %eax, %ebx'
t "32 64" 87d9           'xchgl %ebx, %ecx'
t "32"    8b18           'movl (%eax), %ebx'
t "32"    8918           'movl %ebx, (%eax)'
t "64"    480318         'addq (%rax), %rbx'
t "64"    480118         'addq %rbx, (%rax)'

# Instructions whose name says their size, and that were encoded
# without the prefix that makes it so: cbtw was cwtl, and extended %ax
# into %eax where it is to extend %al into %ax; cwtd was cltd; pushaw
# and popaw moved eight registers of 32 bits; iretw and lretw popped a
# 32-bit frame; `movw %ds, %si` cleared the top of %esi; `pushw %fs`
# pushed four bytes.
t "32 64" 6698           'cbtw'
t "32 64" 6698           'cbw'
t "32 64" 98             'cwtl'
t "32 64" 98             'cwde'
t "32 64" 6699           'cwtd'
t "32 64" 6699           'cwd'
t "32 64" 99             'cltd'
t "32 64" 99             'cdq'
t "64"    4898           'cltq'
t "64"    4899           'cqto'
t "32"    60             'pusha'
t "32"    60             'pushal'
t "32"    6660           'pushaw'
t "32"    61             'popa'
t "32"    61             'popal'
t "32"    6661           'popaw'
t "32 64" 669c           'pushfw'
t "32 64" 669d           'popfw'
t "32"    9c             'pushfl'
t "32 64" cf             'iret'
t "32 64" 66cf           'iretw'
t "64"    48cf           'iretq'
t "32 64" cb             'lret'
t "32 64" 66cb           'lretw'
t "32 64" 66ca0800       'lretw $8'
t "64"    48cb           'lretq'
t "32 64" 66c3           'retw'
t "32 64" 66c20400       'retw $4'
t "32 64" 660f44d8       'cmovew %ax, %bx'
t "32 64" 660fc1c3       'xaddw %ax, %bx'
t "32"    66ffd0         'callw *%ax'
t "32"    66ffe3         'jmpw *%bx'
t "32"    66ff10         'callw *(%eax)'
t "32"    668d0b         'leaw (%ebx), %cx'
t "32 64" 66c8080000     'enterw $8, $0'
t "32 64" 66c9           'leavew'
t "32 64" 6650           'pushw %ax'
t "32 64" 665b           'popw %bx'
t "32 64" 668cde         'movw %ds, %si'
t "32 64" 668cde         'mov %ds, %si'
t "32 64" 668cc0         'mov %es, %ax'
t "64"    66418ce1       'movw %fs, %r9w'
t "32 64" 8cde           'movl %ds, %esi'
t "32 64" 8cde           'mov %ds, %esi'
t "64"    8cde           'movq %ds, %rsi'
t "32 64" 8ede           'movw %si, %ds'
t "32 64" 8ec0           'movw %ax, %es'
t "32"    8c00           'movw %es, (%eax)'
t "32 64" 660fa0         'pushw %fs'
t "32 64" 660fa9         'popw %gs'
t "32"    661f           'popw %ds'
t "32"    660e           'pushw %cs'
t "32"    0fa0           'pushl %fs'
t "64"    0fa0           'pushq %fs'
t "32 64" 0fa0           'push %fs'

# With no suffix an instruction is of the width of its registers, and
# that is so of 64-bit ones: `mov %rsp, %rbp` was `mov %esp, %ebp`, and
# so with every instruction below down to the stack ones.  Those, and
# the few after them, take a 64-bit register without REX.W.
t "64"    4889c1         'mov %rax,%rcx'
t "64"    4889e5         'mov %rsp,%rbp'
t "64"    488903         'mov %rax,(%rbx)'
t "64"    488b03         'mov (%rbx),%rax'
t "64"    48c7c001000000 'mov $1,%rax'
t "64"    4d89c1         'mov %r8,%r9'
t "64"    480103         'add %rax,(%rbx)'
t "64"    4801c3         'add %rax,%rbx'
t "64"    4883c408       'add $8,%rsp'
t "64"    4883ec08       'sub $8,%rsp'
t "64"    4883e4f0       'and $-16,%rsp'
t "64"    483903         'cmp %rax,(%rbx)'
t "64"    4831c0         'xor %rax,%rax'
t "64"    4809fe         'or %rdi,%rsi'
t "64"    4811c3         'adc %rax,%rbx'
t "64"    4883da00       'sbb $0,%rdx'
t "64"    4885c0         'test %rax,%rax'
t "64"    48ffc0         'inc %rax'
t "64"    48ffc9         'dec %rcx'
t "64"    48f7d8         'neg %rax'
t "64"    48f7d2         'not %rdx'
t "64"    48f7e3         'mul %rbx'
t "64"    480fafc3       'imul %rbx,%rax'
t "64"    48d1e3         'sal $1,%rbx'
t "64"    48d1e3         'shl $1,%rbx'
t "64"    48d3e8         'shr %cl,%rax'
t "64"    480fa4c304     'shld $4,%rax,%rbx'
t "64"    488d18         'lea (%rax),%rbx'
t "64"    488d7c2408     'lea 8(%rsp),%rdi'
t "64"    480fc1c3       'xadd %rax,%rbx'
t "64"    480fa3c3       'bt %rax,%rbx'
t "64"    480fbcd8       'bsf %rax,%rbx'
t "64"    f3480fb8d8     'popcnt %rax,%rbx'
t "64"    480f44d8       'cmove %rax,%rbx'
t "64"    480fb6c0       'movzx %al,%rax'
t "64"    480fb7c8       'movzx %ax,%rcx'
t "64"    4863d8         'movsx %eax,%rbx'
t "64"    480fc8         'bswap %rax'
t "64"    490fcc         'bswap %r12'
t "64"    480f38f018     'movbe (%rax),%rbx'
t "64"    f2480f2cc0     'cvttsd2si %xmm0,%rax'
t "64"    f2480f2ac0     'cvtsi2sd %rax,%xmm0'
t "64"    66480f6ec0     'movq %rax,%xmm0'
t "64"    480f01e2       'smsw %rdx'
t "64"    480f1fc0       'nop %rax'
t "64"    48b88877665544332211 'movabs $0x1122334455667788,%rax'
t "64"    50             'push %rax'
t "64"    5b             'pop %rbx'
t "64"    4154           'push %r12'
t "64"    415d           'pop %r13'
t "64"    ffe0           'jmp *%rax'
t "64"    ffd3           'call *%rbx'
t "64"    0f20d8         'mov %cr3,%rax'
t "64"    0f22d8         'mov %rax,%cr3'
t "64"    0f23f8         'mov %rax,%db7'
t "64"    8ce0           'mov %fs,%rax'
t "64"    8ee0           'mov %rax,%fs'
t "64"    0f00c8         'str %rax'
t "64"    0f00c1         'sldt %rcx'
t "64"    f3480faec0     'rdfsbase %rax'
t "64"    0f50c0         'movmskps %xmm0,%rax'
t "64"    660fd7c8       'pmovmskb %xmm0,%rcx'
t "64"    8d18           'lea (%rax),%ebx'
t "64"    89c3           'mov %eax,%ebx'
t "64"    6601c3         'add %ax,%bx'

# movd and movq between a general register, %mm, %xmm and memory.  The
# %mm registers and %rax to %rdi are both 64 bits wide and numbered 0 to
# 7, and nothing told them apart: `movd %xmm0, %rax` was taken for a
# move into %mm0 and came out as 48 0f 6e c0, which is `movq %rax,
# %mm0`.  pextrw into a register was written in its SSE4.1 form, which
# from %mm is not the instruction.
t "32 64" 660f6ec0       'movd %eax, %xmm0'
t "32 64" 660f7ec0       'movd %xmm0, %eax'
t "32 64" 0f6ec0         'movd %eax, %mm0'
t "32 64" 0f7ec0         'movd %mm0, %eax'
t "32"    660f6e08       'movd (%eax), %xmm1'
t "32"    660f7e08       'movd %xmm1, (%eax)'
t "32"    0f6e08         'movd (%eax), %mm1'
t "32"    0f7e08         'movd %mm1, (%eax)'
t "64"    66480f6ec0     'movd %rax, %xmm0'
t "64"    66480f7ec0     'movd %xmm0, %rax'
t "64"    480f6ec0       'movd %rax, %mm0'
t "64"    480f7ec0       'movd %mm0, %rax'
t "64"    66450f6ec8     'movd %r8d, %xmm9'
t "64"    66450f7ec8     'movd %xmm9, %r8d'
t "64"    66480f6ec0     'movq %rax, %xmm0'
t "64"    66480f7ec0     'movq %xmm0, %rax'
t "64"    480f6ec0       'movq %rax, %mm0'
t "64"    480f7ec0       'movq %mm0, %rax'
t "64"    664d0f6ed1     'movq %r9, %xmm10'
t "64"    664d0f7ed1     'movq %xmm10, %r9'
t "32 64" f30f7ed1       'movq %xmm1, %xmm2'
t "32 64" 0f6fd1         'movq %mm1, %mm2'
t "32"    f30f7e08       'movq (%eax), %xmm1'
t "32"    660fd608       'movq %xmm1, (%eax)'
t "32"    0f6f08         'movq (%eax), %mm1'
t "32"    0f7f08         'movq %mm1, (%eax)'
t "64"    f3440f7e08     'movq (%rax), %xmm9'
t "64"    66440fd608     'movq %xmm9, (%rax)'
t "32 64" f20fd6d1       'movdq2q %xmm1, %mm2'
t "32 64" f30fd6d1       'movq2dq %mm1, %xmm2'
t "32 64" 660fc5c001     'pextrw $1, %xmm0, %eax'
t "32 64" 0fc5c001       'pextrw $1, %mm0, %eax'
t "64"    660fc5c001     'pextrw $1, %xmm0, %rax'
t "64"    66450fc5d101   'pextrw $1, %xmm9, %r10d'
t "64"    0fc5cd03       'pextrw $3, %mm5, %ecx'
t "32"    660f3a150001   'pextrw $1, %xmm0, (%eax)'
t "64"    660f3a150007   'pextrw $7, %xmm0, (%rax)'
t "32 64" 660fc4c001     'pinsrw $1, %eax, %xmm0'
t "32"    660fc40001     'pinsrw $1, (%eax), %xmm0'

# A displacement written as the number 0 is no displacement: `0(%eax)`
# is `(%eax)`.  It was kept as a byte of zero.  And (%ebp), (%rbp) and
# (%r13), which cannot be without one, have one byte of zero and had
# four.  The padding a disassembler prints, `nopw 0x0(%rax,%rax,1)`, is
# among these.
t "32"    8b08           'movl 0x0(%eax), %ecx'
t "32"    8b08           'movl 1-1(%eax), %ecx'
t "32"    8b0424         'movl 0(%esp), %eax'
t "64"    8b0424         'movl 0(%rsp), %eax'
t "64"    418b0424       'movl 0(%r12), %eax'
t "32"    8d36           'leal 0(%esi), %esi'
t "32"    dd00           'fldl 0(%eax)'
t "32"    0f2808         'movaps 0(%eax), %xmm1'
t "32"    660f380000     'pshufb 0(%eax), %xmm0'
t "32"    660f1f0400     'nopw 0(%eax,%eax,1)'
t "64"    660f1f0400     'nopw 0(%rax,%rax,1)'
t "64"    0f1f00         'nopl 0(%rax)'
t "32"    8b4500         'movl (%ebp), %eax'
t "32"    8b4500         'movl 0(%ebp), %eax'
t "64"    8b4500         'movl (%rbp), %eax'
t "64"    418b4500       'movl (%r13), %eax'
t "32"    8b4c4500       'movl (%ebp,%eax,2), %ecx'
t "64"    8b4c4500       'movl 0(%rbp,%rax,2), %ecx'
t "64"    418b4c0500     'movl (%r13,%rax), %ecx'
t "64"    418b440500     'movl 0(%r13,%rax), %eax'
t "32"    dd4500         'fldl (%ebp)'
t "32"    0f284d00       'movaps 0(%ebp), %xmm1'
t "64"    f30fb84500     'popcnt 0(%rbp), %eax'
t "64"    660f38004500   'pshufb 0(%rbp), %xmm0'
t "64"    66410f38004500 'pshufb (%r13), %xmm0'
t "32"    660f38004500   'pshufb (%ebp), %xmm0'
t "32"    67668b07       'movw 0(%bx), %ax'
t "32"    67668b4600     'movw (%bp), %ax'
t "32"    67668b4600     'movw 0(%bp), %ax'
t "32"    67668b02       'movw (%bp,%si), %ax'
t "32"    67668b02       'movw 0(%bp,%si), %ax'
t "32"    8b1c8500000000 'movl 0(,%eax,4), %ebx'

[ "$fail" -eq 0 ] && echo "ok: instruction forms ($cases cases)"
exit "$fail"
