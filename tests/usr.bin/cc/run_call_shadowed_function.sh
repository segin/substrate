#!/bin/sh
# A variable that hides a function of its name is what a call of the
# name calls: `down(...)` and `(*down)(...)` of a parameter `down`, a
# pointer to a function, call the parameter.  cc looked the name up
# among the functions first: Linux's walk_tg_tree_from() has parameters
# `down` and `up` and a header declares the semaphore's down() and up(),
# and kernel/sched/core.c did not compile (the Arch config) -- and where
# the two had one type, the wrong function was called.  Both kinds of
# call, a local and a parameter, with the hidden function of the same
# type and of another; outside the variable's scope the function again.
# Results are gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_call_shadowed_function: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

typedef int (*visitor)(int, void *);

/* of the parameters' type, and of another */
int down(int x, void *d)
{
	(void)d;
	return x + 1000;
}
void up(int *sem)
{
	++*sem;
}

static int triple(int x, void *d)
{
	(void)d;
	return x * 3;
}

__attribute__((noinline)) static int walk(visitor down, visitor up, int n)
{
	int ret = (*down)(n, NULL);

	ret += down(n, NULL);
	ret += up(n, NULL) + (**up)(1, NULL);
	return ret;
}

__attribute__((noinline)) static int local(int n)
{
	int r;
	{
		visitor down = triple;

		r = down(n, NULL) + (*down)(n, NULL);
	}
	return r + down(n, NULL);
}

int main(void)
{
	int sem = 0;

	up(&sem);
	printf("%d %d %d\n", walk(triple, triple, 5), local(2), sem);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/s.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/s.o" "$CC_TMP/s.c" 2>"$CC_TMP/c.err"; then
			echo "run_call_shadowed_function: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/s.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_call_shadowed_function: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_call_shadowed_function: ok"
