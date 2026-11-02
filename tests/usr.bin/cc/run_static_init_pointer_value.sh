#!/bin/sh
# In a static initializer the operand of `->` and of `*` is a pointer's
# value: `(&wm)->bits`, Linux's cpumask_bits(&watchdog_cpumask) in
# kernel/watchdog.c, was refused ("unsupported global initializer"), and
# `&p->x` of a pointer variable p was taken for the address of p plus the
# member's offset, where gcc refuses it ("initializer element is not
# constant").  Arrows and dereferences of `&object`, arrays and pointer
# arithmetic on them give gcc's addresses, -m64 and -m32, -O0 and -O2, on
# both code paths; `&p->x` is refused.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_static_init_pointer_value: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
struct cm { unsigned long bits[2]; };
struct cm wm;
unsigned long *b1 = (&wm)->bits;
unsigned long *b2 = ((&wm)->bits) + 1;
struct o { int x; struct cm c[2]; } ob;
unsigned long *b3 = (&ob.c[1])->bits;
unsigned long *b4 = (&ob)->c[1].bits;
struct o arr[3];
int *b5 = &arr->x;
int *b6 = &(arr + 2)->x;
unsigned long *b7 = &(*(&ob)).c[0].bits[1];
int iv[4];
int *b8 = &*(iv + 3);
int (*b9)(const char *, ...) = &printf;
const char *s = "hello";
int main(void)
{
	printf("%d %d %d %d %d %d %d %d %d\n", (int)(b1 - wm.bits), (int)(b2 - wm.bits), (int)((char *)b3 - (char *)&ob),
	       (int)((char *)b4 - (char *)&ob), (int)(b5 - &arr[0].x), (int)((char *)b6 - (char *)arr),
	       (int)((char *)b7 - (char *)&ob), (int)(b8 - iv), b9 == &printf);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_static_init_pointer_value: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_static_init_pointer_value: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done

printf 'struct s { int a, x; };\nstruct s *p;\nint *q = &p->x;\n' >"$CC_TMP/n.c"
if gcc -c -o "$CC_TMP/n.o" "$CC_TMP/n.c" 2>/dev/null; then
	echo "run_static_init_pointer_value: gcc took &p->x; the test is wrong"
	exit 1
fi
if "$CC_BIN" -c -o "$CC_TMP/n.o" "$CC_TMP/n.c" 2>/dev/null; then
	echo "run_static_init_pointer_value: &p->x of a pointer variable is taken; gcc refuses it"
	exit 1
fi
echo "run_static_init_pointer_value: ok"
