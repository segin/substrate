#!/bin/sh
# A block-scope static table of pointers whose initializer names only
# file-scope things is data, with no guard and no code to fill it on the
# first call (codegen/static_local_pointer_table.c checks the values).
# One that names a block-scope static keeps its guard.  A string literal
# and a list of zeros, below, need none either.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

src="$(dirname "$0")/codegen/static_local_pointer_table.c"
status=0
for m in 32 64; do
	"$CC_BIN" -m$m -S -o "$CC_TMP/slt.s" "$src"
	for t in names lists; do
		if grep -q "_${t}_[0-9]*__guard" "$CC_TMP/slt.s"; then
			echo "FAIL -m$m: the static table '$t' is filled at run time, behind a guard"
			status=1
		fi
	done
	if ! grep -q "_mine_[0-9]*__guard" "$CC_TMP/slt.s"; then
		echo "FAIL -m$m: the table naming a block-scope static was laid out as data"
		status=1
	fi
done
# so is a character array from a string literal, and a list of zeros is
# no initializer at all (Linux's pm.c `static const struct gdt_ptr
# null_idt = {0, 0}`)
cat > "$CC_TMP/more.c" <<'EOF'
struct span { int lo, hi; };
const char *text(void) { static char str[8] = "hi"; return str; }
const struct span *none(void) { static const struct span zeros = {0, 0}; return &zeros; }
EOF
for m in 32 64; do
	"$CC_BIN" -m$m -S -o "$CC_TMP/more.s" "$CC_TMP/more.c"
	for t in str zeros; do
		if grep -q "_${t}_[0-9]*__guard" "$CC_TMP/more.s"; then
			echo "FAIL -m$m: the static '$t' is filled at run time, behind a guard"
			status=1
		fi
	done
done
[ "$status" -eq 0 ] && echo "run_static_local_tables: ok"
exit "$status"
