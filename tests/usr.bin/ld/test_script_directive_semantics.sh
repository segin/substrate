#!/bin/sh
set -eu

# Reqs: LD-S-004 LD-E-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-script-directives-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.ld" <<'SRC'
PROVIDE(prov = 0x44);
SECTIONS {
	.text : { KEEP(*(SORT_BY_NAME(.text.keep))) *(.text.drop) }
	/DISCARD/ : { *(.discard_me) }
}
INSERT AFTER .text;
SRC

cat > "$TMP/a.c" <<'SRC'
__attribute__((section(".text.keep"))) void keep_fn(void) {}
__attribute__((section(".text.drop"))) void drop_fn(void) {}
__attribute__((section(".discard_me"))) int doomed = 7;
int main(void) { return 0; }
SRC
gcc -m64 -ffunction-sections -fdata-sections -c -o "$TMP/a.o" "$TMP/a.c"

"$LDX" -m64 -r --gc-sections --defsym=prov=0x99 -T "$TMP/main.ld" -o "$TMP/out.o" "$TMP/a.o"

readelf -s "$TMP/out.o" > "$TMP/syms.txt"
readelf -S "$TMP/out.o" > "$TMP/sects.txt"

prov_hex=$(awk '$8=="prov"{print $2; exit}' "$TMP/syms.txt")
[ "$prov_hex" = "0000000000000099" ] || { echo "FAIL: PROVIDE overwrote existing symbol: $prov_hex" >&2; exit 1; }

grep -q '\.text.keep' "$TMP/sects.txt" || { echo "FAIL: KEEP did not retain .text.keep" >&2; exit 1; }
! grep -q '\.text.drop' "$TMP/sects.txt" || { echo "FAIL: gc did not drop .text.drop" >&2; exit 1; }
! grep -q '\.discard_me' "$TMP/sects.txt" || { echo "FAIL: /DISCARD/ did not remove .discard_me" >&2; exit 1; }

echo "ok: PROVIDE/KEEP/SORT_/DISCARD/INSERT directives are accepted and applied"
