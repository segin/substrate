#!/bin/sh
# A function that takes a label's address (&&label, for a computed goto)
# goes through the register allocator on i386 and x86-64, the address a
# local symbol's -- off %rip on x86-64, under -fPIC too.  Such a function
# used to go to the old emitter, a stack slot for every value.
# CC_MIR_STATS says which path took each function; the program's output
# is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_computed_goto_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/g.c" <<'EOF2'
#include <stdio.h>

/* a threaded interpreter over acc and n: mul, add, dec, jump-if-n-not-zero, halt */
static long __attribute__((noinline)) run(const unsigned char *code, long acc, long n) {
    void *ops[] = {&&op_mul, &&op_add, &&op_dec, &&op_jnz, &&op_halt};
    int pc = 0;

#define NEXT goto *ops[code[pc++]]
    NEXT;
op_mul:
    acc *= n;
    NEXT;
op_add:
    acc += n;
    NEXT;
op_dec:
    n--;
    NEXT;
op_jnz:
    if (n != 0) {
        pc = code[pc];
        NEXT;
    }
    pc++;
    NEXT;
op_halt:
    return acc;
}

/* a label's address compared and kept */
static int __attribute__((noinline)) pick(int k) {
    void *where = k > 2 ? &&big : &&small;
    int r = 0;

    if (where == &&big)
        r = 100;
    goto *where;
small:
    return r + k;
big:
    return r + 2 * k;
}

int main(void) {
    /* again: acc op= n; n--; while n */
    static const unsigned char fact[] = {0, 2, 3, 0, 4};
    static const unsigned char sum[] = {1, 2, 3, 0, 4};

    printf("%ld %ld\n", run(fact, 1, 10), run(sum, 0, 100));
    printf("%d %d %d\n", pick(1), pick(2), pick(5));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/gg" "$CC_TMP/g.c"
	want=$("$CC_TMP/gg")
	for o in O0 O2; do
		CC_MIR_STATS=1 "$CC_BIN" -m$m -$o -o "$CC_TMP/gc" "$CC_TMP/g.c" 2>"$CC_TMP/stats"
		for fn in run pick; do
			if ! grep -q "^mir: $fn: taken" "$CC_TMP/stats"; then
				echo "run_computed_goto_regs: -m$m -$o: $fn does not go through the allocator:"
				grep "^mir: $fn:" "$CC_TMP/stats" || true
				exit 1
			fi
		done
		got=$("$CC_TMP/gc")
		if [ "$got" != "$want" ]; then
			echo "run_computed_goto_regs: -m$m -$o prints"
			echo "$got"
			echo "where gcc's prints"
			echo "$want"
			exit 1
		fi
	done
done
"$CC_BIN" -m64 -O2 -fPIC -S -o "$CC_TMP/g.s" "$CC_TMP/g.c"
if grep -q '\.L[a-z_]*_[0-9]*@GOT' "$CC_TMP/g.s"; then
	echo "run_computed_goto_regs: -fPIC reads a label's address from the GOT"
	exit 1
fi
echo "run_computed_goto_regs: ok"
