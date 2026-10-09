#!/bin/sh
# mov of a constant to a 64-bit register: the bytes, against what the
# processor's manual says they are.
#
# B8+r under REX.W carries eight bytes of constant.  The assembler wrote
# it with four, so the processor took the next four bytes of the program
# for the rest and went on from the middle of the instruction after;
# nothing the in-tree compiler made for x86-64 ran.  A constant that fits
# in 32 bits signed is C7 /0, sign-extended; one that does not is B8+r
# and all eight bytes.
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
command -v objcopy > /dev/null || { echo "SKIP: no objcopy to take the bytes out with"; exit 0; }

# enc WHAT INSTRUCTION BYTES
enc() {
    printf '\t.text\n\t%s\n' "$2" > t.s
    if ! ./as --64 -o t.o t.s 2> err; then echo "FAIL $1: $(head -1 err)"; fail=1; return; fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    if [ "$got" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: $got, not $3"; fail=1; fi
}

enc "zero to rax"                       'movq $0, %rax'            48c7c000000000
enc "minus one to rcx"                  'movq $-1, %rcx'           48c7c1ffffffff
enc "the largest that is sign-extended" 'movq $0x7fffffff, %r9'    49c7c1ffffff7f
enc "the smallest that is"              'movq $-0x80000000, %rdx'  48c7c200000080
enc "one more than fits"                'movq $0x80000000, %rax'   48b80000008000000000
enc "one less than fits"                'movq $-0x80000001, %r10'  49baffffff7fffffffff
enc "forty bits"                        'movq $0x123456789a, %rdx' 48ba9a78563412000000
enc "a 32-bit register, as before"      'movl $5, %eax'            b805000000
enc "an extended 32-bit register"       'movl $5, %r9d'            41b905000000

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
