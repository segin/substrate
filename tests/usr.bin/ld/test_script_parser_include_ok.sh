#!/bin/sh
set -eu

# Reqs: LD-U-010

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-script-ok-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.ld" <<'SRC'
INCLUDE "extra.ld"
SECTIONS { .text : { *(.text) } }
SRC

cat > "$TMP/extra.ld" <<'SRC'
PHDRS { text PT_LOAD; }
SRC

cat > "$TMP/a.c" <<'SRC'
int script_ok(void) { return 1; }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"

"$LDX" -m64 -r -T "$TMP/main.ld" -o "$TMP/out.o" "$TMP/a.o"
if [ ! -f "$TMP/out.o" ]; then
	echo "FAIL: expected output object not produced" >&2
	exit 1
fi

echo "ok: -T linker script parser accepts INCLUDE and nested syntax"
