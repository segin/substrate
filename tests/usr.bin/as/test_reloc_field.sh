#!/bin/sh
# The relocation of an instruction goes on the field it is for, and is
# that field's width (AS-T-192, AS-T-193, AS-T-195, AS-T-208).
#
# It went on the last four bytes of the instruction, or the last eight
# when anything about the instruction was 64 bits.  So:
#
#   - With an immediate after a symbolic address -- `movl $5, var(%rip)`,
#     `cmpl $0, var`, every store of a constant to a variable and every
#     comparison of a variable with one -- the relocation was on the
#     immediate, which was lost, and the address was left 0.
#   - A symbolic immediate was encoded as the one-byte form that 0 fits,
#     and the four-byte relocation written over the opcode: `add $sym,
#     %rbx` was four bytes of zeros.
#   - `addq $sym, 8(%rsp)` got eight bytes of relocation over all of it
#     but its first byte.
#   - `lea var(%rip), %rdi`, with no suffix, was the 32-bit lea.
#
# Each line below is an instruction, its bytes, and its relocations as
# offset:type:symbol+addend.  All are GNU as's.  Run by run-suite.sh,
# which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0
tried=0

# t MODE INSTRUCTION BYTES RELOCATIONS
t() {
    tried=$((tried + 1))
    printf '\t.text\n\t%s\n' "$2" > t.s
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 $2: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    got=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    rel=$(readelf -rW t.o | awk '/ R_/ { print $1 ":" $3 ":" $5 $6 $7 }' | sed 's/^0*//' | tr '\n' ';')
    [ "$got" = "$3" ] || { echo "FAIL --$1 $2: $got, not $3"; fail=1; }
    [ "$rel" = "$4" ] || { echo "FAIL --$1 $2: relocations '$rel', not '$4'"; fail=1; }
}

