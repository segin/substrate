#!/bin/sh
# Before a diagnostic in a function cc prints gcc's "hs.c: In function
# 'f':", once for each function it reports in, and "hs.c: At top level:"
# before one at file scope that follows -- after the "In file included
# from" lines of a header -- for the errors of the parser, of sema and of
# the code generator, and the warnings.  cc printed neither.  What cc
# prints up to its first error is gcc's: the context lines and each
# diagnostic's file, line and kind.  (cc checks file-scope declarations
# before function bodies, so an error at file scope after a function's
# diagnostics, gcc's "At top level:", is not among these.)
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_diag_function_context: skip (no gcc)"
	exit 0
fi

D="$CC_TMP/fnctx"
rm -rf "$D"
mkdir -p "$D/hdr"
cd "$D"
printf 'int ok(void)\n{\n    return 0;\n}\nint f(void)\n{\n    return nope;\n}\n' >sema.c
printf 'static int k(void) { return 1 }\n' >parse.c
printf 'void g(void){ undeclared_y = 1; }\n' >hdr/s.h
printf '#include "hdr/s.h"\nint main(void) { return 0; }\n' >hdr.c
printf 'int f(void)\n{\n    int x = 0;\n    if (x = 1)\n        return x;\n    return nope;\n}\n' >warn.c
printf 'int t = u;\n' >top.c
printf 'long double l(long double a)\n{\n    return a * 2;\n}\n' >back.c

# the context lines and each diagnostic's file:line and kind, to the first error
context() {
	"$@" -c -o /dev/null 2>&1 | awk '
		/: In function |: At top level:$|^In file included from |^                 from / { print; next }
		/: (error|warning): / { split($0, w, ":"); print w[1] ":" w[2] ":" w[4]; if ($0 ~ /: error: /) exit }'
}

for t in "sema.c" "parse.c" "hdr.c" "-Wall warn.c" "top.c" "-mno-80387 back.c"; do
	g=$(context gcc $t)
	c=$(context "$CC_BIN" $t)
	if [ "$c" != "$g" ]; then
		echo "run_diag_function_context: $t: cc says"
		echo "$c"
		echo "where gcc says"
		echo "$g"
		exit 1
	fi
done
echo "run_diag_function_context: ok"
