#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BASE=$(cd "$(dirname "$0")" && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_pp_fuzz.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

# Malformed input may be rejected, but must not crash or hang cc -E.
# The inputs are generated from fixed seeds, so a failure reproduces;
# the input that failed is kept beside the test as pp_fuzz_fail_SEED.c.
fail=0
i=0
while [ $i -lt 64 ]; do
	in="$tmp/in_$i.c"
	awk -v seed=$i 'BEGIN {
		srand(seed);
		n = split("#define #if #ifdef #else #endif #include ( ) , ... __VA_ARGS__ __VA_OPT__ ## # \\ \" '\'' /* */ // 0x1p3 1e+ L\"x\" defined A B x y\n", tok, " ");
		for (k = 0; k < 160; k++) {
			printf "%s%s", tok[int(rand() * n) + 1], (rand() < 0.2) ? "\n" : " ";
		}
	}' > "$in"
	rc=0
	timeout 10 "$CC_BIN" -E "$in" -o "$tmp/out.i" >/dev/null 2>&1 || rc=$?
	if [ $rc -ge 124 ]; then
		cp "$in" "$BASE/pp_fuzz_fail_$i.c"
		echo "FAIL: cc -E exit $rc (124 timeout, >128 signal) on seed $i, kept as pp_fuzz_fail_$i.c"
		fail=1
	fi
	i=$((i + 1))
done
[ $fail -eq 0 ] || exit 1

cat > "$tmp/seed.c" <<'EOF'
#define A(x, ...) x __VA_OPT__(+ (__VA_ARGS__))
#include "pp_s3_quote.h"
#if defined(QQ) && (1 || (1 / 0))
int x = A(1, 2);
#endif
EOF
"$CC_BIN" -E -std=c23 -I"$BASE" "$tmp/seed.c" -o "$tmp/seed.i"
grep -q "int x = 1 + (2);" "$tmp/seed.i"
