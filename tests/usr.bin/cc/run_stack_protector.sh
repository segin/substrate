#!/bin/sh
# -fstack-protector, -strong, -all and -explicit put a canary between a
# function's locals and its return address, set from the thread's guard
# at entry and checked at each return, as gcc does; Linux builds with
# -fstack-protector-strong and its guard at %gs:__ref_stack_chk_guard.
# cc took them and ignored them: a kernel it built had no canary at all.
# The functions each mode guards are gcc's -- by an array, a char array
# of 8 bytes or more, an aggregate holding one, a scalar whose address is
# taken, alloca, a variable-length array, no_stack_protector and
# stack_protect -- read off the guard's load in each function, -m64 and
# -m32, through the MIR path and the legacy emitters; a program running
# a buffer past its end dies in __stack_chk_fail and one that does not
# prints what gcc's does; the guard is where -mstack-protector-guard-reg,
# -offset and -symbol put it; and __SSP__ and its kin are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objdump >/dev/null 2>&1; then
	echo "run_stack_protector: skip (no gcc or objdump)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/k.c" <<'EOF2'
#include <alloca.h>

extern void use(void *);
struct holder { int n; char name[16]; };
struct plain { int a, b; };

__attribute__((noinline)) int no_locals(int x) { return x * 3; }
__attribute__((noinline)) int small_chars(void) { char b[4]; use(b); return b[0]; }
__attribute__((noinline)) int big_chars(void) { char b[8]; use(b); return b[1]; }
__attribute__((noinline)) int ints(void) { int a[4]; use(a); return a[2]; }
__attribute__((noinline)) int holds(void) { struct holder h; use(&h); return h.n; }
__attribute__((noinline)) int address(int x) { int y = x; use(&y); return y; }
__attribute__((noinline)) int plain_taken(void) { struct plain p; use(&p); return p.a; }
__attribute__((noinline)) int with_alloca(int n) { char *p = alloca(n); use(p); return p[0]; }
__attribute__((noinline)) int with_vla(int n) { char v[n]; use(v); return v[0]; }
__attribute__((noinline, no_stack_protector)) int refused(void) { char b[32]; use(b); return b[0]; }
__attribute__((noinline, stack_protect)) int asked(int x) { return x + 1; }
EOF2
cat >"$CC_TMP/run.c" <<'EOF2'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((noinline)) int fill(char *dst, const char *src, size_t n)
{
	memcpy(dst, src, n);
	return dst[0];
}

static char src[256];

/* (its one local is the buffer: running past it reaches the canary, wherever the frame puts it) */
__attribute__((noinline)) int victim(size_t n)
{
	char buf[16];

	memset(src, 'A', sizeof(src));
	return fill(buf, src, n) + buf[n > 16 ? 15 : n - 1];
}

