#!/bin/sh
# An error in an #included file is reported as gcc reports it: under the
# file's name as it was included ("hdr/s.h", the directory as written,
# not the absolute path), at its own line, after gcc's account of the
# #includes that led there ("In file included from hdr/a.h:2,\n from
# nest.c:1:").  cc named the main file for a semantic error in a header,
# with the header's line, and gave a parse error the header's absolute
# path; neither said how the header was included.  The preprocessor now
# writes gcc's line markers, `# 1 "hdr/s.h" 1`, whose flags tell the
# compiler where each file begins and ends.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_diag_header_file: skip (no gcc)"
	exit 0
fi

D="$CC_TMP/hdrdiag"
rm -rf "$D"
mkdir -p "$D/hdr"
cd "$D"
printf '/* s.h */\nvoid f(void){ undeclared_y = 1; }\n' >hdr/s.h
printf '#include "hdr/s.h"\nint main(void) { return 0; }\n' >hs.c
printf 'int ok;\n#include "b.h"\n' >hdr/a.h
printf 'int g(void)\n{\n    return nope;\n}\n' >hdr/b.h
printf '#include "hdr/a.h"\n' >nest.c
printf 'int h(void) { return 1 }\n' >hdr/h.h
printf '#include "hdr/h.h"\n' >ph.c
printf '#include "h.h"\n' >inc.c
printf 'int k(void)\n{\n    return zork;\n}\n' >main.c

# the #include lines, and the first error's file and line
where() {
	"$@" -c -o /dev/null 2>&1 | awk '
		/^In file included from |^                 from / { print; next }
		/: error: / { split($1, p, ":"); print p[1] ":" p[2]; exit }'
}

for t in "hs.c" "nest.c" "ph.c" "-Ihdr inc.c" "main.c"; do
	g=$(where gcc $t)
	c=$(where "$CC_BIN" $t)
	if [ -z "$g" ] || [ "$c" != "$g" ]; then
		echo "run_diag_header_file: $t: cc says"
		echo "$c"
		echo "where gcc says"
		echo "$g"
		exit 1
	fi
done
echo "run_diag_header_file: ok"
