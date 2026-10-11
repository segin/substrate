#!/bin/sh
# The directives that say something to whoever is assembling, and the
# ones that are not directives at all.
#
# .error, .err and .abort fail the assembly; .warning warns; .print
# writes a line; .fail is an error below 500 and a warning from there.
# None was known, and a directive nothing knew was passed over in
# silence: a source that said `.error "this configuration is not
# supported"` was assembled, and so was one with a directive misspelt,
# without whatever the directive was to store.
#
# What GNU as 2.46 does, but for the wording.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# m WHAT STATUS STDERR STDOUT SOURCE [OPTION]: the exit status (0, or 1
# for any failure), a text standard error holds (`-` for an empty one),
# what standard output is, and whether an object is written.
m() {
    cases=$((cases + 1))
    printf "$5" > t.s
    rm -f t.o
    "$AS" -32 ${6:+"$6"} -o t.o t.s > out 2> err
    rc=$?
    [ "$rc" -ne 0 ] && rc=1
    if [ "$rc" != "$2" ]; then
        echo "FAIL $1: exit status $rc, not $2: $(head -1 err)"; fail=1; return
    fi
    if [ "$3" = - ]; then
        [ -s err ] && { echo "FAIL $1: says $(head -1 err)"; fail=1; }
    else
        grep -q -- "$3" err || { echo "FAIL $1: does not say '$3': $(head -1 err)"; fail=1; }
    fi
    [ "$(cat out)" = "$4" ] || { echo "FAIL $1: standard output is '$(cat out)'"; fail=1; }
    if [ "$rc" = 0 ]; then
        [ -s t.o ] || { echo "FAIL $1: no object"; fail=1; }
    else
        [ ! -s t.o ] || { echo "FAIL $1: an object was written"; fail=1; }
    fi
}

m ".error"                  1 'boom'      '' '.data\n.error "boom"\n.byte 1\n'
m ".error names its line"   1 ':3:.*boom' '' '.data\n.byte 1\n.error "boom"\n'
m ".error with no text"     1 '.error'    '' '.data\n.error\n'
m ".err"                    1 '.err'      '' '.data\n.err\n'
m ".abort"                  1 '.abort'    '' '.data\n.abort\n.byte 1\n'
m ".fail below 500"         1 '.fail 499' '' '.data\n.fail 499\n'
m ".fail 500"               0 '.fail 500' '' '.data\n.fail 500\n.byte 1\n'
m ".warning"                0 'careful'   '' '.data\n.warning "careful"\n.byte 1\n'
m ".warning with no text"   0 '.warning'  '' '.data\n.warning\n.byte 1\n'
m ".print"                  0 -           'hello' '.data\n.print "hello"\n.byte 1\n'

# In a branch not taken they say nothing.
m ".error in a false .if"   0 - '' '.data\n.if 0\n.error "no"\n.endif\n.byte 1\n'
m ".error in a true .if"    1 'yes' '' '.data\n.if 1\n.error "yes"\n.endif\n.byte 1\n'
m ".abort in a false .if"   0 - '' '.data\n.if 0\n.abort\n.endif\n.byte 1\n'
m ".warning in a .else"     0 - '' '.data\n.if 1\n.byte 1\n.else\n.warning "no"\n.endif\n'

# A warning is one like the others.
m ".warning, --fatal-warnings" 1 'careful' '' '.data\n.warning "careful"\n.byte 1\n' --fatal-warnings
m ".warning, --no-warn"        0 -         '' '.data\n.warning "careful"\n.byte 1\n' --no-warn

# A directive that is not one.
m "an unknown directive"    1 '\.foobar' '' '.data\n.foobar 1, 2\n.byte 1\n'
m "and its line"            1 ':4:'      '' '.data\n.byte 1\n.byte 2\n.foobar\n'
m "a misspelt one"          1 '\.quadd'  '' '.data\n.quadd 1\n'
m ".dc.q, which GNU as lacks" 1 '\.dc\.q' '' '.data\n.dc.q 1\n'
m "unknown, in a false .if" 0 - '' '.data\n.if 0\n.foobar\n.endif\n.byte 1\n'

# Every error is reported, not the first alone.
cases=$((cases + 1))
printf '.data\n.error "one"\n.foobar\n.error "three"\n' > all.s
"$AS" -32 -o all.o all.s > out 2> err
for want in one foobar three; do
    grep -q "$want" err || { echo "FAIL every error is reported: no '$want'"; fail=1; }
done

# And the statement's label is still a label.
cases=$((cases + 1))
printf '.data\n.byte 9\nx: .warning "w"\n.byte x - .\n' > lab.s
if "$AS" -32 -o lab.o lab.s > out 2> err; then
    objcopy -O binary --only-section=.data lab.o lab.bin
    got=$(od -An -v -tx1 lab.bin | tr -d ' \n')
    [ "$got" = 0900 ] || { echo "FAIL a label on a .warning: .data is $got"; fail=1; }
else
    echo "FAIL a label on a .warning: $(head -1 err)"; fail=1
fi

# What says nothing of the bytes is taken and does nothing, as before.
m "directives of no effect" 0 - '' '.arch i686\n.extern puts\n.file "x.c"\n.ident "cc"\n.text\nnop\n'

[ "$fail" -eq 0 ] && echo "ok: message directives and unknown ones ($cases cases)"
exit "$fail"
