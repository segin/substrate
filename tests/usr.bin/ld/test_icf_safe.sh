#!/bin/sh
set -eu

# Reqs: LD-O-004, LD-U-007

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-icf-safe-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/icf_safe.c" <<'SRC'
int foo(void) { return 7; }
int bar(void) { return 7; }
int _start(void) { return foo() + bar(); }
SRC

gcc -m64 -O0 -ffunction-sections -fdata-sections -c -o "$TMP/icf_safe.o" "$TMP/icf_safe.c"

"$LDX" -m64 -r -o "$TMP/out_no_icf.o" "$TMP/icf_safe.o"
"$LDX" -m64 -r --icf=safe -o "$TMP/out_icf_safe.o" "$TMP/icf_safe.o"

foo_no=$(readelf -s "$TMP/out_no_icf.o" | awk '$8 == "foo" { print $7; exit }')
bar_no=$(readelf -s "$TMP/out_no_icf.o" | awk '$8 == "bar" { print $7; exit }')
foo_safe=$(readelf -s "$TMP/out_icf_safe.o" | awk '$8 == "foo" { print $7; exit }')
bar_safe=$(readelf -s "$TMP/out_icf_safe.o" | awk '$8 == "bar" { print $7; exit }')

if [ -z "$foo_no" ] || [ -z "$bar_no" ] || [ -z "$foo_safe" ] || [ -z "$bar_safe" ]; then
	echo "FAIL: failed to locate foo/bar symbol section indices" >&2
	exit 1
fi
if [ "$foo_no" = "$bar_no" ]; then
	echo "FAIL: baseline unexpectedly has foo/bar already folded" >&2
	readelf -s "$TMP/out_no_icf.o" >&2
	exit 1
fi
if [ "$foo_safe" != "$bar_safe" ]; then
	echo "FAIL: --icf=safe did not fold identical function sections" >&2
	readelf -s "$TMP/out_icf_safe.o" >&2
	exit 1
fi

echo "ok: ld --icf=safe folds identical function sections"
