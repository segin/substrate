#!/bin/sh
set -eu

# Reqs: LD-U-005

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-defsym-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/force.c" <<'SRC'
int force_sym(void) { return 44; }
SRC
gcc -m64 -c -o "$TMP/force.o" "$TMP/force.c"
ar rcs "$TMP/libforce.a" "$TMP/force.o"

cat > "$TMP/dummy.c" <<'SRC'
int dummy(void) { return 0; }
SRC
gcc -m64 -c -o "$TMP/dummy.o" "$TMP/dummy.c"

"$LDX" -m64 -r -L"$TMP" --undefined=force_sym -o "$TMP/out_undef.o" "$TMP/dummy.o" -lforce
if ! nm "$TMP/out_undef.o" | grep -q " force_sym$"; then
	echo "FAIL: --undefined did not force archive extraction of force_sym" >&2
	exit 1
fi

"$LDX" -m64 -r --defsym MYABS=0x1234 -o "$TMP/out_defsym.o" "$TMP/dummy.o"
if ! nm "$TMP/out_defsym.o" | grep -q "0000000000001234 A MYABS$"; then
	echo "FAIL: --defsym did not materialize absolute symbol MYABS=0x1234" >&2
	nm "$TMP/out_defsym.o" >&2
	exit 1
fi

"$LDX" -m64 -r --export-dynamic --no-export-dynamic -o "$TMP/out_export_flag.o" "$TMP/dummy.o"

echo "ok: ld --undefined and --defsym controls"
