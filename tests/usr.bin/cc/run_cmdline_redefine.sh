#!/bin/sh
# A -D that redefines a macro -- one the compiler predefines, or an earlier
# -D's -- with another value is diagnosed as gcc diagnoses it: a warning,
# an error under -Werror wherever -Werror stands.  The same value again is
# no redefinition.  Linux's Kconfig decides CC_HAS_INT128 by whether
# `-Werror -D__SIZEOF_INT128__=0` fails: cc said nothing, so a kernel it
# built went without __int128 although cc has it.  gcc is the judge.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_cmdline_redefine: skip (no gcc)"
	exit 0
fi

fails=0
# the flags, and whether the compile fails (1) or not (0), and whether it says "redefined"
while IFS='|' read -r flags fail said; do
	[ -n "$flags" ] || continue
	g=0
	# shellcheck disable=SC2086
	gcc $flags -c -x c /dev/null -o "$CC_TMP/g.o" 2>/dev/null || g=1
	c=0
	# shellcheck disable=SC2086
	"$CC_BIN" $flags -c -x c /dev/null -o "$CC_TMP/c.o" 2>"$CC_TMP/c.err" || c=1
	if [ "$g" != "$fail" ]; then
		echo "run_cmdline_redefine: [$flags]: gcc exits $g, the case says $fail"
		fails=$((fails + 1))
	fi
	if [ "$c" != "$fail" ]; then
		echo "run_cmdline_redefine: [$flags]: cc exits $c, gcc $g"
		fails=$((fails + 1))
	fi
	if [ "$said" = 1 ] && ! grep -q "redefined" "$CC_TMP/c.err"; then
		echo "run_cmdline_redefine: [$flags]: cc does not say the macro is redefined"
		fails=$((fails + 1))
	fi
	if [ "$said" = 0 ] && grep -q "redefined" "$CC_TMP/c.err"; then
		echo "run_cmdline_redefine: [$flags]: cc calls it a redefinition: $(cat "$CC_TMP/c.err")"
		fails=$((fails + 1))
	fi
done <<'EOF2'
-m64 -Werror -D__SIZEOF_INT128__=0|1|1
-m64 -D__SIZEOF_INT128__=0 -Werror|1|1
-m64 -D__SIZEOF_INT128__=0|0|1
-DFOO=1 -DFOO=2|0|1
-Werror -DFOO=1 -DFOO=2|1|1
-Werror -DFOO=1 -DFOO=1|0|0
-Werror -DFOO -DFOO=1|0|0
-Werror -D__STDC_HOSTED__=1|0|0
-Werror -DBAR=1|0|0
EOF2
if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "run_cmdline_redefine: ok"
