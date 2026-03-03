#!/bin/sh
set -eu

# Reqs: LD-U-006

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
GNU_LD=${GNU_LD:-$(command -v ld || true)}
TMP=${TMPDIR:-/tmp}/ldx86-rel-addend-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

if [ -z "$GNU_LD" ]; then
	echo "FAIL: GNU ld not found in PATH" >&2
	exit 1
fi

cat > "$TMP/rel_addend_i386.s" <<'SRC'
.globl _start
_start:
	nop
.section .data
.globl ptr
ptr:
	.long target + 0x80000000
SRC
as --32 -o "$TMP/rel_addend_i386.o" "$TMP/rel_addend_i386.s"

if ! "$GNU_LD" -m elf_i386 -static -e _start --defsym target=0 -o "$TMP/gnu.out" "$TMP/rel_addend_i386.o"; then
	echo "FAIL: GNU ld baseline link failed unexpectedly" >&2
	exit 1
fi
if ! "$LDX" -m32 -static -e _start --defsym target=0 -o "$TMP/our.out" "$TMP/rel_addend_i386.o"; then
	echo "FAIL: our ld rejected valid i386 REL addend case" >&2
	exit 1
fi

gnu_data=$(readelf -x .data "$TMP/gnu.out" | awk '/^  0x/{print $2; exit}')
our_data=$(readelf -x .data "$TMP/our.out" | awk '/^  0x/{print $2; exit}')
if [ "$gnu_data" != "$our_data" ]; then
	echo "FAIL: i386 REL addend handling diverges from GNU ld" >&2
	echo "  gnu .data: $gnu_data" >&2
	echo "  our .data: $our_data" >&2
	exit 1
fi
if [ "$our_data" != "00000080" ]; then
	echo "FAIL: expected i386 REL addend bytes 00000080, got $our_data" >&2
	exit 1
fi

echo "ok: i386 REL addend extraction matches GNU ld for high-bit absolute addend"
