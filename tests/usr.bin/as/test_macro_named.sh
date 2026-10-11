#!/bin/sh
# A message about a line of a macro's expansion says which macro
# (AS-T-148).
#
# The line such a message gives is the one the macro was called on,
# where its author will look; what is wrong is in the macro's body, and a
# line may call several macros and each of them others.  The message
# named none:
#
#       t.s:9: Error: unsupported mnemonic: bogus
#
# with nothing like `bogus` on line 9.  The marks that carry a line
# through expansion carry the macro's name too, and the message ends
# with it.
#
# GNU as says it another way -- the line in the macro's body, and under
# it "Info: macro invoked from here" with the line of the call -- and
# names no macro, so this test is of this assembler's messages and is
# not one GNU as passes.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

cat > t.s <<'EOF'
	.text
	.macro bad r
	nop
	bogus \r
	.endm
	.macro outer
	bad %eax
	.endm
	nop
	bad %ebx
	outer
	bogus2
	.rept 2
	bad %ecx
	.endr
	.macro good
	nop
	.endm
	good
	bogus3
EOF

"$AS" --32 -o t.o t.s > out 2>&1 && { echo "FAIL: assembled"; fail=1; }

# has WHAT PATTERN [COUNT]: the pattern is on COUNT lines of the messages.
has() {
    cases=$((cases + 1))
    n=$(grep -c -- "$2" out)
    [ "$n" = "${3:-1}" ] || { echo "FAIL $1: $n lines, not ${3:-1}, of: $(tr '\n' '|' < out)"; fail=1; }
}

has "a macro called from the source" "^t\.s:10: Error: .*bogus.*macro 'bad'"
# The one that was being expanded, which is not the one the line calls.
has "a macro called from a macro" "^t\.s:11: Error: .*bogus.*macro 'bad'"
has "a macro called in a repeat, once for each time" "^t\.s:14: Error: .*bogus.*macro 'bad'" 2
# What is not of an expansion names none: a line between two calls, and
# one after a macro that was expanded without fault.
has "a line of the source" '^t\.s:12: Error: .*bogus2$'
has "a line after an expansion" '^t\.s:20: Error: .*bogus3$'
has "no macro named but where there is one" "macro '" 4
has "nothing of a mark is left in a message" "$(printf '[\001\002\003]')" 0

# A warning in an expansion names it too.
cases=$((cases + 1))
printf '\t.data\n\t.macro wide\n\t.byte 300\n\t.endm\n\t.byte 1\n\twide\n' > w.s
"$AS" --32 -o w.o w.s > out 2>&1
grep -q "^w\.s:6: Warning: .*macro 'wide'" out || { echo "FAIL a warning: $(tr '\n' '|' < out)"; fail=1; }

# And what is assembled is what it was.
cases=$((cases + 1))
printf '\t.text\n\t.macro m a\n\t.byte \\a\n\t.endm\n\t.macro two\n\tm 1\n\tm 2\n\t.endm\n\ttwo\n\t.rept 2\n\tm 7\n\t.endr\n\t.byte 9\n' > ok.s
if "$AS" --32 -o ok.o ok.s > out 2>&1; then
    got=$(objcopy -O binary -j .text ok.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$got" = 0102070709 ] || { echo "FAIL the bytes: $got"; fail=1; }
else
    echo "FAIL the bytes: $(tr '\n' '|' < out)"; fail=1
fi

[ "$fail" -eq 0 ] && echo "ok: a message names its macro ($cases cases)"
exit "$fail"
