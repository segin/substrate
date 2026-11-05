#!/bin/sh
# An array of no bound initialized by an empty list, `T a[] = {}`, has no
# elements (a GNU extension): the empty list completes its type with bound 0.
#
# cc gave it one zero element, the rule for an empty list of a part a
# designator reached into: Linux's EHCI driver, whose list of platform
# drivers is empty on x86, registered one NULL driver at boot and oopsed
# in __platform_driver_register.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_empty_array_init: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/ea.c" <<'EOF2'
#include <stdio.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct drv { int id; };
struct pt { int x, y; };

static struct drv *const drivers[] = {
};
int gi[] = {};
struct pt gs[] = {};
int g2[][2] = {};
int gn[3] = {};
struct flex { int n; int f[]; };
struct flex fa = { 1, {} };
struct flex fb = { 2, { 3, 4 } };
struct flex fc = { 5 };

__attribute__((noinline)) static int register_all(struct drv *const *d, unsigned int n)
{
	unsigned int i;
	int sum = 0;

	for (i = 0; i < n; i++)
		sum += d[i]->id;
	return sum;
}

int main(void)
{
	static int sl[] = {};
	int al[] = {};

	struct pt ap[] = {};

	printf("%zu %zu %zu %zu %zu\n", sizeof(drivers), sizeof(gi), sizeof(gs), sizeof(g2), sizeof(gn));
	printf("%zu %zu %zu %zu %zu\n", sizeof(sl), sizeof(al), sizeof(ap), sizeof((int[]){}),
	       sizeof((struct pt[]){}));
	printf("%zu %d %d %d %d\n", sizeof(fa), fa.n, fb.f[1], fc.n, fb.n);
	printf("%d\n", register_all(drivers, ARRAY_SIZE(drivers)));
	return 0;
}
EOF2
for m in -m64 -m32; do
	if ! printf 'int main(void) { return 0; }\n' | gcc $m -x c - -o "$CC_TMP/probe" 2>/dev/null; then
		echo "run_empty_array_init: $m: skip (no gcc $m)"
		continue
	fi
	gcc $m -w -O2 -o "$CC_TMP/g" "$CC_TMP/ea.c"
	"$CC_TMP/g" >"$CC_TMP/g.out"
	for o in -O0 -O2; do
		"$CC_BIN" $m $o -o "$CC_TMP/c" "$CC_TMP/ea.c"
		"$CC_TMP/c" >"$CC_TMP/c.out"
		if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
			echo "run_empty_array_init: $m $o: differs from gcc (gcc first):"
			diff "$CC_TMP/g.out" "$CC_TMP/c.out" || true
			exit 1
		fi
	done
done
echo "run_empty_array_init: ok"
