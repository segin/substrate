#!/bin/sh
# run_i386_default_isa.sh: a cc whose own target is i386 -- Substrate's --
# compiles as cc -m32 does when given no -m32.  It is built here by the
# host's compiler as Substrate's is (-m32, CC_SUBSTRATE_BUILD), and every
# program under codegen/ must come out the same with and without -m32, the
# predefined macros too: an i686, no MMX and no SSE, so floating point on
# the x87 and vectors in memory.  It used to start from SSE2 and MMX.
# Skipped, with a note, if the host cannot build -m32.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CCDIR="$ROOT/usr.bin/cc"
BASE="$ROOT/tests/usr.bin/cc/codegen"
HOST_CC=${HOST_CC:-cc}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_i386_default.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

printf 'int main(void) { return 0; }\n' > "$tmp/probe.c"
if ! $HOST_CC -m32 "$tmp/probe.c" -o "$tmp/probe" >/dev/null 2>&1; then
	echo "run_i386_default_isa: SKIP (the host cannot build -m32 programs)"
	exit 0
fi
srcs=$(sed -n '/^CC_SRCS/,/[^\\]$/p' "$CCDIR/Makefile" | tr -d '\\' | sed 's/CC_SRCS *=//')
[ -n "$srcs" ] || { echo "run_i386_default_isa: no CC_SRCS in $CCDIR/Makefile"; exit 1; }
if ! (cd "$CCDIR" && $HOST_CC -m32 -O1 -DCC_NATIVE_BUILD=1 -DCC_SUBSTRATE_BUILD=1 -Iinclude -I. \
	-o "$tmp/cc" $srcs) >"$tmp/build.log" 2>&1; then
	echo "FAIL: cannot build an i386 cc:"
	sed -n '1,10s/^/    /p' "$tmp/build.log"
	exit 1
fi

fail=0
: | "$tmp/cc" -dM -E - | sort >"$tmp/plain.mac" 2>&1
: | "$tmp/cc" -m32 -dM -E - | sort >"$tmp/m32.mac" 2>&1
if ! cmp -s "$tmp/plain.mac" "$tmp/m32.mac"; then
	echo "FAIL: the predefined macros differ (< cc, > cc -m32)"
	diff "$tmp/plain.mac" "$tmp/m32.mac" | sed -n '1,10s/^/    /p'
	fail=1
fi
# Substrate's cc knows only /usr/include: the host's compiler headers (stddef.h) are found here
inc=$(gcc -print-file-name=include 2>/dev/null)
n=0
for src in "$BASE"/*.c; do
	name=$(basename "$src" .c)
	std=$(sed -n 's/.*cc-std: *\([a-z0-9]*\).*/\1/p' "$src" | head -n 1)
	"$tmp/cc" ${inc:+-I"$inc"} ${std:+-std=$std} -O2 -S -o "$tmp/plain.s" "$src" >/dev/null 2>&1 || continue
	"$tmp/cc" -m32 ${inc:+-I"$inc"} ${std:+-std=$std} -O2 -S -o "$tmp/m32.s" "$src" >/dev/null 2>&1 || continue
	n=$((n + 1))
	if ! cmp -s "$tmp/plain.s" "$tmp/m32.s"; then
		echo "FAIL $name: cc and cc -m32 write different code (< cc, > cc -m32)"
		diff "$tmp/plain.s" "$tmp/m32.s" | sed -n '1,8s/^/    /p'
		fail=1
	fi
done
[ $n -gt 0 ] || { echo "FAIL: no program compiled"; fail=1; }
[ $fail -eq 0 ] && echo "run_i386_default_isa: ok ($n programs)"
exit $fail
