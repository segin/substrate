#!/bin/sh
# A condition of && or || made a bool and tested is threaded: Linux's
# IS_ERR_OR_NULL(p), `!p || IS_ERR_VALUE(p)` inlined, set a variable to 1
# on one way and to a compare on the other, masked it with 0xff and
# branched on it in the next block -- mov $1, jmp, cmp, setae, movzbl,
# test, jne.  A jump to the next label, its only way in, merges the two
# blocks, a 0 or a 1 masked with an odd number is itself, and each way in
# then jumps where its value goes, or branches on its compare.  And a
# narrow value branched on or tested against 0 on x86-64 is tested by
# its own bytes: a bool a call returned is testb %al, %al.  Results are
# gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_bool_join_thread: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/j.c" <<'EOF2'
#include <stdio.h>

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) __builtin_expect((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO, 0)

static inline _Bool IS_ERR_OR_NULL(const void *ptr) { return !ptr || IS_ERR_VALUE((unsigned long)ptr); }

static int puts_seen;
void __attribute__((noinline)) put(void *p) { puts_seen += p != 0; }

void __attribute__((noinline)) f(void *p)
{
	if (IS_ERR_OR_NULL(p))
		return;
	put(p);
}

_Bool __attribute__((noinline)) yes(int x) { return x > 3; }

int __attribute__((noinline)) g(int a, short s)
{
	int r = 0;

	if (yes(a))
		r += 1;
	if (a)
		r += 2;
	if (s)
		r += 4;
	if (a > 2 && (s < 0 || a < 9))
		r += 8;
	return r;
}

int main(void)
{
	int x;

	f(0);
	f((void *)-5L);
	f(&x);
	f(&puts_seen);
	printf("%d %d %d %d\n", puts_seen, g(9, 0), g(0, 3), g(5, (short)-2));
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/j.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/j.c"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_bool_join_thread: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/j.s" "$CC_TMP/j.c"
	if awk '/^f:/{x=1} /^\.size f,/{x=0} x' "$CC_TMP/j.s" | grep -qE '^	set'; then
		echo "run_bool_join_thread: -m$m: f() makes a bool of IS_ERR_OR_NULL()"
		exit 1
	fi
done
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/j.s" "$CC_TMP/j.c"
if awk '/^g:/{x=1} /^\.size g,/{x=0} x' "$CC_TMP/j.s" | grep -qE 'movzbl|movslq'; then
	echo "run_bool_join_thread: g() extends what it only tests"
	exit 1
fi
echo "run_bool_join_thread: ok"
