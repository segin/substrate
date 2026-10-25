#!/bin/sh
# A _Float16 load or store goes through the register allocator on i386
# and x86-64, by libgcc's __extendhfdf2 and __truncdfhf2 as gcc calls
# them -- the half and the double in %xmm0 on x86-64; on i386 pushed,
# the double back in %st0 and the half in %xmm0, through the PLT under
# -fPIC.  Such a function used to go to the old emitter, a stack slot for
# every value.  CC_MIR_STATS says which path took each function; the
# program's output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_float16_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/h.c" <<'EOF2'
#include <stdio.h>

static _Float16 table[6];

static void __attribute__((noinline)) put(int k, double v) {
    table[k] = (_Float16)v;
}

static double __attribute__((noinline)) get(int k) {
    return table[k];
}

/* a value kept across the calls, and a half to a half */
static double __attribute__((noinline)) sum(int n) {
    double t = 0;
    int i;

    for (i = 0; i < n; ++i) {
        t += get(i) * (i + 1);
        table[i] = table[(i + 1) % n];
    }
    return t;
}

int main(void) {
    put(0, 1.0 / 3);
    put(1, 65504.0);
    put(2, -0.0001);
    put(3, 1e-8);
    put(4, 2.5);
    put(5, 100000.0);
    double t;
    int i;

    for (i = 0; i < 6; ++i)
        printf("%.17g\n", get(i));
    t = sum(6);
    printf("%.17g %.17g\n", t, get(0));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/hg" "$CC_TMP/h.c"
	want=$("$CC_TMP/hg")
	for flags in -O0 -O2 "-O2 -fPIC"; do
		# shellcheck disable=SC2086
		CC_MIR_STATS=1 "$CC_BIN" -m$m $flags -c -o "$CC_TMP/h.o" "$CC_TMP/h.c" 2>"$CC_TMP/stats" || {
			cat "$CC_TMP/stats"
			exit 1
		}
		"$CC_BIN" -m$m -o "$CC_TMP/hc" "$CC_TMP/h.o"
		if grep "^mir: .*not taken" "$CC_TMP/stats"; then
			echo "run_float16_regs: -m$m $flags: the above do not go through the allocator"
			exit 1
		fi
		got=$("$CC_TMP/hc")
		if [ "$got" != "$want" ]; then
			echo "run_float16_regs: -m$m $flags prints"
			echo "$got"
			echo "where gcc's prints"
			echo "$want"
			exit 1
		fi
	done
done
echo "run_float16_regs: ok"
