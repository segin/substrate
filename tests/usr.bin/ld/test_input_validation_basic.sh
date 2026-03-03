#!/bin/sh
set -eu

# Reqs: LD-U-012, LD-R-001

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-input-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
int main(void) { return 0; }
SRC
gcc -m64 -c -o "$TMP/main64.o" "$TMP/main.c"

"$LDX" -m64 -r -o "$TMP/out_valid.o" "$TMP/main64.o"

cp "$TMP/main64.o" "$TMP/main64_be_bad.o"
printf '\002' | dd of="$TMP/main64_be_bad.o" bs=1 seek=5 conv=notrunc status=none

if "$LDX" -m64 -r -o "$TMP/out_bad_endian.o" "$TMP/main64_be_bad.o" >"$TMP/bad_endian.err" 2>&1; then
	echo "FAIL: endian-corrupted ELF object unexpectedly linked" >&2
	exit 1
fi
if ! grep -Eq "mismatched class/machine/endianness|failed to open input" "$TMP/bad_endian.err"; then
	echo "FAIL: missing incompatible-endianness diagnostic" >&2
	cat "$TMP/bad_endian.err" >&2
	exit 1
fi

echo "ok: ld rejects incompatible class/machine/endianness inputs"
