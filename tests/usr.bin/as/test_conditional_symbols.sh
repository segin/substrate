#!/bin/sh
# The conditionals and the symbols of the source: .ifdef and .ifndef go
# by whether a name is defined at that line, and .if and its kin by the
# value an absolute symbol has there.
#
# Each case is a source whose conditional would go the other way were
# the symbol not seen -- .ifdef of a name that is defined, .if of one
# that is 0 -- and the byte assembled says which way it went.  The bytes
# are GNU as 2.46's.  (--defsym's symbols are test_cli_defsym.sh.)
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# t WANT SOURCE: SOURCE, its lines parted by |, puts WANT in .data.
t() {
    cases=$((cases + 1))
    { printf '\t.data\n'; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" -32 -o t.o t.s > out 2>&1; then
        echo "FAIL [$2]: refused: $(head -1 out)"
        fail=1
        return
    fi
    objcopy -O binary --only-section=.data t.o data.bin 2>/dev/null
    got=$(od -An -v -tx1 data.bin | tr -d ' \n')
    if [ "$got" != "$1" ]; then
        echo "FAIL [$2]: .data is '$got', GNU as writes '$1'"
        fail=1
    fi
}

# .ifdef: a symbol set, a label, a label with a statement after it, and a
# name that is nowhere.
t 01   '.set X, 1|.ifdef X|.byte 1|.else|.byte 2|.endif'
t 01   'lab:|.ifdef lab|.byte 1|.else|.byte 2|.endif'
t 0902 'lab: .byte 9|.ifndef lab|.byte 1|.else|.byte 2|.endif'
t 02   '.ifdef nowhere|.byte 1|.else|.byte 2|.endif'
t 01   '.ifndef nowhere|.byte 1|.else|.byte 2|.endif'

# Defined at that point: a definition further down does not count, and
# one in a part that is not assembled is no definition.
t 02   '.ifdef X|.byte 1|.else|.byte 2|.endif|.set X, 1'
t 02   '.if 0|.set X, 1|.endif|.ifdef X|.byte 1|.else|.byte 2|.endif'
t 02   '.set X, 1|.ifdef X|.ifdef Y|.byte 1|.else|.byte 2|.endif|.endif'

# .if: the value, not the name's being there.  A symbol that is 0 is
# false.
t 02   '.set X, 0|.if X|.byte 1|.else|.byte 2|.endif'
t 01   '.set X, 0|.if X == 0|.byte 1|.else|.byte 2|.endif'
t 01   '.set K, 2|.if K*2 > 3|.byte 1|.else|.byte 2|.endif'
t 02   '.equ K, 1|.if K*2 > 3|.byte 1|.else|.byte 2|.endif'
t 02   '.equiv Q, 2|.if Q == 2|.byte 2|.endif'

# Each way of giving a symbol a value, and a value given by another's.
t 02   'K = 5|.if K - 5|.byte 1|.else|.byte 2|.endif'
t 01   'K=5|.if K == 5|.byte 1|.endif'
t 07   '.set A, 3|.set B, A + 4|.if B == 7|.byte 7|.endif'
t 01   '.set X, 5 # five|.ifeq X - 5|.byte 1|.endif'

# The value it has at that line: set again, it is the later; set again
# where nothing is assembled, it is as it was.
t 02   '.set X, 1|.set X, 0|.if X|.byte 1|.else|.byte 2|.endif'
t 01   '.set X, 1|.if 0|.set X, 0|.endif|.if X|.byte 1|.else|.byte 2|.endif'

# The other conditionals on a value.
t 0103 '.set X, 4|.ifgt X|.byte 1|.endif|.iflt X|.byte 2|.endif|.ifne X|.byte 3|.endif'
t 02   '.set X, 2|.if X == 1|.byte 1|.elseif X == 2|.byte 2|.else|.byte 3|.endif'

[ "$fail" -eq 0 ] && echo "ok: conditionals see the source's symbols ($cases cases)"
exit "$fail"
