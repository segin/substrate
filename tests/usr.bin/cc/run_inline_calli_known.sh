#!/bin/sh
# A call through a pointer that inlining has made a function's address
# is a call of that function, and inlined as one: gcc turns it direct
# once the pointer is known.  Linux's libeth passes its own
# libeth_xsk_xmit_fill_buf to libeth_xdp_tx_xmit_bulk(), always_inline
# both, which calls it through the pointer for each frame; its
# BUILD_BUG_ON(!__builtin_constant_p(tmo == LIBETH_XDP_TMO)) holds only
# where the caller's constant tmo reaches it, and idpf.o kept a call of
# an undefined __compiletime_assert_N while the call stayed indirect.  A
# pointer not known, and one to a function the file may not inline,
# stay calls through the pointer, as gcc keeps them.  -O2, -m64 and -m32,
# both code paths, every function through MIR; and the program, run,
# prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_inline_calli_known: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void tmo_bad(void), unknown_kept(void);
extern void sink(unsigned long long v);
#define TMO ((const void *)0x61C8864680B583EBull)
struct desc {
	unsigned long long addr;
	unsigned int flags;
};
typedef struct desc (*fill_fn)(const struct desc *d, unsigned int i, unsigned long long priv);

static inline __attribute__((always_inline)) struct desc fill_md(const struct desc *d, unsigned long long priv)
{
	const void *tmo = (const void *)(unsigned long)priv;
	struct desc r = *d;

	if (!__builtin_constant_p(tmo == TMO))
		tmo_bad();
	r.flags |= tmo == TMO ? 1 : 2;
	return r;
}
static inline __attribute__((always_inline)) struct desc fill(const struct desc *d, unsigned int i,
							       unsigned long long priv)
{
	struct desc r;

	if (priv)
		r = fill_md(d, priv);
	else
		r = *d;
	r.flags |= i << 4;
	return r;
}
static inline __attribute__((always_inline)) void xmit_bulk(const struct desc *bulk, unsigned int n,
							   unsigned long long priv, fill_fn f)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		struct desc r = f(&bulk[i], i, priv);

		sink(r.addr + r.flags);
	}
}
void xsk_xmit(const struct desc *bulk, unsigned int n)
{
	xmit_bulk(bulk, n, (unsigned long long)(unsigned long)TMO, fill);
}

/* a pointer not known: a call through it */
static int twice(int x) { return 2 * x; }
static int thrice(int x) { return 3 * x; }
int pick(int which, int x)
{
	int (*op)(int) = which ? twice : thrice;

	if (op(x) == -1)
		unknown_kept();
	return op(x);
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
static unsigned long long sum;
void tmo_bad(void) { bads++; }
void unknown_kept(void) { kepts++; }
void sink(unsigned long long v) { sum += v; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	static const struct desc d[3] = { { 100, 0 }, { 200, 8 }, { 300, 0 } };
	int a;

	xsk_xmit(d, 3);
	a = pick(1, 5) + pick(0, 5);
	printf("%llu %d bads %d kepts %d\n", sum, a, bads, kepts);
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_(bad|kept)$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$(undefined "$CC_TMP/g.o")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env CC_MIR_STATS=1"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_inline_calli_known: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_inline_calli_known: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "unknown_kept" ]; then
			echo "run_inline_calli_known: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_inline_calli_known: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_inline_calli_known: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_inline_calli_known: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_inline_calli_known: ok"
