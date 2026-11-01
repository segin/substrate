#!/bin/sh
# `&*p` is p (C11 6.5.3.2p3), a null pointer constant too, in a static
# initializer: Linux's sysrq.c stands in for a configured-out handler
# with `#define sysrq_showlocks_op (*(const struct sysrq_key_op *)NULL)`
# and puts `&sysrq_showlocks_op` in a table, which cc refused.  The
# values are gcc's, -m64 and -m32, -O0 and -O2, through both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_global_addr_deref: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

struct op { int a; };
static const struct op o1 = { 1 };
int y[4];

#define NOP (*(const struct op *)NULL)
const struct op *t[4] = { &o1, &NOP, &*(const struct op *)((void *)0), &*&o1 };
int *u = &*(y + 2);
int *v = &*y;

int main(void)
{
	printf("%d %d %d %d %d %d\n", t[0] == &o1, t[1] == NULL, t[2] == NULL, t[3] == &o1, u == &y[2], v == y);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			$run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_global_addr_deref: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_global_addr_deref: ok"
