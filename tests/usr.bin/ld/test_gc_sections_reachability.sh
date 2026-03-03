#!/bin/sh
set -eu

# Reqs: LD-E-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-gc-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/root.c" <<'SRC'
int used_fn(void);
int _start(void) { return used_fn(); }
SRC

cat > "$TMP/used.c" <<'SRC'
int used_fn(void) { return 1; }
int unused_fn(void) { return 2; }
SRC

gcc -m64 -ffunction-sections -fdata-sections -c -o "$TMP/root.o" "$TMP/root.c"
gcc -m64 -ffunction-sections -fdata-sections -c -o "$TMP/used.o" "$TMP/used.c"

"$LDX" -m64 -r -o "$TMP/out_keep.o" "$TMP/root.o" "$TMP/used.o"
readelf -SW "$TMP/out_keep.o" > "$TMP/out_keep.sec"
if ! grep -q "[[:space:]]\\.text.unused_fn[[:space:]]" "$TMP/out_keep.sec"; then
	echo "FAIL: baseline link missing .text.unused_fn; test setup invalid" >&2
	cat "$TMP/out_keep.sec" >&2
	exit 1
fi

"$LDX" -m64 -r --gc-sections -o "$TMP/out_gc.o" "$TMP/root.o" "$TMP/used.o"
readelf -SW "$TMP/out_gc.o" > "$TMP/out_gc.sec"
if grep -q "[[:space:]]\\.text.unused_fn[[:space:]]" "$TMP/out_gc.sec"; then
	echo "FAIL: --gc-sections did not discard unreachable .text.unused_fn" >&2
	cat "$TMP/out_gc.sec" >&2
	exit 1
fi
if ! grep -q "[[:space:]]\\.text._start[[:space:]]" "$TMP/out_gc.sec" ||
   ! grep -q "[[:space:]]\\.text.used_fn[[:space:]]" "$TMP/out_gc.sec"; then
	echo "FAIL: --gc-sections removed reachable root/target sections" >&2
	cat "$TMP/out_gc.sec" >&2
	exit 1
fi

echo "ok: ld --gc-sections builds reachability from roots and reloc references"
