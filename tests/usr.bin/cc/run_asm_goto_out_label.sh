#!/bin/sh
# An asm goto's outputs hold where it jumps to one of its labels, not only
# where it falls through, as gcc and clang make them: each label is
# reached through an edge that stores the outputs back to their lvalues
# (lower_asm_stmt()), and through a stub that stores the asm's registers
# and pops what it saved before going on (emit_inline_asm()).  The label
# saw the value from before the asm.  Checked through the register
# allocator and the old emitter (CC_NO_MIR=1), against gcc.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_goto_out_label: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/l.c" <<'EOF2'
#include <stdio.h>

static int __attribute__((noinline)) one(int k) {
    int out = 1;

    asm goto("movl $5, %0\n\ttestl %1, %1\n\tjnz %l2" : "=&r"(out) : "r"(k) : "cc" : yes);
    return out * 10;
yes:
    return out * 100;
}

/* two outputs, two labels, one of them also reached without the asm */
static long __attribute__((noinline)) two(int k) {
    int a = -1;
    int b = -2;

    if (k > 100)
        goto big;
    asm goto("movl %2, %0\n\tleal 7(%2), %1\n\tcmpl $1, %2\n\tje %l3\n\tcmpl $2, %2\n\tje %l4"
             : "=&r"(a), "=&r"(b) : "r"(k) : "cc" : small, big);
    return a * 1000L + b;
small:
    return 1000000L + a * 1000L + b;
big:
    return 2000000L + a * 1000L + b;
}

int main(void) {
    printf("%d %d\n", one(0), one(1));
    printf("%ld %ld %ld %ld\n", two(0), two(1), two(2), two(200));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/lg" "$CC_TMP/l.c"
	want=$("$CC_TMP/lg")
	for path in mir old; do
		for o in O0 O2; do
			if [ $path = old ]; then
				CC_NO_MIR=1 "$CC_BIN" -m$m -$o -o "$CC_TMP/lc" "$CC_TMP/l.c"
			else
				"$CC_BIN" -m$m -$o -o "$CC_TMP/lc" "$CC_TMP/l.c"
			fi
			got=$("$CC_TMP/lc")
			if [ "$got" != "$want" ]; then
				echo "run_asm_goto_out_label: -m$m -$o ($path) prints"
				echo "$got"
				echo "where gcc's prints"
				echo "$want"
				exit 1
			fi
		done
	done
done
echo "run_asm_goto_out_label: ok"
