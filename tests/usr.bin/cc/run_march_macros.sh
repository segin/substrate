#!/bin/sh
# Each -march defines gcc's macros: the instruction set's (__SSE2__,
# __SSE4_2__, __POPCNT__, __FXSR__, __LAHF_SAHF__, ...), the CPU's own
# (__pentium4__, __k8__, ...) and its tuning's (__tune_pentium4__),
# __SSE_MATH__ and __SSE2_MATH__ where SSE computes floating point,
# __code_model_*__, and the __sync builtins' __GCC_HAVE_SYNC_COMPARE_
# AND_SWAP_N.  cc knew a few -march names and gave pentium4, prescott,
# nocona and core2 no __SSE2__ (and with it, the code no SSE2), knew no
# others (pentium-m, nehalem), and took -msse4.2, -mpopcnt and the other
# extensions' options and ignored them.  An extension turned on turns on
# those it needs and one turned off those that need it, as gcc's; and
# on i386 floating point stays the x87's, SSE2 or not, unless
# -mfpmath=sse, as gcc's (__FLT_EVAL_METHOD__ 2).
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 ||
	! gcc -m32 -march=i686 -E -dM -x c /dev/null >/dev/null 2>&1; then
	echo "run_march_macros: skip (no gcc -m32)"
	exit 0
fi

# the macros in question, and their values
macros() {
	"$@" -E -dM -x c /dev/null 2>"$CC_TMP/err" |
		awk '{ print $2 "=" $3 }' |
		grep -E '^__((MMX|SSE[0-9A-Z_]*|SSSE3|AVX[0-9A-Z_]*|POPCNT|CRC32|FMA|F16C|BMI2?|LZCNT|MOVBE|XSAVE|FXSR|LAHF_SAHF|ABM|EVEX256|MMX_WITH_SSE)__|i[3-6]86(__)?|pentium[a-z0-9_]*|nocona(__)?|core2(__)?|corei7(__)?|nehalem(__)?|k8(__)?|tune_[a-z0-9_]+__|code_model_[a-z0-9]+__|GCC_HAVE_SYNC_COMPARE_AND_SWAP_[0-9]+|GCC_ATOMIC_TEST_AND_SET_TRUEVAL|FP_FAST_FMA[A-Z0-9x]*|FLT_EVAL_METHOD__|amd64(__)?|x86_64(__)?)=' |
		# (cc has no 16-byte __sync builtins, cmpxchg16b, so it does not say it has: -mcx16's _16 is gcc's alone)
		grep -v '^__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16=' |
		sort
}

check() {
	macros gcc "$@" >"$CC_TMP/g.m"
	if ! macros "$CC_BIN" "$@" >"$CC_TMP/c.m"; then
		echo "run_march_macros: $*: $(head -1 "$CC_TMP/err")"
		exit 1
	fi
	if ! cmp -s "$CC_TMP/g.m" "$CC_TMP/c.m"; then
		echo "run_march_macros: $*: cc's macros differ from gcc's (< gcc, > cc):"
		diff "$CC_TMP/g.m" "$CC_TMP/c.m" | grep '^[<>]' | tr '\n' ' '
		echo
		exit 1
	fi
}

for m in i386 i486 i586 pentium pentium-mmx pentiumpro i686 pentium2 pentium3 pentium-m pentium4 prescott; do
	check -m32 -march=$m
done
for m in nocona core2 nehalem x86-64 x86-64-v2 x86-64-v3 x86-64-v4; do
	check -m32 -march=$m
	check -m64 -march=$m
done
# the extensions' options, on and off, and what they imply
check -m32 -march=pentium4 -mfpmath=sse
check -m32 -march=pentium4 -mfpmath=387
check -m32 -march=pentium4 -mno-sse2
check -m32 -march=pentium3 -msse2 -mno-mmx
check -m32 -march=pentium-mmx -mno-mmx
check -m32 -march=pentium -mmmx
check -m32 -march=core2 -msse4.2
check -m32 -march=pentium4 -mpopcnt -mlzcnt -mbmi -mmovbe -mcrc32 -mfxsr -mxsave -mcx16
check -m64 -march=x86-64 -msse4.2 -mno-popcnt
check -m64 -march=x86-64 -mavx2 -mfma -mf16c
check -m64 -march=x86-64 -mavx512vl
check -m64 -march=x86-64-v3 -mno-sse4.2
check -m64 -march=x86-64-v3 -mno-avx
check -m64 -march=x86-64-v3 -mno-xsave
check -m64 -march=x86-64-v4 -mno-avx512f
check -m64 -march=x86-64-v4 -mno-avx2
check -m64 -march=x86-64-v4 -mno-sse3
check -m64 -march=x86-64-v2 -mno-sse4
check -m64 -march=x86-64 -mno-mmx -mno-3dnow
check -m64 -march=x86-64 -mabm -msse4a
check -m32 -march=i386 -m3dnow
# an option given again later counts only there; what no option said, the -march's
check -m64 -mno-sse -msse
check -m64 -march=x86-64-v3 -mno-avx -mavx
check -m64 -march=x86-64-v2 -mno-sse -msse3
check -m32 -march=core2 -mno-sse3 -msse
check -m64 -mno-mmx -msse
check -m64 -march=x86-64-v2 -mno-popcnt -msse4.2
check -m64 -march=x86-64-v3 -mno-sse4.2 -msse4.1
check -m64 -msse -mno-sse -mno-80387
check -m64 -mno-sse -mno-mmx -mno-sse2 -mno-avx -mno-sse4a -mno-3dnow -mno-80387
# the tuning, by -mtune or the -march's own
check -m32 -march=pentium4 -mtune=core2
check -m32 -march=i686 -mtune=pentium3
check -m32 -march=core2 -mtune=generic
check -m64 -march=x86-64 -mtune=nehalem
# the code model
check -m64 -march=x86-64 -mcmodel=kernel -fno-pic
check -m64 -march=x86-64 -mcmodel=large
check -m64 -march=x86-64 -mcmodel=medium

check -m32 -march=i686 -mfpmath=sse

# and the code is what the macros say: i386 doubles the x87's with SSE2 too, but under -mfpmath=sse
# (which without SSE gcc warns of and makes the x87's)
printf 'double m(double a, double b) { return a * b; }\n' >"$CC_TMP/m.c"
for f in "-march=pentium4" "-march=pentium4 -msse2" "-march=i686 -msse3" "-march=pentium4 -mfpmath=sse" \
	"-march=core2 -mfpmath=387" "-march=i686 -mfpmath=sse"; do
	g=$(gcc -m32 -O2 $f -S -o - "$CC_TMP/m.c" 2>/dev/null | grep -c mulsd || true)
	if ! "$CC_BIN" -m32 -O2 $f -S -o "$CC_TMP/m.s" "$CC_TMP/m.c" 2>"$CC_TMP/err"; then
		echo "run_march_macros: -m32 $f: $(head -1 "$CC_TMP/err")"
		exit 1
	fi
	c=$(grep -c mulsd "$CC_TMP/m.s" || true)
	if [ "$g" != "$c" ]; then
		echo "run_march_macros: -m32 $f: $c mulsd, gcc's $g"
		exit 1
	fi
done
echo "run_march_macros: ok"
