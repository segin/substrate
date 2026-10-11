#!/bin/sh
# The shortest of the forms an instruction has, and five that were
# wrong (AS-T-186).
#
# Longer than they need be:
# - `addw $0xff80, %bx` and `addl $0xffffff80, %eax` add -128, which a
#   byte holds, and have the form with a byte of immediate; asked
#   whether the number as written was a byte, they took the long form.
#   So `imul` by such a number, and a push of one.
# - A segment that is the one the address has anyway -- %ds, or %ss
#   with the frame or stack pointer for its base -- is no prefix.
# - `pushfq`, `leaveq`, `callq *%rax`, `lgdtq`, and `lsl`, `lar` and
#   `rdpid` of a 64-bit register, had a REX.W that did nothing.
# - `fldl 0(%r8)` had a byte of displacement for its nothing.
#
# Wrong:
# - `jcxz` in 32-bit code with a target of another file had no
#   address-size prefix, and was `jecxz`: it tested the other
#   register.  So `jecxz` in 64-bit code, which was `jrcxz`.
# - `callw sym` was four zeros, the relocation of an address written
#   over the prefix and the opcode; `calll sym` had the relocation of an
#   address where a distance is wanted; `callw 1f` went three bytes
#   short of its label.
# - `enter $sym, $1` was four zeros likewise, the relocation over the
#   opcode.
#
# Each case is the bytes of .text and its relocations, which are GNU
# as's, and GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is MODE SOURCE BYTES RELOCATIONS: `|` in SOURCE is a newline; the
# relocations are type@offset:symbol+addend, sorted.
is() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    relocs=$(readelf -rW t.o | awk '/ R_/ { printf "%s@%x:%s%s\n", $3, strtonum("0x" $1), $5, $6 $7 }' |
             sort | tr '\n' ' ' | sed 's/ $//')
    [ "$bytes" = "$3" ] || { echo "FAIL --$1 [$2]: bytes $bytes, not $3"; fail=1; }
    [ "$relocs" = "$4" ] || { echo "FAIL --$1 [$2]: relocations [$relocs], not [$4]"; fail=1; }
}

# A number that is a signed byte at the width of what it is done to.
is 32 'addw $0xff80,%bx' 6683c380 ''
is 64 'addw $0xff80,%bx' 6683c380 ''
is 32 'addw $0xffff,%si' 6683c6ff ''
is 64 'addw $0xffff,%si' 6683c6ff ''
is 32 'addw $0xffffff80,%bx' 6683c380 ''
is 64 'addw $0xffffff80,%bx' 6683c380 ''
is 32 'addw $0xff80,%ax' 6683c080 ''
is 64 'addw $0xff80,%ax' 6683c080 ''
is 32 'addl $0xffffff80,%eax' 83c080 ''
is 64 'addl $0xffffff80,%eax' 83c080 ''
is 32 'addl $0xffffffff,%ebx' 83c3ff ''
is 64 'addl $0xffffffff,%ebx' 83c3ff ''
is 32 'addl $0xffffff80,sym' 83050000000080 'R_386_32@2:sym'
is 64 'addl $0xffffff80,sym' 8304250000000080 'R_X86_64_32S@3:sym+0'
is 32 'addw $0xffffffff,(%ebx)' 668303ff ''
is 64 'addw $0xffffffff,(%rbx)' 668303ff ''
is 32 'cmpw $0xff80,%bx' 6683fb80 ''
is 64 'cmpw $0xff80,%bx' 6683fb80 ''
is 32 'subl $0xffffff7f,%ebx' 81eb7fffffff ''
is 64 'subl $0xffffff7f,%ebx' 81eb7fffffff ''
is 32 'andl $0xffffffff,(%eax)' 8320ff ''
is 64 'andl $0xffffffff,(%rax)' 8320ff ''
is 32 'imulw $0xff80,%bx,%cx' 666bcb80 ''
is 64 'imulw $0xff80,%bx,%cx' 666bcb80 ''
is 32 'imull $0xffffff80,%ebx,%ecx' 6bcb80 ''
is 64 'imull $0xffffff80,%ebx,%ecx' 6bcb80 ''
is 32 'pushw $0xffff' 666aff ''
is 64 'pushw $0xffff' 666aff ''
is 32 'pushw $0xffffffff' 666aff ''
is 64 'pushw $0xffffffff' 666aff ''
is 32 'push $0xffffffff' 6aff ''
is 32 'pushl $0xffffff80' 6a80 ''
is 64 'addq $0xffffffffffffff80,%rbx' 4883c380 ''
is 64 'addw $0xff80,%r9w' 664183c180 ''
is 64 'addl $0xffffff80,sym(%rip)' 83050000000080 'R_X86_64_PC32@2:sym-5'
is 64 'imull $0xffffff80,(%rax),%ecx' 6b0880 ''

