#!/bin/sh
set -eu

# Reqs: LD-U-006, LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-i386-tls-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/tls.c" <<'SRC'
__thread int t = 5;
int f(void) { return t; }
SRC

gcc -m32 -O2 -fPIC -c -o "$TMP/tls.o" "$TMP/tls.c"
if ! readelf -r "$TMP/tls.o" | grep -q "R_386_TLS_GD"; then
	echo "FAIL: expected R_386_TLS_GD relocation in i386 TLS fixture" >&2
	readelf -r "$TMP/tls.o" >&2 || true
	exit 1
fi

"$LDX" -m32 -shared -e f -o "$TMP/tls.so" "$TMP/tls.o"

if ! readelf -l "$TMP/tls.so" 2>/dev/null | grep -q " TLS "; then
	echo "FAIL: i386 shared output missing PT_TLS segment" >&2
	readelf -l "$TMP/tls.so" >&2 || true
	exit 1
fi

if ! readelf -Ws "$TMP/tls.so" 2>/dev/null | awk '$NF=="t" && $7!="UND"{ok=1} END{exit ok?0:1}'; then
	echo "FAIL: i386 TLS symbol 't' missing or unresolved in output" >&2
	readelf -Ws "$TMP/tls.so" >&2 || true
	exit 1
fi

echo "ok: i386 TLS relocation case links and emits PT_TLS"
