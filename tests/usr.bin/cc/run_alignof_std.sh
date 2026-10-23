#!/bin/sh
# __alignof__ and __alignof are the implementation's own names, taken in
# every -std mode as gcc takes them.  cc knew them in GNU mode only, and
# the host's <stddef.h> aligns max_align_t's members with
# __attribute__((__aligned__(__alignof__(long long)))) under -std=c23
# too -- which failed once aligned()'s argument was parsed as the
# expression it is.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/ao.c" <<'EOF'
typedef struct {
	long long a __attribute__((__aligned__(__alignof__(long long))));
	double b __attribute__((__aligned__(__alignof(double))));
} widest;
int widest_align = __alignof__(widest);
EOF
status=0
for std in c99 c11 c17 c23; do
	if ! "$CC_BIN" -m32 -std=$std -S -o "$CC_TMP/ao.s" "$CC_TMP/ao.c" 2> "$CC_TMP/ao.err"; then
		echo "FAIL -std=$std: $(head -1 "$CC_TMP/ao.err")"
		status=1
	fi
done
[ "$status" -eq 0 ] && echo "run_alignof_std: ok"
exit "$status"
