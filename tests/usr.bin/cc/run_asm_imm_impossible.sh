#!/bin/sh
# An asm's "i" operand that is no constant, where the constraint leaves
# no register or memory to take instead, is an error, as gcc's
# ("impossible constraint in 'asm'").  cc wrote 0 for it, or the
# constant of some store or constant before it in the function, and the
# asm went on with another's value or none: a `.quad %c0` table entry at
# address 0.  A constant, an address, and a parameter of a function
# inlined at the call are fine.  gcc is the judge of which is which.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_imm_impossible: skip (no gcc)"
	exit 0
fi

# NAME BODY [LEVELS]: a function with BODY; whether gcc and cc both accept it, or both refuse it, at LEVELS
check() {
	printf 'int g;\nstatic inline __attribute__((always_inline)) int k(int v) { int r; __asm__("mov %%1, %%0" : "=r"(r) : "i"(v)); return r; }\n%s\n' "$2" >"$CC_TMP/$1.c"
	for o in ${3:-O0 O2}; do
		if gcc -$o -c -o /dev/null "$CC_TMP/$1.c" 2>/dev/null; then want=ok; else want=error; fi
		if "$CC_BIN" -$o -c -o /dev/null "$CC_TMP/$1.c" 2>"$CC_TMP/$1.err"; then got=ok; else got=error; fi
		if [ "$got" != "$want" ]; then
			echo "run_asm_imm_impossible: $1 -$o: cc gives $got, gcc $want"
			head -2 "$CC_TMP/$1.err"
			exit 1
		fi
		if [ "$got" = error ] && ! grep -q "impossible constraint" "$CC_TMP/$1.err"; then
			echo "run_asm_imm_impossible: $1 -$o: $(head -1 "$CC_TMP/$1.err")"
			exit 1
		fi
	done
}
check param 'int f(int x) { int r; __asm__("mov %1, %0" : "=r"(r) : "i"(x)); return r; }'
check global 'int f(void) { int r; __asm__("mov %1, %0" : "=r"(r) : "i"(g)); return r; }'
check after_store 'int f(int x) { int r; g = 5; __asm__("mov %1, %0" : "=r"(r) : "i"(x)); return r; }'
check constant 'int f(void) { int r; __asm__("mov %1, %0" : "=r"(r) : "i"(42)); return r; }'
check address 'int f(void) { int r; __asm__("mov %1, %0" : "=r"(r) : "i"(&g)); return r; }'
# (at -O0 gcc carries no integer into an inlined parameter, and refuses; cc carries it)
check inlined 'int f(void) { return k(7); }' O2
echo "run_asm_imm_impossible: ok"
