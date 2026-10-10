#!/bin/sh
# sal is shl (part of AS-T-172).
#
# The two names are one instruction; a compiler writes sal for a signed
# operand, and gcc at -O0 writes `salq $2, %rax` for an index into an
# array of ints.  With any suffix it was "unsupported mnemonic".
#
# The bytes are GNU as's.  Shifts by a constant 1 are left out in 32-bit
# mode, where the long form is still chosen (AS-T-186).  Run by
# run-suite.sh, which sets $AS.
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
    [ "$got" = "$3" ] || { echo "FAIL --$1 $2: $got, not $3"; fail=1; }
}

enc 32 'sall %eax'          d1e0
enc 32 'salw $3, %bx'       66c1e303
enc 32 'salb %cl, %al'      d2e0
enc 32 'sall $2, (%eax)'    c12002
enc 32 'sal $4, %eax'       c1e004
enc 32 'sal %cl, %ebx'      d3e3

enc 64 'sall $1, %eax'      d1e0
enc 64 'sall %eax'          d1e0
enc 64 'salb %cl, %al'      d2e0
enc 64 'sall $2, (%rax)'    c12002
enc 64 'sal $4, %eax'       c1e004
enc 64 'salq $2, %rax'      48c1e002
enc 64 'salq %rax'          48d1e0
enc 64 'salq %cl, %rdx'     48d3e2

# A name that only begins that way is not it.
printf '\tsalt %%eax\n' > t.s
if "$AS" --32 -o t.o t.s 2> /dev/null; then echo "FAIL: salt assembled"; fail=1; fi

[ "$fail" -eq 0 ] && echo "ok: sal"
exit "$fail"
