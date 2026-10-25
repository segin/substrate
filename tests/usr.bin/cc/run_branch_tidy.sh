#!/bin/sh
# Branches tidied after allocation: once copies have gone, blocks that
# were not empty at selection are, and a return between was a branch
# over a jump -- `jne 2f; jmp .Lc3_epilogue; 2:` for each early return
# of a void function.  A label nothing jumps to goes, a jump or a branch
# to the label that comes next goes, a branch over a jump is the one
# branch the other way, and a branch over a return the one branch to the
# epilogue: c3() has no jmp at all.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_branch_tidy: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/t.c" <<'EOF2'
extern long g(long);
extern long h(long, long);

void c3(unsigned long s, unsigned long e, unsigned long f, unsigned long *p)
{
	s &= ~0xfffUL;
	if (s < f) {
		s += 0x1000;
		if (!s)
			return;
	}
	if (e) {
		e &= ~0xfffUL;
		if (!e)
			return;
	}
	if (s > e - 1)
		return;
	*p = h(s, e);
	*p += g(*p) + g(s) + g(e) + g(f);
}
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

void c3(unsigned long s, unsigned long e, unsigned long f, unsigned long *p);
long g(long x) { return x * 3 + 1; }
long h(long a, long b) { return a ^ b; }

int main(void)
{
	static const unsigned long v[][3] = {
		{0x1234, 0x9000, 0x2000}, {-0x800UL, 0x9000, -1UL}, {0x1234, 0x800, 0},
		{0x1234, 0, 0}, {0x5000, 0x3000, 0}, {0x2000, 0x2001, 0x1000},
	};
	unsigned i;

	for (i = 0; i < sizeof(v) / sizeof(v[0]); ++i) {
		unsigned long p = 7;

		c3(v[i][0], v[i][1], v[i][2], &p);
		printf("%lu ", p);
	}
	printf("\n");
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
	for o in O1 O2 Os; do
		"$CC_BIN" -m$m -$o -c -o "$CC_TMP/t.o" "$CC_TMP/t.c"
		gcc -m$m -o "$CC_TMP/c" "$CC_TMP/t.o" "$CC_TMP/m.o"
		got=$("$CC_TMP/c") || { echo "run_branch_tidy: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_branch_tidy: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/t.s" "$CC_TMP/t.c"
	if awk '/^c3:/{x=1} /^\.size c3,/{x=0} x' "$CC_TMP/t.s" | grep -qE '^	jmp'; then
		echo "run_branch_tidy: -m$m: c3() jumps over a jump"
		awk '/^c3:/{x=1} /^\.size c3,/{x=0} x' "$CC_TMP/t.s" | grep -E '^	j'
		exit 1
	fi
done
echo "run_branch_tidy: ok"