int main(int argc, char **argv)
{
	size_t n = argc > 1 ? (size_t)atoi(argv[1]) : 8;

	printf("%d\n", victim(n));
	return 0;
}
EOF2
# the functions of object $1 that load the guard $2 (objdump's spelling of it)
guarded() {
	objdump -d --no-show-raw-insn "$1" | awk -v g="$2" '
		/^[0-9a-f]+ <[^>]+>:$/ { name = substr($2, 2, length($2) - 3); next }
		index($0, g) && !seen[name]++ { print name }' | sort | tr '\n' ' '
}
for m in 64 32; do
	if [ $m = 64 ]; then guard="%fs:0x28"; else guard="%gs:0x14"; fi
	gcc -m$m -c -o "$CC_TMP/probe.o" "$CC_TMP/k.c" 2>/dev/null || continue
	for mode in -fstack-protector -fstack-protector-strong -fstack-protector-all -fstack-protector-explicit \
		-fno-stack-protector; do
		gcc -m$m -O1 -fno-pic $mode -c -o "$CC_TMP/g.o" "$CC_TMP/k.c"
		want=$(guarded "$CC_TMP/g.o" "$guard")
		for o in O0 O2; do
			for path in mir legacy; do
				run="env"
				if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
				if ! $run "$CC_BIN" -m$m -$o $mode -c -o "$CC_TMP/c.o" "$CC_TMP/k.c" 2>"$CC_TMP/c.err"; then
					echo "run_stack_protector: -m$m -$o $path $mode: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
				got=$(guarded "$CC_TMP/c.o" "$guard")
				if [ "$got" != "$want" ]; then
					echo "run_stack_protector: -m$m -$o $path $mode guards [$got], gcc [$want]"
					exit 1
				fi
			done
		done
	done
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/run.c"
	want=$("$CC_TMP/g" 8)
	for o in O0 O2; do
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			# (and with no frame pointer: the canary off the stack pointer, which the store's pop has moved)
			for pic in -fno-pic -fPIC "-fno-pic -fomit-frame-pointer"; do
				what="-m$m -$o $path $pic"
				# shellcheck disable=SC2086
				if ! $run "$CC_BIN" -m$m -$o $pic -fstack-protector-strong -c -o "$CC_TMP/c.o" "$CC_TMP/run.c" \
					2>"$CC_TMP/c.err"; then
					echo "run_stack_protector: $what: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
				gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o"
				if [ "$("$CC_TMP/c" 8)" != "$want" ]; then
					echo "run_stack_protector: $what prints [$("$CC_TMP/c" 8)], gcc's [$want]"
					exit 1
				fi
				# 40 bytes into a buffer of 16: past the canary, which the return finds changed (and aborts)
				if sh -c 'ulimit -c 0; "$1" 40 >"$2" 2>"$3"' sh "$CC_TMP/c" "$CC_TMP/out" "$CC_TMP/err" 2>/dev/null ||
					! grep -q 'stack smashing' "$CC_TMP/err"; then
					echo "run_stack_protector: $what: an overrun buffer went unnoticed [$(head -c 200 "$CC_TMP/err")]"
					exit 1
				fi
			done
		done
	done
done
# the guard where the options put it: Linux's, and an offset of the thread's
for f in "-m64 -mstack-protector-guard-reg=gs -mstack-protector-guard-symbol=__ref_stack_chk_guard" \
	"-m32 -mstack-protector-guard-reg=fs -mstack-protector-guard-symbol=__ref_stack_chk_guard" \
	"-m64 -mstack-protector-guard-offset=24" "-m64 -mstack-protector-guard=global"; do
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		# shellcheck disable=SC2086
		gcc -O2 -fno-pic -fstack-protector-strong $f -S -o "$CC_TMP/g.s" "$CC_TMP/k.c"
		# shellcheck disable=SC2086
		$run "$CC_BIN" -O2 -fstack-protector-strong $f -S -o "$CC_TMP/c.s" "$CC_TMP/k.c"
		want=$(grep -o '%[fg]s:[^,]*\|__stack_chk_guard[^,]*' "$CC_TMP/g.s" | sed 's/[[:space:]]*$//' | sort -u)
		got=$(grep -o '%[fg]s:[^,]*\|__stack_chk_guard[^,]*' "$CC_TMP/c.s" | sed 's/[[:space:]]*$//' | sort -u)
		if [ "$got" != "$want" ]; then
			echo "run_stack_protector: [$f] $path reads the guard at [$got], gcc at [$want]"
			exit 1
		fi
	done
done
# (not with no option: a distribution's gcc may default to one, as Arch's does to -strong, and cc to none)
for mode in -fstack-protector -fstack-protector-strong -fstack-protector-all -fstack-protector-explicit \
	-fno-stack-protector; do
	# shellcheck disable=SC2086
	want=$(gcc $mode -dM -E - </dev/null | grep '__SSP' || true)
	# shellcheck disable=SC2086
	got=$("$CC_BIN" $mode -dM -E - </dev/null | grep '__SSP' || true)
	if [ "$got" != "$want" ]; then
		echo "run_stack_protector: [$mode] defines [$got], gcc [$want]"
		exit 1
	fi
done
echo "run_stack_protector: ok"
