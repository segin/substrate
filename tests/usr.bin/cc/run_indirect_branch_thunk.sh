#!/bin/sh
# -mindirect-branch=thunk-extern makes every indirect call and jump one
# through __x86_indirect_thunk_<reg>, the retpoline the program supplies
# (Linux's, with CONFIG_MITIGATION_RETPOLINE).  cc took the option and
# ignored it: a kernel built with it had no retpolines, and objtool
# warned "indirect call found in MITIGATION_RETPOLINE build" at each.
# The object has no `call *` or `jmp *` -- calls through a pointer, a
# spilled one, a table of them, a switch's jump table -- and the program,
# linked with thunks that count their calls, prints what gcc's does
# with the thunks called.  -mindirect-branch-cs-prefix puts a cs prefix
# before each through %r8..%r15; =keep and no option keep the
# instructions; gcc's thunk and thunk-inline are refused.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objdump >/dev/null 2>&1; then
	echo "run_indirect_branch_thunk: skip (no gcc or objdump)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

typedef long (*op_t)(long);
extern int thunk_hits;
static long twice(long x) { return x * 2; }
static long thrice(long x) { return x * 3; }
long pub(long x) { return x + 1; }
op_t ops[] = { twice, thrice, pub };
long many(long a, long b, long c, long d, long e, long (*g)(long, long, long, long, long)) { return g(a, b, c, d, e) + g(e, d, c, b, a); }
static long sum5(long a, long b, long c, long d, long e) { return a + 2 * b + 3 * c + 4 * d + 5 * e; }

__attribute__((noinline)) long apply(op_t f, long x) { return f(x); }

__attribute__((noinline)) long table(int i, long x) { return ops[i](x); }

__attribute__((noinline)) long spill(op_t f, long x)
{
	long a = x + 1, b = x + 2, c = x + 3, d = x + 4, e = x + 5, g = x + 6, h = x + 7;

	x = f(a + b) + f(c * d) + f(e - g) + h;
	return x + a + b + c + d + e + g + h;
}

__attribute__((noinline)) long sw(int k, long x)
{
	switch (k) {
	case 0: return twice(x) + 11;
	case 1: return thrice(x) - 4;
	case 2: return x ^ 0x55;
	case 3: return x * x;
	case 4: return -x;
	case 5: return x << 3;
	case 6: return x % 7;
	case 7: return x / 3 + 100;
	}
	return 0;
}

int main(void)
{
	long t = 0;
	int i;

	for (i = 0; i < 3; ++i) {
		t += apply(ops[i], i + 5) + table(i, 7 * i + 1) + spill(ops[i], i);
	}
	for (i = 0; i < 9; ++i) {
		t = t * 3 + sw(i, t % 1000 + i);
	}
	t += many(1, 2, 3, 4, 5, sum5);
	printf("%ld %d\n", t, thunk_hits > 0);
	return 0;
}
EOF2
for m in 64 32; do
	if [ $m = 64 ]; then
		regs="rax rbx rcx rdx rsi rdi rbp r8 r9 r10 r11 r12 r13 r14 r15"
		inc="incl thunk_hits(%rip)"
	else
		regs="eax ebx ecx edx esi edi ebp"
		inc="incl thunk_hits"
	fi
	{
		printf '\t.data\n\t.globl thunk_hits\nthunk_hits:\t.long 0\n\t.text\n'
		for r in $regs; do
			printf '\t.globl __x86_indirect_thunk_%s\n__x86_indirect_thunk_%s:\n\t%s\n\tjmp *%%%s\n' "$r" "$r" "$inc" "$r"
		done
	} >"$CC_TMP/thunks.s"
	gcc -m$m -c -o "$CC_TMP/thunks.o" "$CC_TMP/thunks.s" 2>/dev/null || continue
	gcc -m$m -O1 -no-pie -o "$CC_TMP/g" "$CC_TMP/b.c" "$CC_TMP/thunks.o"
	want=$("$CC_TMP/g" | sed 's/ .*//')
	for o in O0 O2; do
		for path in mir legacy; do
			for pic in -fno-pic -fPIC; do
				run="env"
				if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
				if ! $run "$CC_BIN" -m$m -$o $pic -mindirect-branch=thunk-extern -mindirect-branch-register \
					-c -o "$CC_TMP/b.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
					echo "run_indirect_branch_thunk: -m$m -$o $path $pic: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
				bad=$(objdump -d --no-show-raw-insn "$CC_TMP/b.o" | grep -E '(call|jmp)[lq]? +\*' || true)
				if [ -n "$bad" ]; then
					echo "run_indirect_branch_thunk: -m$m -$o $path $pic: an indirect branch: $(echo "$bad" | head -1)"
					exit 1
				fi
				gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/b.o" "$CC_TMP/thunks.o"
				got=$("$CC_TMP/c")
				if [ "$got" != "$want 1" ]; then
					echo "run_indirect_branch_thunk: -m$m -$o $path $pic prints [$got], gcc's [$want] with the thunks called"
					exit 1
				fi
			done
		done
		# the cs prefix, before a thunk of %r8..%r15 only
		"$CC_BIN" -m$m -$o -mindirect-branch=thunk-extern -mindirect-branch-cs-prefix -S -o "$CC_TMP/b.s" "$CC_TMP/b.c"
		bad=$(awk '$1 ~ /^(call|jmp)$/ && $2 ~ /^__x86_indirect_thunk_/ {
				hi = $2 ~ /_r([89]|1[0-5])$/
				if (hi != (prev == "cs")) print $2
			}
			{ prev = $1 }' "$CC_TMP/b.s")
		if [ -n "$bad" ]; then
			echo "run_indirect_branch_thunk: -m$m -$o -mindirect-branch-cs-prefix: the cs prefix wrong before $bad"
			exit 1
		fi
		if [ $m = 64 ] && ! grep -q '_thunk_r\([89]\|1[0-5]\)$' "$CC_TMP/b.s"; then
			echo "run_indirect_branch_thunk: -m$m -$o: no thunk of %r8..%r15 to check the prefix of"
			exit 1
		fi
		for f in "" -mindirect-branch=keep; do
			# shellcheck disable=SC2086
			"$CC_BIN" -m$m -$o $f -S -o "$CC_TMP/n.s" "$CC_TMP/b.c"
			if grep -q '__x86_indirect_thunk' "$CC_TMP/n.s" || ! grep -q 'call \*' "$CC_TMP/n.s"; then
				echo "run_indirect_branch_thunk: -m$m -$o [$f]: not the instructions"
				exit 1
			fi
		done
	done
done
for v in thunk thunk-inline bogus; do
	if "$CC_BIN" -mindirect-branch=$v -S -o "$CC_TMP/n.s" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
		echo "run_indirect_branch_thunk: -mindirect-branch=$v was taken"
		exit 1
	fi
	if ! grep -q 'mindirect-branch' "$CC_TMP/c.err"; then
		echo "run_indirect_branch_thunk: -mindirect-branch=$v: no diagnostic"
		exit 1
	fi
done
echo "run_indirect_branch_thunk: ok"
