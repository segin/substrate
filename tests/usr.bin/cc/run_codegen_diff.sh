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
# "cc-gcc: <major>" comment, or "cc-gcc-m32: <major>" where only its
# -m32 runs need it: an older gcc (CI's, say) rejects the language, or
# lays the data out as gcc did before.  With an older gcc those runs are
# skipped, and said to be, not failed.  Any other HOST_CC runs them all.
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
	need32=$(sed -n 's/.*cc-gcc-m32: *\([0-9][0-9]*\).*/\1/p' "$src" | head -n 1)
	for m in $modes; do
		want=$need
		if [ $m = 32 ] && [ -n "$need32" ] && [ "${need:-0}" -lt "$need32" ]; then
			want=$need32
		fi
		if [ -n "$gcc_major" ] && [ -n "$want" ] && [ "$gcc_major" -lt "$want" ]; then
			echo "skip $name -m$m: needs gcc $want, $HOST_CC is gcc $gcc_major"
			skipped=$((skipped + 2))
			continue
		fi
		if ! "$HOST_CC" -m$m -std=${std:-gnu11} -w -o "$tmp/ref" "$src" 2>"$tmp/ref.log"; then
			echo "FAIL $name -m$m: $HOST_CC cannot build it"
			fail=1
			continue
		fi
		"$tmp/ref" >"$tmp/ref.out" 2>&1
		echo "exit $?" >>"$tmp/ref.out"
		for o in O0 O2; do
			runs=$((runs + 1))
			if ! "$CC_BIN" -m$m $cc_std -$o -o "$tmp/ours" "$src" >"$tmp/cc.log" 2>&1; then
				echo "FAIL $name -m$m -$o: cc rejected it"
				sed -n '1,5s/^/    /p' "$tmp/cc.log"
				fail=1
				continue
			fi
			timeout 10 "$tmp/ours" >"$tmp/ours.out" 2>&1
			echo "exit $?" >>"$tmp/ours.out"
			if ! cmp -s "$tmp/ref.out" "$tmp/ours.out"; then
				echo "FAIL $name -m$m -$o: output differs from $HOST_CC (< $HOST_CC, > cc)"
				diff "$tmp/ref.out" "$tmp/ours.out" | sed -n '1,12s/^/    /p'
				fail=1
			fi
		done
	done
done
echo "run_codegen_diff: $runs runs, $skipped skipped for an old gcc"
exit $fail
