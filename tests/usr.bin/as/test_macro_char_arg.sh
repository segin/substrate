#!/bin/sh
# A macro argument in single quotes is a character constant, and keeps its
# quotes; only double quotes come off.
#
# The assembler took the quotes off both, so `m 'E'` of a macro writing
# `movb $\s, %al` became `movb $E, %al`, a symbol E, and assembled without
# a word to 0 with a relocation; `m ':'` became `movb $:` and was refused.
# Linux's arch/x86/kernel/relocate_kernel_64.S calls `print_reg 'E', 'x',
# 'c', ':', ...` so.
#
# GNU as is the judge: character constants of letters, digits, `:` and a
# space as macro arguments, beside double-quoted strings, assemble to GNU
# as's bytes and relocations, --32 and --64.
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

bytes_of() { objcopy -O binary -j "$2" "$1" /dev/stdout 2>/dev/null | od -An -v -tx1 | tr -d ' \n'; }
relocs_of() { readelf -rW "$1" | awk '/R_/ { print $1, $3, $5 }'; }

cat > t.s <<'EOF'
.macro print_reg a, b, c, d
	movb	$\a, %al
	movb	$\b, %al
	movb	$\c, %al
	movb	$\d, %al
.endm
.macro str s
	.data
	.ascii "\s"
	.text
.endm
	print_reg 'E', 'x', 'c', ':'
	print_reg 'r', '8', ' ', ':'
	str "hello"
	movl $'A', %ecx
EOF
for m in 32 64; do
    if ! "$GAS" "--$m" -o g.o t.s 2> err; then
        echo "FAIL --$m: GNU as refused it: $(head -1 err)"; fail=1; continue
    fi
    if ! "$AS" "--$m" -o o.o t.s 2> err; then
        echo "FAIL --$m: $(head -1 err | sed 's/.*: //')"; fail=1; continue
    fi
    for s in .text .data; do
        if [ "$(bytes_of o.o $s)" != "$(bytes_of g.o $s)" ]; then
            echo "FAIL --$m $s: $(bytes_of o.o $s), GNU as $(bytes_of g.o $s)"; fail=1
        fi
    done
    if [ "$(relocs_of o.o)" != "$(relocs_of g.o)" ]; then
        echo "FAIL --$m: relocations [$(relocs_of o.o | tr '\n' ' ')], GNU as [$(relocs_of g.o | tr '\n' ' ')]"; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_macro_char_arg: ok"
fi
exit $fail
