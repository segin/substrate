#!/bin/sh
# sym(%reg): a displacement with a symbol in it is 32 bits (AS-T-194,
# for the instructions the 32-bit emitter encodes itself).
#
# The SIMD, x87 and a good many other instructions are encoded in 32-bit
# mode by a function that sized a displacement by its value, and a
# symbol's value there is 0: one byte was kept for it.  The relocation,
# four bytes, was then written over the end of the instruction --
# `movaps sym(%ebx), %xmm0` came out as four zero bytes, the instruction
# gone -- or, for one-byte opcodes, refused: `flds sym(%ebx)` was "failed
# to emit relocations".
#
# The bytes and relocations are GNU as's.  Run by run-suite.sh, which sets
# $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# enc INSTRUCTION BYTES RELOCATION-OFFSET
enc() {
    printf '\t.text\n\t%s\n' "$1" > t.s
    if ! "$AS" --32 -o t.o t.s 2> err; then
        echo "FAIL $1: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$2" ] || { echo "FAIL $1: $got, not $2"; fail=1; }
    rel=$(readelf -rW t.o | awk '/ R_/ { print $1, $3, $5 }' | sed 's/^0*//' | tr '\n' ';')
    [ "$rel" = "${3:+$3 R_386_32 sym;}" ] || { echo "FAIL $1: relocation '$rel'"; fail=1; }
}

enc 'movaps sym(%ebx), %xmm0'       0f288300000000      3
enc 'movdqa sym(%ebp), %xmm1'       660f6f8d00000000    4
enc 'addps sym+4(%edx), %xmm2'      0f589204000000      3
enc 'movaps sym(,%eax,4), %xmm1'    0f280c8500000000    4
enc 'cmpxchgl %eax, sym(%ebx)'      0fb18300000000      3
enc 'prefetcht0 sym(%eax)'          0f188800000000      3
enc 'flds sym(%ebx)'                d98300000000        2
enc 'fldl sym(%ebx)'                dd8300000000        2
enc 'faddl sym(%ebx)'               dc8300000000        2
enc 'fnstcw sym(%ebx)'              d9bb00000000        2
enc 'fildl sym(%ebx)'               db8300000000        2

# A number is still as short as it can be.
enc 'movaps 4(%ebx), %xmm0'         0f284304
enc 'movaps (%ebx), %xmm0'          0f2803
enc 'flds 8(%ebp)'                  d94508
enc 'movaps 0x100(%ebx), %xmm0'     0f288300010000

# The 64-bit mode has emitters of its own for these, with the same fault.
enc64() {
    printf '\t.text\n\t%s\n' "$1" > t.s
    if ! "$AS" --64 -o t.o t.s 2> err; then
        echo "FAIL --64 $1: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$2" ] || { echo "FAIL --64 $1: $got, not $2"; fail=1; }
    rel=$(readelf -rW t.o | awk '/ R_/ { print $1, $3, $5 }' | sed 's/^0*//' | tr '\n' ';')
    [ "$rel" = "$3 R_X86_64_32S sym;" ] || { echo "FAIL --64 $1: relocation '$rel'"; fail=1; }
}
enc64 'movaps sym(%rbx), %xmm0'     0f288300000000      3
enc64 'addsd sym(%rax), %xmm1'      f20f588800000000    4
enc64 'movq sym(%rax), %xmm1'       f30f7e8800000000    4
enc64 'cvttsd2si sym(%rbx), %eax'   f20f2c8300000000    4
enc64 'fldl sym(%rbx)'              dd8300000000        2
enc64 'flds sym(%rax)'              d98000000000        2

[ "$fail" -eq 0 ] && echo "ok: symbolic displacements"
exit "$fail"
