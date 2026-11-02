#!/bin/sh
# Two asm operands that must be in the one register may share it, when
# optimizing, if they are the same expression -- one value: `"+a"(r) :
# "a"(r)`, two inputs `"a"(x), "a"(x)`, the same global or `*p` -- as gcc
# allows them from -O1 on (and refuses them at -O0).  cc refused them at
# every level.  An early-clobber output and two outputs may not share
# the register however alike, as gcc has it.  cc's errors are gcc's at
# each level, and the code it accepts runs as gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_shared_operand: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD
cd "$CC_TMP"

n=0
check() {
	n=$((n + 1))
	printf '%s\n' "$1" >sh$n.c
	for m in -m64 -m32; do
		gcc $m -c -o /dev/null sh$n.c 2>/dev/null || continue
		for o in O0 O1 O2 Os; do
			g=$(gcc $m -$o -S -o /dev/null sh$n.c 2>&1 | grep -m1 'error:' | sed 's/^.*error: //' || true)
			c=$("$CC_BIN" $m -$o -S -o /dev/null sh$n.c 2>&1 | grep -m1 'error:' | sed 's/^.*error: //' || true)
			if [ "$c" != "$g" ]; then
				echo "run_asm_shared_operand: $m -$o: $1"
				echo "  cc: [${c:-accepted}], gcc: [${g:-accepted}]"
				exit 1
			fi
		done
	done
}

check 'int z(int x) { int r = x; __asm__("" : "+a"(r) : "a"(r)); return r; }'
check 'int g; int z(void) { __asm__("" : "+a"(g) : "a"(g)); return g; }'
check 'int z(int *p) { __asm__("" : "+a"(*p) : "a"(*p)); return *p; }'
check 'int z(int x) { __asm__("" : : "a"(x), "a"(x)); return 0; }'
check 'int z(int x) { int r = x; __asm__("" : "=&a"(r) : "a"(r)); return r; }'
check 'int z(void) { int r; __asm__("" : "=a"(r), "=a"(r)); return r; }'
check 'int z(int x, int y) { __asm__("" : : "a"(x), "a"(y)); return 0; }'

# and what is accepted runs as gcc's
cat >run.c <<'EOF2'
#include <stdio.h>

int g = 5;

__attribute__((noinline)) int twice(int x)
{
	int r = x;

	__asm__("addl %1, %0" : "+a"(r) : "a"(r));
	return r;
}

__attribute__((noinline)) int via(int *p)
{
	__asm__("addl %1, %0" : "+a"(*p) : "a"(*p));
	return *p;
}

int main(void)
{
	int v = 7;

	__asm__("addl %1, %0" : "+a"(g) : "a"(g));
	printf("%d %d %d\n", twice(21), via(&v), g);
	return 0;
}
EOF2
for m in -m64 -m32; do
	gcc $m -O2 -o g run.c 2>/dev/null || continue
	want=$(./g)
	for o in O1 O2; do
		"$CC_BIN" $m -$o -c -o c.o run.c
		gcc $m -no-pie -o c c.o
		if [ "$(./c)" != "$want" ]; then
			echo "run_asm_shared_operand: $m -$o prints [$(./c)], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_asm_shared_operand: ok"
