#!/bin/sh
# i386 soft-float (-mno-80387) converts a floating value to an integer by
# the routine gcc calls: to an integer of 4 bytes or fewer, the 32-bit
# __fix{sf,df,xf}si (__fixuns*si for an unsigned one, a char's and a
# short's too), to one of 8, the 64-bit __fix*di -- and from a float,
# __fixsf*.  cc called the 64-bit __fix{df,xf}di for every width,
# signedness and floating type, so code that links against a soft-float
# library with gcc's routines needed two more.  Each function calls what
# gcc -mno-80387 -mno-sse calls, -O0 and -O2, through both code paths, and
# the values are gcc's (the routines from i386_soft_float_routines.c).
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
TDIR=$(cd "$(dirname "$0")" && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_soft_float_fix: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' >"$CC_TMP/m32.c" ||
	! gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_soft_float_fix: skip (no gcc -m32)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/f.c" <<'EOF2'
signed char sc(double d) { return d; }
unsigned char uc(double d) { return d; }
short ss(double d) { return d; }
unsigned short us(double d) { return d; }
int si(double d) { return d; }
unsigned ui(double d) { return d; }
long long sl(double d) { return d; }
unsigned long long ul(double d) { return d; }
signed char fsc(float d) { return d; }
unsigned char fuc(float d) { return d; }
int fsi(float d) { return d; }
unsigned fui(float d) { return d; }
long long fsl(float d) { return d; }
unsigned long long ful(float d) { return d; }
short xss(long double d) { return d; }
unsigned short xus(long double d) { return d; }
int xsi(long double d) { return d; }
unsigned xui(long double d) { return d; }
long long xsl(long double d) { return d; }
unsigned long long xul(long double d) { return d; }
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

signed char sc(double); unsigned char uc(double); short ss(double); unsigned short us(double);
int si(double); unsigned ui(double); long long sl(double); unsigned long long ul(double);
signed char fsc(float); unsigned char fuc(float); int fsi(float); unsigned fui(float);
long long fsl(float); unsigned long long ful(float);
short xss(long double); unsigned short xus(long double); int xsi(long double); unsigned xui(long double);
long long xsl(long double); unsigned long long xul(long double);

int main(void)
{
	printf("%d %d %d %d %d %u %lld %llu\n", sc(-100.75), uc(200.5), ss(-30000.9), us(60000.25), si(-2147483000.5),
	       ui(4000000000.75), sl(-9007199254740991.0), ul(18446744073709549568.0));
	printf("%d %d %d %u %lld %llu\n", fsc(-99.5f), fuc(250.75f), fsi(-16777216.0f), fui(3000000000.0f),
	       fsl(-1099511627776.0f), ful(17293822569102704640.0f));
	printf("%d %d %d %u %lld %llu\n", xss(-32000.5L), xus(65000.5L), xsi(-2000000000.25L), xui(4294967295.5L),
	       xsl(-9223372036854775807.0L), xul(18446744073709551615.0L));
	return 0;
}
EOF2
# the conversion routines each function calls, one "function routine" a line
routines() {
	objdump -dr "$1" | awk '/^[0-9a-f]+ <[^>]+>:$/ { fn = substr($2, 2, length($2) - 3) }
		/R_386_(PC|PLT)32[ 	]+__fix/ { print fn, $NF }' | sort
}
gcc -m32 -O2 -mno-80387 -mno-sse -fno-pic -c -o "$CC_TMP/g.o" "$CC_TMP/f.c"
routines "$CC_TMP/g.o" >"$CC_TMP/want.r"
gcc -m32 -O1 -mno-sse -fno-pic -c -o "$CC_TMP/soft.o" "$TDIR/i386_soft_float_routines.c"
gcc -m32 -O0 -fno-pic -c -o "$CC_TMP/m.o" "$CC_TMP/m.c"
gcc -m32 -no-pie -o "$CC_TMP/g" "$CC_TMP/m.o" "$CC_TMP/g.o" "$CC_TMP/soft.o"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		$run "$CC_BIN" -m32 -$o -mno-80387 -c -o "$CC_TMP/c.o" "$CC_TMP/f.c"
		routines "$CC_TMP/c.o" >"$CC_TMP/got.r"
		if ! cmp -s "$CC_TMP/want.r" "$CC_TMP/got.r"; then
			echo "run_i386_soft_float_fix: $path -$o calls other routines than gcc's:"
			diff "$CC_TMP/want.r" "$CC_TMP/got.r" || true
			exit 1
		fi
		gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/m.o" "$CC_TMP/c.o" "$CC_TMP/soft.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_i386_soft_float_fix: $path -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_i386_soft_float_fix: ok"
