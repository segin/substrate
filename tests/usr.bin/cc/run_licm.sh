#!/bin/sh
# What a loop computes the same on every trip is made once, before the
# loop: a matrix multiply's `&a[i][0]` was an imul and an add on every
# trip of its innermost loop.  What may trap or read memory stays where
# it is, as does what a trip may change: a division in a loop that runs
# no times must not divide, an operand whose address is taken may be
# written through a pointer, a variable written on some trips is not the
# same on all of them, nor is an asm's output.  Results are gcc's, -m64
# and -m32 at -O1 and -O2.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_licm: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/l.c" <<'EOF2'
#include <stdio.h>

#define NI __attribute__((noinline))
enum { N = 24 };
double a[N][N], b[N][N], c[N][N];

NI void mm(void)
{
	int i, j, k;

	for (i = 0; i < N; ++i)
		for (j = 0; j < N; ++j) {
			double s = 0;

			for (k = 0; k < N; ++k)
				s += a[i][k] * b[k][j];
			c[i][j] = s;
		}
}

/* runs no times when n is 0: the division must not be made */
NI int divs(int n, int x, int y)
{
	int s = 0;
	int i;

	for (i = 0; i < n; i++)
		s += x / y + i;
	return s;
}

/* k is written through p in the loop */
NI int through(int n)
{
	int k = 5;
	int *volatile p = &k;
	int s = 0;
	int i;

	for (i = 0; i < n; i++) {
		s += k * 3 + 1;
		*p = i;
	}
	return s;
}

/* m changes on one trip */
NI long sometimes(int n, long m)
{
	long s = 0;
	int i;

	for (i = 0; i < n; i++) {
		if (i == 3)
			m = 7;
		s += m * 11 + (m << 2);
	}
	return s;
}

/* an asm writes v on each trip */
NI unsigned asmout(int n, unsigned v)
{
	unsigned s = 0;
	int i;

	for (i = 0; i < n; i++) {
		__asm__("addl $3, %0" : "+r"(v));
		s += v * 5u;
	}
	return s;
}

/* invariant in both loops, read after them; and one of the outer loop's variable */
NI long nested(int n, long x, long y)
{
	long s = 0;
	long t = 0;
	int i, j;

	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++) {
			t = (x ^ y) * 13 + 7;
			s += t + (long)i * x + j;
		}
		do {
			s -= (y & 0xff) + i;
		} while (s > 100000);
	}
	return s + t;
}

/* a loop entered at its top only by a goto */
NI int gotoloop(int n, int x)
{
	int s = 0;
	int i = 0;

	goto test;
top:
	s += (x | 1) * 9 + i;
	i++;
test:
	if (i < n)
		goto top;
	return s;
}

int main(void)
{
	unsigned long h = 0;
	int i, j;

	for (i = 0; i < N; i++)
		for (j = 0; j < N; j++) {
			a[i][j] = (double)((i * 7 + j * 3) % 11) / 7.0;
			b[i][j] = (double)((i * 5 + j * 13) % 17) / 9.0;
		}
	mm();
	for (i = 0; i < N; i++)
		for (j = 0; j < N; j++)
			h = h * 31 + (unsigned long)(c[i][j] * 1000);
	printf("%lu %d %d %d %ld %ld %u %ld %ld %d %d\n", h, divs(0, 1, 0), divs(5, 100, 7), through(9),
	       sometimes(10, 2), sometimes(2, 2), asmout(6, 1), nested(7, 3, 0x1234), nested(0, 3, 4),
	       gotoloop(12, 4), gotoloop(0, 4));
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/l.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O1 O2; do
		"$CC_BIN" -m$m -$o -c -o "$CC_TMP/l.o" "$CC_TMP/l.c"
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/l.o"
		# (a loop's own counter taken for invariant never ends)
		got=$(timeout 20 "$CC_TMP/c" || echo "exit $?")
		if [ "$got" != "$want" ]; then
			echo "run_licm: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
# mm()'s innermost loop -- from the label its first jump or branch back goes to -- multiplies k by a row's size,
# not i too
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/l.s" "$CC_TMP/l.c"
awk '/^mm:/{x=1} /^\.size mm,/{x=0} x' "$CC_TMP/l.s" >"$CC_TMP/mm.s"
inner=$(awk '/^\.L[a-z_0-9]+:$/{l=$1; sub(/:$/, "", l); n=0} {body[n++]=$0} /^\tj[a-z]+ / && $2 == l {for (i = 0; i < n; i++) print body[i]; exit}' "$CC_TMP/mm.s")
if [ -z "$inner" ]; then
	echo "run_licm: no loop found in mm()"
	cat "$CC_TMP/mm.s"
	exit 1
fi
if [ "$(printf '%s\n' "$inner" | grep -c 'imul')" -ne 1 ]; then
	echo "run_licm: mm()'s inner loop computes a row's address on every trip"
	printf '%s\n' "$inner"
	exit 1
fi
echo "run_licm: ok"
