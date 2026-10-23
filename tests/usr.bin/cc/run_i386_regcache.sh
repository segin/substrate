#!/bin/sh
# The size of i386 code: Linux's 16-bit setup code must fit 32 KiB, and
# cc made it six times that, storing each value to its stack slot after
# every instruction and loading it again for the next.  At -Os a loop
# over a pointer now keeps the pointer a value, not a stack slot (the
# optimizer's slot promotion); a constant operand is an immediate; a
# compare only a branch reads is a branch on the flags, with no setcc;
# a mask of all ones goes; and the values stay in registers between.
# codegen/regcache_values.c checks the values these paths compute.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/rc.c" <<'EOF'
unsigned long length(const char *s) {
	const char *p = s;

	while (*p)
		p++;
	return p - s;
}
EOF
"$CC_BIN" -m32 -Os -S -o "$CC_TMP/rc.s" "$CC_TMP/rc.c"
sed -n '/^length:/,/\.size length/p' "$CC_TMP/rc.s" > "$CC_TMP/length.s"
fail() {
	echo "run_i386_regcache: $1"
	cat "$CC_TMP/length.s"
	exit 1
}
grep -q 'set[a-z]* ' "$CC_TMP/length.s" && fail "a compare made a truth value for its branch"
grep -q 'addl \$1, ' "$CC_TMP/length.s" || fail "the constant 1 is not an immediate"
grep -q 'leal -[0-9]*(%ebp)' "$CC_TMP/length.s" && fail "the pointer is a stack slot"
grep -q 'andl \$-1' "$CC_TMP/length.s" && fail "a mask of all ones is left"
n=$(grep -c '^	[a-z]' "$CC_TMP/length.s")
[ "$n" -le 22 ] || fail "$n instructions, more than 22"
echo "run_i386_regcache: ok"
