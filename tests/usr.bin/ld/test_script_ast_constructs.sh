#!/bin/sh
set -eu

# Reqs: LD-S-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-script-ast-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.ld" <<'SRC'
MEMORY { RAM (rwx) : ORIGIN = 0x1000, LENGTH = 0x2000 }
PHDRS { text PT_LOAD; }
foo = 0x1234;
ASSERT(foo != 0, "foo must be non-zero");
SECTIONS { .text : { *(.text) } > RAM :text }
SRC

cat > "$TMP/a.c" <<'SRC'
int ast_constructs(void) { return 3; }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"

"$LDX" -m64 -r -T "$TMP/main.ld" -o "$TMP/out.o" "$TMP/a.o"
if [ ! -f "$TMP/out.o" ]; then
	echo "FAIL: expected output object not produced" >&2
	exit 1
fi

echo "ok: parser handles MEMORY/PHDRS/assignments/ASSERT/SECTIONS constructs"
