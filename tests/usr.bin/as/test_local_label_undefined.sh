#!/bin/sh
# A reference to a local label that is not there is an error (AS-T-027).
#
# `jmp 1f` with no `1:` after it was assembled to nothing -- the jump was
# simply not in the object -- and `call 2f` to a call of the start of the
# section, with exit status 0.  A typo in a label number made a function
# that fell through where it should have branched.
#
# Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# refused WHAT SOURCE LABEL LINE: fails, names the label and the line, and
# writes no object.
refused() {
    printf "$2" > t.s
    rm -f t.o
    if "$AS" --32 -o t.o t.s 2> err; then echo "FAIL $1: assembled"; fail=1; return; fi
    grep -q "local label $3 is not defined" err || { echo "FAIL $1: $(head -1 err)"; fail=1; }
    grep -q ":$4: local label" err || { echo "FAIL $1: not line $4: $(head -1 err)"; fail=1; }
    [ ! -e t.o ] || { echo "FAIL $1: an object was written"; fail=1; }
}

# text WHAT SOURCE BYTES
text() {
    printf "$2" > t.s
    if ! "$AS" --32 -o t.o t.s 2> err; then
        echo "FAIL $1: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$3" ] || { echo "FAIL $1: $got, not $3"; fail=1; }
}

refused "forward, none at all"     'cli\njmp 1f\nhlt\n'                 1f 2
refused "a call forward"           'cli\nnop\ncall 2f\nhlt\n'           2f 3
refused "backward, none before"    'jmp 1b\n1: nop\n'                   1b 1
refused "forward, only one behind" '1: nop\njmp 1f\n'                   1f 2
refused "another number"           '1: nop\njne 3b\n'                   3b 2
refused "as an immediate"          'movl $1f, %%eax\n'                  1f 1

# Those that are there still resolve, to the nearest in the direction.
text "back and forward"   '1: nop\njmp 1b\njmp 1f\n1: ret\n'   90ebfdeb00c3
text "past another label" 'jmp 3f\n2: nop\n3: ret\n'           eb0190c3

[ "$fail" -eq 0 ] && echo "ok: undefined local labels"
exit "$fail"
