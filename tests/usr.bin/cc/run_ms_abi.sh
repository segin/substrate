#!/bin/sh
# ms_abi: on x86-64, a function type with gcc's ms_abi attribute -- on the
# function, a pointer, a typedef or a structure member -- uses the
# Microsoft x64 convention: arguments by position in %rcx, %rdx, %r8, %r9
# (or %xmm0-3), the rest on the stack past a 32-byte shadow area the caller
# leaves, and the function keeps %rdi, %rsi and %xmm6-15 for its caller.
# UEFI's convention, Linux's __efiapi: cc accepted the attribute and
# ignored it, so the kernel's EFI stub read its system table from the wrong
# register and took #GP calling the firmware, and the kernel never booted
# from GRUB on UEFI.
#
# gcc is the judge: cc calls gcc's ms_abi functions, gcc calls cc's, and
# each prints what gcc on both sides prints; at -O0 and -O2, through the
# allocator and the old emitter (CC_NO_MIR).  An assembler harness puts
# sentinels in %rdi, %rsi and %xmm6-15, calls a cc ms_abi function that
# calls a System V one that clobbers them, and checks they come back.
# Under -mno-sse, as Linux builds the stub, the allocator takes the
# function and keeps %rdi and %rsi.  What this compiler does not pass by
# that convention -- a structure, a floating variadic argument, a variadic
# definition -- is an error, not wrong code.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_ms_abi: skip (no gcc)"
	exit 0
fi
T=$CC_TMP

# the ms_abi functions: built by gcc or by cc
cat >"$T/ms.c" <<'EOF2'
#define MS __attribute__((ms_abi))
long sysv_clobber(long x);

MS long f6(long a, long b, long c, long d, long e, long f)
{
    return a + 10 * b + 100 * c + 1000 * d + 10000 * e + 100000 * f;
}

MS long f8(int a, long b, short c, unsigned d, long e, char f, long g, long h)
{
    return a + 2 * b + 3 * c + 4 * (long)d + 5 * e + 6 * f + 7 * g + 8 * h;
}

#ifndef NO_FP
MS double fd(int a, double b, long c, double d, double e, float f)
{
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6;
}

MS float ff(float a, float b)
{
    return a * b + 0.5f;
}
#endif

MS long f0(void)
{
    return 77;
}

MS void *ptr(void *p, long k)
{
    return (char *)p + k;
}

/* a System V call in between, which may clobber what the caller keeps */
MS long keep(long a, long b, long c, long d)
{
    return sysv_clobber(a + b + c + d) + a;
}
EOF2

# the callers: built by gcc or by cc
cat >"$T/callers.c" <<'EOF2'
#include <stdio.h>
#define MS __attribute__((ms_abi))

MS long f6(long, long, long, long, long, long);
MS long f8(int, long, short, unsigned, long, char, long, long);
MS long f0(void);
MS void *ptr(void *, long);
MS long fv(int, ...);
#ifndef NO_FP
MS double fd(int, double, long, double, double, float);
MS float ff(float, float);
#endif

/* as Linux declares the firmware's tables: (__efiapi *member)(...) */
struct ops {
    long (MS *six)(long, long, long, long, long, long);
    long (MS *eight)(int, long, short, unsigned, long, char, long, long);
    long (MS *none)(void);
    long (MS *var)(int, ...);
};
typedef long (MS *six_t)(long, long, long, long, long, long);

static char buf[8];

int main(void)
{
    struct ops o = {f6, f8, f0, fv};
    volatile six_t t = f6;

    printf("%ld %ld %ld\n", f6(1, 2, 3, 4, 5, 6), o.six(6, 5, 4, 3, 2, 1), t(9, 8, 7, 6, 5, 4));
    printf("%ld %ld\n", f8(1, -2, 3, 4u, 5, 6, 7, 8), o.eight(-1, 2, -3, 40u, -5, 60, -7, 80));
    printf("%ld %ld\n", f0(), o.none());
    printf("%d\n", (int)((char *)ptr(buf, 5) - buf));
    printf("%ld %ld\n", fv(3, 1L, 2L, 3L), o.var(7, 1L, 2L, 3L, 4L, 5L, 6L, 7L));
#ifndef NO_FP
    printf("%.4f %.4f\n", fd(1, 2.5, 3, 4.25, 5.5, 6.75f), (double)ff(1.5f, 3.0f));
#endif
    return 0;
}
EOF2

