#!/bin/sh
# __builtin_constant_p is decided after inlining and constant propagation,
# as gcc decides it from -O1: of a local holding a constant, of an inline
# function's parameter its caller passed a constant, of a variable both
# arms of a conditional set alike.  cc answered as it lowered the call, 0
# for anything not a constant expression there, so Linux's clamp(), whose
# BUILD_BUG_ON asks statically_true((long long)ulo >= 0) of an `auto`
# local holding 1, left a call of the undefined __compiletime_assert_N it
# guards -- 159 in vmlinux.o, thousands in the modules -- and the kernel
# did not link.  The functions undefined are gcc's, -O0 (where it is 0, as
# gcc's) to -O2, and a program prints what gcc's prints: what is not
# constant stays 0, an argument with a side effect is 0 and not evaluated,
# a static initializer and __builtin_choose_expr's condition take the
# answer of the moment.  Only shapes whose answer at -O2 is gcc's fold of
# a value it propagates are asked: none a loop's unrolling or a floating
# value's folding would decide.  -m64 and -m32, both code paths, every
# function through MIR.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_builtin_constant_p_late: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must be decided: each test calls a function nothing defines
cat >"$CC_TMP/u.c" <<'EOF2'
#define statically_true(x) (__builtin_constant_p(x) && (x))
#define __is_nonneg(ux) statically_true((long long)(ux) >= 0)
#define __sign_use(ux) (__builtin_types_compatible_p(typeof(ux), int) ? (2 + __is_nonneg(ux)) : 1)
#define clamp(val, lo, hi) ({ __auto_type uval = (val); __auto_type ulo = (lo); __auto_type uhi = (hi); \
	if (statically_true(ulo > uhi)) order_bad(); \
	if (!(__sign_use(uval) & __sign_use(ulo) & __sign_use(uhi))) sign_bad(); \
	uval < ulo ? ulo : uval > uhi ? uhi : uval; })
extern void order_bad(void), sign_bad(void), lo_bad(void), ai_bad(void), si_bad(void), deep_bad(void);
extern void tern_bad(void), var_bad(void), inc_bad(void), call_bad(void), ptr_bad(void), wide_bad(void);
extern int ext(int);
static inline __attribute__((always_inline)) int ai(int x) { return __builtin_constant_p(x) ? x : -1; }
static inline int si(int x) { return __builtin_constant_p(x) ? x : -1; }
static inline __attribute__((always_inline)) int deep(int x) { return ai(x + 1); }
int f(unsigned v, unsigned hi, int n)
{
	__auto_type lo = 1;
	long long w = 1LL << 40;
	int t = n > 3 ? 4 : 4;
	int i = n;
	char *p = "x";

	if (!(__builtin_constant_p((long long)lo >= 0) && (long long)lo >= 0)) lo_bad();
	if (ai(5) != 5) ai_bad();
	if (si(7) != 7) si_bad();
	if (deep(8) != 9) deep_bad();
	if (!__builtin_constant_p(t)) tern_bad();
	if (__builtin_constant_p(v) || __builtin_constant_p(n + lo)) var_bad();
	if (__builtin_constant_p(i++)) inc_bad();
	if (__builtin_constant_p(ext(1))) call_bad();
	if (__builtin_constant_p(p)) ptr_bad();
	if (!__builtin_constant_p(w >> 8)) wide_bad();
	return clamp(n, 1, (int)hi) + (int)(v < lo ? lo : v > hi ? hi : v) + i;
}
EOF2

# what the answers are
cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>
static inline __attribute__((always_inline)) int ai(int x) { return __builtin_constant_p(x) ? x : -1; }
static inline int si(int x) { return __builtin_constant_p(x) ? x : -1; }
static inline __attribute__((always_inline)) int deep(int x) { return ai(x * 3); }
__attribute__((noinline)) int g(int y) { return __builtin_constant_p(y) ? 1 : 2; }
int ext(int x) { return x + 1; }
int main(int argc, char **argv)
{
	__auto_type lo = 1;
	unsigned char uc = 200;
	long long ll = -5;
	int t = argc > 100 ? 4 : 4;
	int i = 0;
	int j = 0;
	char *p = "x";
	static int s = __builtin_constant_p(argc);
	int ch = __builtin_choose_expr(__builtin_constant_p(lo), 10, 20);

	printf("lo=%d cmp=%d uc=%d ll=%d t=%d\n", __builtin_constant_p(lo), __builtin_constant_p((long long)lo >= 0),
	       __builtin_constant_p(uc), __builtin_constant_p(ll), __builtin_constant_p(t));
	printf("ai=%d si=%d deep=%d ai_var=%d si_var=%d g=%d\n", ai(5), si(6), deep(7), ai(argc), si(argc), g(1));
	printf("argc=%d sum=%d inc=%d i=%d call=%d ptr=%d argv=%d\n", __builtin_constant_p(argc),
	       __builtin_constant_p(argc + lo), __builtin_constant_p(i++), i, __builtin_constant_p(ext(j)),
	       __builtin_constant_p(p), __builtin_constant_p(argv[0][0]));
	printf("comma=%d stmt=%d static=%d choose=%d\n", __builtin_constant_p((j, 4)), __builtin_constant_p(({ lo + 1; })),
	       s, ch);
	return 0;
}
EOF2

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_bad$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O0 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	for o in O0 O1 O2; do
		gcc -m$m -$o -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c"
		want=$(undefined "$CC_TMP/g.o")
		gcc -m$m -$o -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
		want_run=$("$CC_TMP/g")
		for path in mir legacy; do
			run="env CC_MIR_STATS=1"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			what="-m$m -$o $path"
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
				echo "run_builtin_constant_p_late: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_builtin_constant_p_late: $what: a function fell back from MIR"
				fail=1
			fi
			got=$(undefined "$CC_TMP/c.o")
			if [ "$got" != "$want" ]; then
				echo "run_builtin_constant_p_late: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
				fail=1
			fi
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
				echo "run_builtin_constant_p_late: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_builtin_constant_p_late: $what: a function of the program fell back from MIR"
				fail=1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
			got_run=$("$CC_TMP/c")
			if [ "$got_run" != "$want_run" ]; then
				echo "run_builtin_constant_p_late: $what prints"
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
echo "run_builtin_constant_p_late: ok"
