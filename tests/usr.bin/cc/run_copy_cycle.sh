#!/bin/sh
# A variable initialized from itself, gcc's idiom against warnings about
# one read unset -- Linux's paravirt PVOP_CALL_ARGS, `unsigned long __eax
# = __eax, __edx = __edx, ...` -- made copies that copy each other round,
# and copy propagation followed them for ever: every paravirt caller in
# the i386 vDSO and kernel hung the compiler at -O1 and above.  Each
# compiles, in seconds, -m64 and -m32, -O0, -O1 and -O2, on both code
# paths; and the program, whose self-initialized variables are assigned
# before they are read, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_copy_cycle: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
unsigned long sink;
static inline void pv_call(unsigned long arg)
{
	unsigned long __edi = __edi, __esi = __esi, __edx = __edx, __ecx = __ecx, __eax = __eax;
	asm volatile("movl $7, %%eax" : "=D"(__edi), "=S"(__esi), "=d"(__edx), "=c"(__ecx), "=a"(__eax) : "D"(arg)
		     : "memory");
	sink += __eax & 0xff;
}
__attribute__((noinline)) void flush_one(unsigned long v) { pv_call(v); }
__attribute__((noinline)) unsigned long flush_all(void)
{
	unsigned long a = a, b = b;
	a = 3;
	b = a + 4;
	pv_call(0);
	return a * b;
}
int main(void)
{
	unsigned long r;

	flush_one(1);
	r = flush_all();
	printf("%lu %lu\n", sink, r);
	return 0;
}
EOF
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O1 O2; do
			if ! timeout 60 $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_copy_cycle: -m$m $path -$o did not compile within a minute: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_copy_cycle: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_copy_cycle: ok"
