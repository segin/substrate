#!/bin/sh
set -eu

# Reqs: LD-U-006

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
GNU_LD=${GNU_LD:-$(command -v ld || true)}
TMP=${TMPDIR:-/tmp}/ldx86-gotpcrelx-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

if [ -z "$GNU_LD" ]; then
	echo "FAIL: GNU ld not found in PATH" >&2
	exit 1
fi

cat > "$TMP/caller.c" <<'SRC'
extern int ext;
int f(void) { return ext; }
SRC

cat > "$TMP/def.c" <<'SRC'
int ext = 7;
SRC

gcc -m64 -O2 -fno-plt -fPIC -Wa,-mrelax-relocations=yes -c -o "$TMP/caller.o" "$TMP/caller.c"
gcc -m64 -O2 -fPIC -c -o "$TMP/def.o" "$TMP/def.c"

if ! readelf -r "$TMP/caller.o" | grep -q "R_X86_64_REX_GOTP"; then
	echo "FAIL: fixture object did not contain GOTPCRELX relocation" >&2
	readelf -r "$TMP/caller.o" >&2 || true
	exit 1
fi

# Baseline GNU ld must succeed.
"$GNU_LD" -m elf_x86_64 -static -e f -o "$TMP/gnu.out" "$TMP/caller.o" "$TMP/def.o"

# Our ld should also succeed after GOTPCRELX relaxation.
"$LDX" -m64 -static -e f -o "$TMP/our.out" "$TMP/caller.o" "$TMP/def.o"

# Ensure GOT pseudo-symbol is not left unresolved in final output.
if readelf -Ws "$TMP/our.out" | awk '$NF=="_GLOBAL_OFFSET_TABLE_" && $7=="UND"{exit 0} END{exit 1}'; then
	echo "FAIL: _GLOBAL_OFFSET_TABLE_ remained undefined after relaxation" >&2
	readelf -Ws "$TMP/our.out" >&2 || true
	exit 1
fi

# Ensure relaxed LEA encoding exists and MOV-GOT form does not.
if ! objdump -d "$TMP/our.out" | grep -q "48 8d 05"; then
	echo "FAIL: expected relaxed LEA opcode (48 8d 05) not found" >&2
	objdump -d "$TMP/our.out" >&2 || true
	exit 1
fi
if objdump -d "$TMP/our.out" | grep -q "48 8b 05"; then
	echo "FAIL: unrelaxed MOV GOTPCREL form (48 8b 05) still present" >&2
	objdump -d "$TMP/our.out" >&2 || true
	exit 1
fi

echo "ok: x86-64 GOTPCRELX static relaxation succeeds and resolves GOT pseudo-symbol"
