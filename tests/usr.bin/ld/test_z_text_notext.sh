#!/bin/sh
set -eu

# Reqs: LD-U-009, LD-W-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-ztext-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/textrel.S" <<'SRC'
	.text
	.globl _start
_start:
	.quad ext_sym
	xorl %eax, %eax
	ret
SRC
gcc -m64 -c -o "$TMP/textrel.o" "$TMP/textrel.S"

if "$LDX" -m64 -shared --allow-undefined -z text -o "$TMP/out_text.so" "$TMP/textrel.o" >"$TMP/text.err" 2>&1; then
	echo "FAIL: -z text unexpectedly accepted text relocations" >&2
	cat "$TMP/text.err" >&2
	exit 1
fi
if ! grep -q -- "-z text rejects text relocations" "$TMP/text.err"; then
	echo "FAIL: missing -z text relocation diagnostic" >&2
	cat "$TMP/text.err" >&2
	exit 1
fi

"$LDX" -m64 -shared --allow-undefined -z notext -o "$TMP/out_notext.so" "$TMP/textrel.o" >"$TMP/notext.err" 2>&1
if ! grep -q -- "-z notext: allowing text relocations" "$TMP/notext.err"; then
	echo "FAIL: missing -z notext diagnostic line" >&2
	cat "$TMP/notext.err" >&2
	exit 1
fi

echo "ok: ld enforces -z text and permits -z notext with diagnostics"
