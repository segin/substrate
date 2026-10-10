#!/bin/sh
# branch-relax-check.sh FILLER LABEL PART...
#
# The body of the tests of a branch's size: a branch to a label in its
# own section is two bytes where the label is within reach of a byte's
# displacement, and the long form where it is not -- one byte too far is
# long, and one byte nearer is not.
#
# FILLER is what lies between the branch and its label: `fill` for a
# .fill directive, `nop` for that many one-byte instructions.  LABEL is
# the label's name: a compiler's `.Lt`, or an ordinary `target`.  Each
# PART is a set of cases: `one` for a branch alone, at each distance
# about the limit; `grew` for a branch across one that must be long;
# `tight` for a branch that is short only if every branch it crosses was
# given its true size.
#
# What is expected is computed here from the instruction set's own rule
# -- the displacement is counted from the end of the branch, and the
# short form holds -128 to 127 -- and the object is held to it byte for
# byte, with no relocation.  GNU as 2.46 writes the same in every case,
# as this script run with it as $AS shows.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
filler_kind=$1
label=$2
shift 2
parts=" $* "
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
# The longest distance tried: past sixteen bits where the filler is one
# directive, and less where it is a line for every byte.
far=40000
[ "$filler_kind" = nop ] && far=3000
cases=0

# filler N: N bytes of 0x90, as source.
filler() {
    [ "$1" -gt 0 ] || return 0
    if [ "$filler_kind" = fill ]; then
        printf '\t.fill %d, 1, 0x90\n' "$1"
    else
        awk -v n="$1" 'BEGIN { for (i = 0; i < n; i++) print "\tnop" }'
    fi
}

# nops N: N bytes of 0x90, in hex.
nops() {
    awk -v n="$1" 'BEGIN { for (i = 0; i < n; i++) printf "90" }'
}

# le32 V: V as four bytes, least first, in hex.
le32() {
    printf '%02x%02x%02x%02x' $(($1 & 255)) $((($1 >> 8) & 255)) $((($1 >> 16) & 255)) $((($1 >> 24) & 255))
}

# check WHAT WANT: t.s assembles, in each mode, to WANT and nothing to
# relocate.
check() {
    for mode in -32 -64; do
        cases=$((cases + 1))
        rm -f t.o
        if ! "$AS" "$mode" -o t.o t.s > out 2>&1; then
            echo "FAIL $mode $1: refused: $(head -1 out)"
            fail=1
            continue
        fi
        objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
        got=$(od -An -v -tx1 text.bin | tr -d ' \n')
        if [ "$got" != "$2" ]; then
            # The whole of it is hundreds of bytes of filler: show where
            # the branch is.
            echo "FAIL $mode $1: ${#got} hex digits beginning $(printf '%.24s' "$got"), wanted ${#2} beginning $(printf '%.24s' "$2")"
            fail=1
        fi
        if readelf -rW t.o 2>/dev/null | grep -q ' R_'; then
            echo "FAIL $mode $1: a relocation, for a label in the branch's own section"
            fail=1
        fi
    done
}

# Each branch: its mnemonic, its short opcode, its long opcode and the
# long form's length.
branches='jmp:eb:e9:5 je:74:0f84:6 jne:75:0f85:6 jg:7f:0f8f:6 jb:72:0f82:6 jle:7e:0f8e:6'
case $parts in *" one "*) ;; *) branches= ;; esac

for b in $branches; do
    mn=${b%%:*}; rest=${b#*:}
    short=${rest%%:*}; rest=${rest#*:}
    long=${rest%%:*}; longlen=${rest#*:}

    # Forward: the displacement is the filler's length.
    for n in 0 1 100 125 126 127 128 129 130 300 "$far"; do
        { printf '\t.text\n\t%s %s\n' "$mn" "$label"; filler "$n"; printf '%s:\n\tret\n' "$label"; } > t.s
        if [ "$n" -le 127 ]; then
            want="$short$(printf '%02x' "$n")$(nops "$n")c3"
        else
            want="$long$(le32 "$n")$(nops "$n")c3"
        fi
        check "$mn forward over $n" "$want"
    done

    # Backward: the filler's length and the branch's own, negative.
    for n in 0 1 100 124 125 126 127 128 129 300 "$far"; do
        { printf '\t.text\n%s:\n' "$label"; filler "$n"; printf '\t%s %s\n\tret\n' "$mn" "$label"; } > t.s
        if [ $((n + 2)) -le 128 ]; then
            want="$(nops "$n")$short$(printf '%02x' $(((-(n + 2)) & 255)))c3"
        else
            want="$(nops "$n")$long$(le32 $((-(n + longlen))))c3"
        fi
        check "$mn back over $n" "$want"
    done
done

# One branch's size is part of another's distance.
#
# The first jump crosses the second and 124 bytes: 126 if the second is
# short and 129 if it is long.  The second has 254 bytes to cross and is
# long, so the first must be: sized before the second grew, it would be
# short and three bytes short of its label.
case $parts in *" grew "*)
    {
        printf '\t.text\n\tjmp %s\n\tjmp %s2\n' "$label" "$label"
        filler 124
        printf '%s:\n' "$label"
        filler 130
        printf '%s2:\n\tret\n' "$label"
    } > t.s
    check "a jump across a jump that grew" "e981000000e9fe000000$(nops 254)c3"
    ;;
esac

case $parts in *" tight "*)
    # And the other way: the second has 125 bytes to cross and is short,
    # so the first crosses 127 and is short too.  Taking every branch for
    # long until shown otherwise would make it long.
    {
        printf '\t.text\n\tjmp %s\n\tjmp %s2\n' "$label" "$label"
        filler 125
        printf '%s:\n%s2:\n\tret\n' "$label" "$label"
    } > t.s
    check "a jump across a jump that stayed short" "eb7feb7d$(nops 125)c3"

    # A conditional jump back across a long jump forward: the loop a
    # compiler writes.  The jump forward crosses 200 bytes and is long,
    # five bytes; the jump back crosses 121 and those five and its own
    # two, -128, and is short -- by one byte.
    {
        printf '\t.text\n%s:\n' "$label"
        filler 121
        printf '\tjmp %s2\n\tjne %s\n' "$label" "$label"
        filler 198
        printf '%s2:\n\tret\n' "$label"
    } > t.s
    check "a jump back across a long jump, at the limit" "$(nops 121)e9c80000007580$(nops 198)c3"
    ;;
esac

[ "$cases" -gt 0 ] || { echo "FAIL: no part of the cases was named"; exit 1; }
[ "$fail" -eq 0 ] && echo "ok: branch sizes, $filler_kind filler, label $label ($cases cases)"
exit "$fail"
