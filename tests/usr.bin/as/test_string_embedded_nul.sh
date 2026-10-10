#!/bin/sh
# A string with a NUL in it: every byte written is emitted, the ones after
# the NUL too, and what follows the string is where it should be.
#
# A string held as a C string loses what follows its first NUL, and the
# faults that makes are of three kinds, each looked for here: bytes
# missing from the section; a label, or a difference of two labels, placed
# by the shortened length; and the next directive's bytes written over the
# tail.  Each line below is a source, the section its bytes are in, and
# those bytes as GNU as 2.46 emits them, in both modes.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# bytes OBJECT SECTION: the section's contents, in hex, every byte.
bytes() {
    objcopy -O binary --only-section="$2" "$1" sec.bin 2>/dev/null || return 1
    od -An -v -tx1 sec.bin | tr -d ' \n'
}

# t SECTION WANT SOURCE...: SOURCE's lines, in SECTION, assemble to WANT.
t() {
    sec=$1; want=$2; shift 2
    for mode in --32 --64; do
        cases=$((cases + 1))
        {
            case $sec in
            .data|.text) printf '\t%s\n' "$sec" ;;
            *) printf '\t.section %s,"a",@progbits\n' "$sec" ;;
            esac
            for line in "$@"; do printf '%s\n' "$line"; done
        } > t.s
        rm -f t.o
        if ! "$AS" "$mode" -o t.o t.s > out 2>&1; then
            echo "FAIL $mode [$*]: refused: $(head -1 out)"
            fail=1
            continue
        fi
        got=$(bytes t.o "$sec")
        if [ "$got" != "$want" ]; then
            echo "FAIL $mode [$*]: $sec is '$got', GNU as writes '$want'"
            fail=1
        fi
        # The section header's size, which is not derived from the bytes
        # objcopy copied: half the hex digits.
        size=$(readelf -SW t.o 2>/dev/null | awk -v s="$sec" '{ for (i = 1; i < NF; i++) if ($i == s) print $(i + 4) }')
        if [ "$((0x${size:-0}))" -ne "$((${#want} / 2))" ]; then
            echo "FAIL $mode [$*]: $sec has size 0x$size in its header, for $((${#want} / 2)) bytes"
            fail=1
        fi
    done
}

# The NUL alone, first, last, doubled, and between.
t .data 00                      '.ascii "\0"'
t .data 0000                    '.asciz "\0"'
t .data 000000                  '.ascii "\0\0\0"'
t .data 610062                  '.ascii "a\0b"'
t .data 0061                    '.ascii "\0a"'
t .data 6100                    '.ascii "a\0"'
t .data 747261696c696e67000000  '.asciz "trailing\0\0"'

# Each way of writing it.  A hexadecimal escape takes every hexadecimal
# digit that follows it and keeps the low eight bits: "\x00b" is one byte.
t .data 410042                  '.ascii "A\000B"'
t .data 0031                    '.ascii "\0001"'
t .data 410042                  '.ascii "\x41\x0\x42"'
t .data 00                      '.ascii "\x00"'
t .data 610b                    '.ascii "a\x00b"'
t .data 00                      '.ascii "\400"'
t .data ff00ff                  '.ascii "\377\0\377"'

# Each directive, and more than one string to a directive.
t .data 78007900                '.string "x\0y"'
t .data 53756363657373004e6f206d617463680000 '.asciz "Success\000No match\000"'
t .data 61006200                '.ascii "a\0", "b\0"'
t .data 6100006200              '.asciz "a\0", "b"'
t .data 610062006300            '.string "a", "b\0c"'

# Beside the other escapes, and beside what would end a statement or
# begin a comment were it not in a string.
t .data 0a090d080c5c2200        '.ascii "\n\t\r\b\f\\\"\0"'
t .data 000a                    '.ascii "\0\n"'
t .data 73656d693b636f6c6f6e002368617368002f736c617368 '.ascii "semi;colon\0#hash\0/slash"'
t .data c3a900c3bc00            '.asciz "é\0ü"'

# What follows the string follows all of it: bytes, a second directive on
# the line, and another string.
t .data 0178000002              '.byte 1; .asciz "x\0"; .byte 2'
t .data 0000                    '.ascii "\0" ; .ascii "\0"'
t .data 61006263006400ff        '.ascii "a\0b"' '.asciz "c\0d"' '.byte 0xff'
t .data 4100420011223344        '.ascii "A\0B\0"' '.long 0x44332211'

# A label after it is at the full length, and so is a length taken as the
# difference of two labels -- here stored as a byte after the string.
t .data 61006200630006          's:' '.ascii "a\0b\0c\0"' 'e:' '.byte e - s'
t .data 0568006900000021        '.byte e - s' 's:' '.asciz "h\0i\0"' 'e:' '.ascii "\0!"'

# In sections other than .data.
t .rodata 6b00657900            '.asciz "k\0ey"'
t .text 90410042c3              'nop' '.ascii "A\0B"' 'ret'

# The symbol table agrees: a label after a string of six bytes is at 6.
for mode in --32 --64; do
    cases=$((cases + 1))
    printf '\t.data\nfirst:\n\t.asciz "ab\\0cd"\nsecond:\n\t.byte 1\n' > t.s
    if "$AS" "$mode" -o t.o t.s > out 2>&1; then
        at=$(readelf -sW t.o | awk '$8 == "second" { print $2 }')
        if [ "$((0x${at:-ff}))" -ne 6 ]; then
            echo "FAIL $mode: 'second' follows six bytes and is at 0x$at"
            fail=1
        fi
    else
        echo "FAIL $mode: label after a string: refused: $(head -1 out)"
        fail=1
    fi
done

[ "$fail" -eq 0 ] && echo "ok: strings with a NUL in them ($cases cases)"
exit "$fail"
