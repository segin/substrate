#!/bin/sh
# run_codegen_diff.sh: compile each codegen/*.c with the in-tree cc and
# with the host's gcc, for -m64 and -m32 at -O0 and -O2, and require the
# same output and exit status from both.  The programs print what they
# compute, so a miscompile shows as a difference, not as a crash.
#
# A host whose gcc cannot link -m32 programs skips the -m32 runs.
# CC_BIN names another cc to test, such as a build of it for a 32-bit host.
#
# A test that gcc can judge only from some release on names it in a
# "cc-gcc: <major>" comment: an older gcc (CI's, say) rejects the
# language.  With an older gcc its runs are skipped, and said to be, not
# failed.  Any other HOST_CC runs them all.
#
# gcc's -m32 has the ISA its host configured (SSE2 on Arch, a bare i686
# on Ubuntu), and vectors and _Float16 depend on it.  A test that does
# names the -march its -m32 runs need in "cc-march-m32: <arch> ...", and
# both compilers are run with each.
#
# Usage: run_codegen_diff.sh [name ...] -- the codegen/NAME.c named, or
# all of them.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
BASE="$ROOT/tests/usr.bin/cc/codegen"
HOST_CC=${HOST_CC:-gcc}
# the native cc must run the host's as and ld, not substrate's
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_codegen.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

modes=64
if printf 'int main(void) { return 0; }\n' | "$HOST_CC" -m32 -x c - -o "$tmp/probe" 2>/dev/null; then
	modes="64 32"
else
	echo "run_codegen_diff: $HOST_CC cannot link -m32 programs; skipping -m32"
fi

# the major release of HOST_CC if it is gcc, else empty
gcc_major=
if "$HOST_CC" -v 2>&1 | grep -q '^gcc version'; then
	gcc_major=$("$HOST_CC" -dumpversion | cut -d. -f1)
fi

if [ $# -gt 0 ]; then
	srcs=
	for n in "$@"; do srcs="$srcs $BASE/$n.c"; done
else
	srcs=$(ls "$BASE"/*.c)
fi

fail=0
runs=0
skipped=0
for src in $srcs; do
	name=$(basename "$src" .c)
	# a test may name its language in a "cc-std: <std>" comment, for both compilers
	std=$(sed -n 's/.*cc-std: *\([a-z0-9]*\).*/\1/p' "$src" | head -n 1)
	cc_std=${std:+-std=$std}
	need=$(sed -n 's/.*cc-gcc: *\([0-9][0-9]*\).*/\1/p' "$src" | head -n 1)
	# the -m32 -march list, up to a "(" that starts the reason
	march32=$(sed -n 's/.*cc-march-m32: *\([a-z0-9 -]*\).*/\1/p' "$src" | head -n 1)
	runmodes=
	for m in $modes; do
		if [ $m = 32 ] && [ -n "$march32" ]; then
			for a in $march32; do runmodes="$runmodes 32/$a"; done
		else
			runmodes="$runmodes $m"
		fi
	done
	for mode in $runmodes; do
		m=${mode%%/*}
		flags=-m$m
		case $mode in */*) flags="$flags -march=${mode#*/}" ;; esac
		if [ -n "$gcc_major" ] && [ -n "$need" ] && [ "$gcc_major" -lt "$need" ]; then
			echo "skip $name $flags: needs gcc $need, $HOST_CC is gcc $gcc_major"
			skipped=$((skipped + 2))
			continue
		fi
		if ! "$HOST_CC" $flags -std=${std:-gnu11} -w -o "$tmp/ref" "$src" 2>"$tmp/ref.log"; then
			echo "FAIL $name $flags: $HOST_CC cannot build it"
			fail=1
			continue
		fi
		"$tmp/ref" >"$tmp/ref.out" 2>&1
		echo "exit $?" >>"$tmp/ref.out"
		for o in O0 O2; do
			runs=$((runs + 1))
			if ! "$CC_BIN" $flags $cc_std -$o -o "$tmp/ours" "$src" >"$tmp/cc.log" 2>&1; then
				echo "FAIL $name $flags -$o: cc rejected it"
				sed -n '1,5s/^/    /p' "$tmp/cc.log"
				fail=1
				continue
			fi
			timeout 10 "$tmp/ours" >"$tmp/ours.out" 2>&1
			echo "exit $?" >>"$tmp/ours.out"
			if ! cmp -s "$tmp/ref.out" "$tmp/ours.out"; then
				echo "FAIL $name $flags -$o: output differs from $HOST_CC (< $HOST_CC, > cc)"
				diff "$tmp/ref.out" "$tmp/ours.out" | sed -n '1,12s/^/    /p'
				fail=1
			fi
		done
	done
done
echo "run_codegen_diff: $runs runs, $skipped skipped for an old gcc"
exit $fail
