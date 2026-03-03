#!/bin/sh
set -eu

# Reqs: LD-U-005, LD-U-006

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-relparse-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/ref.c" <<'SRC'
extern int ext_symbol;
int use_ext(void) { return ext_symbol; }
SRC
gcc -m64 -c -o "$TMP/ref.o" "$TMP/ref.c"

"$LDX" -m64 -r -o "$TMP/out_valid.o" "$TMP/ref.o"

cp "$TMP/ref.o" "$TMP/ref_bad_reloc.o"
rela_off_hex=$(readelf -SW "$TMP/ref_bad_reloc.o" | awk '$3==".rela.text"{print "0x"$6; exit}')
if [ -z "$rela_off_hex" ]; then
	echo "FAIL: could not locate .rela.text in test object" >&2
	exit 1
fi
printf '\377\377\377\377' | dd of="$TMP/ref_bad_reloc.o" bs=1 seek=$((rela_off_hex + 8)) conv=notrunc status=none

if "$LDX" -m64 -r -o "$TMP/out_bad_reloc.o" "$TMP/ref_bad_reloc.o" >"$TMP/bad_reloc.err" 2>&1; then
	echo "FAIL: malformed relocation type unexpectedly linked" >&2
	exit 1
fi
if ! grep -q "unsupported relocation type" "$TMP/bad_reloc.err"; then
	echo "FAIL: missing malformed relocation diagnostic" >&2
	cat "$TMP/bad_reloc.err" >&2
	exit 1
fi

echo "ok: ld parses relocation/symbol metadata and rejects malformed entries"
