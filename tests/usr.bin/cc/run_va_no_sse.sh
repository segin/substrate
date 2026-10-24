#!/bin/sh
# An x86-64 variadic function saves the vector argument registers only
# when its caller says, in %al, that it passed any -- `testb %al, %al`
# first, as gcc does -- and under -mno-sse not at all.  It saved them
# always, and Linux's _printk(), built -mno-sse with SSE off, took an
# invalid-opcode trap at its first movdqu.  A program passing doubles
# still gets them.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/va.c" <<'EOF2'
#include <stdarg.h>

long sum(int n, ...) {
    va_list ap;
    long s = 0;

    va_start(ap, n);
    while (n-- > 0)
        s += va_arg(ap, long);
    va_end(ap);
    return s;
}
EOF2
cat >"$CC_TMP/vd.c" <<'EOF2'
#include <stdarg.h>
#include <stdio.h>

static double __attribute__((noinline)) dsum(int n, ...) {
    va_list ap;
    double s = 0;

    va_start(ap, n);
    while (n-- > 0)
        s += va_arg(ap, double);
    va_end(ap);
    return s;
}

int main(void) {
    printf("%g %g\n", dsum(3, 1.5, 2.25, 4.0), dsum(9, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.5));
    return 0;
}
EOF2
for o in O0 O2; do
	"$CC_BIN" -m64 -$o -mno-sse -S -o "$CC_TMP/va.s" "$CC_TMP/va.c"
	if grep -q xmm "$CC_TMP/va.s"; then
		echo "run_va_no_sse: -$o -mno-sse touches a vector register:"
		grep xmm "$CC_TMP/va.s" | head -3
		exit 1
	fi
	"$CC_BIN" -m64 -$o -S -o "$CC_TMP/va.s" "$CC_TMP/va.c"
	if ! awk '/testb[ \t]+%al, %al/ { t = 1 } /xmm/ { if (!t) bad = 1; x = 1 } END { exit !(x && !bad) }' \
		"$CC_TMP/va.s"; then
		echo "run_va_no_sse: -$o saves the vector registers without testing %al"
		exit 1
	fi
	"$CC_BIN" -m64 -$o -o "$CC_TMP/vd" "$CC_TMP/vd.c"
	got=$("$CC_TMP/vd")
	if [ "$got" != "7.75 45.5" ]; then
		echo "run_va_no_sse: -$o double varargs give [$got], not [7.75 45.5]"
		exit 1
	fi
done
echo "run_va_no_sse: ok"
