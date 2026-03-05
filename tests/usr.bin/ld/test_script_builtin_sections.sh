#!/bin/sh
set -eu

# Reqs: LD-S-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-script-builtin-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.ld" <<'SRC'
text_sz = SIZEOF(.text);
text_addr = ADDR(.text);
text_load = LOADADDR(.text);
ASSERT(text_sz > 0, "SIZEOF(.text) must be non-zero");
SECTIONS { .text : { *(.text) } }
SRC

cat > "$TMP/a.c" <<'SRC'
int builtin_eval(void) { return 22; }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"

"$LDX" -m64 -r -T "$TMP/main.ld" -o "$TMP/out.o" "$TMP/a.o"

readelf -s "$TMP/out.o" > "$TMP/syms.txt"
sz_hex=$(awk '$8=="text_sz"{print $2; exit}' "$TMP/syms.txt")
[ -n "$sz_hex" ] || { echo "FAIL: missing text_sz symbol" >&2; exit 1; }
[ "$sz_hex" != "0000000000000000" ] || { echo "FAIL: expected non-zero text_sz" >&2; exit 1; }

echo "ok: linker script SIZEOF/ADDR/LOADADDR builtins are parsed and evaluated"
