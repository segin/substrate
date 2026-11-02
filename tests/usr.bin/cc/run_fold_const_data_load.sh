#!/bin/sh
# A load from a const object with a known initializer, at an offset that
# is constant once inlining is done, is the initializer's value, as gcc
# folds it.  Linux's reverse_cpuid_check(), always_inline with a
# constant leaf, has BUILD_BUG_ON(reverse_cpuid[x86_leaf].function == 0):
# left unfolded, its call of an undefined __compiletime_assert_N stayed in
# kvm.o, hundreds of them, and modpost refused the kernel; and where the
# leaf comes through __feature_translate()'s switch, it is constant only
# once slots are promoted and the dead arms gone.  The loads of
# every width and signedness, members, a 2-D array, a string's bytes and
# the zeros past an initializer fold as gcc folds them -- no reference to
# a function only an unfolded load would call, -O1 and -O2, -m64 and -m32,
# with and without -fPIC, both code paths -- and those from a writable, a
# volatile or a weak object do not.  A program reading them prints what
# gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fold_const_data_load: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold: each test of a loaded value calls a function nothing defines
cat >"$CC_TMP/f.c" <<'EOF2'
struct cpuid_reg { unsigned function, index; int reg; };
static const struct cpuid_reg reverse_cpuid[] = {
	[0] = { 1, 0, 3 }, [1] = { 0x80000001, 0, 3 }, [2] = { 0, 0, 0 }, [3] = { 7, 0, 1 },
};
extern void __compiletime_assert_590(void) __attribute__((error("bad")));
extern void __compiletime_assert_591(void) __attribute__((error("bad2")));
static inline __attribute__((always_inline)) void check(unsigned leaf)
{
	if (leaf >= sizeof(reverse_cpuid) / sizeof(reverse_cpuid[0])) __compiletime_assert_591();
	if (reverse_cpuid[leaf].function == 0) __compiletime_assert_590();
}
static inline __attribute__((always_inline)) unsigned bit(int f) { check(f / 32); return 1u << (f & 31); }
unsigned caps[4];
void init(void) { caps[0] |= bit(5); caps[3] |= bit(97); caps[1] |= bit(40); }
int direct(void) { return reverse_cpuid[3].function; }
/* the index a switch returns, a variable of several definitions until the dead arms go: __feature_translate() */
static inline __attribute__((always_inline)) unsigned translate(unsigned f)
{
	switch (f) {
	case 100: return 33;
	case 101: return 34;
	default: return f;
	}
}
static inline __attribute__((always_inline)) unsigned tbit(int f) { f = translate(f); check(f / 32); return 1u << (f & 31); }
void tinit(void) { caps[0] |= tbit(5); caps[3] |= tbit(97); caps[1] |= tbit(100); }

static const signed char sc[] = { -1, 2, -128, 127 };
static const unsigned char uc[] = { 255, 1, 128 };
static const short ss[] = { -2, 0x7fff, -32768 };
static const unsigned short us[] = { 65535, 3 };
static const int si[] = { -5, 0x7fffffff, (int)0x80000000 };
static const unsigned ui[] = { 0xffffffffu, 0x80000001u };
static const long long sll[] = { -7LL, 0x123456789abcdefLL, (long long)0x8000000000000000ULL };
static const unsigned long long ull[] = { 0xfedcba9876543210ULL };
static const long sl[] = { -9L, 0x7fffffffL };
struct rec { unsigned char a; short b; int c; long long d; unsigned e; };
static const struct rec recs[] = { { 200, -300, -70000, -1LL, 0xdeadbeefu }, { 1, 2, 3, 4, 5 } };
static const int grid[3][4] = { { 1, 2, 3, 4 }, { -5, -6, -7, -8 }, { 9 } };
static const char str[] = "hi\tthere\x7f\377";
static const unsigned char ustr[16] = "\200abc";
static const int tail[10] = { 1, 2, 3 };
const int pub[] = { 11, -12, 13 };
int mut[] = { 21, 22, 23 };
static const volatile int cv[] = { 31, 32 };
__attribute__((weak)) const int wk[] = { 41, 42 };
extern void sc_bad(void), uc_bad(void), ss_bad(void), us_bad(void), si_bad(void), ui_bad(void);
extern void sll_bad(void), ull_bad(void), sl_bad(void), rec_bad(void), grid_bad(void), str_bad(void);
extern void ustr_bad(void), tail_bad(void), pub_bad(void), mut_read(void), cv_read(void), wk_read(void);
static inline __attribute__((always_inline)) int at_sc(int i) { return sc[i]; }
static inline __attribute__((always_inline)) long long at_sll(int i) { return sll[i]; }
static inline __attribute__((always_inline)) int at_grid(int i, int j) { return grid[i][j]; }
void probe(void)
{
	if (at_sc(0) != -1 || at_sc(2) != -128 || sc[3] != 127) sc_bad();
	if (uc[0] != 255 || uc[2] != 128) uc_bad();
	if (ss[0] != -2 || ss[1] != 0x7fff || ss[2] != -32768) ss_bad();
	if (us[0] != 65535 || us[1] != 3) us_bad();
	if (si[0] != -5 || si[2] >= 0) si_bad();
	if (ui[0] != 0xffffffffu || ui[1] != 0x80000001u) ui_bad();
	if (at_sll(0) != -7 || at_sll(1) != 0x123456789abcdefLL || at_sll(2) >= 0) sll_bad();
	if (ull[0] != 0xfedcba9876543210ULL) ull_bad();
	if (sl[0] != -9 || sl[1] != 0x7fffffffL) sl_bad();
	if (recs[0].a != 200 || recs[0].b != -300 || recs[0].c != -70000 || recs[0].d != -1 ||
	    recs[0].e != 0xdeadbeefu || recs[1].d != 4) rec_bad();
	if (at_grid(1, 2) != -7 || at_grid(2, 0) != 9 || at_grid(2, 3) != 0) grid_bad();
	if (str[2] != '\t' || str[8] != 0x7f || str[9] != (char)0xff || str[10] != 0) str_bad();
	if (ustr[0] != 0x80 || ustr[3] != 'c' || ustr[4] != 0 || ustr[15] != 0) ustr_bad();
	if (tail[2] != 3 || tail[7] != 0 || tail[9] != 0) tail_bad();
	if (pub[1] != -12) pub_bad();
	if (mut[1] != 22) mut_read();
	if (cv[0] != 31) cv_read();
	if (wk[0] != 41) wk_read();
}
EOF2

