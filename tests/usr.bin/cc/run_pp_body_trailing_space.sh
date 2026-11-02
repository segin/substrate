#!/bin/sh
# White space ending a macro's replacement list is no part of it (C11
# 6.10.3p7).  cc kept it -- a comment after the body leaves a space --
# so Linux's `#define EXC_EXCEPTION 0x28 /* ... */` made
# `EXC_EXCEPTION(%rsp)` `0x28  (%rsp)`; an assembler macro's arguments are
# parted by spaces, so relocate_kernel_64.S's print_reg and vmenter.S's
# RESTORE_GUEST_SPEC_CTRL_BODY got one argument too many and failed to
# assemble.  The -E text of object-like and function-like macros whose
# bodies end in comments, spaces and tabs is gcc's, spacing inside each
# line and all; and an assembler macro given such arguments assembles as
# gcc's preprocessing makes it.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_body_trailing_space: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
export AS

printf '#define A 0x28 /* pushed by the handler */\n#define B (1)   \n#define C\t2\t/* x */ /* y */\n#define F(x) x /* c */\n#define G(x, y) x + y\t \nA(%%rsp) B, C, F(3)(4) G(1, 2);\n' >"$CC_TMP/w.c"
gcc -E -P "$CC_TMP/w.c" | sed 's/^[ \t]*//' | grep . >"$CC_TMP/g.out"
"$CC_BIN" -E -P -o "$CC_TMP/c.raw" "$CC_TMP/w.c"
sed 's/^[ \t]*//' "$CC_TMP/c.raw" | grep . >"$CC_TMP/c.out"
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
	echo "run_pp_body_trailing_space: -E gives [$(cat "$CC_TMP/c.out")], gcc's [$(cat "$CC_TMP/g.out")]"
	exit 1
fi

cat >"$CC_TMP/m.S" <<'EOF'
#define EXC_EXCEPTION	0x28 /* Pushed by handler entry point */
#define LABEL_OF(x)	x	/* the label */
.macro pick a, b, c
	movb	$\a, %al
	movq	\b, %rax
	jmp	\c
.endm
	pick 'E', EXC_EXCEPTION(%rsp), LABEL_OF(.Ldone)
.Ldone:
	ret
EOF
gcc -c -o "$CC_TMP/g.o" "$CC_TMP/m.S"
if ! "$CC_BIN" -c -o "$CC_TMP/c.o" "$CC_TMP/m.S" 2>"$CC_TMP/c.err"; then
	echo "run_pp_body_trailing_space: the assembler macro is refused: $(head -1 "$CC_TMP/c.err")"
	exit 1
fi
if ! cmp -s "$CC_TMP/g.o" "$CC_TMP/c.o" &&
	[ "$(objdump -d "$CC_TMP/g.o" | tail -n +4)" != "$(objdump -d "$CC_TMP/c.o" | tail -n +4)" ]; then
	echo "run_pp_body_trailing_space: the assembler macro assembles otherwise than gcc's"
	exit 1
fi
echo "run_pp_body_trailing_space: ok"
