#!/bin/sh
# A loop that tests at its top tests at its bottom too: the jump back to
# its test is a copy of the test and a branch back into the body, so a
# trip takes one branch, not a jump and a branch, as gcc's loops do.
# Loops with breaks, continues, an exit elsewhere than after the loop,
# unsigned and pointer tests, nesting, no trips at all and a computed
# goto must still run as gcc's do, -m64 and -m32 at -O0 to -O2.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_loop_rotate: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/r.c" <<'EOF2'
#include <stdio.h>

#define NI __attribute__((noinline))

NI long sum(const int *v, int n)
{
	long s = 0;
	int i;

	for (i = 0; i < n; i++)
		s += v[i];
	return s;
}

NI int skips(const int *v, int n)
{
	int s = 0;
	int i;

	for (i = 0; i < n; i++) {
		if (v[i] < 0)
			continue;
		if (v[i] > 1000)
			break;
		s += v[i];
	}
	return s;
}

/* the loop's exit returns from inside it, elsewhere than after it */
NI int find(const unsigned char *p, unsigned n, unsigned char c)
{
	unsigned i = 0;

	while (i < n) {
		if (p[i] == c)
			return (int)i;
		i++;
	}
	return -1;
}

NI unsigned long strlen2(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	return (unsigned long)(p - s);
}

NI long nested(int n, int m)
{
	long s = 0;
	int i, j;

	for (i = 0; i < n; i++)
		for (j = i; j < m; j += 2)
			s += i * j - (j ^ i);
	return s;
}

NI int countdown(unsigned u)
{
	int k = 0;

	while (u-- > 3)
		k += (int)u;
	return k;
}

NI int jumps(int n)
{
	static void *const tab[] = {&&one, &&two};
	int s = 0;
	int i = 0;

again:
	if (i >= n)
		return s;
	goto *tab[i & 1];
one:
	s += 3;
	i++;
	goto again;
two:
	s *= 2;
	i++;
	goto again;
}

int main(void)
{
	static const int v[] = {4, -2, 7, 9, 1200, 5, -8, 3};
	static const unsigned char b[] = "abcdefgh";

	printf("%ld %ld %d %d %d %d %lu %lu %ld %ld %d %d %d %d\n", sum(v, 8), sum(v, 0), skips(v, 8), skips(v, 3),
	       find(b, 8, 'f'), find(b, 8, 'z'), strlen2("rotate"), strlen2(""), nested(9, 13), nested(0, 4),
	       countdown(11), countdown(2), jumps(9), jumps(0));
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/r.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O1 O2; do
		"$CC_BIN" -m$m -$o -c -o "$CC_TMP/r.o" "$CC_TMP/r.c"
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/r.o"
		got=$(timeout 20 "$CC_TMP/c" || echo "exit $?")
		if [ "$got" != "$want" ]; then
			echo "run_loop_rotate: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
# sum()'s loop: its last branch goes back into it, and nothing jumps back to a test
for m in 64 32; do
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/r.s" "$CC_TMP/r.c"
	awk '/^sum:/{x=1} /^\.size sum,/{x=0} x' "$CC_TMP/r.s" >"$CC_TMP/sum.s"
	if grep -qE '^	jmp \.L' "$CC_TMP/sum.s"; then
		echo "run_loop_rotate: -m$m sum() jumps back to its test on every trip"
		cat "$CC_TMP/sum.s"
		exit 1
	fi
	if ! grep -qE '^	j(l|nge|b|nae|ne) \.L' "$CC_TMP/sum.s"; then
		echo "run_loop_rotate: -m$m sum() has no branch back into its loop"
		cat "$CC_TMP/sum.s"
		exit 1
	fi
done
echo "run_loop_rotate: ok"
