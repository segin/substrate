#!/bin/sh
set -eu

# Reqs: LD-U-001, LD-E-001, LD-U-007

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-entry-fallback-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

entry_hex() {
	readelf -h "$1" | awk '/Entry point address:/{print $4}'
}

text_hex() {
	readelf -SW "$1" | awk '/\] \.text[[:space:]]/ {print "0x"$5; exit}'
}

norm_hex() {
	printf "0x%x" $(($1))
}

cat > "$TMP/start_only.s" <<'SRC'
.globl start
	.byte 0x90
	.byte 0x90
start:
	ret
SRC
as --64 -o "$TMP/start_only.o" "$TMP/start_only.s"
"$LDX" -m64 -static -o "$TMP/start_only.out" "$TMP/start_only.o"
start_text=$(norm_hex "$(text_hex "$TMP/start_only.out")")
start_expect=$(printf "0x%x" $((start_text + 2)))
if [ "$(norm_hex "$(entry_hex "$TMP/start_only.out")")" != "$start_expect" ]; then
	echo "FAIL: implicit entry fallback to 'start' did not match symbol address" >&2
	exit 1
fi

cat > "$TMP/foo_only.s" <<'SRC'
.globl foo
	.byte 0x90
	.byte 0x90
	.byte 0x90
foo:
	ret
SRC
as --64 -o "$TMP/foo_only.o" "$TMP/foo_only.s"
"$LDX" -m64 -static -o "$TMP/foo_only.out" "$TMP/foo_only.o"
foo_text=$(norm_hex "$(text_hex "$TMP/foo_only.out")")
foo_expect=$(printf "0x%x" $((foo_text + 3)))
if [ "$(norm_hex "$(entry_hex "$TMP/foo_only.out")")" != "$foo_expect" ]; then
	echo "FAIL: implicit entry fallback to first function symbol failed" >&2
	exit 1
fi

cat > "$TMP/no_symbols.s" <<'SRC'
.text
local_entry:
	.byte 0xc3
SRC
as --64 -o "$TMP/no_symbols.o" "$TMP/no_symbols.s"
"$LDX" -m64 -static -o "$TMP/no_symbols.out" "$TMP/no_symbols.o"
if [ "$(norm_hex "$(entry_hex "$TMP/no_symbols.out")")" != "$(norm_hex "$(text_hex "$TMP/no_symbols.out")")" ]; then
	echo "FAIL: implicit entry fallback to .text address failed" >&2
	exit 1
fi

# Explicit entry must still fail if missing.
if "$LDX" -m64 -static -e definitely_missing -o "$TMP/explicit_fail.out" "$TMP/foo_only.o" \
	>"$TMP/explicit_fail.err" 2>&1; then
	echo "FAIL: explicit missing entry unexpectedly succeeded" >&2
	exit 1
fi

# ET_DYN and ET_REL contexts should not require an entry symbol.
"$LDX" -m64 -shared -o "$TMP/shared_ok.so" "$TMP/no_symbols.o"
"$LDX" -m64 -r -o "$TMP/rel_ok.o" "$TMP/no_symbols.o"

echo "ok: entry fallback matrix passes for ET_EXEC/ET_DYN/ET_REL"
