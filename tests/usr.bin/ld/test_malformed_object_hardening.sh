#!/bin/sh
set -eu

# Reqs: LD-R-001, LD-R-003, LD-R-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-malformed-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/ref.c" <<'SRC'
extern int ext_symbol;
int use_ext(void) { return ext_symbol; }
SRC
gcc -m64 -c -o "$TMP/ref.o" "$TMP/ref.c"

head -c 64 "$TMP/ref.o" > "$TMP/ref_truncated.o"
if "$LDX" -m64 -r -o "$TMP/out_truncated.o" "$TMP/ref_truncated.o" >"$TMP/truncated.err" 2>&1; then
	echo "FAIL: truncated ELF unexpectedly linked" >&2
	exit 1
fi

cp "$TMP/ref.o" "$TMP/ref_bad_off.o"
rela_off_hex=$(readelf -SW "$TMP/ref_bad_off.o" | awk '$3==".rela.text"{print "0x"$6; exit}')
if [ -z "$rela_off_hex" ]; then
	echo "FAIL: could not locate .rela.text in malformed-input test object" >&2
	exit 1
fi
printf '\377\377\377\377\377\377\377\377' | dd of="$TMP/ref_bad_off.o" bs=1 seek=$((rela_off_hex + 0)) conv=notrunc status=none
if "$LDX" -m64 -r -o "$TMP/out_bad_off.o" "$TMP/ref_bad_off.o" >"$TMP/bad_off.err" 2>&1; then
	echo "FAIL: relocation-offset-corrupted ELF unexpectedly linked" >&2
	exit 1
fi
if ! grep -Eq "out-of-range relocation offset|exceeding section bounds" "$TMP/bad_off.err"; then
	echo "FAIL: missing relocation offset hardening diagnostic" >&2
	cat "$TMP/bad_off.err" >&2
	exit 1
fi

dd if=/dev/urandom of="$TMP/random_blob.o" bs=1 count=512 status=none
if "$LDX" -m64 -r -o "$TMP/out_random.o" "$TMP/random_blob.o" >"$TMP/random.err" 2>&1; then
	echo "FAIL: random blob unexpectedly linked as object" >&2
	exit 1
fi

echo "ok: ld malformed-object hardening paths reject truncated/corrupt/fuzzed inputs"
