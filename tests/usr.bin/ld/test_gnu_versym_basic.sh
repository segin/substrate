#!/bin/sh
set -eu

# Reqs: LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
READELF=${READELF:-/usr/bin/readelf}
TMP=${TMPDIR:-/tmp}/ldx86-versym-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/a.c" <<'SRC'
extern int ext(void);
int f(void) { return ext(); }
SRC
gcc -m64 -fPIC -c -o "$TMP/a.o" "$TMP/a.c"

"$LDX" -m64 -shared -o "$TMP/out.so" "$TMP/a.o"

"$READELF" -S "$TMP/out.so" > "$TMP/sections.txt"
"$READELF" -V "$TMP/out.so" > "$TMP/verinfo.txt" 2>/dev/null || true

if ! grep -q "[[:space:]]\\.gnu.version[[:space:]]" "$TMP/sections.txt"; then
	echo "FAIL: missing .gnu.version section" >&2
	cat "$TMP/sections.txt" >&2
	exit 1
fi

if ! grep -q "Version symbols section" "$TMP/verinfo.txt"; then
	echo "FAIL: readelf -V did not detect version symbols table" >&2
	cat "$TMP/verinfo.txt" >&2
	exit 1
fi

echo "ok: dynamic output emits .gnu.version symbols table"