# what the loads read: folded, and through an index not known
cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>
static const signed char sc[] = { -1, 2, -128, 127 };
static const unsigned short us[] = { 65535, 3 };
static const int si[] = { -5, 0x7fffffff, (int)0x80000000 };
static const long long sll[] = { -7LL, 0x123456789abcdefLL, (long long)0x8000000000000000ULL };
struct rec { unsigned char a; short b; int c; long long d; unsigned e; };
static const struct rec recs[] = { { 200, -300, -70000, -1LL, 0xdeadbeefu }, { 1, 2, 3, 4, 5 } };
static const int grid[3][4] = { { 1, 2, 3, 4 }, { -5, -6, -7, -8 }, { 9 } };
static const char str[] = "hi\tthere\x7f\377";
static const int tail[10] = { 1, 2, 3 };
int mut[] = { 21, 22, 23 };
static inline __attribute__((always_inline)) long long get(int k, int i)
{
	switch (k) {
	case 0: return sc[i];
	case 1: return us[i];
	case 2: return si[i];
	case 3: return sll[i];
	case 4: return recs[i].b;
	case 5: return recs[i].e;
	case 6: return grid[i][i + 1];
	case 7: return str[i];
	case 8: return tail[i];
	default: return mut[i];
	}
}
__attribute__((noinline)) long long get_var(int k, int i) { return get(k, i); }
__attribute__((noinline)) void poke(int i, int v) { mut[i] = v; }
int main(void)
{
	volatile int z = 0;
	printf("%lld %lld %lld %lld %lld\n", get(0, 2), get(0, 3), get(1, 0), get(2, 2), get(3, 1));
	printf("%lld %lld %lld %lld %lld\n", get(3, 2), get(4, 0), get(5, 0), get(6, 1), get(6, 2));
	printf("%lld %lld %lld %lld\n", get(7, 8), get(7, 9), get(8, 7), get(9, 1));
	printf("%lld %lld %lld %lld\n", get_var(0, 2 + z), get_var(3, 2 + z), get_var(5, z), get_var(7, 9 + z));
	poke(1, 99);
	printf("%d %lld %lld\n", mut[1], get(9, 1), get_var(9, 1 + z));
	return 0;
}
EOF2

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '^__compiletime_assert|_bad$|_read$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/f.c" 2>/dev/null || continue
	gcc -m$m -O0 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c" 2>/dev/null || continue
	want_run=$("$CC_TMP/g")
	for o in O1 O2; do
		for path in mir legacy; do
			run="env CC_MIR_STATS=1"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			# (an exported const definition folds under -fPIC too, as gcc folds it)
			for pic in no -fPIC; do
				gflag=-fno-pie
				cflag=
				if [ $pic = -fPIC ]; then gflag=-fPIC; cflag=-fPIC; fi
				what="-m$m -$o${cflag:+ $cflag} $path"
				gcc -m$m -$o $gflag -c -o "$CC_TMP/g.o" "$CC_TMP/f.c"
				want=$(undefined "$CC_TMP/g.o")
				if ! $run "$CC_BIN" -m$m -$o $cflag -c -o "$CC_TMP/c.o" "$CC_TMP/f.c" 2>"$CC_TMP/c.err"; then
					echo "run_fold_const_data_load: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
					exit 1
				fi
				if grep 'not taken' "$CC_TMP/c.err"; then
					echo "run_fold_const_data_load: $what: a function fell back from MIR"
					fail=1
				fi
				got=$(undefined "$CC_TMP/c.o")
				if [ "$got" != "$want" ]; then
					echo "run_fold_const_data_load: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
					fail=1
				fi
			done
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
				echo "run_fold_const_data_load: -m$m -$o $path refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_fold_const_data_load: -m$m -$o $path: a function of the program fell back from MIR"
				fail=1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
			got_run=$("$CC_TMP/c")
			if [ "$got_run" != "$want_run" ]; then
				echo "run_fold_const_data_load: -m$m -$o $path prints"
				echo "$got_run"
				echo "gcc's:"
				echo "$want_run"
				fail=1
			fi
		done
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_const_data_load: ok"
