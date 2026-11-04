#!/bin/sh
# Past the size beyond which nothing more is inlined into a function
# (CALLER_MAX_INSTRS), an always_inline function no bigger than gcc's
# max-inline-insns-single once its call's constants are folded into a
# copy of it -- the module's const tables read, its locals made values
# -- is still inlined, the folded copy in place of the body, as gcc
# inlines always_inline whatever the caller's size, folding as it goes.
# Linux's kvm_initialize_cpu_caps() has hundreds of
# __feature_leaf(X86_FEATURE_*) and __feature_bit() calls, each a
# constant once reverse_cpuid[] is read, each checked by BUILD_BUG_ON();
# those past the limit stayed calls, and kvm.o kept 150 calls of
# undefined __compiletime_assert_N.  800 such checks take the caller
# past the limit.  -O2, -m64 and -m32,
# both code paths, every function through MIR; and the program, run,
# prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_inline_past_cap: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function
{
	cat <<'EOF2'
extern void table_bad(void), leaf_bad(void);
unsigned int caps[64];
struct leaf_reg {
	unsigned int function;
	unsigned int index;
};
static const struct leaf_reg reverse[40] = {
	[0] = { 1, 0 },   [1] = { 2, 0 },   [2] = { 3, 0 },   [3] = { 4, 0 },   [4] = { 5, 0 },
	[5] = { 6, 0 },   [6] = { 7, 0 },   [7] = { 8, 0 },   [8] = { 9, 0 },   [9] = { 10, 0 },
	[10] = { 11, 0 }, [11] = { 12, 0 }, [12] = { 13, 0 }, [13] = { 14, 0 }, [14] = { 15, 0 },
	[15] = { 16, 0 }, [16] = { 17, 0 }, [17] = { 18, 0 }, [18] = { 19, 0 }, [19] = { 20, 0 },
	[20] = { 21, 0 }, [21] = { 22, 0 }, [22] = { 23, 0 }, [23] = { 24, 0 }, [24] = { 25, 0 },
	[25] = { 26, 0 }, [26] = { 27, 0 }, [27] = { 28, 0 }, [28] = { 29, 0 }, [29] = { 30, 0 },
	[30] = { 31, 0 }, [31] = { 32, 0 }, [32] = { 33, 0 }, [33] = { 34, 0 }, [34] = { 35, 0 },
	[35] = { 36, 0 }, [36] = { 37, 0 }, [37] = { 38, 0 }, [38] = { 39, 0 }, [39] = { 40, 0 },
};
static inline __attribute__((always_inline)) unsigned int translate(int f)
{
	switch (f) {
	case 1300: return 1270;
	case 1301: return 1271;
	case 1302: return 1272;
	case 1303: return 1273;
	default: return f;
	}
}
static inline __attribute__((always_inline)) void check(unsigned int leaf)
{
	if (leaf >= sizeof(reverse) / sizeof(reverse[0]) || reverse[leaf].function == 0)
		table_bad();
}
static inline __attribute__((always_inline)) unsigned int feature_leaf(int f)
{
	unsigned int leaf = translate(f) / 32;

	check(leaf);
	return leaf;
}
static inline __attribute__((always_inline)) unsigned int feature_bit(int f)
{
	f = translate(f);
	check(f / 32);
	return 1u << (f & 31);
}
void init_caps(void)
{
	unsigned int features = 0;
EOF2
	i=0
	while [ $i -lt 800 ]; do
		echo "	if (feature_leaf($i) != $((i / 32))) leaf_bad();"
		echo "	features |= feature_bit($i);"
		if [ $((i % 32)) -eq 31 ]; then
			echo "	caps[$((i / 32))] = features;"
			echo "	features = 0;"
		fi
		i=$((i + 1))
	done
	cat <<'EOF2'
	caps[63] = features;
}
EOF2
} >"$CC_TMP/u.c"

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads;
void table_bad(void) { bads++; }
void leaf_bad(void) { bads++; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	unsigned long long sum = 0;
	int i;

	init_caps();
	for (i = 0; i < 64; i++)
		sum = sum * 31 + caps[i];
	printf("%llx bads %d\n", sum, bads);
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_bad$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$(undefined "$CC_TMP/g.o")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env CC_MIR_STATS=1"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_inline_past_cap: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_inline_past_cap: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ -n "$want" ]; then
			echo "run_inline_past_cap: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_inline_past_cap: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_inline_past_cap: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_inline_past_cap: ok"
