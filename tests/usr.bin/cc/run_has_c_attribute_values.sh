#!/bin/sh
# __has_c_attribute and __has_attribute have gcc's values, in #if and in
# ordinary text.  cc gave 1 for a standard attribute where gcc gives C23's
# date, 202311, so `#if __has_c_attribute(nodiscard) >= 202003L` was
# false; it took the scope off `gnu::packed` and then found no standard
# attribute "packed", and read no `__packed__` spelling or `gnu::` scope
# at all in __has_attribute; __has_c_attribute in text was left as it
# was; and of the GNU attributes cc implements, constructor, destructor,
# counted_by, gnu_inline, mode and regparm were 0.  gcc is the judge:
# each operand's value, under -std=gnu17 and -std=c23.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_has_c_attribute_values: skip (no gcc)"
	exit 0
fi

ops="nodiscard __nodiscard__ deprecated __deprecated__ fallthrough maybe_unused noreturn _Noreturn
reproducible unsequenced gnu::packed gnu::__packed__ __gnu__::__aligned__ gnu::deprecated
gnu::nonexistent clang::fallthrough packed __packed__ aligned constructor destructor counted_by
gnu_inline mode regparm cleanup not_an_attribute"

{
	echo '#if __has_c_attribute(nodiscard) >= 202003L && __has_attribute(deprecated) >= 201904L'
	echo 'dated'
	echo '#endif'
	echo '#if __has_c_attribute(gnu :: packed) && __has_attribute(gnu::aligned)'
	echo 'scoped_spaced'
	echo '#endif'
	n=0
	for a in $ops; do
		n=$((n + 1))
		echo "#if __has_c_attribute($a) == 202311L"
		echo "c_$n std"
		echo "#elif __has_c_attribute($a)"
		echo "c_$n one"
		echo "#else"
		echo "c_$n none"
		echo "#endif"
		echo "#if __has_attribute($a) == 202311L"
		echo "a_$n std"
		echo "#elif __has_attribute($a)"
		echo "a_$n one"
		echo "#else"
		echo "a_$n none"
		echo "#endif"
		echo "text_$n __has_c_attribute($a) __has_attribute($a)"
	done
} >"$CC_TMP/a.c"
for std in gnu17 c23; do
	gcc -std=$std -E -P "$CC_TMP/a.c" | tr -s ' \t' '  ' | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/g.out"
	"$CC_BIN" -std=$std -E -P -o "$CC_TMP/c.raw" "$CC_TMP/a.c"
	tr -s ' \t' '  ' <"$CC_TMP/c.raw" | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/c.out"
	if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
		echo "run_has_c_attribute_values: -std=$std differs from gcc (cc's on the right):"
		diff "$CC_TMP/g.out" "$CC_TMP/c.out" | grep '^[<>]' | head -20
		exit 1
	fi
done
echo "run_has_c_attribute_values: ok"
