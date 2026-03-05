#!/bin/sh
set -eu

# Reqs: LD-U-009

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-note-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/min.s" <<'SRC'
.text
.globl _start
.type _start,@function
_start:
	xor %eax, %eax
	ret
SRC

as --64 -o "$TMP/min.o" "$TMP/min.s"
"$LDX" -m64 -o "$TMP/min.out" "$TMP/min.o"

if ! readelf -SW "$TMP/min.out" | grep -q "\.note\.substrate_ld"; then
	echo "FAIL: missing .note.substrate_ld section in linked executable" >&2
	readelf -SW "$TMP/min.out" >&2
	exit 1
fi

if ! strings -a "$TMP/min.out" | grep -q "^Substrate Linker v0.1$"; then
	echo "FAIL: .note.substrate_ld is missing expected linker fingerprint string" >&2
	readelf -x .note.substrate_ld "$TMP/min.out" >&2 || true
	exit 1
fi

echo "ok: ld emits .note.substrate_ld in final executable output"
