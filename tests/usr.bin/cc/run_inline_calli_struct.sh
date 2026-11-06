#!/bin/sh
# A structure passed by value through a pointer to a prototyped function
# is passed as the target's ABI passes it to a function declared so --
# in registers, in memory, or as an integer of its size -- since that is
# what the pointer calls; cc passed its address instead, so the function
# read the address as the structure's bytes.  Once a pointer inlining
# has made a function's address is called, the call is inlined as a call
# of that function, structures by value among its parameters, as gcc
# inlines it: Linux's libeth hands libeth_xsk_xmit_fill_buf(), which
# takes a struct libeth_xdp_tx_frame by value, to an always_inline loop
# that calls it through the pointer, and idpf's xsk.o kept a call of an
# undefined __compiletime_assert_N while the call stayed one through the
# pointer.  The undefined functions are gcc's, and the program, run with
# pointers cc cannot know, prints what gcc's prints.  -O2, -m64 and
# -m32, both code paths, every function through MIR.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_inline_calli_struct: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls tmo_bad()
cat >"$CC_TMP/u.c" <<'EOF2'
extern void tmo_bad(void);
#define TMO ((const void *)0x61C8864680B583EBull)
struct frame {
	unsigned long long addr;
	unsigned int len, opts;
};
struct big {
	long a[5];
};
struct mixed {
	double d;
	int i;
};
struct desc {
	unsigned long long addr;
	unsigned int len, flags;
};
void do_xmit(struct desc d, unsigned int i, unsigned long long priv);

static inline __attribute__((always_inline)) struct desc fill_md(const struct frame *f, unsigned long long priv)
{
	const void *tmo = (const void *)(unsigned long)priv;
	struct desc d = { .addr = f->addr, .len = f->len };

	if (!__builtin_constant_p(tmo == TMO))
		tmo_bad();
	d.flags = tmo == TMO ? 1 : 2;
	return d;
}
static inline __attribute__((always_inline)) struct desc fill(struct frame frm, unsigned int i,
							       unsigned long long priv)
{
	struct desc d;

	if (priv)
		d = fill_md(&frm, priv);
	else
		d = (struct desc){ .addr = frm.addr };
	d.flags |= (frm.opts & 1) << 4 | i << 8;
	return d;
}
static inline __attribute__((always_inline)) unsigned int
xmit_bulk(const struct frame *bulk, unsigned int n, unsigned long long priv,
	  struct desc (*f)(struct frame, unsigned int, unsigned long long),
	  void (*x)(struct desc, unsigned int, unsigned long long))
{
	unsigned int i;

	for (i = 0; i < n; i++)
		x(f(bulk[i], i, priv), i, priv);
	return n;
}
unsigned int xsk_xmit(const struct frame *bulk, unsigned int n)
{
	return xmit_bulk(bulk, n, (unsigned long long)(unsigned long)TMO, fill, do_xmit);
}

/* structures through pointers cc cannot know */
long by_frame(struct frame f, long p) { return (long)f.addr + f.len + f.opts + p; }
long by_big(struct big b, long p) { return b.a[0] + b.a[2] + b.a[4] + p; }
long by_mixed(int k, struct mixed m, long p) { return k + (long)m.d + m.i + p; }
long call_frame(long (*fp)(struct frame, long), struct frame f) { return fp(f, 1000); }
long call_big(long (*fp)(struct big, long), struct big b) { return fp(b, 2000); }
long call_mixed(long (*fp)(int, struct mixed, long), struct mixed m) { return fp(7, m, 3000); }
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads;
static unsigned long long sum;
void tmo_bad(void) { bads++; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
void do_xmit(struct desc d, unsigned int i, unsigned long long priv)
{
	sum += d.addr + d.len + d.flags + i + (priv != 0);
}
long (*volatile pf)(struct frame, long) = by_frame;
long (*volatile pb)(struct big, long) = by_big;
long (*volatile pm)(int, struct mixed, long) = by_mixed;
int main(void)
{
	static const struct frame fr[3] = { { 100, 1, 0 }, { 200, 2, 1 }, { 300, 3, 0 } };
	struct frame f = { 100, 20, 3 };
	struct big b = { { 1, 2, 3, 4, 5 } };
	struct mixed m = { 40.5, 9 };
	unsigned int n = xsk_xmit(fr, 3);

	printf("%llu %u bads %d: %ld %ld %ld; %ld %ld %ld\n", sum, n, bads, pf(f, 1000), pb(b, 2000),
	       pm(7, m, 3000), call_frame(pf, f), call_big(pb, b), call_mixed(pm, m));
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '^(tmo_bad|do_xmit)$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$(undefined "$CC_TMP/g.o")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env CC_MIR_STATS=2"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_inline_calli_struct: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_inline_calli_struct: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "do_xmit" ]; then
			echo "run_inline_calli_struct: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_inline_calli_struct: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_inline_calli_struct: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_inline_calli_struct: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_inline_calli_struct: ok"
