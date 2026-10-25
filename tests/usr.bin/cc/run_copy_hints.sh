#!/bin/bash
# A value is given the register its copies suggest most -- each copy a
# vote by how often it runs (tenfold a loop), passed on along chains of
# copies between values -- so a variable an inline asm takes in and gives
# back in a register lives there: a multiply-accumulate asm in a loop had
# its three accumulators in %rax, %rcx and %rdx, the first and last of
# which it clobbers, and copied all seven operands in and out each time
# round.  Results are gcc's; the loop moves no register to another.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_copy_hints: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

static inline void mac2(unsigned long *lo, unsigned long *hi, unsigned long *top,
			unsigned long a, unsigned long b, unsigned long c, unsigned long d)
{
	unsigned long l = *lo, h = *hi, t = *top, x;

	asm("# mac2 {\n\t"
	    "movq %[a], %%rax\n\t"
	    "mulq %[b]\n\t"
	    "addq %%rax, %[l]\n\t"
	    "adcq %%rdx, %[h]\n\t"
	    "adcq $0, %[t]\n\t"
	    "movq %[c], %%rax\n\t"
	    "mulq %[d]\n\t"
	    "addq %%rax, %[l]\n\t"
	    "adcq %%rdx, %[h]\n\t"
	    "adcq $0, %[t]\n\t"
	    "movq %[l], %[x]\n\t"
	    "xorq %[h], %[x]\n\t"
	    "rolq $13, %[x]\n\t"
	    "# mac2 }"
	    : [l] "+r"(l), [h] "+r"(h), [t] "+r"(t), [x] "=&r"(x)
	    : [a] "r"(a), [b] "r"(b), [c] "r"(c), [d] "r"(d)
	    : "rax", "rdx", "cc");
	*lo = l;
	*hi = h;
	*top = t ^ x;
}

unsigned long dot(const unsigned long *u, const unsigned long *v, long n)
{
	unsigned long lo = 0, hi = 0, top = 0;
	long i;

	for (i = 0; i + 1 < n; i += 2)
		mac2(&lo, &hi, &top, u[i], v[i], u[i + 1], v[i + 1]);
	return lo ^ hi ^ top;
}

int main(void)
{
	unsigned long u[16], v[16];
	int i;

	for (i = 0; i < 16; ++i) {
		u[i] = 0x9e3779b97f4a7c15ul * (unsigned long)(i + 1);
		v[i] = 0xbf58476d1ce4e5b9ul ^ ((unsigned long)i << 40);
	}
	printf("%lx\n", dot(u, v, 16));
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/m.c"
want=$("$CC_TMP/g")
"$CC_BIN" -m64 -O2 -o "$CC_TMP/c" "$CC_TMP/m.c"
got=$("$CC_TMP/c")
if [ "$got" != "$want" ]; then
	echo "run_copy_hints: prints [$got], gcc's [$want]"
	exit 1
fi
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/m.s" "$CC_TMP/m.c"
# the loop's body, from its test's branch out to the jump back, but the asm's own text
awk '/^\.Ldot_0:/{z=1} z && !f && /^\tj[a-z]+ \.Ldot_[0-9]+$/{f=1; next} /# mac2 \{/{a=1} f && !a; /# mac2 \}/{a=0} f && /^\tjmp \.Ldot_0$/{f=0; z=0}' "$CC_TMP/m.s" >"$CC_TMP/loop.s"
if ! grep -q 'Ldot_0' "$CC_TMP/loop.s"; then
	echo "run_copy_hints: no loop body found in dot()"
	exit 1
fi
if grep -E '^	movq %[a-z0-9]+, %[a-z0-9]+$' "$CC_TMP/loop.s"; then
	echo "run_copy_hints: dot()'s loop moves the registers above"
	exit 1
fi
echo "run_copy_hints: ok"
