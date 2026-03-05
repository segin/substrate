#!/bin/sh
set -eu

# Reqs: LD-U-005, LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
READELF=${READELF:-/usr/bin/readelf}
TMP=${TMPDIR:-/tmp}/ldx86-verneed-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/provider.c" <<'SRC'
int versioned(void) { return 1; }
SRC

cat > "$TMP/provider.map" <<'SRC'
VERS_1 {
	global:
		versioned;
	local:
		*;
};
SRC

gcc -m64 -fPIC -c -o "$TMP/provider.o" "$TMP/provider.c"
gcc -shared -Wl,--version-script="$TMP/provider.map" -o "$TMP/libvers.so" "$TMP/provider.o"

cat > "$TMP/caller.c" <<'SRC'
extern int versioned_v1(void);
__asm__(".symver versioned_v1,versioned@VERS_1");
int call_v1(void) { return versioned_v1(); }
SRC
gcc -m64 -fPIC -c -o "$TMP/caller.o" "$TMP/caller.c"

"$LDX" -m64 -shared --as-needed -o "$TMP/out.so" "$TMP/caller.o" -L"$TMP" -lvers

"$READELF" -S "$TMP/out.so" > "$TMP/sections.txt"
"$READELF" -V "$TMP/out.so" > "$TMP/verinfo.txt" 2>/dev/null || true
"$READELF" -d "$TMP/out.so" > "$TMP/dynamic.txt"

if ! grep -q "[[:space:]]\\.gnu.version_r[[:space:]]" "$TMP/sections.txt"; then
	echo "FAIL: missing .gnu.version_r section" >&2
	cat "$TMP/sections.txt" >&2
	exit 1
fi
if ! grep -q "Version needs section" "$TMP/verinfo.txt"; then
	echo "FAIL: readelf did not decode version needs section" >&2
	cat "$TMP/verinfo.txt" >&2
	exit 1
fi
if ! grep -q "VERS_1" "$TMP/verinfo.txt"; then
	echo "FAIL: version name VERS_1 missing from version needs table" >&2
	cat "$TMP/verinfo.txt" >&2
	exit 1
fi
if ! grep -q "libvers.so" "$TMP/verinfo.txt"; then
	echo "FAIL: provider library missing from version needs table" >&2
	cat "$TMP/verinfo.txt" >&2
	exit 1
fi
if ! grep -q "(VERNEED)" "$TMP/dynamic.txt" || ! grep -q "(VERNEEDNUM)" "$TMP/dynamic.txt"; then
	echo "FAIL: DT_VERNEED/DT_VERNEEDNUM tags not emitted" >&2
	cat "$TMP/dynamic.txt" >&2
	exit 1
fi

echo "ok: ld emits GNU version need metadata from versioned undefined references"
