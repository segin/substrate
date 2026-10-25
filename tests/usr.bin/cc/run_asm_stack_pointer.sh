#!/bin/sh
# A register variable of the stack pointer in an asm operand is %esp as
# it is.  Linux's ASM_CALL_CONSTRAINT, "+r"(current_stack_pointer) with
# `register unsigned long current_stack_pointer asm("esp")` global, is
# on each asm that calls, the paravirt page-table operations on i386
# among them: its read kept the function from going without a frame
# pointer, and it was copied to a register for the asm.  It is bound to
# %esp itself now, nothing moved, and the function may go without a
# frame pointer.  A local one declared without an initializer starts as
# the register's value, as gcc has it: it was 0, and `xorl %esp, %esp`
# put that in the stack pointer before the asm -- another register's
# variable is still any value, which the selector reads none of: read
# as the register, it sent such a function the old way.  Results are
# gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_stack_pointer: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

#ifdef __x86_64__
#define SP "rsp"
#else
#define SP "esp"
#endif
register unsigned long current_stack_pointer asm(SP);
#define ASM_CALL_CONSTRAINT "+r"(current_stack_pointer)

int calls;
void bump(void) { calls++; }

/* an asm that calls, as Linux's alternative calls do: the stack pointer its operand */
int __attribute__((noinline)) via_global(int *p, int a)
{
	asm volatile("call bump" : ASM_CALL_CONSTRAINT : : "memory", "cc", "eax", "ecx", "edx"
#ifdef __x86_64__
		     , "rsi", "rdi", "r8", "r9", "r10", "r11"
#endif
	);
	return a + *p;
}

int __attribute__((noinline)) via_local(int *p, int a)
{
	register unsigned long sp asm(SP);

	asm volatile("call bump" : "+r"(sp) : : "memory", "cc", "eax", "ecx", "edx"
#ifdef __x86_64__
		     , "rsi", "rdi", "r8", "r9", "r10", "r11"
#endif
	);
	return a + *p;
}

/* another register's variable, its value the asm's: any value before, and the selector's still */
long __attribute__((noinline)) via_other(long a)
{
#ifdef __x86_64__
	register long r asm("r10");
#else
	register long r asm("ecx");
#endif

	asm volatile("mov %1, %0" : "=r"(r) : "r"(a));
	return r + 1;
}

int main(void)
{
	int x = 4;
	int a = via_global(&x, 1);
	int b = via_local(&x, 2);
	long c = via_other(40);

	printf("%d %d %d %ld\n", a, b, calls, c);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/s.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/s.c"
		got=$("$CC_TMP/c") || { echo "run_asm_stack_pointer: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_asm_stack_pointer: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	if CC_MIR_STATS=1 "$CC_BIN" -m$m -O2 -S -o "$CC_TMP/s.s" "$CC_TMP/s.c" 2>&1 | grep 'not taken'; then
		echo "run_asm_stack_pointer: -m$m: a function goes the old way"
		exit 1
	fi
	"$CC_BIN" -m$m -O2 -fomit-frame-pointer -S -o "$CC_TMP/s.s" "$CC_TMP/s.c"
	for fn in via_global via_local; do
		awk -v f="$fn" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/s.s" >"$CC_TMP/fn.s"
		if grep -qE 'mov[lq] %[er]sp, %[er](bp|ax|bx|cx|dx|si|di)|xorl %esp' "$CC_TMP/fn.s"; then
			echo "run_asm_stack_pointer: -m$m: $fn() keeps a frame pointer or copies the stack pointer"
			cat "$CC_TMP/fn.s"
			exit 1
		fi
	done
done
echo "run_asm_stack_pointer: ok"
