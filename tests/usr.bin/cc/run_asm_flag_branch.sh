#!/bin/sh
# An asm's flag output read only by a branch after it -- through copies
# read once -- is the branch, on the condition the asm leaves in the
# flags: Linux's atomic_dec_and_test() and test_bit(), "=@cce" and
# "=@ccc", were `decl; sete %cl; movzbl %cl, %ecx; test %ecx, %ecx; je`
# where gcc has `decl; je`, 207 times in i386 mm/memory.o.  One read
# otherwise, as both() reads it, is still set.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_flag_branch: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/f.c" <<'EOF2'
#include <stdio.h>

static inline _Bool dec_and_test(int *v)
{
	_Bool r;

	asm volatile("decl %0" : "+m"(*v), "=@cce"(r) : : "memory");
	return r;
}

static inline _Bool test_bit(long nr, const unsigned long *a)
{
	_Bool r;

	asm volatile("bt %2, %1" : "=@ccc"(r) : "m"(*a), "Ir"(nr));
	return r;
}

int __attribute__((noinline)) put(int *v)
{
	if (dec_and_test(v))
		return 1;
	return 0;
}

int __attribute__((noinline)) tb(long n, unsigned long *a)
{
	if (test_bit(n, a))
		return 7;
	return 3;
}

int __attribute__((noinline)) both(int *v)
{
	_Bool r = dec_and_test(v);

	if (r)
		*v = 9;
	return r;
}

int main(void)
{
	int x = 2, y = 1;
	unsigned long b = 5;
	int a1 = put(&x);
	int a2 = put(&x);
	int a3 = tb(0, &b);
	int a4 = tb(1, &b);
	int a5 = both(&y);

	printf("%d %d %d %d %d %d\n", a1, a2, a3, a4, a5, y);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }
' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/f.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/f.c"
		got=$("$CC_TMP/c") || { echo "run_asm_flag_branch: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_asm_flag_branch: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/f.s" "$CC_TMP/f.c"
	for fn in put tb; do
		if awk -v f="$fn" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/f.s" | grep -qE '^	set|movzbl'; then
			echo "run_asm_flag_branch: -m$m: $fn() sets a byte of the flags it branches on"
			exit 1
		fi
	done
done
echo "run_asm_flag_branch: ok"
