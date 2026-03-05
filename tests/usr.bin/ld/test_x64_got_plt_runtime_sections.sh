#!/bin/sh
set -eu

# Reqs: LD-S-003 LD-U-006

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
READELF=${READELF:-/usr/bin/readelf}
TMP=${TMPDIR:-/tmp}/ldx86-gotplt-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
extern int puts(const char *);
extern int extvar;
int _start(void) { return puts("x") + extvar; }
SRC
gcc -m64 -fPIC -c -o "$TMP/main.o" "$TMP/main.c"

"$LDX" -m64 -shared -o "$TMP/out.so" "$TMP/main.o"

"$READELF" -S "$TMP/out.so" > "$TMP/sections.txt"
"$READELF" -d "$TMP/out.so" > "$TMP/dynamic.txt"
"$READELF" -r "$TMP/out.so" > "$TMP/relocs.txt"

for sec in ".plt" ".got.plt" ".rela.plt" ".got" ".rela.dyn"; do
	if ! grep -q "[[:space:]]$sec[[:space:]]" "$TMP/sections.txt"; then
		echo "FAIL: missing section $sec" >&2
		cat "$TMP/sections.txt" >&2
		exit 1
	fi
done

for tag in "(PLTGOT)" "(PLTREL)" "(PLTRELSZ)" "(JMPREL)" "(RELA)" "(RELASZ)" "(RELAENT)"; do
	if ! grep -q "$tag" "$TMP/dynamic.txt"; then
		echo "FAIL: missing dynamic tag $tag" >&2
		cat "$TMP/dynamic.txt" >&2
		exit 1
	fi
done

if ! grep -q "R_X86_64_JUMP_SLO" "$TMP/relocs.txt"; then
	echo "FAIL: expected JUMP_SLOT relocations in .rela.plt" >&2
	cat "$TMP/relocs.txt" >&2
	exit 1
fi
if ! grep -q "R_X86_64_GLOB_DAT" "$TMP/relocs.txt"; then
	echo "FAIL: expected GLOB_DAT relocations in .rela.dyn" >&2
	cat "$TMP/relocs.txt" >&2
	exit 1
fi

echo "ok: x86_64 dynamic links synthesize GOT/PLT and runtime relocation tables"
