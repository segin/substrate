#!/bin/sh
# Copies the allocator need not make: a copy between two registers that
# hold the same value already -- a parameter moved to a callee-saved
# register and back for a call, `mov %rdi, %rbx ... mov %rbx, %rdi` --
# goes; and a value given the register of what a chain of copies leads to
# (r = r + k through the copies an int's extensions are) has it, over
# what longer chains of copies pass on: `lea 2(%r13), %rax; mov %rax,
# %r13` was the add of a variable updated.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_copy_coalesce: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/k.c" <<'EOF2'
#include <stdio.h>

long __attribute__((noinline)) sink(long a, long b) { return a * 3 + b; }
_Bool __attribute__((noinline)) yes(int x) { return x > 3; }

/* a and b kept across calls, and passed again as they came */
long __attribute__((noinline)) twice(long a, long b)
{
	long r = sink(a, b);

	return r + sink(a, b);
}

int __attribute__((noinline)) acc(int a, short s)
{
	int r = 0;

	if (yes(a))
		r += 1;
	if (a)
		r += 2;
	if (s)
		r += 4;
	if (a > 2 && (s < 0 || a < 9))
		r += 8;
	return r;
}

int main(void)
{
	printf("%ld %d %d %d\n", twice(5, 7), acc(9, 0), acc(0, 3), acc(5, (short)-2));
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/k.c"
want=$("$CC_TMP/g")
for o in O1 O2; do
	"$CC_BIN" -m64 -$o -o "$CC_TMP/c" "$CC_TMP/k.c"
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_copy_coalesce: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/k.s" "$CC_TMP/k.c"
awk '/^acc:/{x=1} /^\.size acc,/{x=0} x' "$CC_TMP/k.s" >"$CC_TMP/acc.s"
if awk '{ if (prev ~ /^\tleaq -?[0-9]+\(%r[a-z0-9]+\), %r[a-z0-9]+$/) { n = split(prev, a, ", "); split(a[1], b, "("); src = b[2]; sub(/\)/, "", src); if ($0 == "\tmovq " a[2] ", " src) { print prev; print; bad = 1 } } prev = $0 } END { exit !bad }' "$CC_TMP/acc.s"; then
	echo "run_copy_coalesce: acc() adds into a copy of the variable"
	exit 1
fi
awk '/^twice:/{x=1} /^\.size twice,/{x=0} x' "$CC_TMP/k.s" >"$CC_TMP/twice.s"
# before its first call: no parameter copied to a register and back
if awk '/call/{exit} /^\tmovq %r(di|si), %r[a-z0-9]+$/ { split($2 $3, a, ","); from[$3] = $2 } /^\tmovq %r[a-z0-9]+, %r(di|si)$/ { s = $2; sub(/,$/, "", s); d = $3; if ((s) in from && from[s] == d ",") { print; bad = 1 } } END { exit !bad }' "$CC_TMP/twice.s"; then
	echo "run_copy_coalesce: twice() copies a parameter back to where it is"
	exit 1
fi
echo "run_copy_coalesce: ok"
