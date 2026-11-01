#!/bin/sh
# At -O0 an always_inline function is inlined, as gcc inlines it there:
# an asm's "i" operand of its parameter is the caller's constant only so.
# cc inlined nothing at -O0, and a static key's address passed to
# Linux's arch_static_branch() was written 0 -- a jump table entry at
# address 0.  The entries are read back and compared with the addresses;
# a function not always_inline is still called.  Results are gcc's,
# -m64 and -m32, -O0 and -O2.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_always_inline_O0: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/a.c" <<'EOF2'
#include <stdio.h>

#ifdef __x86_64__
#define ADDR ".quad"
#else
#define ADDR ".long"
#endif

struct key {
	int enabled;
};
struct key k1 = { 1 }, k2 = { 0 };
extern void *__start_ccai[], *__stop_ccai[];

static inline __attribute__((always_inline)) int branch(struct key *const key, const int on)
{
	__asm__(".pushsection ccai, \"aw\"\n\t" ADDR " %c0 + %c1\n\t.popsection" : : "i"(key), "i"(on));
	return key->enabled;
}

static inline int plain(int x)
{
	return x + 1;
}

int main(void)
{
	void **p;
	int n = 0;

	n += branch(&k1, 1) + branch(&k2, 0) + plain(4);
	for (p = __start_ccai; p < __stop_ccai; p++)
		n += 10 * ((char *)*p == (char *)&k1 + 1) + 100 * (*p == (void *)&k2) + 1000 * (*p == 0);
	printf("%d %d\n", n, (int)(__stop_ccai - __start_ccai));
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/a.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/a.o" "$CC_TMP/a.c" 2>"$CC_TMP/c.err"; then
			echo "run_always_inline_O0: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/a.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_always_inline_O0: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	# at -O0 a function only inline is still called
	"$CC_BIN" -m$m -O0 -S -o "$CC_TMP/a.s" "$CC_TMP/a.c"
	if ! grep -q 'call.*plain' "$CC_TMP/a.s"; then
		echo "run_always_inline_O0: -m$m -O0 inlined plain(), which is not always_inline"
		exit 1
	fi
done
echo "run_always_inline_O0: ok"
