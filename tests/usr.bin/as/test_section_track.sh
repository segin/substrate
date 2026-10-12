#!/bin/sh
# Which section a source is in: one tracker of it.
#
# The table of sections (as_sections.c) kept the section a source was
# in, the one before and what was pushed, and so did the emitter, each
# its own way and each with its own reading of a section's name.  They
# read one name two ways: a name written in quotes with a space in it
# -- `.pushsection ".data.x y","aw"` -- was the whole of it to the one
# and cut at the space to the other, and the statement after it was
# "unknown section .data.x".
#
# There is one tracker now, which both walk a source with.  This is a
# source that goes through sections every way there is -- pushed,
# popped, `.previous` after each, a group, a name with a space -- and
# the value of each symbol and the size of each section GNU as 2.47
# gives it.  GNU as passes this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

cat > t.s <<'EOF'
	.text
a:	nop
	.pushsection .rodata.str1.1,"aMS",@progbits,1
s1:	.string "x"
	.pushsection ".data.x y","aw"
d1:	.long 1
	.previous
	.long 2
	.previous
	.long 3
	.popsection
s2:	.string "y"
	.popsection
b:	nop
	.section .foo,"ax",@progbits
f:	ret
	.previous
c:	nop
	.previous
g:	ret
	.data
	.long 4
	.bss
	.skip 8
	.section .rodata
	.long 5
	.pushsection .text
e:	nop
	.popsection
	.long 6
	.previous
	.skip 7
	.previous
	.long 8
	.section .grp,"axG",@progbits,sig,comdat
h:	ret
	.text 0
	.subsection 0
z:	nop
EOF

fail=0
if ! "$AS" --64 -o t.o t.s 2> err; then
    echo "FAIL: not assembled: $(head -1 err)"
    exit 1
fi

# (But for the group's signature, which is no label of the source.)
symbols=$(nm t.o | awk 'NF == 3 && $3 != "sig" {print $3 "=" $1}' | sort | tr '\n' ' ')
want_symbols="a=0000000000000000 b=0000000000000001 c=0000000000000002 d1=0000000000000000 e=0000000000000003 f=0000000000000000 g=0000000000000001 h=0000000000000000 s1=0000000000000000 s2=0000000000000006 z=0000000000000004 "
if [ "$symbols" != "$want_symbols" ]; then
    echo "FAIL: symbols: $symbols"
    fail=1
fi

# (The size of each section, by its name: `objdump -h` has the name whole.)
size_of() {
    objdump -h t.o | awk -v name="$1" '{ line = $0; sub(/^ *[0-9]+ /, "", line); if (index(line, name " ") == 1) { rest = substr(line, length(name) + 1); split(rest, f, " "); print f[1]; exit } }'
}
check() {
    got=$(size_of "$1")
    if [ "$got" != "$2" ]; then
        echo "FAIL: $1 is ${got:-missing} bytes, not $2"
        fail=1
    fi
}
check .text 00000005
check .data 00000004
check .bss 0000000f
check .rodata.str1.1 00000008
check ".data.x y" 00000008
check .foo 00000002
check .rodata 0000000c
check .grp 00000001

[ "$fail" = 0 ] && echo "ok: one section tracker"
exit "$fail"
