#!/bin/sh
set -eu

# Reqs: LD-U-009 LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
READELF=${READELF:-/usr/bin/readelf}
TMP=${TMPDIR:-/tmp}/ldx86-tls-seg32-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/tls_mix.c" <<'SRC'
__thread int local_tls = 7;
extern __thread int ext_tls;
int f(void) { return local_tls + ext_tls; }
SRC

gcc -m32 -O2 -fPIC -c -o "$TMP/tls_mix.o" "$TMP/tls_mix.c"
"$LDX" -m32 -shared -o "$TMP/tls_mix.so" "$TMP/tls_mix.o"

"$READELF" -l "$TMP/tls_mix.so" > "$TMP/phdrs.txt"
"$READELF" -d "$TMP/tls_mix.so" > "$TMP/dynamic.txt"
"$READELF" -r "$TMP/tls_mix.so" > "$TMP/relocs.txt"

if ! grep -q " TLS " "$TMP/phdrs.txt"; then
	echo "FAIL: missing PT_TLS segment" >&2
	cat "$TMP/phdrs.txt" >&2
	exit 1
fi

for tag in "(REL)" "(RELSZ)" "(RELENT)"; do
	if ! grep -q "$tag" "$TMP/dynamic.txt"; then
		echo "FAIL: missing dynamic tag $tag" >&2
		cat "$TMP/dynamic.txt" >&2
		exit 1
	fi
done

for t in "R_386_TLS_DTPMOD3" "R_386_TLS_DTPOFF3"; do
	if ! grep -q "$t" "$TMP/relocs.txt"; then
		echo "FAIL: missing TLS relocation $t" >&2
		cat "$TMP/relocs.txt" >&2
		exit 1
	fi
done

echo "ok: i386 TLS output includes PT_TLS and TLS runtime dynamic tags"
