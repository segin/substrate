#!/bin/sh
# An .include that a macro or a repeat writes (AS-T-154).
#
# Files are included before macros are expanded, so an .include that an
# expansion produces -- one in a macro's body, with the file's name an
# argument perhaps, or one in a .rept -- is still a directive when the
# source is made into tokens, and it is the tokenizer that reads the
# file.  That reading was thought to be code that could not run, no test
# having reached it.  It runs, and it was wrong: the file's statements
# had the file's own name and lines, so a file included twice had two
# statements for each of its lines with one name and one line between
# them, and the assembler, which tells statements apart by those, took
# the two for one.
#
#       .rept 2
#       .include "one.s"        with `nop` in it: `nop nop`, one
#       .endr                   statement, which is nopl or an error
#
# The file's lines are lines of the expanded text now, after the
# directive's, and a message about one still names the file and the line
# in it.  GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

printf '\tnop\n\tincl %%eax\n' > body.s
printf 'inc_label:\n\tnop\n' > label.s
printf '\tnop\n.include "body.s"\n\tcld\n' > nested.s
printf '\tnop\n\tbogus %%eax\n' > bad.s

# is NAME WANT: the mnemonics of NAME.s, in order.
is() {
    cases=$((cases + 1))
    rm -f "$1.o"
    if ! "$AS" --32 -o "$1.o" "$1.s" > out 2>&1; then
        echo "FAIL $1: $(tail -1 out)"; fail=1; return
    fi
    got=$(objdump -d --no-show-raw-insn "$1.o" | awk '/^ +[0-9a-f]+:/ { printf "%s ", $2 }' | sed 's/ $//')
    [ "$got" = "$2" ] || { echo "FAIL $1: [$got], not [$2]"; fail=1; }
}

# In a repeat: once for each time round.
printf '.rept 2\n.include "body.s"\n.endr\n\tret\n' > rept.s
is rept 'nop inc nop inc ret'
printf '.rept 3\n.include "body.s"\n.endr\n' > rept3.s
is rept3 'nop inc nop inc nop inc'
# A file of one line: its two statements were one, `nop nop`, which was
# `nopl` or an error.
printf '\tnop\n' > one.s
printf '.rept 2\n.include "one.s"\n.endr\n\tret\n' > one2.s
is one2 'nop nop ret'
printf '.rept 3\n.include "one.s"\n.endr\n\tret\n' > one3.s
is one3 'nop nop nop ret'

# In a macro, and the file's name an argument.
printf '.macro pull f\n.include "\\f"\n.endm\n\tret\n\tpull label.s\n\tret\n' > macro.s
is macro 'ret nop ret'
printf '.macro twice\n.include "body.s"\n.include "body.s"\n.endm\n\ttwice\n\ttwice\n\thlt\n' > twice.s
is twice 'nop inc nop inc nop inc nop inc hlt'

# A file so included that includes another, and what follows it.
printf '.rept 1\n.include "nested.s"\n.endr\n\tret\n' > nest.s
is nest 'nop nop inc cld ret'
printf '.rept 2\n.include "nested.s"\n.endr\n\tret\n' > nest2.s
is nest2 'nop nop inc cld nop nop inc cld ret'

# A label in a file included once is defined once, and is found.
printf '.rept 1\n.include "label.s"\n.endr\n\tjmp inc_label\n' > lab.s
is lab 'nop jmp'

# A message about a line of such a file names the file and that line,
# each time it is read.
cases=$((cases + 1))
printf '.rept 2\n.include "bad.s"\n.endr\n' > err.s
if "$AS" --32 -o err.o err.s > out 2>&1; then
    echo "FAIL err: assembled"; fail=1
elif [ "$(grep -c '^bad\.s:2: Error: ' out)" != 2 ]; then
    echo "FAIL err: not two errors at bad.s:2: $(tr '\n' '|' < out)"; fail=1
fi

# A file that is not there.
cases=$((cases + 1))
printf '.rept 1\n.include "nosuch.s"\n.endr\n' > missing.s
if "$AS" --32 -o missing.o missing.s > out 2>&1; then
    echo "FAIL missing: assembled"; fail=1
fi

[ "$fail" -eq 0 ] && echo "ok: includes written by expansion ($cases cases)"
exit "$fail"
