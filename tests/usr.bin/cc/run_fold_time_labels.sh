#!/bin/sh
# The folder's time on a function of many labels and values stays about
# linear in its size.  It put every value's known constant back at every
# label (a copy of the whole state, the function's values times its
# labels), and worked out a variable's constant by a walk of the whole
# function per value; Linux's kvm_initialize_cpu_caps(), hundreds of
# inlined __feature_translate() switches, took arch/x86/kvm/cpuid.c to
# 400 seconds.  A function of 1200 such inlined switches must compile in
# under a CPU minute (it took five), and the program, run, prints what
# gcc's prints; -O2, -m64 and -m32, both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fold_time_labels: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

{
	cat <<'EOF2'
unsigned int caps[64];
static inline __attribute__((always_inline)) unsigned int translate(int f)
{
	switch (f) {
	case 360: return 704;
	case 361: return 705;
	case 370: return 715;
	case 104: return 776;
	case 244: return 800;
	case 363: return 834;
	case 674: return 836;
	case 683: return 897;
	case 684: return 898;
	case 688: return 933;
	default: return f;
	}
}
static inline __attribute__((always_inline)) unsigned int bit(int f)
{
	f = translate(f);
	return 1u << (f & 31);
}
void init_caps(void)
{
	unsigned int features = 0;
EOF2
	i=0
	while [ $i -lt 1200 ]; do
		echo "	features |= bit($i);"
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
cat >"$CC_TMP/main.c" <<'EOF2'
#include <stdio.h>
extern unsigned int caps[64];
void init_caps(void);
int main(void)
{
	unsigned long long sum = 0;
	int i;

	init_caps();
	for (i = 0; i < 64; i++)
		sum = sum * 31 + caps[i];
	printf("%llx\n", sum);
	return 0;
}
EOF2

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/u.c" "$CC_TMP/main.c" 2>/dev/null || continue
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! (ulimit -t 60; $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c") 2>"$CC_TMP/c.err"; then
			echo "run_fold_time_labels: $what failed or took over a CPU minute: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o" "$CC_TMP/main.c"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_fold_time_labels: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_time_labels: ok"
