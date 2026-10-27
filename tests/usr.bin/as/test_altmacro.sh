#!/bin/sh
# .altmacro, GNU as's alternate macro syntax.
#
# It was an unknown directive.  In its mode an argument `%expr` is the
# value of the expression -- of the symbols .set so far, as they are at
# the call -- `<text>` is the text, with `!c` the character c, and a
# parameter is named in the body by itself as well as by `\name`.  The
# Substrate kernel's isr.S makes its MSI stubs isr80 .. isr191 so, a
# .set in a .rept counting the vector up.  .noaltmacro ends it.
#
# What each assembles to is what GNU as 2.46 makes of it, and GNU as
# passes this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is NAME BYTES SYMBOLS: NAME.s assembles, its .data is BYTES, and the
# names nm lists, in its order, are SYMBOLS.
is() {
    cases=$((cases + 1))
    rm -f t.o t.bin
    if ! "$AS" --32 -o t.o "$1.s" > out 2>&1; then
        echo "FAIL $1: $(tail -1 out)"; fail=1; return
    fi
    objcopy -O binary -j .data t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$2" ] || { echo "FAIL $1: [$got], not [$2]"; fail=1; }
    syms=$(nm t.o | awk '{ print $NF }' | tr '\n' ' ')
    [ "$syms" = "$3" ] || { echo "FAIL $1: symbols [$syms], not [$3]"; fail=1; }
}

# isr.S's way: the vector counted up by a .set in a .rept, each call
# given its value.
cat > stubs.s <<'EOF'
	.macro STUB num
	.global isr\num
isr\num: .byte \num
	.endm
	.data
	.altmacro
	.set vec, 0x50
	.rept 3
	STUB %vec
	.set vec, vec + 1
	.endr
	STUB %(vec * 2)
	.noaltmacro
EOF
is stubs 505152a6 "isr166 isr80 isr81 isr82 vec "

# <text>, with `!` before a character that is to be itself; and a
# parameter by its bare name, which after .noaltmacro is a symbol again.
cat > text.s <<'EOF'
	.macro B v
	.byte v
	.endm
	.macro S s
	.ascii "\s"
	.endm
	.data
	.altmacro
	B 7
	B <9>
	S <a!>b>
	.noaltmacro
	B 8
EOF
is text 0709613e6200 "v "

# A comment after %expr is no part of it, and a symbol assigned with
# `=` counts as one .set.
cat > assign.s <<'EOF'
	.macro B v
	.byte \v
	.endm
	.data
	.altmacro
	k = 3
	B %k + 1   # four
	.noaltmacro
EOF
is assign 04 "k "

[ "$fail" -eq 0 ] && echo "ok: .altmacro ($cases cases)"
exit "$fail"
