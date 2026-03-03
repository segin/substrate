#!/bin/sh
set -eu

# Reqs: LD-E-006, LD-U-010

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-reloc-diag-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/overflow.s" <<'SRC'
.globl _start
_start:
	movl $target, %eax
	ret
SRC
as --64 -o "$TMP/overflow.o" "$TMP/overflow.s"

if "$LDX" -m64 -static -e _start --defsym target=0x100000000 -o "$TMP/out_overflow" "$TMP/overflow.o" \
	>"$TMP/overflow.err" 2>&1; then
	echo "FAIL: relocation overflow link unexpectedly succeeded" >&2
	exit 1
fi

if ! grep -Eq "relocation error: section=\\.text offset=0x[0-9a-f]+ type=10 symbol=target:" "$TMP/overflow.err"; then
	echo "FAIL: relocation diagnostic missing section/offset/type/symbol context" >&2
	cat "$TMP/overflow.err" >&2
	exit 1
fi

echo "ok: relocation diagnostics include section+offset+type+symbol context"
