#!/bin/sh
# A static local's initializer is constant where gcc folds it: inside
# __builtin_constant_p, or in the arm of a conditional its constant
# condition does not choose, a call is never made.  Linux's printk index,
# `static const struct pi_entry _entry = { .fmt = __builtin_constant_p(fmt)
# ? fmt : NULL, ... }`, of a format chosen at run time (i40e_main.c,
# intel_engine_user.c) was refused ("initializer element is not
# constant").  The program prints what gcc's prints, -m64 and -m32, -O0
# and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_static_init_folded_call: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
struct pi_entry { const char *fmt; const char *func; int level; };
static int flag(int n) { return n & 1; }
#define printk_index(_fmt) ({ \
	static const struct pi_entry _entry = { \
		.fmt = __builtin_constant_p(_fmt) ? (_fmt) : NULL, \
		.func = __func__, \
		.level = __builtin_constant_p(_fmt) ? 1 : (sizeof(_fmt) > 4 ? 2 : 3), \
	}; \
	&_entry; })
int main(int argc, char **argv)
{
	const struct pi_entry *a = printk_index(flag(argc) ? "odd\n" : "even\n");
	const struct pi_entry *b = printk_index("plain\n");
	static const int k = 1 ? 5 : 0;
	(void)argv;
	printf("%s %s %d | %s %s %d | %d\n", a->fmt ? a->fmt : "(null)", a->func, a->level, b->fmt, b->func, b->level, k);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O2 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_static_init_folded_call: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_static_init_folded_call: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_static_init_folded_call: ok"
