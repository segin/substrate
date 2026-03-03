#!/bin/sh
set -eu

# Reqs: LD-U-005, LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-ver-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/provider.c" <<'SRC'
int versioned(void) { return 1; }
int versioned2(void) { return 2; }
SRC

cat > "$TMP/provider.map" <<'SRC'
VERS_1 {
	global:
		versioned;
	local:
		*;
};
VERS_2 {
	global:
		versioned2;
} VERS_1;
SRC

gcc -m64 -fPIC -c -o "$TMP/provider.o" "$TMP/provider.c"
gcc -shared -Wl,--version-script="$TMP/provider.map" -o "$TMP/libvers.so" "$TMP/provider.o"

cat > "$TMP/caller_v1.c" <<'SRC'
extern int versioned_v1(void);
__asm__(".symver versioned_v1,versioned@VERS_1");
int call_v1(void) { return versioned_v1(); }
SRC
gcc -m64 -c -o "$TMP/caller_v1.o" "$TMP/caller_v1.c"

"$LDX" -m64 -shared --as-needed -o "$TMP/out_v1.so" "$TMP/caller_v1.o" -L"$TMP" -lvers
readelf -d "$TMP/out_v1.so" > "$TMP/out_v1.dynamic"
if ! grep -q "Shared library: \\[libvers.so\\]" "$TMP/out_v1.dynamic"; then
	echo "FAIL: versioned reference versioned@VERS_1 did not select DSO provider" >&2
	cat "$TMP/out_v1.dynamic" >&2
	exit 1
fi

cat > "$TMP/caller_bad.c" <<'SRC'
extern int versioned_bad(void);
__asm__(".symver versioned_bad,versioned@VERS_DOES_NOT_EXIST");
int call_bad(void) { return versioned_bad(); }
SRC
gcc -m64 -c -o "$TMP/caller_bad.o" "$TMP/caller_bad.c"

"$LDX" -m64 -shared --as-needed -o "$TMP/out_bad.so" "$TMP/caller_bad.o" -L"$TMP" -lvers
readelf -d "$TMP/out_bad.so" > "$TMP/out_bad.dynamic"
if grep -q "Shared library: \\[libvers.so\\]" "$TMP/out_bad.dynamic"; then
	echo "FAIL: unmatched symbol version should not select DSO provider under --as-needed" >&2
	cat "$TMP/out_bad.dynamic" >&2
	exit 1
fi

cat > "$TMP/caller_plain.c" <<'SRC'
extern int versioned(void);
int call_plain(void) { return versioned(); }
SRC
gcc -m64 -c -o "$TMP/caller_plain.o" "$TMP/caller_plain.c"

"$LDX" -m64 -shared --as-needed -o "$TMP/out_plain.so" "$TMP/caller_plain.o" -L"$TMP" -lvers
readelf -d "$TMP/out_plain.so" > "$TMP/out_plain.dynamic"
if ! grep -q "Shared library: \\[libvers.so\\]" "$TMP/out_plain.dynamic"; then
	echo "FAIL: unversioned reference did not resolve through default symbol version" >&2
	cat "$TMP/out_plain.dynamic" >&2
	exit 1
fi

echo "ok: ld resolves DSO providers with GNU symbol version awareness"
