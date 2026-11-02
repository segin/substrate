#!/bin/sh
# va_arg, va_copy and va_start take any va_list lvalue, not only a local
# variable by name: Linux's drivers/firmware/efi/libstub/vsprintf.c reads
# `va_arg(*ap, int)` through a `va_list *`, and cc refused it
# ("__builtin_va_arg lowering expects va_list identifier").  Through a
# pointer, a structure member and an array element, of longs, ints and a
# double: the result is gcc's, -m64 and -m32, -O0 and -O2, on both code
# paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_va_list_lvalues: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
#include <stdarg.h>
struct st { int tag; va_list ap; };
static long get_ptr(va_list *ap) { return va_arg(*ap, long); }
static int get_member(struct st *s) { return va_arg(s->ap, int); }
static double get_double(va_list *ap) { return va_arg(*ap, double); }
static int sum(int n, ...)
{
	struct st s;
	va_list ap;
	va_list many[2];
	long a, c;
	int b;
	double d;
	va_start(ap, n);
	a = get_ptr(&ap);
	va_copy(s.ap, ap);
	b = get_member(&s);
	c = get_ptr(&s.ap);
	d = get_double(&s.ap);
	va_end(s.ap);
	va_start(many[1], n);
	a += va_arg(many[1], long) * 1000;
	va_end(many[1]);
	va_end(ap);
	return (int)(a * 100 + b * 10 + c + d) + n;
}
int main(void) { printf("%d\n", sum(1, 7L, 3, 5L, 2.5)); return 0; }
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_va_list_lvalues: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_va_list_lvalues: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_va_list_lvalues: ok"
