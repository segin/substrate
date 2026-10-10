#!/bin/sh
# Where the source comes from: the standard input when no file is named
# or `-` is, and several files as one source, in the order given.
#
# The assembler took `-` for an option and a second file for a mistake,
# so a compiler run with -pipe, which writes the assembly down a pipe,
# could not use it.  The bytes are GNU as 2.46's, given
# -mx86-used-note=no -- without which GNU begins each file after the
# first in the note section it ended the last with.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
bad() { echo "FAIL: $*"; fail=1; }

data() {
    objcopy -O binary --only-section=.data "$1" data.bin 2>/dev/null || return 1
    od -An -v -tx1 data.bin | tr -d ' \n'
}

printf '\t.data\na:\t.byte 1\n' > one.s
# No newline ends this one: the next file must still begin a line.
printf '\t.byte 2\n\t.long b - a' > two.s
printf 'b:\t.byte 3\n' > three.s

# The standard input, named by nothing and by `-`.
printf '\t.data\n\t.byte 7\n' | "$AS" -32 -o none.o > out 2>&1 || bad "no file named: refused: $(head -1 out)"
[ "$(data none.o)" = 07 ] || bad "no file named: .data is '$(data none.o)', not '07'"
printf '\t.data\n\t.byte 8\n' | "$AS" -32 -o dash.o - > out 2>&1 || bad "-: refused: $(head -1 out)"
[ "$(data dash.o)" = 08 ] || bad "-: .data is '$(data dash.o)', not '08'"

# Not read when a file is named: it would be assembled after the file.
printf '\t.byte 0xee\n' | "$AS" -32 -o named.o one.s > out 2>&1 || bad "a file with something on the standard input: refused"
[ "$(data named.o)" = 01 ] || bad "a file is named and the standard input was read too: '$(data named.o)'"

# Several files are one source: the section carries over, a label of one
# is a label in the next, and a file's last line ends where the file does.
"$AS" -32 -o three.o one.s two.s three.s > out 2>&1 || bad "three files: refused: $(head -1 out)"
[ "$(data three.o)" = 01020600000003 ] || bad "three files: .data is '$(data three.o)', not '01020600000003'"

# In the order given.
printf '\t.data\n\t.byte 4\n' > four.s
printf '\t.byte 5\n' > five.s
"$AS" -32 -o ab.o four.s five.s > out 2>&1 || bad "two files: refused: $(head -1 out)"
"$AS" -32 -o ba.o five.s four.s > out 2>&1 || bad "two files, the other way: refused: $(head -1 out)"
[ "$(data ab.o)" = 0405 ] || bad "four.s five.s: .data is '$(data ab.o)', not '0405'"
# five.s first puts its byte in .text, where a source starts.
[ "$(data ba.o)" = 04 ] || bad "five.s four.s: .data is '$(data ba.o)', not '04'"

# The standard input among files, where the `-` is.
printf '\t.byte 9\n' | "$AS" -32 -o mixed.o one.s - three.s > out 2>&1 || bad "file - file: refused: $(head -1 out)"
[ "$(data mixed.o)" = 010903 ] || bad "file - file: .data is '$(data mixed.o)', not '010903'"

# A file that cannot be read is an error that names it, wherever it is
# in the list, and nothing is written.
"$AS" -32 -o missing.o one.s no-such-file.s > out 2>&1
[ $? -eq 1 ] || bad "a second file that is not there: not an error"
grep -q 'no-such-file\.s' out || bad "a second file that is not there: not named: $(head -1 out)"
[ -e missing.o ] && bad "a second file that is not there: an object was written"

# Where the object goes when no -o says: a.out, here, and nothing else.
mkdir bare && (
    cd bare &&
    "$AS" -32 ../one.s > out 2>&1 &&
    [ "$(ls | grep -v '^out$' | tr '\n' ' ')" = "a.out " ] &&
    cmp -s a.out ../named.o
) || bad "with no -o the object is not a.out, alone: $(ls bare | tr '\n' ' ')"

# A compiler with -pipe: the assembly comes down a pipe to this
# assembler, and the object runs.
if command -v gcc > /dev/null 2>&1; then
    mkdir tools
    ln -s "$AS" tools/as
    cat > prog.c <<'SRC'
#include <stdio.h>
int counter = 5;
int main(void) { printf("piped %d\n", counter + 2); return 0; }
SRC
    if gcc -pipe -B"$work/tools/" -c prog.c -o prog.o > out 2>&1; then
        if gcc prog.o -o prog > out 2>&1; then
            [ "$(./prog)" = "piped 7" ] || bad "gcc -pipe: the program prints '$(./prog)'"
        else
            bad "gcc -pipe: the object does not link: $(head -1 out)"
        fi
    else
        bad "gcc -pipe with this assembler: $(grep -m1 -i 'error' out)"
    fi
else
    bad "there is no gcc to pipe assembly from"
fi

[ "$fail" -eq 0 ] && echo "ok: the standard input, and several sources as one"
exit "$fail"
