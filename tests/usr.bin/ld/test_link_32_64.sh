#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-main-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
int main(void) { return 0; }
SRC

gcc -m64 -c -o "$TMP/main64.o" "$TMP/main.c"
gcc -m32 -c -o "$TMP/main32.o" "$TMP/main.c"

"$LDX" -r -o "$TMP/out_default64.o" "$TMP/main64.o"
if "$LDX" -r -o "$TMP/out_default32.o" "$TMP/main32.o" >"$TMP/default32.err" 2>&1; then
	echo "FAIL: default mode unexpectedly accepted 32-bit input" >&2
	exit 1
fi
if ! grep -q "mismatched class/machine" "$TMP/default32.err"; then
	echo "FAIL: expected mismatched class/machine diagnostic for default 32-bit input" >&2
	cat "$TMP/default32.err" >&2
	exit 1
fi

"$LDX" -m64 -r -o "$TMP/out64.o" "$TMP/main64.o"
"$LDX" -m32 -r -o "$TMP/out32.o" "$TMP/main32.o"

echo "ok: ld defaults to x86-64 and rejects i386 without -m32"
echo "ok: ld linked 64-bit relocatable output"
echo "ok: ld linked 32-bit relocatable output"
