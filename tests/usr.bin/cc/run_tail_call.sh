#!/bin/sh
# A call its return follows is a tail call, as gcc makes it at -O2: the
# epilogue, then `jmp g`, the callee returning for the caller -- where
# its arguments are all in registers (x86-64's, i386 regparm's), it pops
# nothing, and nothing in the caller's frame may be reached through a
# pointer: keep() passes a local's address, and still calls.  Results
# are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_tail_call: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/t.c" <<'EOF2'
#ifdef __i386__
#define RP __attribute__((regparm(3)))
#else
#define RP
#endif
RP long g(long, long);
RP long h(long *);

RP long f(long a, long b)
{
	if (a > b)
		return g(b, a);
	return g(a + 1, b);
}

RP long keep(long a)
{
	long x = a * 3;

	return h(&x); /* (the callee may read x, in this frame) */
}

RP void v(long a)
{
	g(a, a);
}
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

#ifdef __i386__
#define RP __attribute__((regparm(3)))
#else
#define RP
#endif
RP long f(long a, long b);
RP long keep(long a);
RP void v(long a);
static long seen;
RP long g(long x, long y) { seen += x; return x * 10 + y; }
RP long h(long *p) { return *p + 1; }

int main(void)
{
	long a = f(5, 3), b = f(1, 2), c = keep(4);

	v(7);
	printf("%ld %ld %ld %ld\n", a, b, c, seen);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/t.c" "$CC_TMP/m.c"
	want=$("$CC_TMP/g")
	gcc -m$m -O1 -c -o "$CC_TMP/m.o" "$CC_TMP/m.c"
	for o in O0 O1 O2 Os; do
		for fp in "" -fomit-frame-pointer; do
			"$CC_BIN" -m$m -$o $fp -c -o "$CC_TMP/t.o" "$CC_TMP/t.c"
			gcc -m$m -o "$CC_TMP/c" "$CC_TMP/t.o" "$CC_TMP/m.o"
			got=$("$CC_TMP/c") || { echo "run_tail_call: -m$m -$o $fp: the program fails"; exit 1; }
			if [ "$got" != "$want" ]; then
				echo "run_tail_call: -m$m -$o $fp prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/t.s" "$CC_TMP/t.c"
	for fn in f v; do
		if ! awk -v f="$fn" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/t.s" | grep -qE '^	jmp g$'; then
			echo "run_tail_call: -m$m: $fn() does not jump to g"
			exit 1
		fi
	done
	if awk '/^keep:/{x=1} /^\.size keep,/{x=0} x' "$CC_TMP/t.s" | grep -qE '^	jmp h$'; then
		echo "run_tail_call: -m$m: keep() jumps to h with a local of its own passed"
		exit 1
	fi
done
echo "run_tail_call: ok"
