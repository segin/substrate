#!/bin/sh
# run_codegen_random.sh [FIRST [LAST]]: build the random arithmetic
# programs gen_arith.py makes for seeds FIRST..LAST (default 1..40) with
# the in-tree cc and with the host's gcc, -m64 and -m32 at -O0 and -O2,
# and require the same output.  A failure names the seed; reproduce it
# with "python3 gen_arith.py SEED > p.c".
#
# A host whose gcc cannot link -m32 programs skips the -m32 runs.
# CC_BIN names another cc to test, such as a build of it for a 32-bit host.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
GEN="$ROOT/tests/usr.bin/cc/gen_arith.py"
HOST_CC=${HOST_CC:-gcc}
first=${1:-1}
last=${2:-40}
# the native cc must run the host's as and ld, not substrate's
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_random.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

modes=64
if printf 'int main(void) { return 0; }\n' | "$HOST_CC" -m32 -x c - -o "$tmp/probe" 2>/dev/null; then
	modes="64 32"
else
	echo "run_codegen_random: $HOST_CC cannot link -m32 programs; skipping -m32"
fi

fail=0
seed=$first
while [ "$seed" -le "$last" ]; do
	python3 "$GEN" "$seed" >"$tmp/p.c" || exit 1
	for m in $modes; do
		"$HOST_CC" -m$m -w -o "$tmp/ref" "$tmp/p.c" || exit 1
		ref=$("$tmp/ref")
		for o in O0 O2; do
			if ! "$CC_BIN" -m$m -$o -o "$tmp/ours" "$tmp/p.c" >"$tmp/cc.log" 2>&1; then
				echo "FAIL seed $seed -m$m -$o: cc rejected it"
				sed -n '1,5s/^/    /p' "$tmp/cc.log"
				fail=1
				continue
			fi
			got=$(timeout 10 "$tmp/ours" 2>&1)
			if [ "$got" != "$ref" ]; then
				echo "FAIL seed $seed -m$m -$o: printed $got, $HOST_CC's build $ref"
				fail=1
			fi
		done
	done
	seed=$((seed + 1))
done
echo "run_codegen_random: seeds $first..$last"
exit $fail
