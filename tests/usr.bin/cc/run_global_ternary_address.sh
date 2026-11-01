#!/bin/sh
# A static initializer's `?:` whose condition is an address is folded as
# gcc folds it: true for a string literal or a symbol that is not weak.
# cc evaluated the condition as a number, so `&x ? 1 : 2` gave 2, `&x ?:
# y` gave a null pointer, and a string condition -- Linux's SIP_HDR has
# `.clen = ("f") ? sizeof("f") - 1 : 0` -- was refused.  The values are
# gcc's, -m64 and -m32, -O0 and -O2, through both code paths; a weak
# symbol's address, which may be null, is refused, as gcc refuses it.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_global_ternary_address: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

int x, y[4];
int f(void) { return 3; }

int a = &x ? 1 : 2;
int b = "s" ? sizeof("s") - 1 : 0;
int c = (void *)0 ? 5 : 6;
const char *d = "q" ? "y" : "n";
int *e = &x ?: y;
int *g = (int *)0 ?: &y[2];
int h = f ? 7 : 8;
int k = &y[1] ? 9 : 10;

struct hdr { const char *name, *cname; unsigned len, clen; };
#define HDR(n, c) { .name = (n), .cname = (c), .len = sizeof(n) - 1, .clen = (c) ? sizeof(c) - 1 : 0 }
const struct hdr hdrs[] = { HDR("From", "f"), HDR("CSeq", NULL) };

int main(void)
{
	printf("%d %d %d %s %d %d %d %d %u %u %d\n", a, b, c, d, e == &x, g == &y[2], h, k, hdrs[0].clen, hdrs[1].clen,
	       hdrs[1].cname == NULL);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			$run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_global_ternary_address: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
printf 'extern int w __attribute__((weak));\nint a = &w ? 1 : 2;\n' >"$CC_TMP/w.c"
if gcc -c -o "$CC_TMP/w.o" "$CC_TMP/w.c" 2>/dev/null; then
	echo "run_global_ternary_address: skip the weak case (gcc folds it)"
elif "$CC_BIN" -c -o "$CC_TMP/w.o" "$CC_TMP/w.c" 2>/dev/null; then
	echo "run_global_ternary_address: a weak symbol's address folded as a constant condition, which gcc refuses"
	exit 1
fi
echo "run_global_ternary_address: ok"
