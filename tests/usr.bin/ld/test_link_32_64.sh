#!/bin/sh
set -eu

# Reqs: LD-U-002, LD-U-003

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
"$LDX" -r -o "$TMP/out_default32.o" "$TMP/main32.o"

"$LDX" -m64 -r -o "$TMP/out64.o" "$TMP/main64.o"
"$LDX" -m32 -r -o "$TMP/out32.o" "$TMP/main32.o"

if ! readelf -h "$TMP/out_default64.o" | grep -q "Class:.*ELF64"; then
	echo "FAIL: default 64-bit link output is not ELF64" >&2
	readelf -h "$TMP/out_default64.o" >&2
	exit 1
fi
if ! readelf -h "$TMP/out_default32.o" | grep -q "Class:.*ELF32"; then
	echo "FAIL: auto-selected 32-bit default link output is not ELF32" >&2
	readelf -h "$TMP/out_default32.o" >&2
	exit 1
fi

echo "ok: ld handles default host mode and auto-selects i386/x86-64 from input"
echo "ok: ld linked 64-bit relocatable output"
echo "ok: ld linked 32-bit relocatable output"