# gcc's variadic ms_abi function (cc defines none: see the errors below), and the System V clobberer
cat >"$T/gcc_side.c" <<'EOF2'
#define MS __attribute__((ms_abi))

MS long fv(int n, ...)
{
    __builtin_ms_va_list ap;
    long t = 0;
    int i;

    __builtin_ms_va_start(ap, n);
    for (i = 0; i < n; ++i) {
        t = t * 3 + __builtin_va_arg(ap, long);
    }
    __builtin_ms_va_end(ap);
    return t;
}

long sysv_clobber(long x)
{
#ifdef NO_FP
    __asm__ volatile("movq $-1, %%rdi\n\tmovq $-1, %%rsi" ::: "rdi", "rsi");
#else
    __asm__ volatile("movq $-1, %%rdi\n\tmovq $-1, %%rsi\n\t"
                     "pcmpeqd %%xmm6, %%xmm6\n\tpcmpeqd %%xmm7, %%xmm7\n\tpcmpeqd %%xmm8, %%xmm8\n\t"
                     "pcmpeqd %%xmm9, %%xmm9\n\tpcmpeqd %%xmm10, %%xmm10\n\tpcmpeqd %%xmm11, %%xmm11\n\t"
                     "pcmpeqd %%xmm12, %%xmm12\n\tpcmpeqd %%xmm13, %%xmm13\n\tpcmpeqd %%xmm14, %%xmm14\n\t"
                     "pcmpeqd %%xmm15, %%xmm15"
                     ::: "rdi", "rsi", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14",
                         "xmm15");
#endif
    return x;
}
EOF2

# keep_check(fn, xmm): fn(1, 2, 3, 4) as a Microsoft x64 caller calls it, sentinels in %rdi, %rsi and (xmm)
# %xmm6-15; *result gets fn's result, the return a bit for each register that did not come back
cat >"$T/harness.S" <<'EOF2'
	.text
	.globl	keep_check
	.type	keep_check, @function
keep_check:
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	movq	%rdi, %rbx
	movl	%esi, %r12d
	movq	%rdx, %r13
	subq	$32, %rsp
	movabsq	$0x1111111111111111, %rdi
	movabsq	$0x2222222222222222, %rsi
	testl	%r12d, %r12d
	jz	1f
	.irp	k, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	movabsq	$0x5a5a5a5a5a5a5a00 + \k, %rax
	movq	%rax, %xmm\k
	.endr
1:	movl	$1, %ecx
	movl	$2, %edx
	movl	$3, %r8d
	movl	$4, %r9d
	call	*%rbx
	addq	$32, %rsp
	movq	%rax, (%r13)
	xorl	%eax, %eax
	movabsq	$0x1111111111111111, %rcx
	cmpq	%rcx, %rdi
	je	2f
	orl	$1, %eax
2:	movabsq	$0x2222222222222222, %rcx
	cmpq	%rcx, %rsi
	je	3f
	orl	$2, %eax
3:	testl	%r12d, %r12d
	jz	9f
	.irp	k, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	movq	%xmm\k, %rcx
	movabsq	$0x5a5a5a5a5a5a5a00 + \k, %rdx
	cmpq	%rdx, %rcx
	je	4f
	orl	$(1 << (\k - 4)), %eax
4:
	.endr
9:	popq	%r13
	popq	%r12
	popq	%rbx
	ret
	.size	keep_check, .-keep_check
	.section .note.GNU-stack,"",@progbits
EOF2

cat >"$T/keep_main.c" <<'EOF2'
#include <stdio.h>
#define MS __attribute__((ms_abi))
MS long keep(long, long, long, long);
int keep_check(void *fn, int xmm, long *result);

int main(void)
{
    long r = 0;
    int lost = keep_check((void *)keep, XMM, &r);

    printf("lost %#x result %ld\n", lost, r);
    return lost != 0;
}
EOF2

fail() {
	echo "run_ms_abi: $*"
	exit 1
}

gcc -O1 -c -o "$T/gcc_side.o" "$T/gcc_side.c"
gcc -O1 -o "$T/want" "$T/ms.c" "$T/callers.c" "$T/gcc_side.o"
want=$("$T/want")

