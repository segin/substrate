#!/bin/sh
set -eu

# Reqs: LD-U-005, LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
READELF=${READELF:-/usr/bin/readelf}
TMP=${TMPDIR:-/tmp}/ldx86-verdef-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/def.c" <<'SRC'
int foo_impl(void) { return 7; }
__asm__(".symver foo_impl,foo@@VERS_FOO");
SRC
gcc -m64 -fPIC -c -o "$TMP/def.o" "$TMP/def.c"

"$LDX" -m64 -shared -o "$TMP/out.so" "$TMP/def.o"

"$READELF" -S "$TMP/out.so" > "$TMP/sections.txt"
"$READELF" -V "$TMP/out.so" > "$TMP/verinfo.txt" 2>/dev/null || true
"$READELF" -d "$TMP/out.so" > "$TMP/dynamic.txt"

if ! grep -q "[[:space:]]\\.gnu.version_d[[:space:]]" "$TMP/sections.txt"; then
	echo "FAIL: missing .gnu.version_d section" >&2
	cat "$TMP/sections.txt" >&2
	exit 1
fi
if ! grep -q "Version definition section" "$TMP/verinfo.txt"; then
	echo "FAIL: readelf did not decode version definition section" >&2
	cat "$TMP/verinfo.txt" >&2
	exit 1
fi
if ! grep -q "VERS_FOO" "$TMP/verinfo.txt"; then
	echo "FAIL: version name VERS_FOO missing from definition table" >&2
	cat "$TMP/verinfo.txt" >&2
	exit 1
fi
if ! grep -q "(VERDEF)" "$TMP/dynamic.txt" || ! grep -q "(VERDEFNUM)" "$TMP/dynamic.txt"; then
	echo "FAIL: DT_VERDEF/DT_VERDEFNUM tags not emitted" >&2
	cat "$TMP/dynamic.txt" >&2
	exit 1
fi

echo "ok: ld emits GNU version definition metadata for versioned exports"
