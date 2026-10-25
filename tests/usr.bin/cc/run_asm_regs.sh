#!/bin/sh
# An inline asm's register operands are in registers the selector gives
# them, not frame slots: inputs moved into them before, outputs moved
# out after, and the asm reads, writes and clobbers only those -- so the
# values of the function stay in registers across it.  Linux's test_bit()
# stored the address it tests and its flag output to the frame and read
# both back; a function of nothing but asm had a frame for them.  The
# programs' output is gcc's, under fixed registers, early clobbers, tied
# inputs, flag outputs, memory operands by address, immediates and
# callee-saved clobbers.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/a.c" <<'EOF2'
#include <stdio.h>

#ifdef __x86_64__
#define W "q"
#define BX "rbx"
#else
#define W "l"
#define BX "ebx"
#endif
typedef unsigned long word;

static inline __attribute__((always_inline)) _Bool test_bit(long nr, const volatile word *addr)
{
	_Bool old;

	asm volatile("bt" W " %2,%1" : "=@ccc"(old) : "m"(*(word *)addr), "Ir"(nr) : "memory");
	return old;
}

static inline word add3(word a, word b, word c)
{
	word r;

	asm("lea (%1,%2), %0\n\tadd %3, %0" : "=&r"(r) : "r"(a), "r"(b), "r"(c));
	return r;
}

static inline word xadd(word *p, word v)
{
	asm volatile("lock xadd %0, %1" : "+r"(v), "+m"(*p) : : "memory");
	return v;
}

static inline unsigned shifted(unsigned x, unsigned n)
{
	asm("shl %%cl, %0" : "=r"(x) : "0"(x), "c"(n));
	return x;
}

static inline word fixed_regs(word a, word b)
{
	word d;

	asm("mov %1, %0\n\tadd %2, %0" : "=D"(d) : "S"(a), "d"(b));
	return d;
}

static inline word in_rbx(word a)
{
	word r;

	/* a callee-saved register the asm clobbers: the function saves it */
	asm("mov %1, %%" BX "\n\tadd %%" BX ", %%" BX "\n\tmov %%" BX ", %0" : "=r"(r) : "r"(a) : BX);
	return r;
}

static inline int is_zero(word x)
{
	_Bool z;

	asm("test %1, %1" : "=@ccz"(z) : "r"(x));
	return z;
}

word __attribute__((noinline)) mix(word *v, int n, word k)
{
	word t = 0;
	int i;

	for (i = 0; i < n; ++i) {
		/* t, i, n, k live across every asm */
		if (test_bit(i & 7, &v[i]))
			t = add3(t, v[i], k);
		t += xadd(&v[i], (word)i);
		t ^= shifted((unsigned)t, (unsigned)(i & 3));
		t += fixed_regs(t, k) + in_rbx((word)i);
		if (is_zero(v[i] & 1))
			t += 5;
	}
	return t;
}

int main(void)
{
	word v[16];
	int i;

	for (i = 0; i < 16; ++i)
		v[i] = (word)(i * 37 + 11);
	printf("%lu\n", (unsigned long)mix(v, 16, 3));
	for (i = 0; i < 16; ++i)
		printf("%lu ", (unsigned long)v[i]);
	printf("\n");
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/ag" "$CC_TMP/a.c"
	want=$("$CC_TMP/ag")
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/ac" "$CC_TMP/a.c"
		got=$("$CC_TMP/ac")
		if [ "$got" != "$want" ]; then
			echo "run_asm_regs: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done

# test_bit() alone: no frame for its operands
cat >"$CC_TMP/t.c" <<'EOF2'
extern void g(void);
int f(unsigned long *p)
{
	_Bool old;

	asm volatile("btq $6,%1" : "=@ccc"(old) : "m"(*p) : "memory");
	if (old)
		g();
	return 0;
}
EOF2
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/t.s" "$CC_TMP/t.c"
if grep -E '\(%rbp\)|sub[q]? +\$[0-9]+, %rsp' "$CC_TMP/t.s"; then
	echo "run_asm_regs: the asm's operands went through the frame"
	exit 1
fi
echo "run_asm_regs: ok"
