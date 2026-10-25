#!/bin/sh
# On i386 a 64-bit value cut to a long and masked with 32 bits of ones
# is its low word, nothing done: Linux's page-table masks are 64-bit
# constants (PHYSICAL_PAGE_MASK) cut to pteval_t, and the and of the
# pair with 0xffffffff was kept, `andl $-1, %reg`, 84 times in i386
# mm/memory.o.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_low_word: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_i386_low_word: skip (no gcc -m32)"
	exit 0
fi

cat >"$CC_TMP/l.c" <<'EOF2'
#include <stdio.h>

typedef unsigned long u32l;
typedef unsigned long long u64;

u32l __attribute__((noinline)) a1(u32l x) { return (u32l)((u64)x & 0xffffffffULL); }
u32l __attribute__((noinline)) a2(u32l *p) { u64 v = *p; return (u32l)(v & ~0ULL) & 0x80; }
u32l __attribute__((noinline)) a3(u32l x, u32l y)
{
	u64 m = (x & 0x80) ? ~0x3fffffULL : ~0xfffULL;

	return (u32l)((u64)y & ~m);
}
static inline u64 widen(u32l v) { return v; }
u32l __attribute__((noinline)) a4(u32l *p) { return (u32l)(widen(*p) & 0xffffffffULL) & 0x181; }

int main(void)
{
	u32l v = 0x12345681;

	printf("%lx %lx %lx %lx %lx\n", a1(0xdeadbeef), a2(&v), a3(0x80, 0xffffffff), a3(0, 0x12345678), a4(&v));
	return 0;
}
EOF2
gcc -m32 -O1 -o "$CC_TMP/g" "$CC_TMP/l.c"
want=$("$CC_TMP/g")
for o in O0 O1 O2 Os; do
	"$CC_BIN" -m32 -$o -o "$CC_TMP/c" "$CC_TMP/l.c"
	got=$("$CC_TMP/c") || { echo "run_i386_low_word: -$o: the program fails"; exit 1; }
	if [ "$got" != "$want" ]; then
		echo "run_i386_low_word: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/l.s" "$CC_TMP/l.c"
if grep -qE 'andl \$-1,' "$CC_TMP/l.s"; then
	echo "run_i386_low_word: a word is masked with all its bits"
	exit 1
fi
echo "run_i386_low_word: ok"
