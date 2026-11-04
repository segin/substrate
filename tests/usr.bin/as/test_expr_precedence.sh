#!/bin/sh
# + and - bind tighter than the comparisons, as GNU as ranks them.
#
# The GNU as manual puts + and - in one rank with ==, !=, <, <=, > and >=,
# and the assembler did the same, but gas itself ranks them above:
# `5 != 7 - 2` is 0.  Linux's arch/x86/boot/header.S asks
# `.if ZO_efi32_stub_entry != ZO_efi64_stub_entry - 0x200` and stopped on
# its `.error "32-bit and 64-bit EFI entry points do not match"` with the
# entry points 0x200 apart.
#
# GNU as is the judge: data words mixing every rank, and a header.S-shaped
# .if/.error, assemble to GNU as's bytes, --32 and --64.
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

data_of() { objcopy -O binary -j .data "$1" /dev/stdout | od -An -v -tx1 | tr -d ' \n'; }

cat > t.s <<'EOF'
	.set e32, 0x0000000000f3e300
	.set e64, 0x0000000000f3e500
	.if e32 != e64 - 0x200
	.error "32-bit and 64-bit EFI entry points do not match"
	.endif
	.data
	.long 5 != 7 - 2
	.long 1 + 2 == 3
	.long 2 < 1 + 3
	.long 3 - 1 > 1
	.long 1 == 1 - 1
	.long 4 - 2 - 1
	.long 1 | 2 + 1
	.long 1 + 2 | 4
	.long 2 * 3 + 1
	.long 1 + 2 * 3
	.long 6 - 4 >> 1
	.long 1 == 1 && 2 > 1
	.long 0 || 3 - 3 == 0
	.long 1 < 2 == -1
EOF
for m in 32 64; do
    if ! "$GAS" "--$m" -o g.o t.s 2> err; then
        echo "FAIL --$m: GNU as refused it: $(head -1 err)"; fail=1; continue
    fi
    if ! "$AS" "--$m" -o o.o t.s 2> err; then
        echo "FAIL --$m: $(head -1 err | sed 's/.*: //')"; fail=1; continue
    fi
    if [ "$(data_of o.o)" != "$(data_of g.o)" ]; then
        echo "FAIL --$m: $(data_of o.o), GNU as $(data_of g.o)"; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_expr_precedence: ok"
fi
exit $fail
