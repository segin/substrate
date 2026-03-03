#!/bin/sh
set -eu

# Reqs: LD-U-004, LD-U-005

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-dso-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/prov.c" <<'SRC'
int provided(void) { return 11; }
SRC
gcc -m64 -fPIC -c -o "$TMP/prov_pic.o" "$TMP/prov.c"
gcc -shared -o "$TMP/libprov.so" "$TMP/prov_pic.o"

cat > "$TMP/prov_static.c" <<'SRC'
int provided(void) { return 22; }
int provided_static_marker(void) { return 333; }
SRC
gcc -m64 -c -o "$TMP/prov_static.o" "$TMP/prov_static.c"
ar rcs "$TMP/libprovstatic.a" "$TMP/prov_static.o"

cat > "$TMP/caller.c" <<'SRC'
int provided(void);
int call_provided(void) { return provided(); }
SRC
gcc -m64 -c -o "$TMP/caller.o" "$TMP/caller.c"

"$LDX" -m64 -shared -Map "$TMP/link.map" -o "$TMP/out.so" "$TMP/caller.o" -L"$TMP" -lprov -lprovstatic

if [ ! -s "$TMP/link.map" ]; then
	echo "FAIL: missing map output for shared link with import-only DSO input" >&2
	exit 1
fi
if ! grep -q "DSO Inputs:" "$TMP/link.map" || ! grep -q "libprov.so" "$TMP/link.map"; then
	echo "FAIL: map output missing import-only DSO provider entry" >&2
	cat "$TMP/link.map" >&2
	exit 1
fi
if grep -q "provided_static_marker" "$TMP/link.map"; then
	echo "FAIL: static archive provider was extracted despite DSO provider resolution" >&2
	cat "$TMP/link.map" >&2
	exit 1
fi

echo "ok: ld parses ET_DYN inputs as import-only providers and uses them in resolution"
