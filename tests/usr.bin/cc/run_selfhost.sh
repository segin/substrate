#!/bin/sh
# run_selfhost.sh: the compiler must compile itself correctly.
#
# Stage 2 is cc built by the cc under test (a host build, CC_BIN); it
# must pass the codegen, ABI and random-arithmetic checks.  Stage 3 is cc
# built by stage 2; for every one of cc's sources it must write the same
# assembly as stage 2, so that a miscompilation cannot hide by being
# consistent.  The objects are linked by the host's cc against its libc,
# as a host build of cc is.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BASE="$ROOT/tests/usr.bin/cc"
CCDIR="$ROOT/usr.bin/cc"
CC_BIN=${CC_BIN:-"$CCDIR/cc"}
HOST_CC=${HOST_CC:-cc}
export AS=${AS:-as} LD=${LD:-ld}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_selfhost.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

srcs=$(sed -n '/^CC_SRCS/,/emit_s\.c$/p' "$CCDIR/Makefile" | tr -d '\\' | sed 's/CC_SRCS *=//')
[ -n "$srcs" ] || { echo "run_selfhost: no CC_SRCS in $CCDIR/Makefile"; exit 1; }

# build_cc COMPILER DIR: compile cc's sources with COMPILER, link DIR/cc
build_cc() {
	mkdir -p "$2"
	for f in $srcs; do
		if ! (cd "$CCDIR" && "$1" -O1 -Iinclude -c "$f" -o "$2/$(echo "$f" | tr / _).o") \
		    >"$2/build.log" 2>&1; then
			echo "FAIL: $1 cannot compile $f:"
			sed -n '1,5s/^/    /p' "$2/build.log"
			return 1
		fi
	done
	"$HOST_CC" -o "$2/cc" "$2"/*.o -lm || { echo "FAIL: cannot link $2/cc"; return 1; }
}

build_cc "$CC_BIN" "$tmp/stage2" || exit 1
fail=0
for t in run_codegen_diff.sh run_codegen_abi.sh run_codegen_random.sh; do
	if ! CC_BIN="$tmp/stage2/cc" sh "$BASE/$t" >"$tmp/$t.log" 2>&1; then
		echo "FAIL: stage 2 (cc built by cc) fails $t:"
		grep FAIL "$tmp/$t.log" | sed -n '1,5s/^/    /p'
		fail=1
	fi
done

build_cc "$tmp/stage2/cc" "$tmp/stage3" || exit 1
mkdir -p "$tmp/s2" "$tmp/s3"
differ=0
for f in $srcs; do
	n=$(echo "$f" | tr / _)
	(cd "$CCDIR" && "$tmp/stage2/cc" -O1 -Iinclude -S "$f" -o "$tmp/s2/$n.s" &&
	    "$tmp/stage3/cc" -O1 -Iinclude -S "$f" -o "$tmp/s3/$n.s") || { echo "FAIL: -S $f"; fail=1; continue; }
	if ! cmp -s "$tmp/s2/$n.s" "$tmp/s3/$n.s"; then
		echo "FAIL: stage 2 and stage 3 write different assembly for $f"
		differ=$((differ + 1))
		fail=1
	fi
done
echo "run_selfhost: $(echo $srcs | wc -w) sources, stage 3 differs on $differ"
exit $fail
