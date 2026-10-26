#!/bin/sh
# The driver takes the options gcc takes and refuses those it refuses, for
# the ones a build probes for: Linux's Kconfig decides a capability by
# whether `cc -Werror OPTION -c -x c /dev/null` succeeds.  cc took clang's
# -falloc-token-max=, -fsanitize-coverage-stack-depth-callback-min=,
# -frandomize-layout-seed-file=, -fsanitize=kernel-memory and kcfi, and
# -mllvm, which gcc refuses -- a kernel it configured claimed them -- and
# refused gcc's --param (taking its value as an input file) and
# -ftrivial-auto-var-init=pattern.  gcc is the judge of each; and pattern
# fills an automatic aggregate with no initializer with 0xfe bytes, as
# gcc's does.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_driver_gcc_options: skip (no gcc)"
	exit 0
fi

fails=0
while read -r flags; do
	[ -n "$flags" ] || continue
	g=ok
	# shellcheck disable=SC2086
	gcc -Werror $flags -c -x c /dev/null -o "$CC_TMP/g.o" 2>/dev/null || g=refused
	c=ok
	# shellcheck disable=SC2086
	"$CC_BIN" -Werror $flags -c -x c /dev/null -o "$CC_TMP/c.o" 2>"$CC_TMP/c.err" || c=refused
	if [ "$g" != "$c" ]; then
		echo "run_driver_gcc_options: [$flags]: gcc $g it, cc $c it $(head -1 "$CC_TMP/c.err")"
		fails=$((fails + 1))
	fi
done <<'EOF2'
-falloc-token-max=123
-fsanitize-coverage-stack-depth-callback-min=1
-frandomize-layout-seed-file=/dev/null
-fsanitize=kernel-memory
-fsanitize=kcfi -fsanitize-cfi-icall-experimental-normalize-integers
-fsanitize=address,kcfi
-mllvm -tsan-distinguish-volatile=1
-ftrivial-auto-var-init=pattern
-ftrivial-auto-var-init=zero
-ftrivial-auto-var-init=uninitialized
-fsanitize=thread --param tsan-distinguish-volatile=1
-fsanitize=thread --param=tsan-distinguish-volatile=1
-fsanitize=kernel-address
-fsanitize=undefined,bounds,shift
-fno-strict-aliasing
EOF2

cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>

struct big { unsigned int a[8]; unsigned char c[5]; };

void __attribute__((noinline)) show(const struct big *b, const unsigned short *s)
{
	printf("%x %x %x %x\n", b->a[0], b->a[7], b->c[4], s[2]);
}

int main(void)
{
	struct big b;
	unsigned short s[4];

	show(&b, s);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -ftrivial-auto-var-init=pattern -o "$CC_TMP/g" "$CC_TMP/p.c"
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -ftrivial-auto-var-init=pattern -o "$CC_TMP/c" "$CC_TMP/p.c"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_driver_gcc_options: -m$m -$o: pattern leaves [$got], gcc [$want]"
			fails=$((fails + 1))
		fi
	done
done
if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "run_driver_gcc_options: ok"
