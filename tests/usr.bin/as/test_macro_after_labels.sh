#!/bin/sh
# A macro called after labels is expanded however many the labels are.
# One label before it was handled, and a second made the macro's name
# read as an instruction: Linux's ALTERNATIVE_2 writes
# `740: 740: \oldinstr`, and retpoline.S's oldinstr is the macro
# RETPOLINE ("'RETPOLINE' does not take these operands", the Arch
# config).  Its shape, nested as there; one label, two, three, and a
# line of labels and statements; .text is GNU as's, and the labels are
# where GNU as puts them.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

if ! command -v objdump >/dev/null 2>&1 || ! as --version 2>/dev/null | grep -q GNU; then
    echo "test_macro_after_labels: skip (no GNU as or objdump)"
    exit 0
fi

cat > t.s <<'EOF'
	.text
.macro POL reg
	jmp *%\reg
.endm
.macro RETPOL reg
	POL \reg
	int3
.endm
.macro ALT old, new
740:		740:		\old	;	741:	\new ;
.endm
.macro THUNK reg
	ALT "RETPOL \reg", "lfence; jmp *%\reg; int3"
.endm
	THUNK rax
one:	POL rbx
two:	three:	POL rcx
a1:	a2:	a3:	RETPOL rdx
b1:	nop ; b2: POL rsi ; nop
	.globl one, two, three, a1, a2, a3, b1, b2
EOF

if ! "$AS" --64 -o c.o t.s > out 2>&1; then
    echo "FAIL test_macro_after_labels: $(head -1 out)"
    exit 1
fi
as --64 -o g.o t.s || exit 1
fail=0
objdump -d -j .text g.o | sed -n '/<.text>:/,$p' > want
objdump -d -j .text c.o | sed -n '/<.text>:/,$p' > got
if ! cmp -s want got; then
    echo "FAIL test_macro_after_labels: .text differs from GNU as's"
    diff want got
    fail=1
fi
nm g.o | sort > want.sym
nm c.o | sort > got.sym
if ! cmp -s want.sym got.sym; then
    echo "FAIL test_macro_after_labels: symbols differ from GNU as's"
    diff want.sym got.sym
    fail=1
fi
[ $fail -eq 0 ] && echo "test_macro_after_labels: ok"
exit $fail
