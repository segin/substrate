#!/bin/sh
# An asm the selector does not bind -- here one with a 64-bit "=A"
# output, i386's atomic64_inc_return(), which calls through
# ASM_CALL_CONSTRAINT -- is emitted with each operand in a register of
# its own, loaded from its slot before and stored after.  The stack
# pointer's operand ("+r"(current_stack_pointer), {esp} since 3e2d8b0b6)
# was too: %esp copied to a slot, the asm's %esi saved by a push, and
# %esp loaded back from the slot, undoing the push -- the call's return
# address landed on the saved %esi, the pop took a word of the frame,
# and %esp was left four bytes off, so what was pushed after wrote over
# the frame's bottom.  Linux's flush_tlb_mm_range() then flushed for an
# mm that was a return address.  The stack pointer's operand is the
# register as it is, never loaded or stored.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_asm_stack_pointer_pair: skip (no gcc -m32)"
	exit 0
fi

cat >"$CC_TMP/a.c" <<'EOF2'
#include <stdio.h>

register unsigned long current_stack_pointer asm("esp");
#define ASM_CALL_CONSTRAINT "+r"(current_stack_pointer)

typedef struct { long long counter; } atomic64_t;

/* atomic64_inc_return_cx8()'s shape: *%esi incremented, its new value in %edx:%eax */
__asm__(".globl inc_cx8_thunk\n"
	"inc_cx8_thunk:\n"
	"\tmovl (%esi), %eax\n\tmovl 4(%esi), %edx\n"
	"\taddl $1, %eax\n\tadcl $0, %edx\n"
	"\tmovl %eax, (%esi)\n\tmovl %edx, 4(%esi)\n"
	"\tret\n");

static inline long long atomic64_inc_return(atomic64_t *v)
{
	long long a;

	asm volatile("call inc_cx8_thunk" : "=&A"(a), ASM_CALL_CONSTRAINT : "S"(v) : "memory", "ecx");
	return a;
}

atomic64_t gen = { 41 };

long long __attribute__((noinline)) bump(int k, int *out)
{
	int local[6] = { k, k + 1, k + 2, k + 3, k + 4, k + 5 };
	long long r = atomic64_inc_return(&gen);

	*out = local[0] + local[1] + local[2] + local[3] + local[4] + local[5];
	return r;
}

int main(void)
{
	int s;
	long long r = bump(1, &s);

	printf("%lld %d %lld\n", r, s, gen.counter);
	return 0;
}
EOF2
gcc -m32 -O1 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/a.c"
want=$("$CC_TMP/g")
for o in O0 O1 O2 Os; do
	for fp in "" -fomit-frame-pointer; do
		"$CC_BIN" -m32 -$o $fp -o "$CC_TMP/c" "$CC_TMP/a.c"
		got=$("$CC_TMP/c") || { echo "run_asm_stack_pointer_pair: -$o $fp: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_asm_stack_pointer_pair: -$o $fp prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/a.s" "$CC_TMP/a.c"
if awk '/^bump:/{x=1} /^\.size bump,/{x=0} x' "$CC_TMP/a.s" | grep -qE '^	movl [^,]+, %esp$'; then
	echo "run_asm_stack_pointer_pair: bump() loads the stack pointer from a slot"
	exit 1
fi
echo "run_asm_stack_pointer_pair: ok"