t 64 'movl $5, sym(%rip)' c7050000000005000000 '2:R_X86_64_PC32:sym-8;'
t 64 'cmpl $0, sym(%rip)' 833d0000000000 '2:R_X86_64_PC32:sym-5;'
t 64 'cmpl $1000, sym(%rip)' 813d00000000e8030000 '2:R_X86_64_PC32:sym-8;'
t 64 'addl $1, sym' 8304250000000001 '3:R_X86_64_32S:sym+0;'
t 64 'movl $1, sym' c704250000000001000000 '3:R_X86_64_32S:sym+0;'
t 64 'movq $1, sym' 48c704250000000001000000 '4:R_X86_64_32S:sym+0;'
t 64 'movq $1, sym(%rip)' 48c7050000000001000000 '3:R_X86_64_PC32:sym-8;'
t 64 'cmpb $0, sym(%rip)' 803d0000000000 '2:R_X86_64_PC32:sym-5;'
t 64 'movb $1, sym(%rip)' c6050000000001 '2:R_X86_64_PC32:sym-5;'
t 64 'cmpw $0x1234, sym(%rip)' 66813d000000003412 '3:R_X86_64_PC32:sym-6;'
t 64 'addq $1, sym(%rip)' 4883050000000001 '3:R_X86_64_PC32:sym-5;'
t 64 'addq $1000, sym(%rip)' 48810500000000e8030000 '3:R_X86_64_PC32:sym-8;'
t 64 'movl $5, sym(%rax)' c7800000000005000000 '2:R_X86_64_32S:sym+0;'
t 64 'movl $5, sym(%rax,%rbx,4)' c784980000000005000000 '3:R_X86_64_32S:sym+0;'
t 64 'movl $5, sym(,%rbx,4)' c7049d0000000005000000 '3:R_X86_64_32S:sym+0;'
t 64 'testb $1, sym(%rip)' f6050000000001 '2:R_X86_64_PC32:sym-5;'
t 64 'testl $1, sym+4(%rip)' f7050000000001000000 '2:R_X86_64_PC32:sym-4;'
t 64 'imul $3, sym(%rip), %eax' 6b050000000003 '2:R_X86_64_PC32:sym-5;'
t 64 'imul $300, sym(%rip), %eax' 6905000000002c010000 '2:R_X86_64_PC32:sym-8;'
t 64 'shll $2, sym(%rip)' c1250000000002 '2:R_X86_64_PC32:sym-5;'
t 64 'pinsrb $3, sym(%rip), %xmm2' 660f3a20150000000003 '5:R_X86_64_PC32:sym-5;'
t 64 'pshufd $1, sym(%rip), %xmm1' 660f700d0000000001 '4:R_X86_64_PC32:sym-5;'
t 64 'cmpl $0, sym@GOTPCREL(%rip)' 833d0000000000 '2:R_X86_64_GOTPCREL:sym-5;'
t 64 'mov sym, %eax' 8b042500000000 '3:R_X86_64_32S:sym+0;'
t 64 'mov sym(%rip), %eax' 8b0500000000 '2:R_X86_64_PC32:sym-4;'
t 64 'incl sym(%rip)' ff0500000000 '2:R_X86_64_PC32:sym-4;'
t 64 'add $sym, %ebx' 81c300000000 '2:R_X86_64_32:sym+0;'
t 64 'add $sym, %rbx' 4881c300000000 '3:R_X86_64_32S:sym+0;'
t 64 'sub $sym, %rsp' 4881ec00000000 '3:R_X86_64_32S:sym+0;'
t 64 'cmp $sym, %r9d' 4181f900000000 '3:R_X86_64_32:sym+0;'
t 64 'addl $sym, (%rbx)' 810300000000 '2:R_X86_64_32:sym+0;'
t 64 'addq $sym, 8(%rsp)' 488144240800000000 '5:R_X86_64_32S:sym+0;'
t 64 'addl $sym, 8(%rbx,%rbx,2)' 81445b0800000000 '4:R_X86_64_32:sym+0;'
t 64 'addw $sym, %cx' 6681c10000 '3:R_X86_64_16:sym+0;'
t 64 'addb $sym, %cl' 80c100 '2:R_X86_64_8:sym+0;'
t 64 'or $sym+4, %edx' 81ca00000000 '2:R_X86_64_32:sym+4;'
t 64 'mov $sym, %eax' b800000000 '1:R_X86_64_32:sym+0;'
t 64 'movq $sym, %rax' 48c7c000000000 '3:R_X86_64_32S:sym+0;'
t 64 'movabs $sym, %rax' 48b80000000000000000 '2:R_X86_64_64:sym+0;'
t 64 'push $sym' 6800000000 '1:R_X86_64_32S:sym+0;'
t 64 'imul $sym, %eax, %ebx' 69d800000000 '2:R_X86_64_32:sym+0;'
t 64 'movl $sym, (%rbx)' c70300000000 '2:R_X86_64_32:sym+0;'
t 64 'movq $sym, (%rax)' 48c70000000000 '3:R_X86_64_32S:sym+0;'
t 64 'testl $sym, (%rbx)' f70300000000 '2:R_X86_64_32:sym+0;'
t 64 'test $sym, %ebx' f7c300000000 '2:R_X86_64_32:sym+0;'
t 64 'lea sym(%rip), %rdi' 488d3d00000000 '3:R_X86_64_PC32:sym-4;'
t 64 'lea 8(%rax), %rdi' 488d7808 ''
t 64 'lea (%rax,%rbx,2), %r9' 4c8d0c58 ''
t 64 'lea 8(%rax), %edi' 8d7808 ''
t 32 'movl $5, sym' c7050000000005000000 '2:R_386_32:sym;'
t 32 'movb $1, sym' c6050000000001 '2:R_386_32:sym;'
t 32 'cmpb $0, sym' 803d0000000000 '2:R_386_32:sym;'
t 32 'movw $7, sym' 66c705000000000700 '3:R_386_32:sym;'
t 32 'testb $1, sym' f6050000000001 '2:R_386_32:sym;'
t 32 'testl $1, sym+4' f7050400000001000000 '2:R_386_32:sym;'
t 32 'shll $2, sym' c1250000000002 '2:R_386_32:sym;'
t 32 'imul $3, sym, %eax' 6b050000000003 '2:R_386_32:sym;'
t 32 'movl $5, sym(,%ebx,4)' c7049d0000000005000000 '3:R_386_32:sym;'
t 32 'movl $5, sym(%eax,%ebx,4)' c784980000000005000000 '3:R_386_32:sym;'
t 32 'movl $7, sym@GOTOFF(%ebx)' c7830000000007000000 '2:R_386_GOTOFF:sym;'
t 32 'mov sym, %ecx' 8b0d00000000 '2:R_386_32:sym;'
t 32 'incl sym' ff0500000000 '2:R_386_32:sym;'
t 32 'pshufd $1, sym, %xmm1' 660f700d0000000001 '4:R_386_32:sym;'
t 32 'movss sym, %xmm0' f30f100500000000 '4:R_386_32:sym;'
t 32 'fldl sym' dd0500000000 '2:R_386_32:sym;'
t 32 'jmp *sym(,%eax,4)' ff248500000000 '3:R_386_32:sym;'
t 32 'add $sym, %ebx' 81c300000000 '2:R_386_32:sym;'
t 32 'addl $sym, (%ebx)' 810300000000 '2:R_386_32:sym;'
t 32 'addw $sym, %cx' 6681c10000 '3:R_386_16:sym;'
t 32 'addb $sym, %cl' 80c100 '2:R_386_8:sym;'
t 32 'or $sym+4, %edx' 81ca04000000 '2:R_386_32:sym;'
t 32 'mov $sym, %eax' b800000000 '1:R_386_32:sym;'
t 32 'push $sym' 6800000000 '1:R_386_32:sym;'
t 32 'imul $sym, %eax, %ebx' 69d800000000 '2:R_386_32:sym;'
t 32 'movl $sym, (%ebx)' c70300000000 '2:R_386_32:sym;'
t 32 'test $sym, %ebx' f7c300000000 '2:R_386_32:sym;'

[ "$tried" -ge 70 ] || { echo "FAIL: only $tried cases were tried"; fail=1; }
[ "$fail" -eq 0 ] && echo "ok: relocation fields ($tried instructions)"
exit "$fail"
