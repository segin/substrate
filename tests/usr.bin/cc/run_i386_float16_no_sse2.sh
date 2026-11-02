#!/bin/sh
# Without SSE2 an i386 _Float16 may be copied -- loaded, stored, passed,
# taken -- as gcc lets it be, and its conversions and operations, and a
# returned one, are refused as gcc refuses them, with gcc's messages and
# at the place.  cc converts a half through libgcc, in %xmm0, so it
# refused every function with one, copies too, and said so nowhere in
# particular.  Copies are now the halves' bits; the program, cc's code
# calling gcc's both ways, has gcc's bytes, -O0 and -O2, both code paths,
# no %xmm in it.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_float16_no_sse2: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' >"$CC_TMP/m32.c" ||
	! gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_float16_no_sse2: skip (no gcc -m32)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# refused, with gcc's message, at a line
n=0
while IFS= read -r body; do
	n=$((n + 1))
	printf '%s\n' "$body" >"$CC_TMP/r$n.c"
	want=$(gcc -m32 -march=i686 -S -o /dev/null "$CC_TMP/r$n.c" 2>&1 | grep -m1 'error:' | sed 's/.*error: //')
	got=$("$CC_BIN" -m32 -march=i686 -S -o "$CC_TMP/r.s" "$CC_TMP/r$n.c" 2>&1 | grep -m1 'error:' || true)
	if [ -z "$want" ]; then
		echo "run_i386_float16_no_sse2: gcc takes [$body]; the test is wrong"
		exit 1
	fi
	case $got in
	*":1:"*"error: $want") ;;
	*)
		echo "run_i386_float16_no_sse2: [$body]: cc says [$got], gcc [$want], at its line"
		exit 1
		;;
	esac
done <<'EOF'
float f(_Float16 a) { return a; }
_Float16 g; void f(float x) { g = x; }
_Float16 g; void f(int i) { g = i; }
_Float16 g, h; void f(void) { g = g * h; }
_Float16 g; int f(void) { return g < g; }
_Float16 f(void) { return 1; }
_Float16 r(void); void f(_Float16 *p) { *p = r(); }
EOF

# copies: taken
cat >"$CC_TMP/lib.c" <<'EOF'
_Float16 kept[4];
void keep(int i, _Float16 h) { kept[i] = h; }
void give(void (*to)(int, _Float16)) { to(0, kept[1]); to(1, kept[2]); }
EOF
cat >"$CC_TMP/main.c" <<'EOF'
#include <stdio.h>
#include <string.h>
struct pair { _Float16 a, b; };
static const unsigned short src_bits[3] = { 0x3e00, 0xc000, 0x7bff }; /* 1.5, -2, 65504: no conversion */
static _Float16 src[3];
static _Float16 back[2];
extern _Float16 kept[4];
void keep(int, _Float16);
void give(void (*)(int, _Float16));
static void take(int i, _Float16 h) { back[i] = h; }
int main(void)
{
	struct pair p, q;
	unsigned char b[sizeof kept + sizeof back + sizeof q];
	size_t i;

	memcpy(src, src_bits, sizeof src);
	p.a = src[0];
	p.b = src[1];
	keep(1, src[2]);
	keep(2, p.b);
	q = p;
	q.a = kept[1];
	give(take);
	memcpy(b, kept, sizeof kept);
	memcpy(b + sizeof kept, back, sizeof back);
	memcpy(b + sizeof kept + sizeof back, &q, sizeof q);
	for (i = 0; i < sizeof b; i++)
		printf("%02x", b[i]);
	printf("\n");
	return 0;
}
EOF
gcc -m32 -march=i686 -O2 -c -o "$CC_TMP/gl.o" "$CC_TMP/lib.c"
gcc -m32 -march=i686 -O2 -c -o "$CC_TMP/gm.o" "$CC_TMP/main.c"
gcc -m32 -no-pie -o "$CC_TMP/g" "$CC_TMP/gl.o" "$CC_TMP/gm.o"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		for f in lib main; do
			if ! $run "$CC_BIN" -m32 -march=i686 -$o -S -o "$CC_TMP/c$f.s" "$CC_TMP/$f.c" 2>"$CC_TMP/c.err"; then
				echo "run_i386_float16_no_sse2: $path -$o $f.c refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			if grep -q xmm "$CC_TMP/c$f.s"; then
				echo "run_i386_float16_no_sse2: $path -$o $f.c uses %xmm without SSE2"
				exit 1
			fi
			$run "$CC_BIN" -m32 -march=i686 -$o -c -o "$CC_TMP/c$f.o" "$CC_TMP/$f.c"
		done
		gcc -m32 -no-pie -o "$CC_TMP/a" "$CC_TMP/cmain.o" "$CC_TMP/gl.o"
		gcc -m32 -no-pie -o "$CC_TMP/b" "$CC_TMP/gm.o" "$CC_TMP/clib.o"
		for x in a b; do
			got=$("$CC_TMP/$x")
			if [ "$got" != "$want" ]; then
				echo "run_i386_float16_no_sse2: $path -$o ($x) prints $got, gcc's $want"
				exit 1
			fi
		done
	done
done
echo "run_i386_float16_no_sse2: ok"
