#!/bin/sh
# A plain load of what a store just wrote, with nothing between that may
# write it, reads the value stored -- as gcc's value numbering reads it
# -- even where the object's address has escaped, so the store stays.
# Linux's ad7768_get_voltage_sel() passes &val to regmap_read(), then
# assigns val = FIELD_GET(...) and clamp()s val, whose BUILD_BUG_ON asks
# whether val is known not to be negative: reading val back from memory
# lost the field's range, and iio's ad7768-1 kept a call of an undefined
# __compiletime_assert_N.  The value is read back as the load extends
# it, so only one already so extended is taken; a call, or a store
# through a pointer that may reach the object, between the two keeps the
# load, as gcc keeps it.  -O2, -m64 and -m32, both code paths, every
# function through MIR; and the program, run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_cse_store_load: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void sel_bad(void), byte_bad(void), global_bad(void), call_kept(void), alias_kept(void);
extern int reg_read(int reg, int *val);
extern void touch(void);
#define NONNEG(x) (__builtin_constant_p((long)(x) >= 0) && ((long)(x) >= 0))

int counter;
short narrow;

int get_sel(int n)
{
	int ret, val;

	ret = reg_read(0x17, &val);
	if (ret)
		return ret;
	val = (int)(((unsigned int)val & 0x1c) >> 2);
	if (!NONNEG(val))
		sel_bad();
	return val < 1 ? 1 : val > n ? n : val;
}

int bytes(int x)
{
	unsigned char c;
	int ret = reg_read(1, (int *)0) + x;

	reg_read(2, (int *)&c);
	c = (unsigned char)(x & 0x7f);
	if (c > 0x7f)
		byte_bad();
	return ret + c;
}

int globals(int x)
{
	narrow = (short)(x & 0x3ff);
	if (narrow < 0)
		global_bad();
	counter = x & 0xff;
	if (counter > 255)
		global_bad();
	return counter + narrow;
}

/* a call between may write it */
int called(int x)
{
	counter = x & 0xff;
	touch();
	if (counter > 255)
		call_kept();
	return counter;
}

/* a store through a pointer that may reach it */
int aliased(int x, int *p)
{
	counter = x & 0xff;
	*p = x;
	if (counter > 255)
		alias_kept();
	return counter;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void sel_bad(void) { bads++; }
void byte_bad(void) { bads++; }
void global_bad(void) { bads++; }
void call_kept(void) { kepts++; }
void alias_kept(void) { kepts++; }
int reg_read(int reg, int *val) { if (val) *val = reg * 0x11; return 0; }
void touch(void) { }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	int a = get_sel(9);
	int b = bytes(300);
	int c = globals(70000);
	int d = called(513);
	int e = aliased(513, &counter);

	printf("%d %d %d %d %d bads %d kepts %d\n", a, b, c, d, e, bads, kepts);
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
		run="env CC_MIR_STATS=2"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_cse_store_load: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_cse_store_load: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "alias_kept call_kept" ]; then
			echo "run_cse_store_load: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_cse_store_load: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_cse_store_load: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_cse_store_load: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_cse_store_load: ok"
