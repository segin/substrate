#!/bin/sh
set -eu

# Reqs: LD-U-002, LD-U-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-hostmode-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
int main(void) { return 0; }
SRC

gcc -m64 -c -o "$TMP/main64.o" "$TMP/main.c"
gcc -m32 -c -o "$TMP/main32.o" "$TMP/main.c"

"$LDX" -r -o "$TMP/out64.o" "$TMP/main64.o"
"$LDX" -r -o "$TMP/out32.o" "$TMP/main32.o"

if ! readelf -h "$TMP/out64.o" | grep -q "Class:.*ELF64"; then
	echo "FAIL: default host-mode link did not preserve ELF64 input mode" >&2
	readelf -h "$TMP/out64.o" >&2
	exit 1
fi
if ! readelf -h "$TMP/out32.o" | grep -q "Class:.*ELF32"; then
	echo "FAIL: auto-switched mode did not preserve ELF32 input mode" >&2
	readelf -h "$TMP/out32.o" >&2
	exit 1
fi

if "$LDX" -r -o "$TMP/mixed.o" "$TMP/main64.o" "$TMP/main32.o" >"$TMP/mixed.err" 2>&1; then
	echo "FAIL: mixed-arch link unexpectedly succeeded without explicit mode split" >&2
	exit 1
fi
if ! grep -q "mismatched class/machine/endianness" "$TMP/mixed.err"; then
	echo "FAIL: mixed-arch failure missing mismatch diagnostic" >&2
	cat "$TMP/mixed.err" >&2
	exit 1
fi

echo "ok: ld host-mode defaults and auto mode selection handle i386/x86_64 inputs"
