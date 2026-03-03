#!/bin/sh
set -eu

# Reqs: LD-U-006, LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-tls-basic-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/tls.c" <<'SRC'
__thread int t = 5;
int f(void) { return t; }
SRC

gcc -m64 -O2 -fPIC -c -o "$TMP/tls.o" "$TMP/tls.c"
if ! readelf -r "$TMP/tls.o" | grep -q "R_X86_64_TLSGD"; then
	echo "FAIL: expected R_X86_64_TLSGD relocation in TLS fixture" >&2
	readelf -r "$TMP/tls.o" >&2 || true
	exit 1
fi

"$LDX" -m64 -shared -e f -o "$TMP/tls.so" "$TMP/tls.o"

if ! readelf -l "$TMP/tls.so" | grep -q " TLS "; then
	echo "FAIL: shared output missing PT_TLS segment" >&2
	readelf -l "$TMP/tls.so" >&2 || true
	exit 1
fi

if ! readelf -Ws "$TMP/tls.so" | awk '$NF=="t" && $7!="UND"{ok=1} END{exit ok?0:1}'; then
	echo "FAIL: TLS symbol 't' missing or unresolved in output" >&2
	readelf -Ws "$TMP/tls.so" >&2 || true
	exit 1
fi

echo "ok: x86-64 TLS relocation case links and emits PT_TLS"
