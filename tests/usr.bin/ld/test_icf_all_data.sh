#!/bin/sh
set -eu

# Reqs: LD-O-004, LD-R-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-icf-all-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/icf_all.c" <<'SRC'
int data_a = 5;
int data_b = 5;
int _start(void) { return data_a + data_b; }
SRC

gcc -m64 -O0 -ffunction-sections -fdata-sections -c -o "$TMP/icf_all.o" "$TMP/icf_all.c"

"$LDX" -m64 -r --icf=safe -o "$TMP/out_icf_safe.o" "$TMP/icf_all.o"
"$LDX" -m64 -r --icf=all -o "$TMP/out_icf_all.o" "$TMP/icf_all.o"

a_safe=$(readelf -s "$TMP/out_icf_safe.o" | awk '$8 == "data_a" { print $7; exit }')
b_safe=$(readelf -s "$TMP/out_icf_safe.o" | awk '$8 == "data_b" { print $7; exit }')
a_all=$(readelf -s "$TMP/out_icf_all.o" | awk '$8 == "data_a" { print $7; exit }')
b_all=$(readelf -s "$TMP/out_icf_all.o" | awk '$8 == "data_b" { print $7; exit }')

if [ -z "$a_safe" ] || [ -z "$b_safe" ] || [ -z "$a_all" ] || [ -z "$b_all" ]; then
	echo "FAIL: failed to locate data_a/data_b symbol section indices" >&2
	exit 1
fi
if [ "$a_safe" = "$b_safe" ]; then
	echo "FAIL: --icf=safe should not fold writable data sections" >&2
	readelf -s "$TMP/out_icf_safe.o" >&2
	exit 1
fi
if [ "$a_all" != "$b_all" ]; then
	echo "FAIL: --icf=all did not fold identical writable data sections" >&2
	readelf -s "$TMP/out_icf_all.o" >&2
	exit 1
fi

echo "ok: ld --icf=all folds writable data while --icf=safe does not"
