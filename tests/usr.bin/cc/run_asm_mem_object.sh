#!/bin/sh
# An asm's "m" input of an aggregate, or of a global, is the object
# itself, as gcc has it: `"m"(gp)` of a struct gave the asm a stack slot
# holding the struct's address -- the address of an aggregate being its
# value -- so Substrate's `lgdt %0` loaded a descriptor of garbage and
# the kernel triple-faulted before printing anything.  A global scalar
# was a copy of its value on the stack; it is the global now (`movl
# gs`), and an array or a member of a structure the same.  Results are
# gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_mem_object: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

struct p { unsigned short l; unsigned b; } __attribute__((packed));
static struct p gp = { 0x1234, 0x56789abc };
unsigned gs = 77;
unsigned arr[4] = { 5, 6, 7, 8 };
struct w { int a; struct p in; } gw = { 1, { 3, 0xabcdef } };

unsigned __attribute__((noinline)) f(void)
{
	unsigned r;

	__asm__ volatile("movl 2+%1, %0" : "=r"(r) : "m"(gp));
	return r;
}

unsigned __attribute__((noinline)) g(void)
{
	unsigned r;

	__asm__ volatile("movl %1, %0" : "=r"(r) : "m"(gs));
	return r;
}

unsigned __attribute__((noinline)) h(void)
{
	struct p lp = { 1, 0x11223344 };
	unsigned r;

	__asm__ volatile("movl 2+%1, %0" : "=r"(r) : "m"(lp));
	return r;
}

unsigned __attribute__((noinline)) a2(void)
{
	unsigned r;

	__asm__ volatile("movl 8+%1, %0" : "=r"(r) : "m"(arr));
	return r;
}

unsigned __attribute__((noinline)) mw(void)
{
	unsigned r;

	__asm__ volatile("movl 2+%1, %0" : "=r"(r) : "m"(gw.in));
	return r;
}

int main(void)
{
	unsigned x = f(), y = g(), z = h(), u = a2(), v = mw();

	printf("%x %u %x %u %x\n", x, y, z, u, v);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/m.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/m.c"
		got=$("$CC_TMP/c") || { echo "run_asm_mem_object: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_asm_mem_object: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/m.s" "$CC_TMP/m.c"
	for sym in gp gs arr; do
		if ! grep -qE "^	movl ([0-9]+\\+)?$sym(\\(%rip\\))?, %" "$CC_TMP/m.s"; then
			echo "run_asm_mem_object: -m$m: the asm does not read $sym itself"
			exit 1
		fi
	done
done
echo "run_asm_mem_object: ok"
