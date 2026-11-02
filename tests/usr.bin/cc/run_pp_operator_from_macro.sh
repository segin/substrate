#!/bin/sh
# An operator that a macro makes has its value, in #if as in text: #if
# replaces its operators and then expands its macros, so an operator one
# made was left unread ("unsupported #if expression").  glibc's cdefs.h
# defines __glibc_has_extension(ext) as __has_extension (ext) when
# __has_extension is defined, and once it was, no file including
# <stdio.h> compiled.  gcc is the judge: __has_feature and
# __has_extension made by macros, in #if and in text, under -std=gnu17
# and -std=c11; and a program including <stdio.h> compiles and runs.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_operator_from_macro: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/m.c" <<'EOF'
#ifdef __has_extension
# define HAS_EXT(e) __has_extension (e)
#else
# define HAS_EXT(e) 0
#endif
#ifdef __has_feature
# define HAS_FEAT(f) __has_feature(f)
#else
# define HAS_FEAT(f) 0
#endif
#if (16 << 16) + 2 >= ((4) << 16) + (5) || HAS_EXT (c_alignas)
ok_or
#endif
#if HAS_EXT (c_alignas)
ext_alignas
#endif
#if HAS_FEAT(c_static_assert)
feat_static_assert
#endif
#if HAS_FEAT(not_a_feature) || HAS_EXT(not_an_extension)
neither
#endif
text HAS_EXT(c_alignas) HAS_FEAT(c_generic_selections) HAS_FEAT(nothing)
EOF
for std in gnu17 c11; do
	gcc -std=$std -E -P "$CC_TMP/m.c" | tr -s ' \t' '  ' | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/g.out"
	if ! "$CC_BIN" -std=$std -E -P -o "$CC_TMP/c.raw" "$CC_TMP/m.c" 2>"$CC_TMP/c.err"; then
		echo "run_pp_operator_from_macro: -std=$std refused: $(head -1 "$CC_TMP/c.err")"
		exit 1
	fi
	tr -s ' \t' '  ' <"$CC_TMP/c.raw" | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/c.out"
	if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
		echo "run_pp_operator_from_macro: -std=$std differs from gcc (cc's on the right):"
		diff "$CC_TMP/g.out" "$CC_TMP/c.out" | grep '^[<>]' | head -20
		exit 1
	fi
done

printf '#include <stdio.h>\nint main(void) { printf("%%d\\n", 6 * 7); return 0; }\n' >"$CC_TMP/s.c"
if ! "$CC_BIN" -c -o "$CC_TMP/s.o" "$CC_TMP/s.c" 2>"$CC_TMP/c.err"; then
	echo "run_pp_operator_from_macro: <stdio.h> refused: $(grep -v note "$CC_TMP/c.err" | head -1)"
	exit 1
fi
gcc -no-pie -o "$CC_TMP/s" "$CC_TMP/s.o"
if [ "$("$CC_TMP/s")" != 42 ]; then
	echo "run_pp_operator_from_macro: the <stdio.h> program does not print 42"
	exit 1
fi
echo "run_pp_operator_from_macro: ok"
