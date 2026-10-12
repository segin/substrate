#!/bin/sh
# What as_data.c is -- string literals and LEB128, by a unit test -- and
# what the data directives make of a source, by the assembler: the bytes
# of .data, which are written out here as GNU as makes them.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-data-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -D_GNU_SOURCE -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_data.c" \
   "$ROOT/tests/usr.bin/as/test_data_core.c" \
   -o "$TMP/test_data_core"
"$TMP/test_data_core"

AS=${AS:-"$ROOT/usr.bin/as/as"}

fail=0

# bytes NAME WANT < source: the bytes of .data, in hex.
bytes() {
    cat > "$TMP/$1.s"
    if ! (cd "$TMP" && "$AS" --64 -o "$1.o" "$1.s") 2> "$TMP/$1.err"; then
        echo "FAIL $1: not assembled: $(head -1 "$TMP/$1.err")" >&2
        fail=1
        return
    fi
    objcopy -O binary --only-section=.data "$TMP/$1.o" "$TMP/$1.bin"
    got=$(od -An -v -tx1 "$TMP/$1.bin" | tr -d ' \n')
    if [ "$got" != "$2" ]; then
        echo "FAIL $1: .data is $got, not $2" >&2
        fail=1
    fi
}

# refused NAME < source
refused() {
    cat > "$TMP/$1.s"
    if (cd "$TMP" && "$AS" --64 -o "$1.o" "$1.s") 2> /dev/null; then
        echo "FAIL $1: assembled" >&2
        fail=1
    fi
}

printf '\021\042\063\104' > "$TMP/blob.bin"

bytes ints 01020300040005000000060000000700000000000000080000000000000009000a00 <<'SRC'
	.data
	.byte 1, 2
	.short 3
	.hword 4
	.long 5
	.int 6
	.quad 7
	.8byte 8
	.word 9
	.value 10
SRC

bytes floats 0000c03f0000000000000440 <<'SRC'
	.data
	.float 1.5
	.double 2.5
SRC

bytes strings 4142434400454600610a6200 <<'SRC'
	.data
	.ascii "AB"
	.asciz "CD"
	.string "EF"
	.string "a\nb"
SRC

bytes space 0100000000004100410041000000000002 <<'SRC'
	.data
	.byte 1
	.zero 3
	.space 2
	.fill 3, 2, 0x41
	.skip 4
	.byte 2
SRC

bytes org 010000000000000002 <<'SRC'
	.data
	.byte 1
	.org 8
	.byte 2
SRC

bytes incbin 012233021122334403 <<'SRC'
	.data
	.byte 1
	.incbin "blob.bin", 1, 2
	.byte 2
	.incbin "blob.bin"
	.byte 3
SRC

bytes leb 01e58e26c0bb7802 <<'SRC'
	.data
	.byte 1
	.uleb128 624485
	.sleb128 -123456
	.byte 2
SRC

# With nothing after them: a directive of numbers or of space puts
# nothing, one of LEB128 a zero -- as GNU as has them.  (The list of data
# directives that was made before the object was written refused these.)
bytes bare 01000002 <<'SRC'
	.data
	.byte 1
	.byte
	.short
	.long
	.quad
	.word
	.float
	.double
	.zero
	.skip
	.space
	.fill
	.fill ,
	.ds.b
	.dcb.b
	.uleb128
	.sleb128
	.byte 2
SRC

for d in .ascii .asciz .string .string8 .string16 .org .incbin '.zero ,'; do
    refused bare-refused <<SRC
	.data
	.byte 1
	$d
	.byte 2
SRC
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "ok: data core"
