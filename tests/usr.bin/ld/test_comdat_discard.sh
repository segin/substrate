#!/bin/sh
set -eu

# Reqs: LD-U-004, LD-S-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-comdat-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/dup1.s" <<'SRC'
.section .text.dupcase,"axG",@progbits,dupsym,comdat
.weak dupsym
.type dupsym,@function
dupsym:
	mov $1, %eax
	ret
SRC

cat > "$TMP/dup2.s" <<'SRC'
.section .text.dupcase,"axG",@progbits,dupsym,comdat
.weak dupsym
.type dupsym,@function
dupsym:
	mov $2, %eax
	ret
SRC

as --64 -o "$TMP/dup1.o" "$TMP/dup1.s"
as --64 -o "$TMP/dup2.o" "$TMP/dup2.s"

"$LDX" -m64 -r -o "$TMP/out.o" "$TMP/dup1.o" "$TMP/dup2.o"

if [ "$(nm "$TMP/out.o" | awk '$3 == "dupsym" && $2 ~ /[Ww]/ { c++ } END { print c+0 }')" -ne 1 ]; then
	echo "FAIL: COMDAT duplicate discard did not keep a single dupsym definition" >&2
	nm "$TMP/out.o" >&2
	exit 1
fi

objcopy -O binary --only-section=.text.dupcase "$TMP/out.o" "$TMP/dupcase.bin"
if [ "$(wc -c < "$TMP/dupcase.bin")" -ne 6 ]; then
	echo "FAIL: COMDAT duplicate section was not discarded (expected 6-byte function body)" >&2
	od -An -tx1 -v "$TMP/dupcase.bin" >&2
	exit 1
fi
if [ "$(od -An -tx1 -v "$TMP/dupcase.bin" | tr -d ' \n')" != "b801000000c3" ]; then
	echo "FAIL: COMDAT leader selection did not keep first definition body" >&2
	od -An -tx1 -v "$TMP/dupcase.bin" >&2
	exit 1
fi

echo "ok: ld COMDAT duplicate discard keeps one group leader"
