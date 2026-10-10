#!/bin/sh
# Jumps to a compiler's local labels: where a jump is written long, every
# jump that measures its distance across it must count it as long.
#
# The loop below is the shape every compiled loop has: a jump back to the
# top that is too far for the short form, and a jump forward over that
# one to the end.  The assembler counted the backward jump as two bytes
# when measuring the forward one and then wrote it as five, so the
# forward jump landed three bytes short, inside the other, and any
# program with a loop died of an illegal instruction.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

${CC:-cc} -O0 -w -o "$work/as" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/as/*.c "$top"/usr.lib/elfobj/src/*.c 2>/dev/null ||
    { echo "FAIL: the assembler does not build for the host"; exit 1; }

cd "$work" || exit 1

# loop FILLER-BEFORE FILLER-INSIDE: a function that adds 0, 1 and 2, with
# that many six-byte instructions at the top of the loop and in its body.
loop() {
    printf '\t.text\n\t.globl main\n\t.type main, @function\nmain:\n'
    printf '\tmovl $0, %%eax\n\tmovl $0, %%ecx\n.Ltop:\n'
    i=0; while [ $i -lt "$1" ]; do printf '\taddl $0x11223344, %%edx\n'; i=$((i + 1)); done
    printf '\tcmpl $3, %%ecx\n\tjne .Lbody\n\tjmp .Lend\n.Lbody:\n'
    i=0; while [ $i -lt "$2" ]; do printf '\taddl $0x11223344, %%edx\n'; i=$((i + 1)); done
    printf '\taddl %%ecx, %%eax\n\taddl $1, %%ecx\n\tjmp .Ltop\n.Lend:\n\tret\n'
    printf '\t.section .note.GNU-stack,"",@progbits\n'
}

# run WHAT FLAG CCFLAG BEFORE INSIDE
run() {
    loop "$4" "$5" > t.s
    if ! ./as "$2" -o t.o t.s 2> err; then echo "FAIL $1: $(head -1 err)"; fail=1; return; fi
    if ! ${CC:-cc} $3 -w -o t t.o 2> lerr; then echo "FAIL $1: does not link: $(head -1 lerr)"; fail=1; return; fi
    ./t; rc=$?
    if [ "$rc" -eq 3 ]; then echo "ok   $1"; else echo "FAIL $1: the loop gave $rc, not 3"; fail=1; fi
}

for bits in 32 64; do
    [ $bits = 32 ] && m=-m32 || m=-m64
    run "$bits-bit: every jump short"                         --$bits $m 0 0
    run "$bits-bit: the jump back long, the jump over it short" --$bits $m 30 0
    run "$bits-bit: both long"                                --$bits $m 30 30
    run "$bits-bit: the jump back just past the short form"   --$bits $m 20 0
    run "$bits-bit: the jump forward just past the short form" --$bits $m 0 21
done

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
