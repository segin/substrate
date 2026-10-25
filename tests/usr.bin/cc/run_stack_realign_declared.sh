#!/bin/sh
# Under a stack boundary below 16 bytes, only an object whose declaration
# asks for 16-byte alignment makes the prologue realign the stack: the
# frame puts any local of 16 bytes or more on a 16-byte boundary by
# choice, and 501793579 realigned for those too -- `andl $-16, %esp` and
# every access to them off %esp -- which made Linux's i386 maple_tree.o
# 645 bytes larger.  A declared one is still aligned.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_stack_realign_declared: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/r.c" <<'EOF2'
#include <stdio.h>
#include <stdint.h>

void __attribute__((noinline)) fill(int *p, int n) { while (n-- > 0) p[n] = n * 3; }
int __attribute__((noinline)) aligned_ok(void *p) { return ((uintptr_t)p & 15) == 0; }

int __attribute__((noinline)) plain(int k)
{
	int buf[8];

	fill(buf, 8);
	return buf[k];
}

int __attribute__((noinline)) declared(int k)
{
	_Alignas(16) int buf[8];

	fill(buf, 8);
	return buf[k] + aligned_ok(buf) * 100;
}

int main(void)
{
	int a = plain(5), b = declared(6);

	printf("%d %d\n", a, b);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	b=3
	[ "$m" = 32 ] && b=2
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/r.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -mpreferred-stack-boundary=$b -o "$CC_TMP/c" "$CC_TMP/r.c"
		got=$("$CC_TMP/c") || { echo "run_stack_realign_declared: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_stack_realign_declared: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -mpreferred-stack-boundary=$b -S -o "$CC_TMP/r.s" "$CC_TMP/r.c"
	if awk '/^plain:/{x=1} /^\.size plain,/{x=0} x' "$CC_TMP/r.s" | grep -qE '^	and[lq] \$-16, %[er]sp$'; then
		echo "run_stack_realign_declared: -m$m: plain() realigns the stack for a local no declaration aligns"
		exit 1
	fi
	if ! awk '/^declared:/{x=1} /^\.size declared,/{x=0} x' "$CC_TMP/r.s" | grep -qE '^	and[lq] \$-16, %[er]sp$'; then
		echo "run_stack_realign_declared: -m$m: declared() does not realign the stack for its 16-byte local"
		exit 1
	fi
done
echo "run_stack_realign_declared: ok"
