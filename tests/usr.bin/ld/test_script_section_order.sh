#!/bin/sh
set -eu

# Reqs: LD-S-004 LD-U-009

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-script-order-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.ld" <<'SRC'
SECTIONS {
	.data : { *(.data) }
	.text : { *(.text) }
}
SRC

cat > "$TMP/a.c" <<'SRC'
int g_data = 1;
int order_fn(void) { return g_data; }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"

"$LDX" -m64 -r -T "$TMP/main.ld" -o "$TMP/out.o" "$TMP/a.o"

readelf -S "$TMP/out.o" > "$TMP/sects.txt"
data_idx=$(awk '$3==".data"{gsub(/\[/, "", $2); gsub(/\]/, "", $2); print $2; exit}' "$TMP/sects.txt")
text_idx=$(awk '$3==".text"{gsub(/\[/, "", $2); gsub(/\]/, "", $2); print $2; exit}' "$TMP/sects.txt")

[ -n "$data_idx" ] || { echo "FAIL: missing .data section" >&2; exit 1; }
[ -n "$text_idx" ] || { echo "FAIL: missing .text section" >&2; exit 1; }
[ "$data_idx" -lt "$text_idx" ] || { echo "FAIL: expected .data before .text (got $data_idx vs $text_idx)" >&2; exit 1; }

echo "ok: SECTIONS script order drives output section ordering"
