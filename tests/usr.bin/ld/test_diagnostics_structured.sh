#!/bin/sh
set -eu

# Reqs: LD-U-010

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-diag-structured-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/start.S" <<'SRC'
	.text
	.globl _start
_start:
	call missing_symbol
	xor %rdi, %rdi
	mov $60, %rax
	syscall
SRC
gcc -m64 -c -o "$TMP/start.o" "$TMP/start.S"

if "$LDX" -m64 -o "$TMP/fail.out" "$TMP/start.o" >"$TMP/unres.log" 2>&1; then
	echo "FAIL: unresolved link unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q 'undefined reference to `missing_symbol`' "$TMP/unres.log"; then
	echo "FAIL: unresolved reference diagnostic missing" >&2
	sed -n '1,120p' "$TMP/unres.log" >&2
	exit 1
fi
if ! grep -q 'note: category=unresolved-symbol' "$TMP/unres.log"; then
	echo "FAIL: structured unresolved diagnostic note missing" >&2
	sed -n '1,120p' "$TMP/unres.log" >&2
	exit 1
fi

cat > "$TMP/bad.ld" <<'SRC'
SECTIONS {
  .text : { *(.text)
}
SRC
cat > "$TMP/base.c" <<'SRC'
int foo(void) { return 1; }
SRC
gcc -m64 -c -o "$TMP/base.o" "$TMP/base.c"

if "$LDX" -m64 -r -T "$TMP/bad.ld" -o "$TMP/bad.o" "$TMP/base.o" >"$TMP/script.log" 2>&1; then
	echo "FAIL: invalid linker script unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q 'linker script parse error' "$TMP/script.log"; then
	echo "FAIL: linker script parse diagnostic missing" >&2
	sed -n '1,120p' "$TMP/script.log" >&2
	exit 1
fi
if ! grep -q 'note: category=script-parse' "$TMP/script.log"; then
	echo "FAIL: structured script parse note missing" >&2
	sed -n '1,120p' "$TMP/script.log" >&2
	exit 1
fi

echo "ok: structured diagnostics include category/source/hint notes"