for o in O0 O2; do
	for path in mir legacy; do
		run=env
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		# cc calls gcc's ms_abi functions
		gcc -O1 -c -o "$T/ms_gcc.o" "$T/ms.c"
		$run "$CC_BIN" -m64 -$o -c -o "$T/callers_cc.o" "$T/callers.c" || fail "-$o $path: cc rejects the callers"
		gcc -o "$T/a" "$T/callers_cc.o" "$T/ms_gcc.o" "$T/gcc_side.o"
		got=$("$T/a") || fail "-$o $path: cc calling gcc's ms_abi functions fails"
		[ "$got" = "$want" ] || fail "-$o $path: cc calling gcc's ms_abi functions prints [$got], gcc's [$want]"
		# gcc calls cc's
		$run "$CC_BIN" -m64 -$o -c -o "$T/ms_cc.o" "$T/ms.c" || fail "-$o $path: cc rejects the ms_abi functions"
		gcc -O1 -c -o "$T/callers_gcc.o" "$T/callers.c"
		gcc -o "$T/b" "$T/callers_gcc.o" "$T/ms_cc.o" "$T/gcc_side.o"
		got=$("$T/b") || fail "-$o $path: gcc calling cc's ms_abi functions fails"
		[ "$got" = "$want" ] || fail "-$o $path: gcc calling cc's ms_abi functions prints [$got], gcc's [$want]"
		# cc's keeps its caller's %rdi, %rsi and %xmm6-15 across a System V call
		gcc -O1 -DXMM=1 -o "$T/k" "$T/keep_main.c" "$T/harness.S" "$T/ms_cc.o" "$T/gcc_side.o"
		got=$("$T/k") || fail "-$o $path: cc's ms_abi function loses its caller's registers: [$got]"
		[ "$got" = "lost 0 result 11" ] || fail "-$o $path: keep: [$got]"
	done
	# -mno-sse, as Linux builds its EFI stub: the allocator takes every ms_abi function, keeping %rdi and %rsi
	gcc -O1 -DNO_FP -c -o "$T/gcc_side_nofp.o" "$T/gcc_side.c"
	CC_MIR_STATS=1 "$CC_BIN" -m64 -$o -mno-sse -DNO_FP -c -o "$T/ms_nosse.o" "$T/ms.c" 2>"$T/stats" ||
		fail "-$o -mno-sse: cc rejects the ms_abi functions"
	if grep -q "not taken" "$T/stats"; then
		fail "-$o -mno-sse: an ms_abi function does not go through the allocator: $(cat "$T/stats")"
	fi
	if objdump -d "$T/ms_nosse.o" | grep -q "%xmm"; then
		fail "-$o -mno-sse: an SSE register is touched"
	fi
	gcc -O1 -DXMM=0 -o "$T/kn" "$T/keep_main.c" "$T/harness.S" "$T/ms_nosse.o" "$T/gcc_side_nofp.o"
	got=$("$T/kn") || fail "-$o -mno-sse: cc's ms_abi function loses its caller's registers: [$got]"
	[ "$got" = "lost 0 result 11" ] || fail "-$o -mno-sse: keep: [$got]"
done

# __has_attribute knows it
printf '#if !__has_attribute(ms_abi) || !__has_attribute(sysv_abi)\n#error no\n#endif\nint x;\n' >"$T/has.c"
"$CC_BIN" -m64 -c -o "$T/has.o" "$T/has.c" || fail "__has_attribute(ms_abi) is 0"

# what is not passed so is an error, never wrong code
reject() {
	src="$1"
	want_msg="$2"
	printf '%s\n' "$src" >"$T/bad.c"
	if "$CC_BIN" -m64 -c -o "$T/bad.o" "$T/bad.c" 2>"$T/bad.err"; then
		fail "accepted: $src"
	fi
	grep -q "$want_msg" "$T/bad.err" || fail "for [$src] said [$(cat "$T/bad.err")], not [$want_msg]"
}
reject 'struct s { long a; }; __attribute__((ms_abi)) long g(struct s x) { return x.a; }' \
	'an ms_abi function with a parameter of this type is not supported'
reject '__attribute__((ms_abi)) long g(int n, ...) { return n; }' 'a variadic ms_abi function is not supported'
reject 'struct s { long a; }; long (__attribute__((ms_abi)) *p)(struct s); long h(struct s v) { return p(v); }' \
	'a call of an ms_abi function with an argument of this type is not supported'
reject '__attribute__((ms_abi)) long fv(int, ...); long h(void) { return fv(1, 2.0); }' \
	'a call of an ms_abi function with a floating variadic argument is not supported'

echo "run_ms_abi: ok"
