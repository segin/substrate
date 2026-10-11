#!/bin/sh
# The listing -al writes (AS-T-391).
#
# It was the source with its lines numbered and nothing more.  A listing
# is for seeing what each line became: beside each line, the offset in
# its section at which its bytes begin, and the bytes; four to a line,
# and the rest on lines after it.  The layout is GNU as's:
#
#    2 0000 B8010000 	f:	mov $1, %eax
#    2      00
#    3 0005 C3       		ret
#
# GNU as passes this test, given that it writes its listing to the
# standard output with a heading before and tables of symbols after:
# the test takes the lines between.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# listing SOURCE: the listing's lines of the source, in `lst`.
listing() {
    printf "$1" > t.s
    rm -f t.o lst raw
    if "$AS" --version 2> /dev/null | grep -q '^GNU assembler'; then
        "$AS" --32 -al -o t.o t.s > raw 2> err || return 1
        sed -n '4,$p' raw | sed '/^.$/,$d' > lst
    else
        "$AS" --32 -al=lst -o t.o t.s > raw 2> err || return 1
    fi
}

# has WHAT SOURCE LINE...: each LINE is a line of the listing, as it is.
has() {
    what=$1
    cases=$((cases + 1))
    if ! listing "$2"; then
        echo "FAIL $what: $(head -1 err)"; fail=1; return
    fi
    shift 2
    for want in "$@"; do
        grep -qxF -- "$want" lst || { echo "FAIL $what: no line '$want' in:"; sed 's/^/    /' lst; fail=1; return; }
    done
}

T=$(printf '\t')
has "an instruction, and one of a byte" 'mov $1, %%eax\nret\n' \
    "   1 0000 B8010000 ${T}mov \$1, %eax" \
    "   1      00" \
    "   2 0005 C3       ${T}ret"
has "a line that is nothing" '\n# said\n\t.text\nnop\n' \
    "   1              ${T}" \
    "   2              ${T}# said" \
    "   3              ${T}${T}.text" \
    "   4 0000 90       ${T}nop"
has "a label" 'f:\tnop\ng:\n\tret\n' \
    "   1 0000 90       ${T}f:${T}nop" \
    "   2              ${T}g:" \
    "   3 0001 C3       ${T}${T}ret"
has "data, twelve bytes of it" '\t.data\n\t.long 1,2,3\n' \
    "   2 0000 01000000 ${T}${T}.long 1,2,3" \
    "   2      02000000 " \
    "   2      03000000 "
has "a string" '\t.data\n\t.byte 9\n\t.asciz "hello world"\n' \
    "   2 0000 09       ${T}${T}.byte 9" \
    "   3 0001 68656C6C ${T}${T}.asciz \"hello world\"" \
    "   3      6F20776F " \
    "   3      726C6400 "
has "each section from nothing" '\tnop\n\t.data\n\t.byte 1\n\t.text\n\tret\n\t.data\n\t.byte 2\n' \
    "   1 0000 90       ${T}${T}nop" \
    "   3 0000 01       ${T}${T}.byte 1" \
    "   5 0001 C3       ${T}${T}ret" \
    "   7 0001 02       ${T}${T}.byte 2"
has "two statements on a line" '\tnop; ret\n\thlt\n' \
    "   1 0000 90C3     ${T}${T}nop; ret" \
    "   2 0002 F4       ${T}${T}hlt"
has "an offset past nine" '\t.skip 12\n\tnop\n' \
    "   1 0000 00000000 ${T}${T}.skip 12" \
    "   2 000c 90       ${T}${T}nop"
has "what a macro makes, on the line that calls it" \
    '\t.macro m\n\tinc %%eax\n\tdec %%eax\n\t.endm\n\tm\n\tret\n' \
    "   2              ${T}${T}inc %eax" \
    "   5 0000 4048     ${T}${T}m" \
    "   6 0002 C3       ${T}${T}ret"

# No more than five lines for one line of the source, however many bytes.
cases=$((cases + 1))
if listing '\t.data\n\t.fill 40,1,7\n\t.byte 1\n'; then
    n=$(grep -c '^   2 ' lst)
    [ "$n" -eq 5 ] || { echo "FAIL forty bytes: $n lines of the listing, not 5"; fail=1; }
    grep -qxF "   3 0028 01       ${T}${T}.byte 1" lst || { echo "FAIL forty bytes: the line after is not at 0028"; fail=1; }
else
    echo "FAIL forty bytes: $(head -1 err)"; fail=1
fi

# The object is the one made without a listing.
cases=$((cases + 1))
printf 'f:\tmov $1, %%eax\n\tjmp f\n\t.data\n\t.long f\n' > t.s
"$AS" --32 -o plain.o t.s 2> /dev/null
"$AS" --32 -al=l.lst -o listed.o t.s > /dev/null 2>&1
cmp -s plain.o listed.o || { echo "FAIL: the object made with a listing is another"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: listing ($cases cases)"
exit "$fail"
