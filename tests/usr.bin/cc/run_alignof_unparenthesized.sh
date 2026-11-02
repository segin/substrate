#!/bin/sh
# __alignof__ of an expression need not parenthesize it, as sizeof's need
# not: Linux's drivers/vhost/vhost.c tests `BUILD_BUG_ON(__alignof__
# *vq->avail > VRING_AVAIL_ALIGN_SIZE)`, and cc refused it ("expected '('
# after _Alignof/alignof").  Dereferences, members and a variable,
# unparenthesized and parenthesized: the values are gcc's, -m64 and -m32,
# on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_alignof_unparenthesized: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
struct avail { unsigned short flags, idx; unsigned short ring[]; };
struct used { unsigned short flags; unsigned int v; } __attribute__((aligned(16)));
struct vq { struct avail *avail; struct used *used; int i; char c; };
int main(void)
{
	struct vq q, *vq = &q;
	short s;
	printf("%zu %zu %zu %zu %zu %zu %d\n", __alignof__ *vq->avail, __alignof__ *vq->used, __alignof__ vq->i,
	       __alignof__ q.c, __alignof__ s, __alignof__ (q), __alignof__ *vq->used > 8);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		if ! $run "$CC_BIN" -m$m -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
			echo "run_alignof_unparenthesized: -m$m $path refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_alignof_unparenthesized: -m$m $path prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_alignof_unparenthesized: ok"
