#!/bin/sh
# Under i386 soft-float (-mno-80387) a _Float16 crosses a call as gcc's
# does -- an argument or parameter its bits in a stack word, a result its
# bits in %xmm0 -- and its arithmetic is evaluated in float, as gcc's
# (__extendhfsf2, float's operations, __truncsfhf2).  The pass refused a
# half parameter, argument or result.  Objects of each compiler call the
# other's (run_i386_float16_abi.sh's programs, taken from it) and chains
# of + - * / over many random halves (run_float16_eval.sh's) print what
# gcc's alone print, -O0 and -O2, both code paths, no MIR fallback, with
# libgcc's soft-float routines, which i386 libgcc lacks, made by
# i386_soft_float_routines.c.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
TDIR=$(cd "$(dirname "$0")" && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_soft_float16_abi: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' >"$CC_TMP/m32.c" ||
	! gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_soft_float16_abi: skip (no gcc -m32)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# a here-document of another test, by the file it writes
take() {
	awk -v f="$2" 'index($0, "cat >\"$CC_TMP/" f "\" <<") == 1 { on = 1; next } on && /^EOF$/ { on = 0 } on' \
		"$TDIR/$1" >"$CC_TMP/$2"
}
take run_i386_float16_abi.sh lib.c
take run_i386_float16_abi.sh main.c
take run_float16_eval.sh ar.c
gcc -m32 -O1 -mno-sse -fno-pic -c -o "$CC_TMP/soft.o" "$TDIR/i386_soft_float_routines.c"
for f in lib main ar; do
	gcc -m32 -O2 -msse2 -mfpmath=387 -mno-80387 -fno-pic -c -o "$CC_TMP/g$f.o" "$CC_TMP/$f.c"
done
gcc -m32 -no-pie -o "$CC_TMP/g" "$CC_TMP/glib.o" "$CC_TMP/gmain.o" "$CC_TMP/soft.o"
want=$("$CC_TMP/g")
gcc -m32 -no-pie -o "$CC_TMP/g" "$CC_TMP/gar.o" "$CC_TMP/soft.o"
want_ar=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		for f in lib main ar; do
			if ! $run CC_MIR_STATS=2 "$CC_BIN" -m32 -march=pentium4 -msse2 -mfpmath=387 -mno-80387 -$o -c \
				-o "$CC_TMP/c$f.o" "$CC_TMP/$f.c" 2>"$CC_TMP/c.err"; then
				echo "run_i386_soft_float16_abi: $path -$o $f.c refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep -q 'not taken' "$CC_TMP/c.err"; then
				echo "run_i386_soft_float16_abi: $path -$o $f.c: $(grep 'not taken' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
		done
		gcc -m32 -no-pie -o "$CC_TMP/a" "$CC_TMP/cmain.o" "$CC_TMP/glib.o" "$CC_TMP/soft.o"
		gcc -m32 -no-pie -o "$CC_TMP/b" "$CC_TMP/gmain.o" "$CC_TMP/clib.o" "$CC_TMP/soft.o"
		for x in a b; do
			got=$("$CC_TMP/$x")
			if [ "$got" != "$want" ]; then
				echo "run_i386_soft_float16_abi: $path -$o ($x) prints [$got], gcc's [$want]"
				exit 1
			fi
		done
		gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/car.o" "$CC_TMP/soft.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want_ar" ]; then
			echo "run_i386_soft_float16_abi: arithmetic, $path -$o: $got, gcc's $want_ar"
			exit 1
		fi
	done
done
echo "run_i386_soft_float16_abi: ok"
