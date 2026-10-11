#!/bin/sh
# A number too wide for the directive that stores it is stored cut down
# to the directive's width, and the assembler says so: `.byte 256` is
# the byte 00 and "value 0x100 truncated to 0x0".  The bytes were right;
# nothing was said.
#
# What counts as too wide is GNU as 2.46's test: bits lost from the
# value and from its negative both.  So 255 and -128 pass for a byte,
# and so does -129, which is stored as 7f, where 256 and -256 do not.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# v WANT WARNED SOURCE: the bytes of .data, and whether a truncation is
# warned of (the text of it, or `-`).
v() {
    for mode in 32 64; do
        cases=$((cases + 1))
        printf '\t.data\n\t%s\n' "$3" > t.s
        rm -f t.o
        if ! "$AS" "-$mode" -o t.o t.s > out 2> err; then
            echo "FAIL $mode [$3]: refused: $(head -1 err)"
            fail=1
            continue
        fi
        objcopy -O binary --only-section=.data t.o data.bin 2>/dev/null
        got=$(od -An -v -tx1 data.bin | tr -d ' \n')
        said=$(sed -n 's/.*\(value 0x[0-9a-f]* truncated to 0x[0-9a-f]*\).*/\1/p' err | head -1)
        if [ "$got" != "$1" ] || [ "${said:--}" != "$2" ]; then
            echo "FAIL $mode [$3]: $got, ${said:-no warning}; and GNU as: $1, $2"
            fail=1
        fi
    done
}

v 00        'value 0x100 truncated to 0x0'            '.byte 256'
v 00        'value 0x100 truncated to 0x0'            '.byte 1+255'
v ff0100    'value 0x1ff truncated to 0xff'           '.byte 0x1ff, 1, 0'
v 00        'value 0xffffffffffffff00 truncated to 0x0' '.byte -256'
v 0000      'value 0x10000 truncated to 0x0'          '.word 0x10000'
v 7011      'value 0x11170 truncated to 0x1170'       '.short 70000'
v 4523      'value 0x12345 truncated to 0x2345'       '.hword 0x12345'
v 00000000  'value 0x100000000 truncated to 0x0'      '.long 0x100000000'
v ffffffff  'value 0x1ffffffff truncated to 0xffffffff' '.int 0x1ffffffff'
v 89674523  'value 0x123456789 truncated to 0x23456789' '.4byte 0x123456789'

# What fits, by GNU's test, and is not remarked on.
v ff        -  '.byte 255'
v 80        -  '.byte -128'
v 7f        -  '.byte -129'
v 01        -  '.byte -255'
v ffff      -  '.word 65535'
v ff7f      -  '.word -32769'
v ffffffff  -  '.long 0xffffffff'
v 00000080  -  '.long -2147483648'
v ffffff7f  -  '.long -2147483649'
v 8877665544332211 - '.quad 0x1122334455667788'

# It is a warning like the others.
cases=$((cases + 2))
printf '\t.data\n\t.byte 256\n' > w.s
"$AS" -32 --fatal-warnings -o w.o w.s > out 2> err &&
    { echo "FAIL: --fatal-warnings does not count a truncated value"; fail=1; }
"$AS" -32 --no-warn -o w.o w.s > out 2> err
[ -s err ] && { echo "FAIL: --no-warn does not silence it: $(head -1 err)"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: values too wide for their directive ($cases cases)"
exit "$fail"
