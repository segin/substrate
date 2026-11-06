#!/bin/sh
# __builtin_object_size() is a size_t, unsigned, to typeof and
# __auto_type as to everything else: the parser had no type for it, so
# typeof() of a call was "cannot determine the type of typeof's
# operand" and __auto_type made it an int.  Linux's fortified memcpy()
# takes min(__builtin_object_size(p, 0), len), whose BUILD_BUG_ON asks
# whether a signed operand is known not to be negative -- of an unsigned
# one it never asks -- and net/bluetooth/mgmt.c's set_mesh_sync() kept a
# call of an undefined __compiletime_assert_N, the bluetooth module
# unloadable.  What gcc folds leaves no reference; -O2, -m64 and -m32,
# both code paths, every function through MIR; and the program, run,
# prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_object_size_type: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function
cat >"$CC_TMP/u.c" <<'EOF2'
extern void signed_bad(void), size_bad(void);
#define IS_SIGNED(x) ((typeof(x))(-1) < (typeof(x))1)
#define NONNEG(x) (__builtin_constant_p((long long)(x) >= 0) && ((long long)(x) >= 0))
/* as Linux's min(): a signed operand must be known not to be negative */
#define CHECKED_MIN(a, b)                                                   \
	({                                                                  \
		__auto_type _a = (a);                                       \
		__auto_type _b = (b);                                       \
		if (IS_SIGNED(_a) && !NONNEG(_a))                           \
			signed_bad();                                       \
		_a < _b ? _a : _b;                                          \
	})

/* (called with a pointer whose object neither compiler knows) */
__attribute__((noinline)) unsigned long copy_len(char *cp, unsigned long len)
{
	typeof(__builtin_object_size(cp, 1)) whole = __builtin_object_size(cp, 0);

	if (IS_SIGNED(whole) || sizeof(whole) != sizeof(void *))
		size_bad();
	return CHECKED_MIN(__builtin_object_size(cp, 0), len);
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads;
void signed_bad(void) { bads++; }
void size_bad(void) { bads++; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	char buf[24];
	unsigned long a = copy_len(buf, 7);
	unsigned long b = copy_len(buf + 4, 70);

	printf("%lu %lu bads %d\n", a, b, bads);
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_bad$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$(undefined "$CC_TMP/g.o")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env CC_MIR_STATS=2"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_object_size_type: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_object_size_type: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ -n "$want" ]; then
			echo "run_object_size_type: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_object_size_type: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_object_size_type: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_object_size_type: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_object_size_type: ok"
