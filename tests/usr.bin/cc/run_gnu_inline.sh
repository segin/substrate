#!/bin/sh
# gnu_inline, the GNU89 meaning of inline: an `extern inline` function
# defines nothing outside its file, and a plain `inline` one is an
# ordinary external definition.  Linux defines native_save_fl
# `extern inline __attribute__((gnu_inline))` in a header; cc made it a
# global definition in every file that included it, and vmlinux did not
# link ("duplicate strong definition of `native_save_fl`").  cc has no
# inliner, so such a function is a copy local to the file; two files
# including it link, and a plain gnu_inline function is callable from
# another file, as with gcc.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/gi.h" <<'EOF'
#define gnu_inline inline __attribute__((__gnu_inline__))
extern gnu_inline unsigned long save_fl(void);
extern gnu_inline __attribute__((__always_inline__)) unsigned long save_fl(void) { return 42; }
gnu_inline int defined_here(int x);
EOF
cat > "$CC_TMP/gi_a.c" <<'EOF'
#include "gi.h"
gnu_inline int defined_here(int x) { return x * 3; }
int from_a(void) { return (int)save_fl() + 1; }
EOF
cat > "$CC_TMP/gi_b.c" <<'EOF'
#include <stdio.h>
#include "gi.h"
int from_a(void);
int main(void) {
	printf("%d %d %d\n", (int)save_fl(), from_a(), defined_here(5));
	return 0;
}
EOF

for m in 32 64; do
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -std=gnu11 -c -o "$CC_TMP/gi_a.o" "$CC_TMP/gi_a.c"
		"$CC_BIN" -m$m -$o -std=gnu11 -c -o "$CC_TMP/gi_b.o" "$CC_TMP/gi_b.c"
		# no global save_fl in either object
		! nm "$CC_TMP/gi_a.o" "$CC_TMP/gi_b.o" | grep -q ' T save_fl$'
		nm "$CC_TMP/gi_a.o" | grep -q ' T defined_here$'
		"$CC_BIN" -m$m -o "$CC_TMP/gi" "$CC_TMP/gi_a.o" "$CC_TMP/gi_b.o"
		[ "$("$CC_TMP/gi")" = "42 43 15" ]
	done
done
echo "run_gnu_inline: ok"
