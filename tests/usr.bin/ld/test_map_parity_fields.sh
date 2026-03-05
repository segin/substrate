#!/bin/sh
set -eu

# Reqs: LD-U-011

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-map-parity-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/a.c" <<'SRC'
int map_symbol_a(void) { return 11; }
SRC
cat > "$TMP/b.c" <<'SRC'
int map_symbol_a(void);
int map_symbol_b(void) { return map_symbol_a(); }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"
gcc -m64 -c -o "$TMP/b.o" "$TMP/b.c"

"$LDX" -m64 -r -Map "$TMP/out.map" -o "$TMP/out.o" "$TMP/a.o" "$TMP/b.o"

if ! grep -q '^Program Headers:' "$TMP/out.map"; then
	echo "FAIL: map file missing Program Headers section" >&2
	sed -n '1,160p' "$TMP/out.map" >&2
	exit 1
fi
if ! grep -q 'map_symbol_a' "$TMP/out.map"; then
	echo "FAIL: map file missing symbol entries" >&2
	sed -n '1,200p' "$TMP/out.map" >&2
	exit 1
fi
if ! grep -Eq 'map_symbol_a.*source=' "$TMP/out.map"; then
	echo "FAIL: map file missing symbol provenance field" >&2
	sed -n '1,240p' "$TMP/out.map" >&2
	exit 1
fi

echo "ok: map file includes section/symbol/provenance/program-header parity fields"
