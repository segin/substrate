#!/bin/sh
# Common subexpressions are found through copies made in the block: two
# copies of a variable (what promoting a local to a value leaves) are the
# same value while neither is written, so an index computed of each is
# computed once -- and the pass runs again once slots are values.  v[i]
# taken twice in one statement computed i*16 twice.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_cse_copies: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/c.c" <<'EOF2'
#include <stdio.h>

struct pt { long x, y; };

long __attribute__((noinline)) norm2(const struct pt *v, int n)
{
	long t = 0;
	int i;

	for (i = 0; i < n; ++i)
		t += v[i].x * v[i].x + v[i].y * v[i].y;
	return t;
}

int __attribute__((noinline)) shuffle(int *a, int n)
{
	int i;
	int j = 0;

	for (i = 0; i < n; ++i) {
		int k = i;	/* a copy, then i written: k keeps the old */

		i = i + 0;
		a[k] += a[i] + j;
		j = k;
	}
	return j;
}

int main(void)
{
	struct pt v[5] = {{1, 2}, {3, 4}, {5, 6}, {7, 8}, {9, 10}};
	int a[6] = {1, 2, 3, 4, 5, 6};
	int r = shuffle(a, 6);

	printf("%ld %d %d %d\n", norm2(v, 5), r, a[0], a[5]);
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/c.c"
want=$("$CC_TMP/g")
for o in O1 O2; do
	"$CC_BIN" -m64 -$o -o "$CC_TMP/c" "$CC_TMP/c.c"
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_cse_copies: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/c.s" "$CC_TMP/c.c"
n=$(awk '/^norm2:/{f=1} /^\.size norm2/{f=0} f' "$CC_TMP/c.s" | grep -cE 'imul[q]? +\$16|shl[q]? +\$4' || true)
if [ "$n" -gt 1 ]; then
	echo "run_cse_copies: v[i]'s offset is computed $n times in norm2's loop"
	exit 1
fi
echo "run_cse_copies: ok"
