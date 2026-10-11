#!/bin/sh
# `.file N "name"` and `.loc`, and what a compiler's configure script
# concludes from them (AS-T-144).
#
# GCC's configure asks an assembler two things about these.  It gives a
# number to two files, and takes an assembler that says nothing for one
# that does not keep the numbers it is given; and it assembles a `.loc`
# and looks for a `.debug_line` section, and takes an assembler that
# makes one for one that writes line tables, and leaves its own
# unwritten.
#
# This assembler took the number twice, and made a `.debug_line` with
# nothing in it.  So it was found to be broken where it was not, and to
# make line tables, which it does not.  It refuses the number, as GNU as
# does; and it makes no `.debug_line`, which is not what GNU as does and
# is the truth: the directives are taken and no table is made of them.
#
# The refusals here are GNU as's too.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
gnu=0
"$AS" --version 2> /dev/null | grep -q '^GNU assembler' && gnu=1

# refused WHAT SOURCE
refused() {
    cases=$((cases + 1))
    printf "$2" > t.s
    if "$AS" --32 -o t.o t.s > out 2>&1; then
        echo "FAIL $1: assembled"; fail=1
    fi
}

# taken WHAT SOURCE
taken() {
    cases=$((cases + 1))
    printf "$2" > t.s
    rm -f t.o
    if ! "$AS" --32 -o t.o t.s > out 2>&1; then
        echo "FAIL $1: $(tail -1 out)"; fail=1
    fi
}

refused "a number given to a second file" '\t.file 1 "foo.s"\n\t.file 1 "bar.s"\n'
refused "and after others" '\t.file 1 "a.c"\n\t.file 2 "b.c"\n\t.file 3 "c.c"\n\t.file 2 "d.c"\n'
taken "a number given to the same file again" '\t.file 1 "foo.s"\n\t.file 1 "foo.s"\n\tnop\n'
taken "several files, and the one with no number" '\t.file "plain.c"\n\t.file 1 "a.c"\n\t.file 2 "b.c"\n\tnop\n'
taken "file 0, which a compiler writes for the unit" '\t.file 0 "a.c"\n\t.file 1 "a.c"\n\tnop\n'

# The message says which number, and whose it is.
cases=$((cases + 1))
printf '\tnop\n\t.file 1 "foo.s"\n\t.file 1 "bar.s"\n' > t.s
"$AS" --32 -o t.o t.s > out 2>&1
grep -q '^t\.s:3: Error: .*1' out || { echo "FAIL the message: $(tr '\n' '|' < out)"; fail=1; }

# `.loc` is taken; this assembler makes no line table of it, and no
# section that says it has.
taken "a .loc" '\t.text\n\t.file 1 "t.c"\n\t.loc 1 3 0\n\tnop\n\t.loc 1 5 0\n\tret\n'
if [ "$gnu" = 0 ]; then
    cases=$((cases + 1))
    if readelf -SW t.o | grep -q 'debug_line'; then
        echo "FAIL: a .debug_line with no line table in it"; fail=1
    fi
    cases=$((cases + 1))
    got=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$got" = 90c3 ] || { echo "FAIL the bytes with .loc: $got"; fail=1; }
fi

[ "$fail" -eq 0 ] && echo "ok: file numbers ($cases cases)"
exit "$fail"
