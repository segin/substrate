#!/bin/sh
set -eu

# Reqs: LD-U-006 LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
READELF=${READELF:-/usr/bin/readelf}
TMP=${TMPDIR:-/tmp}/ldx86-tls-ext32-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/tls_ext.c" <<'SRC'
extern __thread int ext;
extern __thread int ext_ie __attribute__((tls_model("initial-exec")));
int gd(void) { return ext; }
int ie(void) { return ext_ie; }
SRC

gcc -m32 -O2 -fPIC -c -o "$TMP/tls_ext.o" "$TMP/tls_ext.c"

"$LDX" -m32 -shared -o "$TMP/tls_ext.so" "$TMP/tls_ext.o"

"$READELF" -r "$TMP/tls_ext.so" > "$TMP/relocs.txt"

for t in "R_386_TLS_DTPMOD3" "R_386_TLS_DTPOFF3" "R_386_TLS_TPOFF32"; do
	if ! grep -q "$t" "$TMP/relocs.txt"; then
		echo "FAIL: missing TLS dynamic relocation $t" >&2
		cat "$TMP/relocs.txt" >&2
		exit 1
	fi
done

if ! grep -q "R_386_JUMP_SLO" "$TMP/relocs.txt"; then
	echo "FAIL: missing ___tls_get_addr JUMP_SLOT relocation" >&2
	cat "$TMP/relocs.txt" >&2
	exit 1
fi

echo "ok: i386 external TLS GD/IE models emit runtime TLS relocations"
