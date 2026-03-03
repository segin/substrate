#!/bin/sh
set -eu

# Reqs: LD-U-008, LD-S-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-merge-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/a.s" <<'SRC'
.section .merge,"a",@progbits
.p2align 4
.globl merge_a
merge_a:
.byte 0x11
SRC

cat > "$TMP/b.s" <<'SRC'
.section .merge,"aw",@progbits
.p2align 5
.globl merge_b
merge_b:
.byte 0x22
SRC

as --64 -o "$TMP/a.o" "$TMP/a.s"
as --64 -o "$TMP/b.o" "$TMP/b.s"

"$LDX" -m64 -r -o "$TMP/out.o" "$TMP/a.o" "$TMP/b.o"
readelf -SW "$TMP/out.o" > "$TMP/out.sec"

if ! grep -E "[[:space:]]\\.merge[[:space:]].*[[:space:]]WA[[:space:]]" "$TMP/out.sec" >/dev/null; then
	echo "FAIL: merged .merge section did not union input flags to WA" >&2
	cat "$TMP/out.sec" >&2
	exit 1
fi

if ! grep -E "[[:space:]]\\.merge[[:space:]].*[[:space:]]32$" "$TMP/out.sec" >/dev/null; then
	echo "FAIL: merged .merge section did not keep max alignment (expected 32)" >&2
	cat "$TMP/out.sec" >&2
	exit 1
fi

cat > "$TMP/c.s" <<'SRC'
.section .merge,"aw",@nobits
.globl merge_c
merge_c:
.space 4
SRC
as --64 -o "$TMP/c.o" "$TMP/c.s"

if "$LDX" -m64 -r -o "$TMP/out_bad.o" "$TMP/a.o" "$TMP/c.o" >"$TMP/out_bad.log" 2>&1; then
	echo "FAIL: merge should fail on section type mismatch for same output section name" >&2
	cat "$TMP/out_bad.log" >&2
	exit 1
fi

echo "ok: ld merge policy unions flags, keeps max alignment, and rejects type mismatch"
