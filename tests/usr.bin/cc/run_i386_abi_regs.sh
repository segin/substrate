#!/bin/sh
# Every function of the ABI corpus (codegen_abi/) goes through the
# register allocator on i386: vectors in %xmm0-2 and %mm0-2 (with SSE and MMX) or memory, arguments
# and results; a vector's stack copy aligned to its size from 16, a
# _Complex _Float128's to 16, with the gaps between laid out as gcc lays
# them; a structure in registers under regparm; a structure of a float or
# a double back in %st0.  These used to go to the old emitter, a stack
# slot for every value.  CC_MIR_STATS says which path took each function;
# each object, linked with gcc's build of the other, must print what an
# all-gcc build prints.  Needs a host gcc that builds -m32 programs;
# skipped, with a note, if not.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
BASE="$ROOT/tests/usr.bin/cc/codegen_abi"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_i386_abi_regs: skip (gcc cannot build -m32 programs)"
	exit 0
fi

# Vectors travel by the ISA (registers only with SSE and MMX), and gcc's
# -m32 has its host's, so both compilers are told: without them, and with.
printf '_BitInt(100) probe;\n' > "$CC_TMP/bitint.c"
for arch in i686 pentium4; do
	flags="-m32 -march=$arch -std=gnu2x"
	# a gcc without _BitInt cannot judge those cases: neither half has them
	if ! gcc $flags -w -c -o "$CC_TMP/bitint.o" "$CC_TMP/bitint.c" 2>/dev/null; then
		flags="$flags -DABI_NO_BITINT"
	fi
	gcc $flags -w -Wno-psabi -o "$CC_TMP/ref" "$BASE/abi_main.c" "$BASE/abi_lib.c"
	"$CC_TMP/ref" >"$CC_TMP/ref.out" 2>&1
	for o in O0 O2; do
		for mix in main lib; do
			if [ $mix = main ]; then other=lib; else other=main; fi
			CC_MIR_STATS=1 "$CC_BIN" $flags -$o -c -o "$CC_TMP/ours.o" "$BASE/abi_$mix.c" 2>"$CC_TMP/stats"
			if grep "^mir: .*not taken" "$CC_TMP/stats"; then
				echo "run_i386_abi_regs: -march=$arch -$o abi_$mix.c: the above do not go through the allocator"
				exit 1
			fi
			gcc $flags -w -Wno-psabi -c -o "$CC_TMP/theirs.o" "$BASE/abi_$other.c"
			gcc -m32 -no-pie -o "$CC_TMP/mixed" "$CC_TMP/ours.o" "$CC_TMP/theirs.o"
			"$CC_TMP/mixed" >"$CC_TMP/mixed.out" 2>&1 || true
			if ! cmp -s "$CC_TMP/ref.out" "$CC_TMP/mixed.out"; then
				echo "run_i386_abi_regs: -march=$arch -$o cc-built abi_$mix.c: output differs from gcc's (< gcc, > mixed)"
				diff "$CC_TMP/ref.out" "$CC_TMP/mixed.out" | sed -n '1,10p'
				exit 1
			fi
		done
	done
done
echo "run_i386_abi_regs: ok"
