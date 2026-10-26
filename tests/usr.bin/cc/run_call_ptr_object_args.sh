#!/bin/sh
# A call through a pointer object named directly -- `fp(x)`, fp a variable
# or a global, as Linux's `delay_fn(loops)` in lib/delay.c is -- converts
# its arguments to the prototype's parameter types (C11 6.5.2.2p7), as a
# call of (*fp), of s->fn or of a function does.  Sema took the function
# type only from a pointer expression, so such a call converted nothing:
# a 32-bit count went to a u64 parameter with whatever %edx held (an i386
# kernel spun in delay_tsc() whenever its TSC calibrated), an int to a
# double parameter in an integer register, a short unnarrowed.  Results
# are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_call_ptr_object_args: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>

typedef unsigned long long u64;
typedef void fn_t(u64, double, short, unsigned char);

struct ops { fn_t *fn; };

void __attribute__((noinline)) take(u64 x, double d, short s, unsigned char c)
{
	printf("%llu %.1f %d %u\n", x, d, s, c);
}

fn_t *volatile gp = take;
static fn_t *sp = take;
static struct ops o = { take };

int main(void)
{
	fn_t *lp = take;
	struct ops *po = &o;
	unsigned long l = 4355;
	int i = 7, big = 70000, w = 300;

	gp(l, i, big, w);
	sp(l, i, big, w);
	lp(l, i, big, w);
	(*gp)(l, i, big, w);
	po->fn(l, i, big, w);
	o.fn(l, i, big, w);
	take(l, i, big, w);
	return 0;
}
EOF2
cat >"$CC_TMP/d.c" <<'EOF2'
typedef unsigned long long u64;
static void (*delay_fn)(u64);
void __delay(unsigned long loops) { delay_fn(loops); }
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/p.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/p.c"
		got=$("$CC_TMP/c") || { echo "run_call_ptr_object_args: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_call_ptr_object_args: -m$m -$o prints:"
			echo "$got"
			echo "gcc's:"
			echo "$want"
			exit 1
		fi
	done
done
case "$modes" in
*32*)
	"$CC_BIN" -m32 -O2 -mregparm=3 -fno-pic -S -o "$CC_TMP/d.s" "$CC_TMP/d.c"
	if ! awk '/^__delay:/{x=1} x' "$CC_TMP/d.s" | grep -qE '^	xorl %edx, %edx$'; then
		echo "run_call_ptr_object_args: __delay() passes its count to delay_fn's u64 without the high word"
		exit 1
	fi
	;;
esac
echo "run_call_ptr_object_args: ok"
