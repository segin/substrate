#!/bin/sh
# A block comment in a directive may run over several lines: comments
# become a space before directives are seen (C11 5.1.1.2 phase 3), so
# the newlines inside one do not end the directive.  Linux's printk
# index macro has such a comment in its body, without backslashes, and
# cc ended the #define at its first line -- a kernel with
# CONFIG_PRINTK_INDEX did not build.  The lines after a directive keep
# their numbers (__LINE__).  gcc is the judge.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_directive_comment: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/d.c" <<'EOF2'
#include <stdio.h>
#define EMIT(x, y)				\
	do {					\
		/*
		 * a comment of three lines,
		 * no backslashes
		 */				\
		printf("%d ", (x) + (y));	\
	} while (0)
#define SUM 1 /* one
	two */ + 2
#if SUM == 3 /* a
	condition's comment */
#define CHOSEN 10
#else
#define CHOSEN 20
#endif /* and an
	#endif's */
int main(void)
{
	int l = __LINE__;

	EMIT(1, 2);
	printf("%d %d %d\n", SUM, CHOSEN, l);
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/d.c"
want=$("$CC_TMP/g")
for o in O0 O2; do
	if ! "$CC_BIN" -$o -o "$CC_TMP/c" "$CC_TMP/d.c" 2>"$CC_TMP/c.err"; then
		echo "run_pp_directive_comment: -$o: $(head -1 "$CC_TMP/c.err")"
		exit 1
	fi
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_pp_directive_comment: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
echo "run_pp_directive_comment: ok"
