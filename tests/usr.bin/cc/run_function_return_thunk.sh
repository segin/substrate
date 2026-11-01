#!/bin/sh
# -mfunction-return=thunk-extern makes every return a jump to
# __x86_return_thunk, the program's (Linux's, with
# CONFIG_MITIGATION_RETHUNK).  cc took the option and ignored it: a
# kernel built with it returned by `ret`, and objtool warned "'naked'
# return found in MITIGATION_RETHUNK build" at each.  The object has no
# `ret`; an i386 function popping a structure result's hidden pointer
# (`ret $4`) pops it and jumps to __x86_return_thunk_ecx, as gcc's
# does; and the program, linked with thunks that count their calls,
# prints what gcc's does with the thunks called.  =keep and no option
# return by `ret`; gcc's thunk and thunk-inline are refused.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objdump >/dev/null 2>&1; then
	echo "run_function_return_thunk: skip (no gcc or objdump)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdarg.h>
#include <stdio.h>

extern int ret_hits, ret_ecx_hits;
struct big { int a[6]; };

__attribute__((noinline)) struct big mk(int x)
{
	struct big b = {{x, x + 1, x * 2, x * 3, -x, 7}};

	return b;
}

__attribute__((noinline)) long fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

__attribute__((noinline)) int sum(int n, ...)
{
	va_list ap;
	int s = 0;

	va_start(ap, n);
	while (n-- > 0) {
		s += va_arg(ap, int);
	}
	va_end(ap);
	return s;
}

__attribute__((noinline)) void nothing(int *p) { if (*p > 100) return; *p += 1; }

int main(void)
{
	struct big b = mk(9);
	int k = 5;

	nothing(&k);
	printf("%d %ld %d %d %d %d\n", b.a[0] + b.a[2] + b.a[4] + b.a[5], fib(15), sum(4, 1, 2, 3, 4), k,
	       ret_hits > 0, ret_ecx_hits > 0);
	return 0;
}
EOF2
for m in 64 32; do
	if [ $m = 64 ]; then
		inc="incl ret_hits(%rip)"
		inc_ecx="nop"
	else
		inc="incl ret_hits"
		inc_ecx="incl ret_ecx_hits"
	fi
	{
		printf '\t.data\n\t.globl ret_hits\nret_hits:\t.long 0\n\t.globl ret_ecx_hits\nret_ecx_hits:\t.long 0\n\t.text\n'
		printf '\t.globl __x86_return_thunk\n__x86_return_thunk:\n\t%s\n\tret\n' "$inc"
		if [ $m = 32 ]; then
			printf '\t.globl __x86_return_thunk_ecx\n__x86_return_thunk_ecx:\n\t%s\n\tjmp *%%ecx\n' "$inc_ecx"
		fi
	} >"$CC_TMP/thunks.s"
	gcc -m$m -c -o "$CC_TMP/thunks.o" "$CC_TMP/thunks.s" 2>/dev/null || continue
	gcc -m$m -O1 -no-pie -o "$CC_TMP/g" "$CC_TMP/b.c" "$CC_TMP/thunks.o"
	want=$("$CC_TMP/g" | awk '{ print $1, $2, $3, $4 }')
	# i386 returns a structure through a hidden pointer it pops (ret $4); x86-64 pops nothing
	if [ $m = 32 ]; then want_ecx=1; else want_ecx=0; fi
	for o in O0 O2; do
		for path in mir legacy; do
			for pic in -fno-pic -fPIC; do
				run="env"
				if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
				if ! $run "$CC_BIN" -m$m -$o $pic -mfunction-return=thunk-extern \
					-c -o "$CC_TMP/b.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
					echo "run_function_return_thunk: -m$m -$o $path $pic: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
				# (gcc's i386 pc thunk returns by ret, as cc's does: Linux has no rethunk on i386)
				bad=$(objdump -d --no-show-raw-insn "$CC_TMP/b.o" | awk '
					/^[0-9a-f]+ <[^>]+>:$/ { name = $2; next }
					name !~ /get_pc_thunk/ && $2 ~ /^ret/ { print name; exit }')
				if [ -n "$bad" ]; then
					echo "run_function_return_thunk: -m$m -$o $path $pic: a ret in $bad"
					exit 1
				fi
				gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/b.o" "$CC_TMP/thunks.o"
				got=$("$CC_TMP/c")
				if [ "$got" != "$want 1 $want_ecx" ]; then
					echo "run_function_return_thunk: -m$m -$o $path $pic prints [$got], gcc's [$want] with the thunks called: [1 $want_ecx]"
					exit 1
				fi
			done
		done
		for f in "" -mfunction-return=keep; do
			# shellcheck disable=SC2086
			"$CC_BIN" -m$m -$o $f -S -o "$CC_TMP/n.s" "$CC_TMP/b.c"
			if grep -q '__x86_return_thunk' "$CC_TMP/n.s" || ! grep -q 'ret' "$CC_TMP/n.s"; then
				echo "run_function_return_thunk: -m$m -$o [$f]: not ret"
				exit 1
			fi
		done
	done
done
for v in thunk thunk-inline bogus; do
	if "$CC_BIN" -mfunction-return=$v -S -o "$CC_TMP/n.s" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
		echo "run_function_return_thunk: -mfunction-return=$v was taken"
		exit 1
	fi
	if ! grep -q 'mfunction-return' "$CC_TMP/c.err"; then
		echo "run_function_return_thunk: -mfunction-return=$v: no diagnostic"
		exit 1
	fi
done
echo "run_function_return_thunk: ok"
