#!/bin/sh
# `*` before an operand that is not a register (AS-T-028).
#
# `call *foo` calls what the pointer at foo points to: ff 15 and the
# address of foo.  The parser parsed what followed the star and returned
# it as it would be without one -- a label, which to call and jmp is a
# direct branch -- so `call *foo` was e8 rel32, a call of foo itself: the
# processor ran the bytes of a function pointer.
#
# The bytes and relocations are GNU as's.  Run by run-suite.sh, which sets
# $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# enc MODE INSTRUCTION BYTES [RELOCATION]
enc() {
    printf '\t.text\n\t%s\n' "$2" > t.s
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 $2: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$3" ] || { echo "FAIL --$1 $2: $got, not $3"; fail=1; }
    rel=$(readelf -rW t.o | awk '/ R_/ { print $3 }' | tr '\n' ' ' | sed 's/ $//')
    [ "$rel" = "${4:-}" ] || { echo "FAIL --$1 $2: relocation '$rel', not '${4:-}'"; fail=1; }
}

enc 32 'call *foo'             ff1500000000    R_386_32
enc 32 'jmp *foo'              ff2500000000    R_386_32
enc 32 'call *foo+4'           ff1504000000    R_386_32
enc 32 'jmp *foo-8'            ff25f8ffffff    R_386_32
enc 32 'call *0x1000'          ff1500100000
enc 32 'call *foo(%eax)'       ff9000000000    R_386_32
enc 32 'call *%fs:foo'         64ff1500000000  R_386_32
enc 32 'lcall *foo'            ff1d00000000    R_386_32
# The forms that were right: a register, and memory through one.
enc 32 'call *%eax'            ffd0
enc 32 'jmp *(%eax)'           ff20
enc 32 'call *4(%eax,%eax,4)'  ff548004
# And without the star a label is still a direct branch.
enc 32 'jmp foo'               e9fcffffff      R_386_PC32

enc 64 'call *foo'             ff142500000000  R_X86_64_32S
enc 64 'jmp *foo'              ff242500000000  R_X86_64_32S
enc 64 'call *0x1000'          ff142500100000
enc 64 'call *foo(%rip)'       ff1500000000    R_X86_64_PC32
enc 64 'jmp *foo(%rip)'        ff2500000000    R_X86_64_PC32
enc 64 'call *foo(%rax)'       ff9000000000    R_X86_64_32S
enc 64 'call *%rax'            ffd0
enc 64 'jmp *(%rax)'           ff20
enc 64 'call foo'              e800000000      R_X86_64_PLT32

[ "$fail" -eq 0 ] && echo "ok: indirect branches through memory"
exit "$fail"
