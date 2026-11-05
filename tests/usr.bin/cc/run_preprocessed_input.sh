#!/bin/sh
# Preprocessed C -- a file ending in .i, or any file after -x cpp-output --
# is compiled as it is, the preprocessor not run on it again, as gcc
# compiles it; -E passes it by, writing nothing.  It was refused:
# "unsupported input type: t.i".  The .i is gcc's own -E output, its line
# markers with gcc's flags; what the program prints is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_preprocessed_input: skip (no gcc)"
	exit 0
fi

fail() {
	echo "run_preprocessed_input: $*"
	exit 1
}

cat >"$CC_TMP/pi.h" <<'EOF2'
#define SCALE 7
static inline int scaled(int v) { return v * SCALE; }
EOF2
cat >"$CC_TMP/pi.c" <<'EOF2'
#include "pi.h"
#define TWICE(x) ((x) + (x))
int pi_value(int v) { return TWICE(scaled(v)) + __LINE__; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
#include <stdio.h>
int pi_value(int v);
int main(void) { printf("%d\n", pi_value(3)); return 0; }
EOF2

(cd "$CC_TMP" && gcc -E pi.c -o pi.i) || fail "gcc -E failed"
cp "$CC_TMP/pi.i" "$CC_TMP/pi.pp"
gcc -c "$CC_TMP/pi.c" -o "$CC_TMP/g.o"
gcc "$CC_TMP/main.c" "$CC_TMP/g.o" -o "$CC_TMP/g.out"
want=$("$CC_TMP/g.out")

# the macro is gone from the .i: preprocessed again it is still the same C,
# but a SCALE redefined on the command line must not reach it
"$CC_BIN" -O2 -DSCALE=1 -c "$CC_TMP/pi.i" -o "$CC_TMP/i.o" 2>"$CC_TMP/i.err" ||
	fail "cc -c pi.i failed: $(cat "$CC_TMP/i.err")"
gcc "$CC_TMP/main.c" "$CC_TMP/i.o" -o "$CC_TMP/i.out"
got=$("$CC_TMP/i.out")
[ "$got" = "$want" ] || fail "pi.i printed $got, gcc's $want"

"$CC_BIN" -O2 -x cpp-output -c "$CC_TMP/pi.pp" -o "$CC_TMP/x.o" 2>"$CC_TMP/x.err" ||
	fail "cc -x cpp-output -c pi.pp failed: $(cat "$CC_TMP/x.err")"
gcc "$CC_TMP/main.c" "$CC_TMP/x.o" -o "$CC_TMP/x.out"
got=$("$CC_TMP/x.out")
[ "$got" = "$want" ] || fail "-x cpp-output pi.pp printed $got, gcc's $want"

# -S writes pi.s beside it, as for a .c
(cd "$CC_TMP" && "$CC_BIN" -S pi.i) || fail "cc -S pi.i failed"
grep -q 'pi_value' "$CC_TMP/pi.s" || fail "cc -S pi.i wrote no pi_value"

# -E: nothing, and success
got=$("$CC_BIN" -E "$CC_TMP/pi.i") || fail "cc -E pi.i failed"
[ -z "$got" ] || fail "cc -E pi.i wrote [$got], gcc writes nothing"

echo "run_preprocessed_input: ok"
