#!/bin/sh
# run_crosshost.sh: a cc built for a 32-bit host writes what one built for
# a 64-bit host writes.
#
# cc is built twice from the same sources by the host's compiler, once
# with -m32 and once as usual, and both compile every program under
# codegen/ at -m64 and -m32.  The assembly must be identical: a host
# `long` (32 bits on i386) where a target value needs 64 shows up as a
# difference here.  Skipped, with a note, if the host cannot build -m32.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BASE="$ROOT/tests/usr.bin/cc"
CCDIR="$ROOT/usr.bin/cc"
HOST_CC=${HOST_CC:-cc}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_crosshost.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

srcs=$(sed -n '/^CC_SRCS/,/[^\\]$/p' "$CCDIR/Makefile" | tr -d '\\' | sed 's/CC_SRCS *=//')
[ -n "$srcs" ] || { echo "run_crosshost: no CC_SRCS in $CCDIR/Makefile"; exit 1; }

# build_cc FLAGS OUT: build cc with the host compiler and FLAGS into OUT
build_cc() {
	(cd "$CCDIR" && $HOST_CC $1 -O1 -DCC_NATIVE_BUILD=1 -Iinclude -I. -o "$2" $srcs) >"$tmp/build.log" 2>&1
}

printf 'int main(void) { return 0; }\n' > "$tmp/probe.c"
if ! $HOST_CC -m32 "$tmp/probe.c" -o "$tmp/probe" >/dev/null 2>&1; then
	echo "run_crosshost: SKIP (the host cannot build -m32 programs)"
	exit 0
fi
build_cc "" "$tmp/cc64" || { echo "run_crosshost: 64-bit-host build of cc failed"; cat "$tmp/build.log"; exit 1; }
build_cc "-m32" "$tmp/cc32" || { echo "run_crosshost: 32-bit-host build of cc failed"; cat "$tmp/build.log"; exit 1; }

fails=0
n=0
for f in "$BASE"/codegen/*.c; do
	for m in -m64 -m32; do
		n=$((n + 1))
		"$tmp/cc64" $m -S "$f" -o "$tmp/a64.s" >/dev/null 2>&1; r64=$?
		"$tmp/cc32" $m -S "$f" -o "$tmp/a32.s" >/dev/null 2>&1; r32=$?
		if [ $r64 -ne $r32 ] || { [ $r64 -eq 0 ] && ! cmp -s "$tmp/a64.s" "$tmp/a32.s"; }; then
			echo "run_crosshost: $(basename "$f") $m differs between a 32- and a 64-bit-hosted cc"
			fails=$((fails + 1))
		fi
	done
done
echo "run_crosshost: $n compiles, $fails differ"
[ $fails -eq 0 ]
