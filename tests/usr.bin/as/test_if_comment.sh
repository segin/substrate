#!/bin/sh
# A comment after an .if or .elseif expression is no part of it.
#
# Linux's lib/crypto/x86/sha1-avx2-asm.S writes `.if (i == 0) # Initialize
# and rotate registers` inside a macro; the assembler read the comment as
# part of the expression and refused it (".if: the expression is not a
# number").
#
# GNU as is the judge: .if and .elseif with `#` and block comments after
# them, at the top level, in a macro and in a .rept, assemble to GNU as's
# bytes, --32 and --64.
# Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
GAS=${AS_GNU:-as}
if ! "$GAS" --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as is the judge"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

text_of() { objcopy -O binary -j .text "$1" /dev/stdout | od -An -v -tx1 | tr -d ' \n'; }

cat > t.s <<'EOF'
	.set k, 2
	.if (k == 2) # a comment
	nop
	.endif
	.if k == 3 /* a block comment */
	int3
	.elseif (k > 1) # another
	hlt
	.endif
.macro pick i
	.if (\i == 0) # Initialize and rotate registers
	cld
	.elseif ((\i & 1) == 1) # odd
	std
	.else
	cli
	.endif
.endm
	pick 0
	pick 1
	pick 2
	.set j, 0
	.rept 3
	pick j
	.set j, j+1
	.endr
EOF
for m in 32 64; do
    if ! "$GAS" "--$m" -o g.o t.s 2> err; then
        echo "FAIL --$m: GNU as refused it: $(head -1 err)"; fail=1; continue
    fi
    if ! "$AS" "--$m" -o o.o t.s 2> err; then
        echo "FAIL --$m: $(head -1 err | sed 's/.*: //')"; fail=1; continue
    fi
    if [ "$(text_of o.o)" != "$(text_of g.o)" ]; then
        echo "FAIL --$m: $(text_of o.o), GNU as $(text_of g.o)"; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_if_comment: ok"
fi
exit $fail
