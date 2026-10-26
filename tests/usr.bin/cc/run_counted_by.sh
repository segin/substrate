#!/bin/sh
# gcc's counted_by(m) on a flexible array member or a pointer member, and
# __builtin_counted_by_ref(p->fam): &p->m when fam is counted by m, else a
# null `void *`.  Linux's __flex_counter() is the builtin where
# __has_builtin() says it exists, and its __set_flex_counter() picks by
# _Generic between the counter and a scratch size_t -- so a kernel built
# with CC_HAS_COUNTED_BY (gcc 15.1 on) sets the counter.  Results are
# gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_counted_by: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/cb.c" <<'EOF2'
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#if __has_builtin(__builtin_counted_by_ref)
#define __flex_counter(FAM) __builtin_counted_by_ref(FAM)
#else
#define __flex_counter(FAM) ((void *)NULL)
#endif
#define __set_flex_counter(FAM, COUNT) \
	({ *_Generic(__flex_counter(FAM), void *: &(size_t){ 0 }, default: __flex_counter(FAM)) = (COUNT); })

struct a { unsigned short n; long pad; int arr[] __attribute__((__counted_by__(n))); };
struct b { int k; int arr[]; };
struct c { char *buf __attribute__((counted_by(len))); unsigned len; };
struct d { struct { int x; long cnt; }; char tail[] __attribute__((counted_by(cnt))); };

int main(void)
{
	struct a *x = calloc(1, sizeof(*x) + 8 * sizeof(int));
	struct b *y = calloc(1, sizeof(*y) + 8 * sizeof(int));
	struct d *w = calloc(1, sizeof(*w) + 8);
	struct c z = { 0, 0 };

	__set_flex_counter(x->arr, 7);
	__set_flex_counter(y->arr, 9);
	__set_flex_counter(w->tail, 3);
	*__builtin_counted_by_ref(z.buf) = 5;
	printf("%d %d %u %ld %d %d %d\n", x->n, y->k, z.len, w->cnt, __builtin_counted_by_ref(y->arr) == (void *)0,
	       (int)sizeof(*__builtin_counted_by_ref(x->arr)), (int)sizeof(*__builtin_counted_by_ref(w->tail)));
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/cb.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/cb.c"
		got=$("$CC_TMP/c") || { echo "run_counted_by: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_counted_by: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_counted_by: ok"
