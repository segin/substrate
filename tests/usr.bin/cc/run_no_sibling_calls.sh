#!/bin/sh
# -fno-optimize-sibling-calls makes no tail call: a call in tail position
# is a call and a return, so that the caller is on the stack, as gcc
# has it (Linux keeps some functions so for its stack traces).  cc took
# the option and ignored it.  Under it the object has no jump to the
# callee; without it, and under -foptimize-sibling-calls after it, cc
# makes the jump on x86-64; and the program runs as gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objdump >/dev/null 2>&1; then
	echo "run_no_sibling_calls: skip (no gcc or objdump)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

__attribute__((noinline)) int callee(int x) { return x * 3 + 1; }
__attribute__((noinline)) int tail(int x) { return callee(x + 2); }

int main(void)
{
	printf("%d\n", tail(4));
	return 0;
}
EOF2
# whether tail()'s code jumps to callee
jumps() {
	# (callee is in tail's section: the jump is resolved, objdump names its target, and no relocation is left)
	objdump -d --no-show-raw-insn "$1" | awk '
		/^[0-9a-f]+ <[^>]+>:$/ { on = $2 == "<tail>:"; next }
		on && $2 ~ /^jmp/ && /<callee>/ { print "jumps"; exit }'
}
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/b.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for f in -fno-optimize-sibling-calls "" "-fno-optimize-sibling-calls -foptimize-sibling-calls"; do
		# shellcheck disable=SC2086
		"$CC_BIN" -m$m -O2 $f -c -o "$CC_TMP/c.o" "$CC_TMP/b.c"
		got=$(jumps "$CC_TMP/c.o")
		# (i386's argument is pushed: cc makes no tail call of it either way, so only the absence is checked)
		if [ "$f" = -fno-optimize-sibling-calls ]; then exp=""; elif [ $m = 64 ]; then exp=jumps; else exp=$got; fi
		if [ "$got" != "$exp" ]; then
			echo "run_no_sibling_calls: -m$m [$f]: tail() [$got], not [$exp]"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o"
		if [ "$("$CC_TMP/c")" != "$want" ]; then
			echo "run_no_sibling_calls: -m$m [$f] prints [$("$CC_TMP/c")], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_no_sibling_calls: ok"
