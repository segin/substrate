#!/bin/sh
# An integer conversion is one extending move, or none: legalization
# writes every one as a shift left and back right, and that pair is the
# movsx or movzx gcc makes -- (signed char) and (unsigned short) on i386
# with no shll/sarl/shrl -- and nothing at all when the value is held so
# already: an int loaded on x86-64 is sign-extended by its movslq, and
# widening it to long shifts nothing.  codegen/ checks the values.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/ext.c" <<'EOF'
int narrow(int x, int y) {
    return (signed char)x + (unsigned short)y;
}

long widen(const int *p) {
    return *p;
}
EOF
for o in O1 O2 Os; do
	"$CC_BIN" -m32 -$o -S -o "$CC_TMP/ext32.s" "$CC_TMP/ext.c"
	body=$(sed -n '/^narrow:/,/^\.size/p' "$CC_TMP/ext32.s")
	if echo "$body" | grep -Eq 'sh[lr]l|sarl' || ! echo "$body" | grep -q movsbl || ! echo "$body" | grep -q movzwl; then
		echo "run_int_extend: narrow -m32 -$o is not movsbl and movzwl:"
		echo "$body"
		exit 1
	fi
	"$CC_BIN" -m64 -$o -S -o "$CC_TMP/ext64.s" "$CC_TMP/ext.c"
	body=$(sed -n '/^widen:/,/^\.size/p' "$CC_TMP/ext64.s")
	if echo "$body" | grep -Eq 'sh[lr]q|sarq' || [ "$(echo "$body" | grep -c movslq)" -ne 1 ]; then
		echo "run_int_extend: widen -m64 -$o is not one movslq:"
		echo "$body"
		exit 1
	fi
done
echo "run_int_extend: ok"
