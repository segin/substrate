#!/bin/sh
# -MD and -MMD list each header as gcc lists it: as it was found -- the
# including file's directory as its name has it, or the -I directory as
# written, joined to the #include's name ("hdr/b.h", "inc/sub/c.h") --
# not by its absolute path, which cc gave, so a dependency file did not
# move with its tree.  The list is gcc's, in gcc's order, and -MMD's
# leaves the system headers out as gcc's does.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_deps_as_found: skip (no gcc)"
	exit 0
fi

D="$CC_TMP/deps"
rm -rf "$D"
mkdir -p "$D/hdr" "$D/inc/sub"
cd "$D"
printf '#include "b.h"\n#include <stddef.h>\n' >hdr/a.h
printf 'int b;\n' >hdr/b.h
printf '#include "sub/c.h"\n' >inc/i.h
printf 'int c;\n' >inc/sub/c.h
printf '#include "hdr/a.h"\n#include "i.h"\nint main(void) { return 0; }\n' >m.c

# the rule's words, one a line (gcc's implicit stdc-predef.h is gcc's alone)
words() {
	tr -s ' \\\n' '\n\n\n' <"$1" | grep -v '^$' | grep -v 'stdc-predef'
}

for f in -MD -MMD; do
	gcc $f -MF g.d -MT t.o -Iinc -c -o g.o m.c
	"$CC_BIN" $f -MF c.d -MT t.o -Iinc -c -o c.o m.c
	words g.d >g.list
	words c.d >c.list
	if ! cmp -s g.list c.list; then
		echo "run_deps_as_found: $f: cc's dependencies differ from gcc's (< gcc, > cc):"
		diff g.list c.list || true
		exit 1
	fi
done
echo "run_deps_as_found: ok"
