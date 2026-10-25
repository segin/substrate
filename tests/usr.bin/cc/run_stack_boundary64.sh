#!/bin/sh
# -mpreferred-stack-boundary=3, which Linux builds x86-64 with, keeps
# the stack 8-byte aligned at a call, not the psABI's 16: cc took it on
# i386 only, and on x86-64 a function with no frame of its own but its
# calls still made one of 8 bytes to align them, `subq $8, %rsp` and
# `addq $8, %rsp`.  A function with a 16-byte-aligned object in its frame
# still takes the stack as 16-byte aligned, as without the option.
# Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_stack_boundary64: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>
#include <stdint.h>

long __attribute__((noinline)) g(long x) { return x * 3 + 1; }
int __attribute__((noinline)) aligned_ok(void *p) { return ((uintptr_t)p & 15) == 0; }

long __attribute__((noinline)) twice(long a)
{
	long r = g(a);

	return r + g(r);
}

int __attribute__((noinline)) with16(int k)
{
	_Alignas(16) char buf[32];

	buf[0] = (char)k;
	return aligned_ok(buf) * 10 + buf[0];
}

int main(void)
{
	long a = twice(4);
	int b = with16(3);

	printf("%ld %d\n", a, b);
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/b.c"
want=$("$CC_TMP/g")
for o in O0 O1 O2 Os; do
	for fp in "" -fomit-frame-pointer; do
		"$CC_BIN" -m64 -$o $fp -mpreferred-stack-boundary=3 -o "$CC_TMP/c" "$CC_TMP/b.c"
		got=$("$CC_TMP/c") || { echo "run_stack_boundary64: -$o $fp: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_stack_boundary64: -$o $fp prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
"$CC_BIN" -m64 -O2 -mpreferred-stack-boundary=3 -S -o "$CC_TMP/b.s" "$CC_TMP/b.c"
if awk '/^twice:/{x=1} /^\.size twice,/{x=0} x' "$CC_TMP/b.s" | grep -qE '^	(sub|add)q \$8, %rsp$'; then
	echo "run_stack_boundary64: twice() aligns its calls to 16 bytes under -mpreferred-stack-boundary=3"
	exit 1
fi
echo "run_stack_boundary64: ok"
