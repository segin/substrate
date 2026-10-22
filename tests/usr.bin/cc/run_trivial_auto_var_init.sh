#!/bin/sh
# -ftrivial-auto-var-init=zero: an automatic variable with no initializer
# starts as zero, structure, union and array too.  Linux builds with it;
# cc warned the option unsupported and left aggregates as the stack had
# them, and the slot optimizer may read such a field as the value stored
# to it later.  =uninitialized is the default; =pattern is refused.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/avi.c" <<'EOF'
#include <stdio.h>
struct s { int a; char b[13]; long c; };
union u { int i; char c[24]; };

__attribute__((noinline)) static int dirty(void) {
	volatile unsigned char junk[512];
	int i, sum = 0;

	for (i = 0; i < 512; i++)
		junk[i] = 0xaa;
	for (i = 0; i < 512; i++)
		sum += junk[i];
	return sum;
}

__attribute__((noinline)) static int nonzero(const void *p, unsigned long n) {
	const volatile unsigned char *q = p;
	unsigned long i;

	for (i = 0; i < n; i++)
		if (q[i] != 0)
			return 1;
	return 0;
}

/* a field read before the store that follows it is the zero, not the stored value */
__attribute__((noinline)) static int read_then_store(void) {
	struct s x;
	int before = x.a;

	x.a = 5;
	return before + x.a;
}

__attribute__((noinline)) static int check(void) {
	struct s x;
	union u y;
	int arr[17];
	char *ptrs[3];
	long scalar;
	int bad = 0;

	bad |= nonzero(&x, sizeof(x));
	bad |= nonzero(&y, sizeof(y)) << 1;
	bad |= nonzero(arr, sizeof(arr)) << 2;
	bad |= nonzero(ptrs, sizeof(ptrs)) << 3;
	bad |= nonzero(&scalar, sizeof(scalar)) << 4;
	return bad;
}

int main(void) {
	int r1, r2;

	dirty();
	r1 = check();
	dirty();
	r2 = read_then_store();
	printf("%d %d\n", r1, r2);
	return 0;
}
EOF

for m in 32 64; do
	for o in O0 O2 Os; do
		"$CC_BIN" -m$m -$o -ftrivial-auto-var-init=zero -o "$CC_TMP/avi" "$CC_TMP/avi.c"
		out=$("$CC_TMP/avi")
		if [ "$out" != "0 5" ]; then
			echo "run_trivial_auto_var_init: -m$m -$o printed '$out', not '0 5'"
			exit 1
		fi
	done
done
"$CC_BIN" -m32 -ftrivial-auto-var-init=uninitialized -c -o "$CC_TMP/avi.o" "$CC_TMP/avi.c"
if "$CC_BIN" -m32 -ftrivial-auto-var-init=pattern -c -o "$CC_TMP/avi.o" "$CC_TMP/avi.c" 2> "$CC_TMP/avi.err"; then
	echo "run_trivial_auto_var_init: =pattern was taken"
	exit 1
fi
grep -q 'must be zero or uninitialized' "$CC_TMP/avi.err"
echo "run_trivial_auto_var_init: ok"
