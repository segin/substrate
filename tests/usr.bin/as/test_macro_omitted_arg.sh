#!/bin/sh
# A macro argument may be omitted: `m 1, , 3` has three arguments, its
# second parameter keeping its default (empty without one).  The
# assembler dropped the empty one, and the arguments after it moved up
# a parameter: Linux's aes-xts-avx-x86_64.S calls
# `_vaes_1x \enc, 1, \xmm_suffix, \data, tmp=\tmp` with \xmm_suffix
# empty, and its VAES functions did not assemble ("has an empty
# operand", the Arch config).  Omitted arguments before a named one, in
# the middle, trailing and all of them; .text is GNU as's.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

if ! command -v objdump >/dev/null 2>&1; then
    echo "test_macro_omitted_arg: skip (no objdump)"
    exit 0
fi

cat > t.s <<'EOF'
	.text
.macro inner a, b, c
	vpxor \a, \b, \c
.endm
.macro outer enc, i, sfx, data, tmp
.ifnb \sfx
	inner %xmm1, \data, \data
.else
	inner \tmp, \data, \data
.endif
.endm
.macro dflt a=1, b=2, c=3
	.byte \a, \b, \c
.endm
.macro none
	nop
.endm
	outer 1, 5, , %ymm0, tmp=%ymm4
	outer 1, 5, _XMM, %xmm0, tmp=%xmm4
	outer 1, 5, , %ymm2, %ymm3
	dflt 5, , 7
	dflt , , 9
	dflt 4,
	dflt
	none
EOF

fail=0
if ! "$AS" --64 -o c.o t.s > out 2>&1; then
    echo "FAIL test_macro_omitted_arg: $(head -1 out)"
    exit 1
fi
if command -v as >/dev/null 2>&1 && as --64 -o g.o t.s 2>/dev/null; then
    objdump -d -j .text g.o | sed -n '/<.text>:/,$p' > want
    objdump -d -j .text c.o | sed -n '/<.text>:/,$p' > got
    if ! cmp -s want got; then
        echo "FAIL test_macro_omitted_arg: .text differs from GNU as's"
        diff want got
        fail=1
    fi
else
    # without GNU as: the bytes it makes, recorded
    got=$(objcopy -O binary -j .text c.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    want=c5fdefc4c5f9efc1c5edefd305020701020904020301020390
    [ "$got" = "$want" ] || { echo "FAIL test_macro_omitted_arg: [$got], not [$want]"; fail=1; }
fi
[ $fail -eq 0 ] && echo "test_macro_omitted_arg: ok"
exit $fail
