#!/bin/sh
# x86-64's va_list is gcc's __builtin_va_list, an array of one `struct
# __va_list_tag { unsigned gp_offset, fp_offset; void *overflow_arg_area,
# *reg_save_area; }`: 24 bytes an object holds, a parameter a pointer to
# them.  cc's was a pointer to a record of its own, 8 bytes, so a structure
# holding a va_list was laid out otherwise than gcc's and a va_list or a
# va_list * passed between gcc's code and cc's was misread.  Objects of
# each compiler call the other's -- a va_list passed on, through a pointer,
# in a structure, va_copy'd, glibc's vsprintf given one -- and print what
# gcc's alone print, sizes and offsets too, -O0 and -O2, both code paths,
# no MIR fallback.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_x64_va_list_abi: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/lib.c" <<'EOF'
#include <stdarg.h>
#include <stdio.h>
struct holder { int tag; va_list ap; char after; };
int vsum(int n, va_list ap) { int s = 0; while (n--) s += va_arg(ap, int); return s; }
double vmix(int n, va_list *app)
{
	double s = 0;

	while (n--) {
		s += va_arg(*app, double);
		s += va_arg(*app, int);
	}
	return s;
}
int hsum(struct holder *h, int n) { int s = 0; while (n--) s += va_arg(h->ap, int); return s; }
int sum(int n, ...)
{
	va_list ap, aq;
	int a, b;

	va_start(ap, n);
	va_copy(aq, ap);
	a = vsum(n, ap);
	b = vsum(n, aq);
	va_end(aq);
	va_end(ap);
	return a * 1000 + b;
}
int vfmt(char *buf, const char *fmt, va_list ap) { return vsprintf(buf, fmt, ap); }
EOF
cat >"$CC_TMP/main.c" <<'EOF'
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
struct holder { int tag; va_list ap; char after; };
int vsum(int n, va_list ap);
double vmix(int n, va_list *app);
int hsum(struct holder *h, int n);
int sum(int n, ...);
int vfmt(char *buf, const char *fmt, va_list ap);
static int callv(int n, ...) { va_list ap; int r; va_start(ap, n); r = vsum(n, ap); va_end(ap); return r; }
static double callm(int n, ...)
{
	va_list ap;
	double r;

	va_start(ap, n);
	r = vmix(n, &ap);
	r += va_arg(ap, int);
	va_end(ap);
	return r;
}
static int callh(int n, ...)
{
	struct holder h;
	int r;

	h.tag = 7;
	h.after = 9;
	va_start(h.ap, n);
	r = hsum(&h, n);
	va_end(h.ap);
	return r + h.tag + h.after;
}
static int fmt(char *b, const char *f, ...) { va_list ap; int r; va_start(ap, f); r = vfmt(b, f, ap); va_end(ap); return r; }
int main(void)
{
	char b[128];

	fmt(b, "%d %s %g %ld|", 42, "x", 2.5, 1234567890123L);
	printf("%zu %zu %zu %zu %d %g %d %d %s\n", sizeof(va_list), _Alignof(va_list), sizeof(struct holder),
	       offsetof(struct holder, after), callv(5, 1, 2, 3, 4, 5, 6, 7), callm(2, 1.5, 2, 2.5, 3, 77),
	       callh(3, 10, 20, 30), sum(9, 1, 2, 3, 4, 5, 6, 7, 8, 9), b);
	return 0;
}
EOF
gcc -O2 -c -o "$CC_TMP/gl.o" "$CC_TMP/lib.c"
gcc -O2 -c -o "$CC_TMP/gm.o" "$CC_TMP/main.c"
gcc -o "$CC_TMP/g" "$CC_TMP/gl.o" "$CC_TMP/gm.o"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		for f in lib main; do
			if ! $run CC_MIR_STATS=2 "$CC_BIN" -$o -c -o "$CC_TMP/c$f.o" "$CC_TMP/$f.c" 2>"$CC_TMP/c.err"; then
				echo "run_x64_va_list_abi: $path -$o $f.c refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep -q 'not taken' "$CC_TMP/c.err"; then
				echo "run_x64_va_list_abi: $path -$o $f.c: $(grep 'not taken' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
		done
		gcc -o "$CC_TMP/a" "$CC_TMP/cmain.o" "$CC_TMP/gl.o"
		gcc -o "$CC_TMP/b" "$CC_TMP/gm.o" "$CC_TMP/clib.o"
		gcc -o "$CC_TMP/c" "$CC_TMP/cmain.o" "$CC_TMP/clib.o"
		for x in a b c; do
			got=$("$CC_TMP/$x" 2>&1) || got="$got (exit $?)"
			if [ "$got" != "$want" ]; then
				echo "run_x64_va_list_abi: $path -$o ($x) prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_x64_va_list_abi: ok"