# And one that is not: too large for the width, or not negative in it.
is 32 'addw $0x80,%bx' 6681c38000 ''
is 64 'addw $0x80,%bx' 6681c38000 ''
is 32 'addl $0x80,%ebx' 81c380000000 ''
is 64 'addl $0x80,%ebx' 81c380000000 ''
is 32 'addw $0xff7f,%bx' 6681c37fff ''
is 64 'addw $0xff7f,%bx' 6681c37fff ''
is 32 'addw $0x10000,%ax' 66050000 ''
is 64 'addw $0x10000,%ax' 66050000 ''
is 32 'addw $0x7fffffff,%bx' 6681c3ffff ''
is 64 'addw $0x7fffffff,%bx' 6681c3ffff ''
is 32 'addw $0x80000000,%bx' 6681c30000 ''
is 64 'addw $0x80000000,%bx' 6681c30000 ''
is 32 'pushw $0x7fffffff' 6668ffff ''
is 64 'pushw $0x7fffffff' 6668ffff ''
is 32 'movw $0xff80,%bx' 66bb80ff ''
is 64 'movw $0xff80,%bx' 66bb80ff ''
is 32 'testw $0xff80,%bx' 66f7c380ff ''
is 64 'testw $0xff80,%bx' 66f7c380ff ''
is 32 'addb $0xff,%bl' 80c3ff ''
is 64 'addb $0xff,%bl' 80c3ff ''

# The segment an address has anyway is no prefix; another is one.
is 32 'movl %eax,%ds:sym' a300000000 'R_386_32@1:sym'
is 64 'movl %eax,%ds:sym' 89042500000000 'R_X86_64_32S@3:sym+0'
is 32 'movl %ds:sym,%ecx' 8b0d00000000 'R_386_32@2:sym'
is 64 'movl %ds:sym,%ecx' 8b0c2500000000 'R_X86_64_32S@3:sym+0'
is 32 'movl %ss:(%ebp),%ecx' 8b4d00 ''
is 64 'movl %ss:(%rbp),%ecx' 8b4d00 ''
is 32 'leal %ss:(%ebp),%edx' 8d5500 ''
is 64 'leal %ss:(%rbp),%edx' 8d5500 ''
is 32 'movl %ss:(%esp),%ecx' 8b0c24 ''
is 64 'movl %ss:(%rsp),%ecx' 8b0c24 ''
is 32 'movl %ds:4(%ebx,%ebp),%ecx' 8b4c2b04 ''
is 64 'movl %ds:4(%rbx,%rbp),%ecx' 8b4c2b04 ''
is 32 'movl %ss:4(%ebp,%ebx),%ecx' 8b4c1d04 ''
is 64 'movl %ss:4(%rbp,%rbx),%ecx' 8b4c1d04 ''
is 32 'movl %ds:(%ebp),%ecx' 3e8b4d00 ''
is 64 'movl %ds:(%rbp),%ecx' 3e8b4d00 ''
is 32 'movl %ss:(%ebx),%ecx' 368b0b ''
is 64 'movl %ss:(%rbx),%ecx' 368b0b ''
is 32 'movl %ss:4(%ebx,%ebp),%ecx' 368b4c2b04 ''
is 64 'movl %ss:4(%rbx,%rbp),%ecx' 368b4c2b04 ''
is 32 'movl %es:(%edi),%ecx' 268b0f ''
is 64 'movl %es:(%rdi),%ecx' 268b0f ''
is 32 'movl %fs:(%ebp),%ecx' 648b4d00 ''
is 64 'movl %fs:(%rbp),%ecx' 648b4d00 ''
is 32 'fldl %ds:(%eax)' dd00 ''
is 64 'fldl %ds:(%rax)' dd00 ''
is 32 'addps %ds:(%eax),%xmm0' 0f5800 ''
is 64 'addps %ds:(%rax),%xmm0' 0f5800 ''
is 32 'jmp *%ds:(%eax)' ff20 ''
is 64 'jmp *%ds:(%rax)' ff20 ''
is 32 'movw %ds:(%bp),%ax' 3e67668b4600 ''
is 32 'movw %ss:(%bp,%si),%ax' 67668b02 ''
is 32 'movw %ds:(%bx),%ax' 67668b07 ''
is 32 'movsl %ds:(%esi),%es:(%edi)' a5 ''
is 32 'lodsl %ds:(%esi),%eax' ad ''
is 32 'movsl %fs:(%esi),%es:(%edi)' 64a5 ''
is 64 'movl %ds:sym(%rip),%ecx' 8b0d00000000 'R_X86_64_PC32@2:sym-4'
is 64 'movl %cs:sym(%rip),%ecx' 2e8b0d00000000 'R_X86_64_PC32@3:sym-4'

