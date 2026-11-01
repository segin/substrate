#!/bin/sh
# -mgeneral-regs-only uses no register but the general ones -- no SSE,
# MMX or x87 -- as gcc has it: __SSE__, __SSE2__ and __MMX__ go, integer
# code compiles, and floating point is refused, as gcc refuses it.
# Substrate's x86-64 kernel builds with it.  cc took it and ignored it.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_general_regs_only: skip (no gcc)"
	exit 0
fi

want=$(gcc -m64 -mgeneral-regs-only -dM -E - </dev/null | grep -E '#define __(MMX|SSE|SSE2)__ ' | sort || true)
got=$("$CC_BIN" -m64 -mgeneral-regs-only -dM -E - </dev/null | grep -E '#define __(MMX|SSE|SSE2)__ ' | sort || true)
if [ "$got" != "$want" ]; then
	echo "run_general_regs_only: defines [$(echo $got)], gcc [$(echo $want)]"
	exit 1
fi
printf 'long sum(const long *p, int n) { long s = 0; while (n-- > 0) s += *p++; return s; }\n' >"$CC_TMP/int.c"
printf 'double twice(double x) { return x * 2; }\n' >"$CC_TMP/fp.c"
printf 'int scaled(int x) { double d = x; return (int)(d * 1.5); }\n' >"$CC_TMP/fplocal.c"
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		if ! $run "$CC_BIN" -m64 -$o -mgeneral-regs-only -c -o "$CC_TMP/o.o" "$CC_TMP/int.c" 2>"$CC_TMP/c.err"; then
			echo "run_general_regs_only: -$o $path: integer code refused: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		if objdump -d "$CC_TMP/o.o" | grep -q '%xmm\|%mm\|%st'; then
			echo "run_general_regs_only: -$o $path: a register not a general one"
			exit 1
		fi
		for src in fp.c fplocal.c; do
			if gcc -m64 -O2 -mgeneral-regs-only -c -o "$CC_TMP/g.o" "$CC_TMP/$src" 2>/dev/null; then
				continue
			fi
			if $run "$CC_BIN" -m64 -$o -mgeneral-regs-only -c -o "$CC_TMP/o.o" "$CC_TMP/$src" 2>"$CC_TMP/c.err"; then
				echo "run_general_regs_only: -$o $path: $src compiled, which gcc refuses"
				exit 1
			fi
		done
	done
done
echo "run_general_regs_only: ok"
