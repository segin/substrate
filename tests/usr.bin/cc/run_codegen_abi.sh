#!/bin/sh
# run_codegen_abi.sh: build codegen_abi/abi_main.c and abi_lib.c into one
# program three ways -- both with the host's gcc, main with cc and lib
# with gcc, and the reverse -- for -m64 and -m32 at -O0 and -O2, and
# require all three to print the same.  This holds cc to the platform
# calling convention, not merely to one it agrees with itself on.
#
# i386 passes vectors by the ISA: in %xmm registers only with SSE, in %mm
# ones only with MMX.  gcc's -m32 has whatever ISA the host's gcc was
# configured with (SSE2 on Arch, a bare i686 on Ubuntu), so the -m32 runs
# name theirs, both with -march=i686 (neither) and -march=pentium4 (both).
#
# A host whose gcc cannot link -m32 programs skips the -m32 runs, and one
# without _BitInt for a mode builds neither half with the _BitInt cases.
# CC_BIN names another cc to test.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
BASE="$ROOT/tests/usr.bin/cc/codegen_abi"
HOST_CC=${HOST_CC:-gcc}
# one language for both compilers (the _BitInt cases need C23; gnu2x is the spelling older gccs know)
STD=gnu2x
# the native cc must run the host's as and ld, not substrate's
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_abi.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

# each a mode, and for -m32 the -march after the slash
modes=64
if printf 'int main(void) { return 0; }\n' | "$HOST_CC" -m32 -x c - -o "$tmp/probe" 2>/dev/null; then
	modes="64 32/i686 32/pentium4"
else
	echo "run_codegen_abi: $HOST_CC cannot link -m32 programs; skipping -m32"
fi

fail=0
runs=0
printf '_BitInt(100) probe;\n' > "$tmp/bitint.c"
for mode in $modes; do
	m=${mode%%/*}
	flags=-m$m
	case $mode in */*) flags="$flags -march=${mode#*/}" ;; esac
	# A gcc without _BitInt (before 14, -m32 and -m64 alike) cannot
	# judge those cases, so neither half has them.
	nobitint=
	if ! "$HOST_CC" $flags -std=$STD -w -c -o "$tmp/bitint.o" "$tmp/bitint.c" 2>/dev/null; then
		nobitint=-DABI_NO_BITINT
		echo "run_codegen_abi: $flags: $HOST_CC has no _BitInt; its cases are skipped"
	fi
	# -Wno-psabi: gcc notes the vectors' ABI changes (GCC 4.6; without SSE) even under -w
	"$HOST_CC" $flags -std=$STD $nobitint -w -Wno-psabi -o "$tmp/ref" "$BASE/abi_main.c" "$BASE/abi_lib.c" || exit 1
	"$tmp/ref" >"$tmp/ref.out" 2>&1
	for o in O0 O2; do
		for mix in main lib; do
			runs=$((runs + 1))
			if [ $mix = main ]; then
				ours="$BASE/abi_main.c"
				theirs="$BASE/abi_lib.c"
			else
				ours="$BASE/abi_lib.c"
				theirs="$BASE/abi_main.c"
			fi
			if ! "$CC_BIN" $flags -std=$STD $nobitint -$o -c -o "$tmp/ours.o" "$ours" >"$tmp/cc.log" 2>&1; then
				echo "FAIL $flags -$o cc-built $mix: cc rejected it"
				sed -n '1,5s/^/    /p' "$tmp/cc.log"
				fail=1
				continue
			fi
			"$HOST_CC" $flags -std=$STD $nobitint -w -Wno-psabi -c -o "$tmp/theirs.o" "$theirs" || exit 1
			"$HOST_CC" $flags -no-pie -o "$tmp/mixed" "$tmp/ours.o" "$tmp/theirs.o" || exit 1
			timeout 10 "$tmp/mixed" >"$tmp/mixed.out" 2>&1
			if ! cmp -s "$tmp/ref.out" "$tmp/mixed.out"; then
				echo "FAIL $flags -$o cc-built $mix: output differs from an all-$HOST_CC build (< $HOST_CC, > mixed)"
				diff "$tmp/ref.out" "$tmp/mixed.out" | sed -n '1,10s/^/    /p'
				fail=1
			fi
		done
	done
done
echo "run_codegen_abi: $runs runs"
exit $fail
