#!/bin/sh
# typeof_unqual of an array type unqualifies its elements too: an array
# type's qualifiers are its elements' (C23 6.7.4.1).  cc dropped only the
# array's own, none, so `typeof_unqual(ca) m;` for a const array was
# const and refused an assignment, and Linux's per_cpu_ptr(), which casts
# a per-CPU address to `typeof_unqual(*ptr) *` to reach another CPU's
# copy as a plain pointer, kept a per-CPU array's __seg_gs elements:
# per_cpu(vector_irq, 0)[i] in init_IRQ() added %gs to an address that
# was absolute already, and an SMP x86-64 kernel faulted on its way up.
# Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_typeof_unqual_array: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/a.c" <<'EOF2'
#include <stdio.h>

int main(void)
{
	const int ca[3] = { 1, 2, 3 };
	volatile short vb[2][2] = { { 4, 5 }, { 6, 7 } };
	__typeof_unqual__(ca) m;
	__typeof_unqual__(vb) n;

	m[1] = ca[2] * 7;
	n[1][0] = vb[0][1];
	printf("%d %d %d %d\n", m[1], n[1][0], __builtin_types_compatible_p(__typeof_unqual__(ca), int[3]),
	       __builtin_types_compatible_p(__typeof_unqual__(vb), short[2][2]));
	return 0;
}
EOF2
cat >"$CC_TMP/p.c" <<'EOF2'
typedef void *vec_t[4];
vec_t __seg_gs vector_irq;
unsigned long off;
#define per_cpu_ptr(ptr) ((__typeof_unqual__(*(ptr)) *)((unsigned long)(ptr) + off))
void set(void *d) { (*per_cpu_ptr(&vector_irq))[2] = d; }
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -std=gnu11 -o "$CC_TMP/g" "$CC_TMP/a.c"
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -std=gnu11 -o "$CC_TMP/c" "$CC_TMP/a.c"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_typeof_unqual_array: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -fno-pic -S -o "$CC_TMP/p.s" "$CC_TMP/p.c"
	if grep -qE '%[fg]s:' "$CC_TMP/p.s"; then
		echo "run_typeof_unqual_array: -m$m: per_cpu_ptr()'s plain pointer to a __seg_gs array is used through %gs"
		exit 1
	fi
done
echo "run_typeof_unqual_array: ok"
