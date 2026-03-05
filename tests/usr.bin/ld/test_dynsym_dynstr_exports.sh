#!/bin/sh
set -eu

# Reqs: LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-dynsym-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/lib.c" <<'SRC'
__attribute__((visibility("default"))) int exported_fn(void) { return 1; }
__attribute__((visibility("hidden"))) int hidden_fn(void) { return 2; }
int call_hidden(void) { return hidden_fn(); }
SRC
gcc -m64 -fPIC -c -o "$TMP/lib.o" "$TMP/lib.c"

"$LDX" -m64 -shared -o "$TMP/libout.so" "$TMP/lib.o"

readelf -WS "$TMP/libout.so" > "$TMP/sections.txt"
readelf --dyn-syms "$TMP/libout.so" > "$TMP/dynsyms.txt"

if ! grep -q "\.dynstr" "$TMP/sections.txt"; then
	echo "FAIL: missing .dynstr section in shared output" >&2
	cat "$TMP/sections.txt" >&2
	exit 1
fi
if ! grep -q "\.dynsym" "$TMP/sections.txt"; then
	echo "FAIL: missing .dynsym section in shared output" >&2
	cat "$TMP/sections.txt" >&2
	exit 1
fi
if ! grep -q "exported_fn" "$TMP/dynsyms.txt"; then
	echo "FAIL: exported symbol missing from dynamic symbol table output" >&2
	cat "$TMP/dynsyms.txt" >&2
	exit 1
fi
if grep -q "hidden_fn" "$TMP/dynsyms.txt"; then
	echo "FAIL: hidden symbol leaked into dynamic symbol table" >&2
	cat "$TMP/dynsyms.txt" >&2
	exit 1
fi

echo "ok: ld builds .dynsym/.dynstr from dynamic export policy"
