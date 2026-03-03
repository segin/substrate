#!/bin/sh
set -eu

# Reqs: LD-U-001, LD-U-009

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-layout-base-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/base.s" <<'SRC'
.text
.globl _start
.type _start,@function
_start:
	ret
SRC

as --64 -o "$TMP/base.o" "$TMP/base.s"

"$LDX" -m64 -o "$TMP/base_exec.out" "$TMP/base.o"
"$LDX" -m64 -shared -o "$TMP/base_shared.so" "$TMP/base.o"

exec_vaddr_hex=$(readelf -Wl "$TMP/base_exec.out" | awk '$1 == "LOAD" { print $3; exit }')
shared_off_hex=$(readelf -Wl "$TMP/base_shared.so" | awk '$1 == "LOAD" { print $2; exit }')
shared_vaddr_hex=$(readelf -Wl "$TMP/base_shared.so" | awk '$1 == "LOAD" { print $3; exit }')

if [ -z "$exec_vaddr_hex" ] || [ -z "$shared_off_hex" ] || [ -z "$shared_vaddr_hex" ]; then
	echo "FAIL: missing PT_LOAD segments for base address validation" >&2
	readelf -Wl "$TMP/base_exec.out" >&2
	readelf -Wl "$TMP/base_shared.so" >&2
	exit 1
fi

exec_vaddr=$((exec_vaddr_hex))
shared_off=$((shared_off_hex))
shared_vaddr=$((shared_vaddr_hex))

if [ "$exec_vaddr" -lt $((0x400000)) ]; then
	echo "FAIL: ET_EXEC default base below expected x86-64 default (0x400000)" >&2
	readelf -Wl "$TMP/base_exec.out" >&2
	exit 1
fi
if [ "$shared_vaddr" -ge $((0x1000)) ] || [ "$shared_vaddr" -ne "$shared_off" ]; then
	echo "FAIL: ET_DYN default layout should be near-zero and offset-relative, got off=$shared_off_hex vaddr=$shared_vaddr_hex" >&2
	readelf -Wl "$TMP/base_shared.so" >&2
	exit 1
fi

echo "ok: ld applies expected default base policy for ET_EXEC and ET_DYN"
