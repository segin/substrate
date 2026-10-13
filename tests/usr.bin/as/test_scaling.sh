#!/bin/sh
# Assembly time is linear in what is assembled (AS-T-228).
#
# The audit's shapes -- plain instructions, a load of a symbol of
# another object each, a word of data naming one each, a branch to a
# label each, a section for every function, and a label less the label
# before it -- are each assembled at n statements and at 4n, and 4n may
# take no more than five times as long as n.  (Four times is linear;
# the fifth is for the noise of a busy machine.  Branches between
# labels took 24 times as long at 4n, a label less a label 18 and a
# section per function 7.)
#
# Times are of the processor, user and system, from the shell's own
# `times`; each shape is run twice and the lesser time kept.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
N=${AS_SCALING_N:-20000}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

gen() {
    awk -v shape="$1" -v n="$2" 'BEGIN {
        if (shape == "plain") for (i = 0; i < n; i++) print "addl $1,%eax"
        if (shape == "extern") for (i = 0; i < n; i++) print "movl ext" i ",%eax"
        if (shape == "data") for (i = 0; i < n; i++) print ".long ext" i
        if (shape == "branch") for (i = 0; i < n; i++) print ".L" i ": jne .L" ((i * 7919) % n)
        if (shape == "section") for (i = 0; i < n / 4; i++) {
            print ".section .text.f" i ",\"ax\",@progbits"; print "f" i ": pushl %ebp"; print "movl %esp,%ebp"; print "ret"
        }
        if (shape == "difference") { print "d0: .long 0"; for (i = 1; i < n; i++) print "d" i ": .long d" i " - d" (i - 1) }
    }'
}

# The processor time of one assembly, in hundredths of a second.
cost() {
    sh -c '"$1" --32 -o t.o "$2" > /dev/null 2>&1 || echo failed >&2; times' sh "$AS" "$1" 2> err | awk '
        function secs(t,   m, s) { m = t; sub(/m.*/, "", m); s = t; sub(/.*m/, "", s); sub(/s/, "", s); return m * 60 + s }
        NR == 2 { printf "%d\n", (secs($1) + secs($2)) * 100 + 0.5 }'
    [ -s err ] && return 1
    return 0
}

fail=0
cases=0
for shape in plain extern data branch section difference; do
    cases=$((cases + 1))
    gen "$shape" "$N" > one.s
    gen "$shape" $((N * 4)) > four.s
    a=$(cost one.s) || { echo "FAIL $shape: not assembled at n=$N"; fail=1; continue; }
    b=$(cost four.s) || { echo "FAIL $shape: not assembled at n=$((N * 4))"; fail=1; continue; }
    # The lesser of the times of each, measured again while the larger
    # looks too long: the suite runs its tests side by side, on a
    # machine that is doing other things, and the clock counts in
    # hundredths.  (A run of GitHub's took 90 ms and 490 for a shape
    # that takes 440 and 1880 here, and failed.)  What is quadratic
    # stays too long however often it is measured.
    tries=0
    while :; do
        a2=$(cost one.s)
        b2=$(cost four.s)
        [ "$a2" -lt "$a" ] && a=$a2
        [ "$b2" -lt "$b" ] && b=$b2
        tries=$((tries + 1))
        # (Under five hundredths of a second there is nothing to
        # measure; and two hundredths are the clock's own.)
        low=$a
        [ "$low" -lt 5 ] && low=5
        [ "$b" -le $((low * 5 + 2)) ] && break
        [ "$tries" -ge 4 ] && break
    done
    # And at twice the size, before it is called too long.  The smaller
    # of the two is what a processor's cache holds and the larger is
    # not, on some machines: each statement of the larger then costs
    # more, though the work is in proportion.  (A run of GitHub's, of
    # the assembler as a 32-bit program, took 100 ms and 590 for the
    # loads of external symbols, once those had been made quicker; here
    # they take 160, 660 and, for four times that again, 2,540.)  At
    # twice the size both are past the cache, and what is quadratic is
    # further over than it was.
    if [ "$b" -gt $((low * 5 + 2)) ]; then
        gen "$shape" $((N * 2)) > one.s
        gen "$shape" $((N * 8)) > four.s
        a2=$(cost one.s) && b2=$(cost four.s) || { echo "FAIL $shape: not assembled at n=$((N * 8))"; fail=1; continue; }
        low2=$a2
        [ "$low2" -lt 5 ] && low2=5
        if [ "$b2" -gt $((low2 * 5 + 2)) ]; then
            echo "FAIL $shape: $N statements in ${a}0 ms and $((N * 4)) in ${b}0 ms, more than five times as long; and $((N * 2)) in ${a2}0 ms and $((N * 8)) in ${b2}0 ms"
            fail=1
        fi
    fi
done

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
