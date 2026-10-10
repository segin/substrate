#!/bin/sh
# `ret $n` keeps its count (AS-T-039).
#
# The 32-bit encoder wrote C3 for `ret` whatever followed it, so a
# function that pops its own arguments -- stdcall, pascal, anything from a
# compiler told -mrtd -- returned without popping them.  The 64-bit one
# kept the count but let `retq` bring a REX.W, and let a count that does
# not fit sixteen bits wrap.
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
    [ "$got" = "$3" ] || { echo "FAIL --$1 $2: $got, not $3"; fail=1; }
}

# refused MODE INSTRUCTION
refused() {
    printf '\t.text\n\t%s\n' "$2" > t.s
    if "$AS" "--$1" -o t.o t.s 2> /dev/null; then echo "FAIL --$1 $2: assembled"; fail=1; fi
}

for m in 32 64; do
    enc $m 'ret'          c3
    enc $m 'ret $4'       c20400
    enc $m 'ret $0'       c20000
    enc $m 'ret $0xffff'  c2ffff
    enc $m 'ret $-1'      c2ffff
    enc $m 'retw'         66c3
    enc $m 'retw $8'      66c20800
    enc $m 'lret $8'      ca0800
    refused $m 'ret $0x10000'
    refused $m 'ret $4, $5'
    refused $m 'ret %eax'
done
enc 32 'retl'     c3
enc 32 'retl $8'  c20800
enc 64 'retq'     c3
enc 64 'retq $8'  c20800

[ "$fail" -eq 0 ] && echo "ok: ret"
exit "$fail"
