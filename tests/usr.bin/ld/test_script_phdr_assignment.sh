#!/bin/sh
set -eu

# Reqs: LD-U-009 LD-S-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-script-phdr-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.ld" <<'SRC'
PHDRS {
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}
SECTIONS {
	.text : { *(.text) } :text
	.data : { *(.data) } :data
}
SRC

cat > "$TMP/start.c" <<'SRC'
int g_value = 42;
void _start(void) { for (;;) { g_value++; if (g_value == 0) break; } }
SRC
gcc -m64 -ffreestanding -fno-pie -c -o "$TMP/start.o" "$TMP/start.c"

"$LDX" -m64 -T "$TMP/main.ld" -o "$TMP/out.elf" "$TMP/start.o"

readelf -l "$TMP/out.elf" > "$TMP/phdrs.txt"

load_count=$(grep -c ' LOAD ' "$TMP/phdrs.txt")
[ "$load_count" -ge 2 ] || { echo "FAIL: expected at least two PT_LOAD segments" >&2; exit 1; }
grep -q 'R E' "$TMP/phdrs.txt" || { echo "FAIL: missing executable PT_LOAD flags" >&2; exit 1; }
grep -q 'RW ' "$TMP/phdrs.txt" || { echo "FAIL: missing writable PT_LOAD flags" >&2; exit 1; }

echo "ok: PHDRS + :phdr script mapping drives program header creation"
