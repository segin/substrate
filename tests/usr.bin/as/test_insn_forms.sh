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

[ "$fail" -eq 0 ] && echo "ok: instruction forms ($cases cases)"
exit "$fail"
