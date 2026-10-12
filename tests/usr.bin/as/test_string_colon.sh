#!/bin/sh
# A string that is a colon, and `.base64`.
#
# `.string ":"` was read as a label named `.string` -- a name, and a
# colon after it -- with nothing behind: the directive was gone, no byte
# of it was written and nothing was said.  It is what a compiler writes
# for the ":" of a C source, and a program built from that had an empty
# string where its colon was.  A string is a string whatever is in it.
#
# `.base64` is the bytes its strings say in base 64, which a current gcc
# writes its tables of constants with; it was no directive.
#
# The bytes of .data are held to what GNU as 2.47 writes.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

data() {
    cases=$((cases + 1))
    printf '\t.data\n%s\n' "$1" > t.s
    if ! "$AS" --64 -o t.o t.s 2> err; then
        got="refused"
    else
        objcopy -O binary --only-section=.data t.o t.bin
        got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    fi
    [ "$got" = "$2" ] || { echo "FAIL [$1]: $got, not $2"; fail=1; }
}

data '	.string ":"' 3a00
data '	.ascii ":"' 3a
data '	.asciz ":"' 3a00
data '	.string ":", ":"' 3a003a00
data '	.ascii "a" ":" "b"' 613a62
data 'lab:	.string ":"' 3a00
data '	.string "::"' 3a3a00
data '	.string ";"' 3b00
data '	.string "#"' 2300
data '	.string "="' 3d00
data '	.string ","' 2c00
data '	.string "/"' 2f00
# A label is still one, and a name before a colon that is no string.
data 'a: b:	.byte 1' 01
data '1:	.byte 2' 02

data '	.base64 "EQESBwA="' 1101120700
data '	.base64 "ZpAA"' 669000
data '	.base64 "QQ=="' 41
data '	.base64 "QUI="' 4142
data '	.base64 "QUJD", "RA=="' 41424344
data '	.base64 ""' refused
data '	.base64 "+/+/"' fbffbf
data '	.base64 "Q*=="' refused

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
