#!/bin/sh
# run_march_opcodes.sh: an i386 -march gets that CPU's instructions only.
# march_atomics.c is built by cc for -march=i386, i486, i586 and i686 at
# -O0 and -O2; GNU as, told the same -march, must accept the assembly (it
# rejects cmpxchg and xadd before the 486, cmpxchg8b before the Pentium,
# cmov and fucomip before the Pentium Pro), and the program, linked with
# gcc's build of the __sync/__atomic functions an older CPU calls instead
# (march_atomic_lib.c), must print what gcc's build of it prints.
#
# Needs a host gcc that builds -m32 programs; skipped, with a note, if not.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
DIR="$ROOT/tests/usr.bin/cc"
# the native cc must run the host's as and ld, not substrate's
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_march.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

if ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$tmp/probe" 2>/dev/null; then
	echo "run_march_opcodes: skip (gcc cannot build -m32 programs)"
	exit 0
fi

gcc -m32 -march=i686 -O2 -Wno-sync-nand -c -o "$tmp/lib.o" "$DIR/march_atomic_lib.c" || exit 1
fail=0
for arch in i386 i486 i586 i686; do
	gcc -m32 -march=$arch -O2 -w -Wno-sync-nand -o "$tmp/ref" "$DIR/march_atomics.c" "$tmp/lib.o" || exit 1
	"$tmp/ref" >"$tmp/ref.out" 2>&1
	for o in O0 O2; do
		if ! "$CC_BIN" -m32 -march=$arch -$o -S -o "$tmp/ours.s" "$DIR/march_atomics.c" >"$tmp/cc.log" 2>&1; then
			echo "FAIL -march=$arch -$o: cc rejected it"
			sed -n '1,5s/^/    /p' "$tmp/cc.log"
			fail=1
			continue
		fi
		# as's i386 and i486 have no FPU unless told; gcc's have the x87
		asarch=$arch
		case $arch in i386 | i486) asarch=$arch+387 ;; esac
		if ! as --32 -march=$asarch -o "$tmp/ours.o" "$tmp/ours.s" >"$tmp/as.log" 2>&1; then
			echo "FAIL -march=$arch -$o: an instruction the CPU lacks"
			grep 'Error' "$tmp/as.log" | sed -n '1,5s/^.*Error: /    /p'
			fail=1
			continue
		fi
		gcc -m32 -no-pie -o "$tmp/ours" "$tmp/ours.o" "$tmp/lib.o" || exit 1
		timeout 10 "$tmp/ours" >"$tmp/ours.out" 2>&1
		if ! cmp -s "$tmp/ref.out" "$tmp/ours.out"; then
			echo "FAIL -march=$arch -$o: output differs from gcc's (< gcc, > cc)"
			diff "$tmp/ref.out" "$tmp/ours.out" | sed -n '1,10s/^/    /p'
			fail=1
		fi
	done
done
[ $fail -eq 0 ] && echo "run_march_opcodes: ok"
exit $fail
