#!/bin/sh
# An i386 function taking a small structure by value under -mregparm=3
# -- its words in %eax, %edx, %ecx, as Linux's kernel_cap_t and ktime
# helpers take them -- goes through the register allocator: the words
# stored into a frame object at entry, whose address is the parameter.
# Such a function used to go to the old emitter, a stack slot for every
# value.  CC_MIR_STATS says which path took each function; the library,
# called by gcc's build of the program (printf, variadic, takes no
# regparm), prints what an all-gcc build prints.  Needs a host gcc that
# builds -m32 programs; skipped, with a note, if not.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_i386_regparm_struct_regs: skip (gcc cannot build -m32 programs)"
	exit 0
fi

cat >"$CC_TMP/lib.c" <<'EOF2'
typedef struct { unsigned int val[2]; } cap_t;
typedef struct { unsigned int a, b, c; } three_t;

cap_t cap_combine(const cap_t a, const cap_t b) {
    cap_t r;

    r.val[0] = a.val[0] | b.val[0];
    r.val[1] = a.val[1] | b.val[1];
    return r;
}

int cap_issubset(int k, const cap_t a) {
    return (int)(a.val[0] + a.val[1]) * k;
}

unsigned int sum3(three_t t) {
    return t.a * 100 + t.b * 10 + t.c;
}
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
#include <stdio.h>

typedef struct { unsigned int val[2]; } cap_t;
typedef struct { unsigned int a, b, c; } three_t;
cap_t cap_combine(const cap_t a, const cap_t b);
int cap_issubset(int k, const cap_t a);
unsigned int sum3(three_t t);

int main(void) {
    cap_t x = {{0x11, 0x200}};
    cap_t y = {{0x22, 0x400}};
    three_t t = {1, 2, 3};
    cap_t z = cap_combine(x, y);

    printf("%x %x %d %u\n", z.val[0], z.val[1], cap_issubset(3, z), sum3(t));
    return 0;
}
EOF2
F="-m32 -mregparm=3 -freg-struct-return"
# shellcheck disable=SC2086
gcc $F -O1 -fno-pie -no-pie -o "$CC_TMP/ref" "$CC_TMP/main.c" "$CC_TMP/lib.c"
want=$("$CC_TMP/ref")
# shellcheck disable=SC2086
gcc $F -O1 -fno-pie -c -o "$CC_TMP/main.o" "$CC_TMP/main.c"
for o in O0 O2; do
	# shellcheck disable=SC2086
	CC_MIR_STATS=1 "$CC_BIN" $F -$o -c -o "$CC_TMP/lib.o" "$CC_TMP/lib.c" 2>"$CC_TMP/stats"
	if grep "^mir: .*not taken" "$CC_TMP/stats"; then
		echo "run_i386_regparm_struct_regs: -$o: the above do not go through the allocator"
		exit 1
	fi
	gcc -m32 -no-pie -o "$CC_TMP/mixed" "$CC_TMP/main.o" "$CC_TMP/lib.o"
	got=$("$CC_TMP/mixed")
	if [ "$got" != "$want" ]; then
		echo "run_i386_regparm_struct_regs: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
echo "run_i386_regparm_struct_regs: ok"
