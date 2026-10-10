#!/bin/sh
# The branches with only a one-byte displacement -- loop, loope, loopne
# and the jumps on the count register -- and three instructions whose
# operand may be left out: xlat, aam, aad.  And `rep nop`, which is pause.
#
# A branch of this kind to a label was refused, with no message, in both
# modes: the code that resolves a branch to a label knew jmp, call and
# the conditional jumps and nothing else.  The count register is named by
# the mnemonic or by its suffix, and where it is not the one the mode's
# address size gives, the instruction has the prefix 67 and is a byte
# longer -- so the displacement is one less going back.
#
# Each line is a mode, what GNU as 2.46 assembles the source to (or
# `refused`), and the source, its lines parted by |.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

t() {
    mode=$1; want=$2; src=$3
    cases=$((cases + 1))
    { printf '\t.text\n'; printf '%s\n' "$src" | tr '|' '\n'; } > t.s
    rm -f t.o
    if "$AS" "-$mode" -o t.o t.s > out 2>&1; then
        objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
        got=$(od -An -v -tx1 text.bin | tr -d ' \n')
        if readelf -rW t.o 2>/dev/null | grep -q ' R_'; then
            got="$got with a relocation"
        fi
    else
        got=refused
        # A refusal gives the line and says why, in words.
        grep -q ':[0-9][0-9]*: .*[a-z][a-z]' out ||
            { echo "FAIL $mode [$src]: refused with no reason: $(head -1 out)"; fail=1; }
    fi
    if [ "$got" != "$want" ]; then
        echo "FAIL $mode [$src]: $got, and GNU as: $want"
        fail=1
    fi
}

# loop and its kin: back, forward, and to each kind of label.
for mode in 32 64; do
    t $mode e2fe    '1: loop 1b'
    t $mode e1fe    '1: loope 1b'
    t $mode e1fe    '1: loopz 1b'
    t $mode e0fe    '1: loopne 1b'
    t $mode e0fe    '1: loopnz 1b'
    t $mode e20190  'loop 1f|nop|1:'
    t $mode 90e2fd  't: nop|loop t'
    t $mode 90e2fd  '.Lt: nop|loop .Lt'
    t $mode e10190  'loope .Lt|nop|.Lt:'
done

# The counter by suffix: the mode's own needs no prefix, the other does,
# and one the mode has not got is refused.
t 32 e2fe     '1: loopl 1b'
t 32 67e2fd   '1: loopw 1b'
t 32 refused  '1: loopq 1b'
t 64 e2fe     '1: loopq 1b'
t 64 67e2fd   '1: loopl 1b'
t 64 refused  '1: loopw 1b'
t 32 67e1fd   '1: loopew 1b'
t 64 67e0fd   '1: loopnel 1b'

# The jumps on the count register, whose name says which.
t 32 e3fe      '1: jecxz 1b'
t 32 67e3fd    '1: jcxz 1b'
t 32 refused   '1: jrcxz 1b'
t 64 e3fe      '1: jrcxz 1b'
t 64 67e3fd    '1: jecxz 1b'
t 64 refused   '1: jcxz 1b'
t 32 e30190    'jecxz 1f|nop|1:'
t 32 67e30190  'jcxz 1f|nop|1:'
t 64 e30190    'jrcxz 1f|nop|1:'
t 64 67e30190  'jecxz 1f|nop|1:'
t 32 90e3fd    't: nop|jecxz t'
t 64 9067e3fc  't: nop|jecxz t'

# The reach is a byte: -128 and 127 from the end of the instruction, and
# not one more.  126 bytes back and the two of the loop is -128.
t 32 "$(awk 'BEGIN { for (i = 0; i < 126; i++) printf "90" }')e280" 't: .fill 126, 1, 0x90|loop t'
t 32 refused  't: .fill 127, 1, 0x90|loop t'
t 32 "e27f$(awk 'BEGIN { for (i = 0; i < 127; i++) printf "90" }')" 'loop t|.fill 127, 1, 0x90|t:'
t 32 refused  'loop t|.fill 128, 1, 0x90|t:'
# With the prefix the instruction is three bytes, and 125 back is -128.
t 64 "$(awk 'BEGIN { for (i = 0; i < 125; i++) printf "90" }')67e380" 't: .fill 125, 1, 0x90|jecxz t'
t 64 refused  't: .fill 126, 1, 0x90|jecxz t'

# It is counted at its true length by a jump that crosses it: 124 bytes
# and a three-byte jecxz are 127, which a short jump reaches.
t 64 "eb7f67e300$(awk 'BEGIN { for (i = 0; i < 124; i++) printf "90" }')c3" 'jmp .Le|jecxz .Ln|.Ln:|.fill 124, 1, 0x90|.Le: ret'

# xlat, with nothing after it and by its other name.
for mode in 32 64; do
    t $mode d7 'xlat'
    t $mode d7 'xlatb'
done

# aam and aad: base ten unless another is written; not in 64-bit code.
t 32 d40a     'aam'
t 32 d50a     'aad'
t 32 d40a     'aam $10'
t 32 d510     'aad $16'
t 64 refused  'aam'
t 64 refused  'aad'

# rep nop is pause.
for mode in 32 64; do
    t $mode f390 'rep nop'
    t $mode f390 'repz nop'
    t $mode f390 'pause'
    # And a nop with the other prefix is that, which GNU takes.
    t $mode f290 'repne nop'
done

[ "$fail" -eq 0 ] && echo "ok: loop, jcxz and their kin; xlat, aam, aad; rep nop ($cases cases)"
exit "$fail"
