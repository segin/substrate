#!/bin/sh
# A conditional one of whose arms is a null pointer constant has the
# other arm's type (C11 6.5.15p6), and a null pointer constant is an
# integer constant expression of value 0, or one cast to void *: not
# only a literal 0.  Linux's __is_constexpr(x) is
# `sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8))`,
# and cc took the conditional's type for void * whatever x was:
# static_assert(struct_size_t(...) <= KMALLOC_MAX_SIZE) in
# security/landlock/domain.h was no constant (the Arch config).  Where x
# is not constant the cast is no null pointer constant, and the type is
# void *.  Results are gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_null_pointer_constant_cond: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/n.c" <<'EOF2'
#include <stddef.h>
#include <stdio.h>

#define __is_constexpr(x) (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))
#define __BUILD_BUG_ON_ZERO_MSG(e, msg) ((int)sizeof(struct { _Static_assert(!(e), msg); }))
#define __is_array(a) (!__builtin_types_compatible_p(typeof(a), typeof(&(a)[0])))
#define __must_be_array(a) __BUILD_BUG_ON_ZERO_MSG(!__is_array(a), "must be array")
#define flex_array_size(p, member, count) \
	__builtin_choose_expr(__is_constexpr(count), (count) * sizeof(*(p)->member) + __must_be_array((p)->member), \
			      (size_t)-1)
#define struct_size_t(type, member, count) \
	__builtin_choose_expr(__is_constexpr(count), sizeof(type) + flex_array_size((type *)NULL, member, count), \
			      (size_t)-1)

enum { LEN = 4107 };
struct details {
	void *pid;
	unsigned uid;
	char comm[16];
	char path[];
};

_Static_assert(__is_constexpr(LEN), "a constant");
_Static_assert(__is_constexpr(LEN * 0 + 3), "a constant expression");
_Static_assert(struct_size_t(struct details, path, LEN) <= (1UL << 22), "fits");
_Static_assert(sizeof(*(1 ? (void *)0 : (int *)8)) == sizeof(int), "the other arm's type");
_Static_assert(sizeof(*(1 ? (long *)8 : (void *)(1 - 1))) == sizeof(long), "either arm");

static int counter = 5;

int main(void)
{
	int v = 7;
	int *p = 1 ? (void *)0 : &v;
	int *q = 0 ? (void *)0 : &v;

	_Static_assert(__is_constexpr(sizeof(v)), "sizeof is constant");
	printf("%d %d %zu %d %d\n", __is_constexpr(LEN), __is_constexpr(counter) ? 1 : 0,
	       struct_size_t(struct details, path, LEN), p == NULL, *q);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/n.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/n.o" "$CC_TMP/n.c" 2>"$CC_TMP/c.err"; then
			echo "run_null_pointer_constant_cond: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/n.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_null_pointer_constant_cond: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_null_pointer_constant_cond: ok"
