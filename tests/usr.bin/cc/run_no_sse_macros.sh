#!/bin/sh
# -mno-sse, -mno-sse2 and -mno-mmx take __SSE__, __SSE2__ and __MMX__
# away on x86-64 too, as gcc's do (-mno-sse SSE2 with it); Linux's
# x86-64 builds with all three.  cc defined them there whatever the
# options said, its floating point being SSE's.  The macros of each
# combination are gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_no_sse_macros: skip (no gcc)"
	exit 0
fi

# (-m32 at an -march of its own: a host gcc's -m32 defaults to x86-64's ISA, cc's to i686's)
for m in -m64 "-m32 -march=i686"; do
	more=""
	if [ "$m" = -m64 ]; then more='-mno-sse:-msse -mno-sse2:-msse2 -msse2:-mno-sse'; fi
	for f in "" -mno-sse -mno-sse2 -mno-mmx "-mno-sse -mno-sse2 -mno-mmx" $more; do
		f=$(echo "$f" | tr ':' ' ')
		# shellcheck disable=SC2086
		want=$(gcc $m $f -dM -E - </dev/null 2>/dev/null | grep -E '#define __(MMX|SSE|SSE2)__ ' | sort) || continue
		# shellcheck disable=SC2086
		got=$("$CC_BIN" $m $f -dM -E - </dev/null 2>/dev/null | grep -E '#define __(MMX|SSE|SSE2)__ ' | sort) || true
		if [ "$got" != "$want" ]; then
			echo "run_no_sse_macros: [$m $f] defines [$(echo $got)], gcc [$(echo $want)]"
			exit 1
		fi
	done
done
echo "run_no_sse_macros: ok"
