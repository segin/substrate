#!/bin/sh
set -eu

# Reqs: LD-U-010

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-script-diag-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.ld" <<'SRC'
INCLUDE "broken.ld"
SECTIONS { .text : { *(.text) } }
SRC

cat > "$TMP/broken.ld" <<'SRC'
SECTIONS { .data : { *(.data) } 
SRC

cat > "$TMP/a.c" <<'SRC'
int script_bad(void) { return 2; }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"

if "$LDX" -m64 -r -T "$TMP/main.ld" -o "$TMP/out.o" "$TMP/a.o" >"$TMP/stdout.txt" 2>"$TMP/stderr.txt"; then
	echo "FAIL: expected linker script parse failure" >&2
	exit 1
fi

if ! grep -q "broken.ld:[0-9][0-9]*:[0-9][0-9]*" "$TMP/stderr.txt"; then
	echo "FAIL: missing source location for included script error" >&2
	cat "$TMP/stderr.txt" >&2
	exit 1
fi
if ! grep -q "include stack:" "$TMP/stderr.txt"; then
	echo "FAIL: missing include stack diagnostics" >&2
	cat "$TMP/stderr.txt" >&2
	exit 1
fi
if ! grep -q "main.ld" "$TMP/stderr.txt"; then
	echo "FAIL: include stack missing parent script path" >&2
	cat "$TMP/stderr.txt" >&2
	exit 1
fi

echo "ok: -T parser reports include-stack source diagnostics"
