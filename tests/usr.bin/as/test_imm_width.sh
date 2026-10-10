#!/bin/sh
# An immediate is as wide as its operand (AS-T-061, AS-T-062).
#
# In 64-bit mode `movb $1, %al` was b0 01 00 00 00: a helper asked for
# eight bits wrote thirty-two, and the three bytes of zeros were run as
# the next instruction.  That broke every call of a variadic function from
# substrate's own compiler, which sets %al before one.  And a 16-bit
# operation with a constant too big for a byte -- addw, cmpw, testw,
# imulw, pushw -- had a 32-bit immediate after its 66 prefix, two bytes
# too many.
#
# The bytes are GNU as's.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# enc MODE INSTRUCTION BYTES
enc() {
    printf '\t.text\n\t%s\n' "$2" > t.s
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 $2: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    if [ "$got" != "$3" ]; then echo "FAIL --$1 $2: $got, not $3"; fail=1; fi
}

enc 64 'movb $1, %al'               b001
enc 64 'movb $0, %al'               b000
enc 64 'movb $0x7f, %cl'            b17f
enc 64 'movb $0x7f, %r9b'           41b17f
enc 64 'movb $1, (%rax)'            c60001
enc 64 'addw $0x1000, %bx'          6681c30010
enc 64 'subw $0x1000, (%rax)'       6681280010
enc 64 'cmpw $0x1234, (%rax)'       6681383412
enc 64 'andw $0x1000, %cx'          6681e10010
enc 64 'orw $1, %ax'                6683c801
enc 64 'pushw $0x1234'              66683412
enc 64 'testw $0x1000, %bx'         66f7c30010
enc 64 'testl $0x10000, (%rax)'     f70000000100
enc 64 'imulw $0x1000, %bx, %ax'    6669c30010
enc 64 'imull $0x10000, %ebx, %eax' 69c300000100
enc 64 'addl $0x1000, %ebx'         81c300100000
enc 64 'movl $1, %eax'              b801000000

enc 32 'movb $1, %al'               b001
enc 32 'movb $0x7f, %cl'            b17f
enc 32 'movb $1, (%eax)'            c60001
enc 32 'addw $0x1000, %bx'          6681c30010
enc 32 'subw $0x1000, (%eax)'       6681280010
enc 32 'cmpw $0x1234, (%eax)'       6681383412
enc 32 'andw $0x1000, %cx'          6681e10010
enc 32 'pushw $0x1234'              66683412
enc 32 'testw $0x1000, %bx'         66f7c30010
enc 32 'testl $0x10000, (%eax)'     f70000000100
enc 32 'imulw $0x1000, %bx, %ax'    6669c30010
enc 32 'imull $0x10000, %ebx, %eax' 69c300000100
enc 32 'addl $0x1000, %ebx'         81c300100000

# The sequence the compiler writes before a variadic call: the call must
# follow the two bytes of the move directly.
printf '\t.text\n\tmovb $0, %%al\n\tcall f\n' > t.s
"$AS" --64 -o t.o t.s 2> err || { echo "FAIL variadic call: $(head -1 err)"; fail=1; }
objcopy -O binary -j .text t.o t.bin
case "$(od -An -v -tx1 t.bin | tr -d ' \n')" in
b000e8????????) ;;
*) echo "FAIL variadic call: $(od -An -v -tx1 t.bin | tr -d ' \n')"; fail=1 ;;
esac

[ "$fail" -eq 0 ] && echo "ok: immediate widths"
exit "$fail"
