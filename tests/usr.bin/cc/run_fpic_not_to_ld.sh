#!/bin/sh
# -fPIC and -fpic are the compiler's options: gcc's link line has
# neither, and GNU ld would read -fPIC as `-f PIC', an auxiliary filter
# named PIC, put in the shared object's dynamic section.  cc passed the
# option to ld as given.  The ld that cc runs gets no -fpic or -fPIC,
# for a shared object or an executable, as collect2 from gcc gets none.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fpic_not_to_ld: skip (no gcc)"
	exit 0
fi

echo 'int f(int x) { return x + 1; }' >"$CC_TMP/a.c"
for opt in -fPIC -fpic; do
	for link in -shared ""; do
		# (gcc's: the collect2 line, its link)
		g=$(gcc -### $opt $link -o "$CC_TMP/a.out" "$CC_TMP/a.c" 2>&1 | grep collect2 | grep -c -e '"-fPIC"' -e '"-fpic"' || true)
		c=$("$CC_BIN" -### $opt $link -o "$CC_TMP/a.out" "$CC_TMP/a.c" 2>&1 | grep -E '(^|/)ld ' | grep -c -w -e -fPIC -e -fpic || true)
		if [ "$c" != "$g" ]; then
			echo "run_fpic_not_to_ld: $opt $link: ld is given the option $c time(s), gcc's linker $g"
			exit 1
		fi
	done
done
echo "run_fpic_not_to_ld: ok"
