#!/bin/sh
# cc predefines gcc's macros for the optimization asked for:
# __OPTIMIZE__ at -O1 and above, __OPTIMIZE_SIZE__ too at -Os and -Oz,
# __NO_INLINE__ at -O0 and without -O.  It defined none, and Linux's
# fortify-string.h, included only `#if defined(__OPTIMIZE__)`, never
# was: a kernel with CONFIG_FORTIFY_SOURCE had no fortified string
# functions, and lib/string_helpers.c, which names their enum, did not
# build (the Arch config).  A -U or -D on the command line overrides
# them, as gcc's.  gcc is the judge, for -E and -dM.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_optimize_macros: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/m.c" <<'EOF2'
#ifdef __OPTIMIZE__
optimize __OPTIMIZE__
#endif
#ifdef __OPTIMIZE_SIZE__
size __OPTIMIZE_SIZE__
#endif
#ifdef __NO_INLINE__
noinline __NO_INLINE__
#endif
EOF2
for flags in "" -O0 -O -O1 -O2 -O3 -Os -Oz -Og -Ofast "-O2 -U__OPTIMIZE__" "-O0 -D__OPTIMIZE__=7"; do
	# shellcheck disable=SC2086
	want=$(gcc $flags -E -P "$CC_TMP/m.c" | tr -s ' \n' ' ')
	# shellcheck disable=SC2086
	got=$("$CC_BIN" $flags -E -P "$CC_TMP/m.c" | tr -s ' \n' ' ')
	if [ "$got" != "$want" ]; then
		echo "run_optimize_macros: [$flags] gives [$got], gcc's [$want]"
		exit 1
	fi
	# shellcheck disable=SC2086
	want=$(gcc $flags -dM -E - </dev/null | grep -E '__OPTIMIZE|__NO_INLINE__' | sort)
	# shellcheck disable=SC2086
	got=$("$CC_BIN" $flags -dM -E - </dev/null | grep -E '__OPTIMIZE|__NO_INLINE__' | sort || true)
	if [ "$got" != "$want" ]; then
		echo "run_optimize_macros: -dM [$flags] gives [$got], gcc's [$want]"
		exit 1
	fi
done
echo "run_optimize_macros: ok"
