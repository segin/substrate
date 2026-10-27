#!/bin/sh
# run_substrate_headers.sh: cc's sources compile against Substrate's own
# headers, as the cc that goes on the disk images is built.
#
# The rest of the suite builds cc for the host, against the host's
# headers.  Substrate's <stdarg.h> makes va_start a function-like macro,
# so the selector's call through its `va_start` member became a call of
# `__builtin_va_start` there and the image's cc did not build at all.
# Each source is compiled with the target build's flags (Makefile.inc's
# CFLAGS for i386, with the cc Makefile's own), checking only.
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CCDIR="$ROOT/usr.bin/cc"
HOST_CC=${HOST_CC:-cc}
srcs=$(sed -n '/^CC_SRCS/,/[^\\]$/p' "$CCDIR/Makefile" | tr -d '\\' | sed 's/CC_SRCS *=//')
[ -n "$srcs" ] || { echo "run_substrate_headers: no CC_SRCS in $CCDIR/Makefile"; exit 1; }
if ! printf 'int x;\n' | $HOST_CC -m32 -x c -fsyntax-only - >/dev/null 2>&1; then
	echo "run_substrate_headers: SKIP (the host compiler has no -m32)"
	exit 0
fi
gcc_inc=$($HOST_CC -m32 -print-file-name=include)
log=$(mktemp "${TMPDIR:-/tmp}/cc_substrate_headers.XXXXXX") || exit 1
trap 'rm -f "$log"' EXIT INT TERM
fail=0
n=0
for s in $srcs; do
	n=$((n + 1))
	if ! (cd "$CCDIR" && $HOST_CC -m32 -std=c2x -Wall -Wextra -Werror -fsyntax-only -nostdinc \
		-I"$ROOT/include" -I"$ROOT/sys/include" -I"$ROOT/sys" -I"$gcc_inc" -fno-builtin \
		-DCC_SUBSTRATE_BUILD=1 -Iinclude "$s") >"$log" 2>&1; then
		echo "FAIL $s:"
		sed -n '1,4s/^/    /p' "$log"
		fail=1
	fi
done
echo "run_substrate_headers: $n sources"
exit $fail
