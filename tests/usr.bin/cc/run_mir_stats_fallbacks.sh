#!/bin/sh
# CC_MIR_STATS=1 prints only the functions the register allocator does not
# take, the fallbacks; 2 prints every function, taken or not; unset, 0 or
# anything else prints nothing.  The higher level says more.
#
# 1 once printed every function: a Linux build printed some sixty thousand
# "taken" lines, every static inline of every header once for each file,
# around the handful of fallbacks that are the point.
#
# gpr() falls back: it reads a global register variable other than the
# stack or frame pointer, which only the older path does.  plain() is taken.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/fb.c" <<'EOF2'
register unsigned long kept asm("r12");

unsigned long gpr(void) { return kept; }

int plain(int a) { return a + 1; }
EOF2

# the stats lines of a compile with CC_MIR_STATS set to $1 (unset if "-")
stats() {
	if [ "$1" = - ]; then
		"$CC_BIN" -m64 -O2 -c -o "$CC_TMP/fb.o" "$CC_TMP/fb.c" 2>&1 | grep '^mir:' || true
	else
		CC_MIR_STATS=$1 "$CC_BIN" -m64 -O2 -c -o "$CC_TMP/fb.o" "$CC_TMP/fb.c" 2>&1 | grep '^mir:' || true
	fi
}

# (the reason names a line of the selector; only its start is checked)
fallback='mir: gpr: not taken: '
got=$(stats 1)
case "$got" in
"$fallback"*)
	if [ "$(printf '%s\n' "$got" | wc -l)" -ne 1 ]; then
		echo "run_mir_stats_fallbacks: CC_MIR_STATS=1 printed [$got], not only gpr()"
		exit 1
	fi
	;;
*)
	echo "run_mir_stats_fallbacks: CC_MIR_STATS=1 printed [$got], not only [$fallback...]"
	exit 1
	;;
esac
got=$(stats 2)
if ! printf '%s\n' "$got" | grep -q "^$fallback" || ! printf '%s\n' "$got" | grep -qx 'mir: plain: taken'; then
	echo "run_mir_stats_fallbacks: CC_MIR_STATS=2 printed [$got], not both functions"
	exit 1
fi
for v in - 0 x; do
	got=$(stats $v)
	if [ -n "$got" ]; then
		echo "run_mir_stats_fallbacks: CC_MIR_STATS=$v printed [$got]"
		exit 1
	fi
done
echo "run_mir_stats_fallbacks: ok"
