#!/bin/sh
# An option the assembler has not got is an error that names it, and
# nothing is assembled; and what is inside a -Wa is an option like any
# other -- read, obeyed, and refused if there is no such option.
#
# The assembler took anything that began with a dash and did nothing
# with it, so a misspelt option assembled as if it had not been given;
# and it kept the contents of a -Wa without reading them at all.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
bad() { echo "FAIL: $*"; fail=1; }

printf '\t.data\n\t.long 0x11223344\n' > plain.s
printf '\t.data\n\t.long X\n' > usesx.s

data() {
    objcopy -O binary --only-section=.data "$1" data.bin 2>/dev/null || return 1
    od -An -v -tx1 data.bin | tr -d ' \n'
}

# Options there are not, long and short, alone and after a real one, and
# inside a -Wa in either of its forms.  Each is refused by name, with
# exit status 1 and no object.
n=0
refused() {
    name=$1; shift
    n=$((n + 1))
    "$AS" "$@" -o "bad$n.o" plain.s > out 2>&1
    rc=$?
    [ "$rc" -eq 1 ] || bad "$*: exit $rc, not 1"
    [ -e "bad$n.o" ] && bad "$*: an object was written"
    grep -q -- "$name" out || bad "$*: the message does not name $name: $(head -1 out)"
}
refused --bogus-option  -32 --bogus-option
refused --bogus-option  --bogus-option -32
refused -Z              -32 -Z
refused --warnn         -32 --warnn
refused --no-such       -32 -Wa,--no-such
refused --no-such       -32 -Wa,--32,--no-such
refused --no-such       -32 -Wa --no-such

# The control: with none of them, the same source assembles.
"$AS" -32 -o good.o plain.s > out 2>&1 || bad "the source is refused with no bad option: $(head -1 out)"
[ "$(data good.o)" = 44332211 ] || bad "the source assembles to '$(data good.o)'"

# What is in a -Wa is obeyed: a symbol defined there is defined, in each
# way of writing it, and a mode chosen there is the mode.
"$AS" -32 -Wa,--defsym=X=7 -o wa1.o usesx.s > out 2>&1 || bad "-Wa,--defsym=X=7: refused: $(head -1 out)"
[ "$(data wa1.o)" = 07000000 ] || bad "-Wa,--defsym=X=7: X is '$(data wa1.o)'"
"$AS" -32 -Wa,--defsym,X=8 -o wa2.o usesx.s > out 2>&1 || bad "-Wa,--defsym,X=8: refused: $(head -1 out)"
[ "$(data wa2.o)" = 08000000 ] || bad "-Wa,--defsym,X=8: X is '$(data wa2.o)'"
"$AS" -Wa --defsym=X=9,--64 -o wa3.o usesx.s > out 2>&1 || bad "-Wa --defsym=X=9,--64: refused: $(head -1 out)"
[ "$(data wa3.o)" = 09000000 ] || bad "-Wa --defsym=X=9,--64: X is '$(data wa3.o)'"
readelf -h wa3.o 2>/dev/null | grep -q 'ELF64' || bad "-Wa ...,--64: the object is not a 64-bit one"

# A -Wa with nothing in it is a misuse.
"$AS" -32 -Wa, -o empty.o plain.s > out 2>&1
[ $? -eq 2 ] || bad "-Wa, with nothing after the comma is not refused as a misuse"

# What a compiler's driver passes and the assembler has no use for is
# taken, and changes nothing.
for opt in --gdwarf-2 --gdwarf-5 --noexecstack --from-cc -Wa,--gdwarf-2; do
    "$AS" -32 "$opt" -o "ign.o" plain.s > out 2>&1 || bad "$opt: refused: $(head -1 out)"
    cmp -s good.o ign.o || bad "$opt: the object differs from the one made without it"
    [ -s out ] && bad "$opt: says something: $(head -1 out)"
done

[ "$fail" -eq 0 ] && echo "ok: unknown options are refused, and -Wa is read"
exit "$fail"
