#!/bin/sh
# -mno-80387 forbids x87 instructions and -mno-fp-ret-in-387 results in
# %st0; Linux's x86-64 builds with both, as gcc refuses a program that
# would need them.  cc took both and ignored them, and made x87 code.
# A program with no floating point compiles under both, -m64 and -m32,
# through the MIR path and the legacy emitters, as gcc compiles it --
# one whose only floating point is a constant folded to an integer too,
# at -O0 as well, and with -mno-sse besides.  One that would compute or
# return in the x87 -- a long double on x86-64 -- is refused where gcc
# refuses it; on i386 under -mno-80387 gcc compiles floating point as
# soft-float calls, and so does cc (run_i386_soft_float.sh), and under
# -mno-fp-ret-in-387 alone returns in the general registers, as cc does
# (run_i386_no_fp_ret_in_387.sh); with neither option it compiles.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_no_80387: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/int.c" <<'EOF2'
int sum(const int *p, int n) { int s = 0; while (n-- > 0) s += *p++; return s; }
EOF2
# floating point only in a constant, folded: Linux's cx88-dsp.c (refused, once: the double was left in cc's table)
cat >"$CC_TMP/folded.c" <<'EOF2'
#define INT_PI ((int)(3.141592653589 * 32768.0))
int period(unsigned x) { return x / INT_PI + (int)3.5; }
EOF2
# the same at -O0, where gcc folds them too, a float's, a long double's and implicit conversions among them
cat >"$CC_TMP/folded0.c" <<'EOF2'
#define INT_PI ((int)(3.141592653589 * 32768.0))
int period(unsigned x) { int y = -41.25 * 4; return x / INT_PI + (int)3.5 + (int)-3.5f + (int)3.5L + y; }
EOF2
cat >"$CC_TMP/ld64.c" <<'EOF2'
long double half(long double x) { return x / 2; }
EOF2
cat >"$CC_TMP/ldcall.c" <<'EOF2'
extern long double get(void);
int whole(void) { return (int)get(); }
EOF2
cat >"$CC_TMP/d32.c" <<'EOF2'
double twice(double x) { return x * 2; }
EOF2
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for m in 64 32; do
		for f in -mno-80387 -mno-fp-ret-in-387 "-mno-80387 -mno-fp-ret-in-387" "-mno-80387 -mno-sse -mno-sse2"; do
			for case in "O2 int.c" "O2 folded.c" "O0 folded0.c" "O2 folded0.c"; do
				o=${case% *}
				src=${case#* }
				# shellcheck disable=SC2086
				if ! gcc -m$m -$o $f -c -o "$CC_TMP/g.o" "$CC_TMP/$src" 2>/dev/null; then
					continue
				fi
				# shellcheck disable=SC2086
				if ! $run "$CC_BIN" -m$m -$o $f -c -o "$CC_TMP/o.o" "$CC_TMP/$src" 2>"$CC_TMP/c.err"; then
					echo "run_no_80387: -m$m -$o $path [$f] $src: no floating point computed, refused: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
			done
		done
	done
	for case in "64 ld64.c" "64 ldcall.c" "32 d32.c" "32 ld64.c" "32 ldcall.c"; do
		m=${case% *}
		src=${case#* }
		for f in -mno-80387 -mno-fp-ret-in-387; do
			# shellcheck disable=SC2086
			gcc -m$m -O2 $f -c -o "$CC_TMP/g.o" "$CC_TMP/$src" 2>/dev/null && gcc_ok=1 || gcc_ok=0
			# shellcheck disable=SC2086
			$run "$CC_BIN" -m$m -O2 $f -c -o "$CC_TMP/o.o" "$CC_TMP/$src" 2>"$CC_TMP/c.err" && cc_ok=1 || cc_ok=0
			if [ $gcc_ok = 0 ] && [ $cc_ok = 1 ]; then
				echo "run_no_80387: -m$m $path [$f] $src: compiled, which gcc refuses"
				exit 1
			fi
			if [ $m = 32 ] && [ $gcc_ok = 1 ] && [ $cc_ok = 0 ]; then
				echo "run_no_80387: -m$m $path [$f] $src: refused, which gcc compiles: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			if [ $cc_ok = 0 ] && ! grep -q -- "$f" "$CC_TMP/c.err"; then
				echo "run_no_80387: -m$m $path [$f] $src: refused without saying why: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
		done
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/o.o" "$CC_TMP/$src" 2>"$CC_TMP/c.err"; then
			echo "run_no_80387: -m$m $path $src: refused with neither option: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
	done
done
echo "run_no_80387: ok"
