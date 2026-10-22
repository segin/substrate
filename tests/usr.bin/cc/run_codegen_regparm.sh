#!/bin/sh
# The i386 regparm calling convention, as gcc has it, across compilers:
# -mregparm=3 (as Linux builds i386 with) and the regparm(N) attribute
# (its asmlinkage is regparm(0)).  rp_lib.c and rp_main.c are built one by
# cc and one by gcc, both ways, and the output must be the all-gcc
# build's.  rp_mem.c gives them a memcpy built the same way: under
# -mregparm the compilers call it with register arguments too.
#
# cc ignored -mregparm and the attribute, passing everything on the stack:
# a kernel whose assembly calls C (or C calls assembly) with the registers
# would have read every argument from the wrong place.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
BASE="$ROOT/tests/usr.bin/cc/codegen_regparm"
HOST_CC=${HOST_CC:-gcc}
COMMON="-m32 -mregparm=3 -fno-builtin -fno-pie"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_regparm.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

if ! printf 'int main(void) { return 0; }\n' | "$HOST_CC" -m32 -x c - -o "$tmp/probe" 2>/dev/null; then
	echo "run_codegen_regparm: $HOST_CC cannot link -m32 programs; skipping"
	exit 0
fi

fail=0
runs=0
# -mregparm=3 alone, and with -freg-struct-return as Linux's i386 build has them
for extra in "" "-freg-struct-return"; do
FLAGS="$COMMON $extra"
# shellcheck disable=SC2086
"$HOST_CC" $FLAGS -w -c -o "$tmp/mem.o" "$BASE/rp_mem.c" || exit 1
"$HOST_CC" $FLAGS -w -I"$BASE" -o "$tmp/ref" "$BASE/rp_main.c" "$BASE/rp_lib.c" "$tmp/mem.o" -no-pie || exit 1
"$tmp/ref" >"$tmp/ref.out" 2>&1
for o in O0 O2; do
	for mix in main lib; do
		runs=$((runs + 1))
		if [ $mix = main ]; then
			ours="$BASE/rp_main.c"
			theirs="$BASE/rp_lib.c"
		else
			ours="$BASE/rp_lib.c"
			theirs="$BASE/rp_main.c"
		fi
		if ! "$CC_BIN" $FLAGS -$o -I"$BASE" -c -o "$tmp/ours.o" "$ours" >"$tmp/cc.log" 2>&1; then
			echo "FAIL $extra -$o cc-built $mix: cc rejected it"
			sed -n '1,5s/^/    /p' "$tmp/cc.log"
			fail=1
			continue
		fi
		"$HOST_CC" $FLAGS -w -I"$BASE" -c -o "$tmp/theirs.o" "$theirs" || exit 1
		"$HOST_CC" -m32 -no-pie -o "$tmp/mixed" "$tmp/ours.o" "$tmp/theirs.o" "$tmp/mem.o" || exit 1
		timeout 10 "$tmp/mixed" >"$tmp/mixed.out" 2>&1
		if ! cmp -s "$tmp/ref.out" "$tmp/mixed.out"; then
			echo "FAIL $extra -$o cc-built $mix: output differs from an all-$HOST_CC build (< $HOST_CC, > mixed)"
			diff "$tmp/ref.out" "$tmp/mixed.out" | sed -n '1,10s/^/    /p'
			fail=1
		fi
	done
done
done
echo "run_codegen_regparm: $runs runs"
exit $fail
