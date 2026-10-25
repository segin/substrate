#!/bin/sh
# -fomit-frame-pointer, which Linux builds with: a function that needs no
# frame pointer addresses its frame off the stack pointer -- counting what
# a call's pushes have moved it by, and what a callee pops (i386's hidden
# pointer to a structure result) -- and has %ebp (%rbp) to allocate, saved
# by the prologue as the others are.  One that reads its frame's address,
# moves the stack pointer itself, or takes variadic arguments keeps one.
# Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_omit_frame_pointer: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/f.c" <<'EOF2'
#include <stdarg.h>
#include <stdio.h>

struct big { long a, b, c, d; };

struct big __attribute__((noinline)) mkbig(long x)
{
	struct big r = { x, x * 2, x * 3, x * 4 };

	return r;
}

long __attribute__((noinline)) many(long a, long b, long c, long d, long e, long f, long g, long h, long i)
{
	return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i;
}

/* calls with stack arguments and structure results, values live across them all */
long __attribute__((noinline)) busy(long x, long y)
{
	long p = x * 3, q = y ^ 5, r = x + y, s = x - y, t = x * y, u = p + q;
	struct big b = mkbig(p);
	long v = many(p, q, r, s, t, u, b.a, b.d, x);
	struct big c = mkbig(v & 0xff);
	long arr[6];
	int i;

	for (i = 0; i < 6; ++i)
		arr[i] = many(i, p, q, r, s, t, u, c.b, arr[0] * (i > 0));
	return v + c.c + arr[5] + p + q + r + s + t + u + b.b;
}

/* keeps its frame pointer: an asm's memory operand in a slot, */
int __attribute__((noinline)) asm_slot(int x)
{
	int v = x;

	asm("addl $5, %0" : "+m"(v));
	return v * 2;
}

/* va_start's area, */
long __attribute__((noinline)) varsum(int n, ...)
{
	va_list ap;
	long s = 0;

	va_start(ap, n);
	while (n-- > 0)
		s += va_arg(ap, long);
	va_end(ap);
	return s;
}

/* and alloca's and the frame's address */
long __attribute__((noinline)) vla(int n)
{
	long a[n];
	long s = 0;
	int i;

	for (i = 0; i < n; ++i)
		a[i] = i * busy(i, n);
	for (i = 0; i < n; ++i)
		s += a[i];
	return s + (long)(__builtin_frame_address(0) != 0);
}

int main(void)
{
	printf("%ld %ld %ld %d\n", busy(7, 3), varsum(4, 1L, 2L, 3L, 40L), vla(5), asm_slot(3));
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
F="-fomit-frame-pointer -fno-asynchronous-unwind-tables"
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/f.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o $F -o "$CC_TMP/c" "$CC_TMP/f.c"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_omit_frame_pointer: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 $F -S -o "$CC_TMP/f.s" "$CC_TMP/f.c"
	if awk '/^busy:/{x=1} /^\.size busy,/{x=0} x' "$CC_TMP/f.s" | grep -qE 'mov[lq] %[er]sp, %[er]bp'; then
		echo "run_omit_frame_pointer: -m$m: busy() still makes a frame pointer"
		exit 1
	fi
	if ! awk '/^vla:/{x=1} /^\.size vla,/{x=0} x' "$CC_TMP/f.s" | grep -qE 'mov[lq] %[er]sp, %[er]bp'; then
		echo "run_omit_frame_pointer: -m$m: vla() has no frame pointer"
		exit 1
	fi
done
echo "run_omit_frame_pointer: ok"
