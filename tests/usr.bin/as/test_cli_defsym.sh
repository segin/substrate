#!/bin/sh
# --defsym NAME=VALUE: NAME is an absolute symbol with that value from
# the first line of the source.
#
# Every case uses the symbol, and is held to bytes that only its value
# gives: an option that is taken and never read assembles a source that
# does not mention the name just as well.  The bytes, and the symbol
# table's line for the name, are GNU as 2.46's.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
bad() { echo "FAIL: $*"; fail=1; }

# bytes OBJECT SECTION: the section's contents, in hex.
bytes() {
    objcopy -O binary --only-section="$2" "$1" sec.bin 2>/dev/null || return 1
    od -An -v -tx1 sec.bin | tr -d ' \n'
}

# t SECTION WANT SOURCE OPTION...: with the options, SOURCE (its lines
# parted by |) puts WANT in SECTION, and leaves nothing to relocate.
t() {
    sec=$1; want=$2; src=$3; shift 3
    cases=$((cases + 1))
    { printf '\t%s\n' "$sec"; printf '%s\n' "$src" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" -32 "$@" -o t.o t.s > out 2>&1; then
        bad "[$*] [$src]: refused: $(head -1 out)"
        return
    fi
    got=$(bytes t.o "$sec")
    [ "$got" = "$want" ] || bad "[$*] [$src]: $sec is '$got', GNU as writes '$want'"
    if readelf -rW t.o 2>/dev/null | grep -q 'R_386'; then
        bad "[$*] [$src]: a relocation is left for a symbol whose value was given"
    fi
}

# The control: without the option the name is not defined, and the
# assembler leaves it for the linker.  Were this not so, nothing below
# would show the option to have done anything.
printf '\t.data\n\t.long X\n' > t.s
rm -f t.o
if "$AS" -32 -o t.o t.s > out 2>&1; then
    readelf -sW t.o | awk '$8 == "X" && $7 == "UND" { found = 1 } END { exit !found }' ||
        bad "without --defsym, X is not an undefined symbol"
    readelf -rW t.o | grep -q 'R_386_32 .* X$' || bad "without --defsym, .long X has no relocation against X"
else
    bad "without --defsym, .long X is refused: $(head -1 out)"
fi

# As data, in both spellings of the option, in each radix, and negative.
t .data 44332211 '.long X'          --defsym X=0x11223344
t .data 05000000 '.long X'          --defsym=X=5
t .data 08000000 '.long X'          --defsym X=010
t .data ffffffff '.long X'          --defsym X=-1
t .data 2a       '.byte X'          --defsym X=42

# Two of them, and in an expression.
t .data 05010000 '.long X + Y'      --defsym X=5 --defsym Y=0x100
t .data 0c       '.byte (Y - X) * 4' --defsym X=5 --defsym Y=8

# In the conditionals: its value for .if, its existence for .ifdef.
t .data 01 '.if X|.byte 1|.else|.byte 2|.endif'      --defsym X=1
t .data 02 '.if X|.byte 1|.else|.byte 2|.endif'      --defsym X=0
t .data 01 '.ifdef X|.byte 1|.else|.byte 2|.endif'   --defsym X=0
t .data 02 '.ifdef X|.byte 1|.else|.byte 2|.endif'
t .data 02 '.ifndef X|.byte 1|.else|.byte 2|.endif'  --defsym X=9
t .data 03 '.if X == 3|.byte 3|.endif'               --defsym X=3

# It is a symbol like one made by .set, which may set it again.
t .data 0703 '.byte X|.set X, 3|.byte X'             --defsym X=7

# In an instruction: an immediate, an address, and a displacement small
# enough for one byte, which a symbol left to the linker never is.
t .text b8550000008b1d550000008b5155 'mov $X, %eax|mov X, %ebx|mov X(%ecx), %edx' --defsym X=0x55

# The symbol table has it: absolute, with the value, local unless the
# source says otherwise.
sym() {
    readelf -sW t.o | awk '$8 == "X" { print $2 ":" $5 ":" $7 }'
}
printf '\t.data\n\t.long X\n' > t.s
rm -f t.o
"$AS" -32 --defsym X=0x1234 -o t.o t.s > out 2>&1 || bad "--defsym X=0x1234: refused: $(head -1 out)"
[ "$(sym)" = "00001234:LOCAL:ABS" ] || bad "--defsym X=0x1234: the symbol is '$(sym)', not 00001234:LOCAL:ABS"
printf '\t.globl X\n\t.data\n\t.long X\n' > t.s
rm -f t.o
"$AS" -32 --defsym X=0x1234 -o t.o t.s > out 2>&1 || bad "--defsym with .globl: refused: $(head -1 out)"
[ "$(sym)" = "00001234:GLOBAL:ABS" ] || bad "--defsym with .globl: the symbol is '$(sym)', not 00001234:GLOBAL:ABS"

# With no NAME=VALUE to take, it is an error and nothing is written.
printf '\t.data\n\t.long 1\n' > t.s
rm -f t.o
"$AS" -32 -o t.o t.s --defsym > out 2>&1 && bad "--defsym with no argument is accepted"
[ -e t.o ] && bad "--defsym with no argument: an object was written"
rm -f t.o
"$AS" -32 --defsym X -o t.o t.s > out 2>&1 && bad "--defsym X, with no value, is accepted"
[ -e t.o ] && bad "--defsym X: an object was written"

[ "$fail" -eq 0 ] && echo "ok: --defsym ($cases uses of a symbol)"
exit "$fail"
