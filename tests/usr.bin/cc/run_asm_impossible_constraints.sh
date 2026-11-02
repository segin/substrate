#!/bin/sh
# The asm statements gcc refuses for their constraints cc refuses too,
# with gcc's words: an x87-only operand (`t`, `u`, `f`) that is not
# floating point, and an input matched by number to a `+` output, are
# "inconsistent operand constraints in an 'asm'", and
# two operands that need the one register they may not share -- two
# outputs, two inputs, an input and a `+` or `&` output -- or an operand
# in a register the clobbers name, are "'asm' operand has impossible
# constraints or there are not enough registers".  cc took `"=t"(int)`
# with `"0"`, and `"+a"` with `"a"`, and the others.  What gcc allows --
# an output and an input in one register, an input matched by number, a
# double in %st(0) -- cc compiles.  (gcc at -O0 is the judge: with
# optimization its register allocator sometimes shares a register when
# two operands hold one value.)
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_impossible_constraints: skip (no gcc)"
	exit 0
fi
cd "$CC_TMP"

n=0
check() {
	m=$1
	shift
	n=$((n + 1))
	printf '%s\n' "$*" >asm$n.c
	g=$(gcc $m -O0 -S -o /dev/null asm$n.c 2>&1 | grep -m1 'error:' | sed 's/^.*error: //' || true)
	for o in O0 O2; do
		c=$("$CC_BIN" $m -$o -S -o /dev/null asm$n.c 2>&1 | grep -m1 'error:' | sed 's/^.*error: //' || true)
		if [ "$c" != "$g" ]; then
			echo "run_asm_impossible_constraints: $m -$o: $*"
			echo "  cc: [${c:-accepted}], gcc: [${g:-accepted}]"
			exit 1
		fi
	done
}

for m in -m64 -m32; do
	check $m 'int z(int x) { int r; __asm__("" : "=t"(r) : "0"(x)); return r; }'
	check $m 'int z(int x) { int r; __asm__("" : "=t"(r)); return r; }'
	check $m 'int z(int x) { __asm__("" : : "u"(x)); return 0; }'
	check $m 'int z(int x) { __asm__("" : : "f"(x)); return 0; }'
	check $m 'double z(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }'
	check $m 'long double z(long double x) { long double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }'
	check $m 'int z(int x, int y) { int r = y; __asm__("" : "+a"(r) : "a"(x)); return r; }'
	check $m 'int z(int x, int y) { int r = y; __asm__("" : "+b"(r) : "b"(x)); return r; }'
	check $m 'int z(int x) { int r; __asm__("" : "=&a"(r) : "a"(x)); return r; }'
	check $m 'int z(int x) { int r; __asm__("" : "=a"(r) : "a"(x)); return r; }'
	check $m 'int z(int x, int y) { __asm__("" : : "a"(x), "a"(y)); return 0; }'
	check $m 'int z(void) { int r, s; __asm__("" : "=a"(r), "=a"(s)); return r + s; }'
	check $m 'int z(int x, int y) { int r = y; __asm__("" : "+a"(r) : "0"(x)); return r; }'
	check $m 'int z(int x, int y) { int r = y; __asm__("" : "+rm"(r) : "0"(x)); return r; }'
	check $m 'int z(int x) { int r; __asm__("" : "=r"(r) : "0"(x)); return r; }'
	check $m 'int z(int x) { int r; __asm__("" : "=a"(r) : "a"(x) : "eax"); return r; }'
	check $m 'int z(int x) { int r; __asm__("" : "=r"(r) : "S"(x) : "%esi"); return r; }'
	check $m 'int z(int x) { int r; __asm__("" : "=d"(r) : "c"(x) : "eax", "memory", "cc"); return r; }'
done
echo "run_asm_impossible_constraints: ok"
