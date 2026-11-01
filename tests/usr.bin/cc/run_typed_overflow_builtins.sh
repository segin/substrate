#!/bin/sh
# The typed overflow builtins, __builtin_{s,u}{add,sub,mul}{,l,ll}_overflow,
# are the generic ones with both operands converted to the builtin's type
# (int, long, long long; unsigned for the u ones).  cc knew only the
# generic three, and Linux's crypto/ecc.c, which calls
# __builtin_uaddll_overflow, did not build (the Arch config).  Every
# variant on operands that overflow its type and ones that do not, wider
# operands converted first; __has_builtin knows them.  Results are
# gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_typed_overflow_builtins: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/o.c" <<'EOF2'
#include <limits.h>
#include <stdio.h>

#define NI __attribute__((noinline))
#define ONE(name, T, fmt)                                                      \
	NI static void t_##name(T a, T b)                                      \
	{                                                                      \
		T r = 0;                                                       \
		int o = __builtin_##name##_overflow(a, b, &r);                 \
		printf(#name " %d " fmt "\n", o, r);                           \
	}
ONE(sadd, int, "%d")
ONE(ssub, int, "%d")
ONE(smul, int, "%d")
ONE(saddl, long, "%ld")
ONE(ssubl, long, "%ld")
ONE(smull, long, "%ld")
ONE(saddll, long long, "%lld")
ONE(ssubll, long long, "%lld")
ONE(smulll, long long, "%lld")
ONE(uadd, unsigned, "%u")
ONE(usub, unsigned, "%u")
ONE(umul, unsigned, "%u")
ONE(uaddl, unsigned long, "%lu")
ONE(usubl, unsigned long, "%lu")
ONE(umull, unsigned long, "%lu")
ONE(uaddll, unsigned long long, "%llu")
ONE(usubll, unsigned long long, "%llu")
ONE(umulll, unsigned long long, "%llu")

#if __has_builtin(__builtin_uaddll_overflow) && __has_builtin(__builtin_smul_overflow)
#define HAS 2
#else
#define HAS 0
#endif

int main(void)
{
	unsigned long long c = 0;
	long long wide = 0x100000005LL;
	int r = 0;
	int o;

	t_sadd(INT_MAX, 1); t_sadd(3, 4);
	t_ssub(INT_MIN, 1); t_ssub(3, 4);
	t_smul(INT_MAX, 2); t_smul(-3, 4);
	t_saddl(LONG_MAX, 1); t_saddl(3, 4);
	t_ssubl(LONG_MIN, 1); t_ssubl(3, 4);
	t_smull(LONG_MAX, 2); t_smull(-3, 4);
	t_saddll(LLONG_MAX, 1); t_saddll(3, 4);
	t_ssubll(LLONG_MIN, 1); t_ssubll(3, 4);
	t_smulll(LLONG_MAX, 2); t_smulll(-3, 4);
	t_uadd(UINT_MAX, 1); t_uadd(3, 4);
	t_usub(0, 1); t_usub(4, 3);
	t_umul(UINT_MAX, 2); t_umul(3, 4);
	t_uaddl(ULONG_MAX, 1); t_uaddl(3, 4);
	t_usubl(0, 1); t_usubl(4, 3);
	t_umull(ULONG_MAX, 2); t_umull(3, 4);
	t_uaddll(ULLONG_MAX, 1); t_uaddll(3, 4);
	t_usubll(0, 1); t_usubll(4, 3);
	t_umulll(ULLONG_MAX, 2); t_umulll(3, 4);
	/* as crypto/ecc.c adds with carry; and an operand wider than int converted to int first */
	o = __builtin_uaddll_overflow(ULLONG_MAX, 2ULL, &c);
	printf("%d %llu\n", o, c);
	o = __builtin_sadd_overflow(wide, 1, &r);
	printf("%d %d\n", o, r);
	printf("%d\n", HAS);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/o.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/o.o" "$CC_TMP/o.c" 2>"$CC_TMP/c.err"; then
			echo "run_typed_overflow_builtins: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/o.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_typed_overflow_builtins: -m$m -$o differs from gcc:"
			printf '%s\n' "$got" >"$CC_TMP/got"
			printf '%s\n' "$want" >"$CC_TMP/want"
			diff "$CC_TMP/want" "$CC_TMP/got" || true
			exit 1
		fi
	done
done
echo "run_typed_overflow_builtins: ok"