# Of 64 bits with no REX.W to say so; and two that have it.
is 64 'pushfq' 9c ''
is 64 'popfq' 9d ''
is 64 'leaveq' c9 ''
is 64 'callq *%rax' ffd0 ''
is 64 'jmpq *%rax' ffe0 ''
is 64 'callq *(%rax)' ff10 ''
is 64 'callq *%r8' 41ffd0 ''
is 64 'jmpq *(%r8)' 41ff20 ''
is 64 'lgdtq (%rax)' 0f0110 ''
is 64 'lidtq (%rax)' 0f0118 ''
is 64 'sgdtq (%rax)' 0f0100 ''
is 64 'sidtq (%rax)' 0f0108 ''
is 64 'lsl %rax,%rbx' 0f03d8 ''
is 64 'lsl %ax,%rbx' 0f03d8 ''
is 64 'lsl (%rax),%rbx' 0f0318 ''
is 64 'lsl %rax,%r9' 440f03c8 ''
is 64 'lar %rax,%rbx' 0f02d8 ''
is 64 'lar (%r8),%r9' 450f0208 ''
is 64 'rdpid %rax' f30fc7f8 ''
is 64 'rdpid %r9' f3410fc7f9 ''
is 64 'retq' c3 ''
is 64 'pushq %rax' 50 ''
is 64 'lretq' 48cb ''
is 64 'iretq' 48cf ''

# A displacement of nothing is none, but for the frame pointer and %r13.
is 64 'fldl 0(%r8)' 41dd00 ''
is 64 'flds 0(%r9)' 41d901 ''
is 64 'fldl 0(%r12)' 41dd0424 ''
is 64 'fldl 0(%r13)' 41dd4500 ''
is 64 'fldl 0(%rax)' dd00 ''
is 64 'fldl 0(%rbp)' dd4500 ''
is 64 'fstpt 0(%r10)' 41db3a ''
is 64 'fldl 0(%r8,%r9)' 43dd0408 ''
is 64 'fldl 4(%r8)' 41dd4004 ''

# The counter that is not the mode's own has the address-size prefix.
is 32 'jcxz sym' 67e3ff 'R_386_PC8@2:sym'
is 32 'jcxz 1f|nop|1:' 67e30190 ''
is 32 '1: nop|jcxz 1b' 9067e3fc ''
is 32 'jecxz sym' e3ff 'R_386_PC8@1:sym'
is 32 '.code16|jecxz sym' 67e3ff 'R_386_PC8@2:sym'
is 32 '.code16|jcxz sym' e3ff 'R_386_PC8@1:sym'
is 32 '.code16|jecxz 1f|1:' 67e300 ''
is 64 'jecxz sym' 67e300 'R_X86_64_PC8@2:sym-1'
is 64 'jecxz 1f|1:' 67e300 ''
is 64 'jrcxz sym' e300 'R_X86_64_PC8@1:sym-1'

# A call by another size than the mode's.
is 32 'callw sym' 66e8feff 'R_386_PC16@2:sym'
is 32 'calll sym' e8fcffffff 'R_386_PC32@1:sym'
is 32 'callw 1f|1:' 66e80000 ''
is 32 'calll 1f|1:' e800000000 ''
is 32 'nop|callw sym|callw 2f|nop|2: ret' 9066e8feff66e8010090c3 'R_386_PC16@3:sym'
is 32 '.code16|call 1f|1:' e80000 ''
is 32 '.code16|calll 1f|1:' 66e800000000 ''
is 32 '.code16|callw 1f|1:' e80000 ''
is 32 '.code16|call sym' e8feff 'R_386_PC16@1:sym'
is 32 '.code16|calll sym' 66e8fcffffff 'R_386_PC32@2:sym'
is 64 'callq sym' e800000000 'R_X86_64_PLT32@1:sym-4'
is 64 'callq 1f|1:' e800000000 ''

# A frame whose size is a symbol: two bytes behind the opcode.
is 32 'enter $sym,$1' c8000001 'R_386_16@1:sym'
is 64 'enter $sym,$1' c8000001 'R_X86_64_16@1:sym+0'
is 32 'enter $sym+4,$1' c8040001 'R_386_16@1:sym'
is 64 'enter $sym+4,$1' c8000001 'R_X86_64_16@1:sym+4'
is 32 'enter $8,$1' c8080001 ''
is 64 'enter $8,$1' c8080001 ''

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
