#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-mode-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
int main(void) { return 0; }
SRC

gcc -m64 -c -o "$TMP/main64.o" "$TMP/main.c"
gcc -m32 -c -o "$TMP/main32.o" "$TMP/main.c"

"$LDX" -m elf_x86_64 -r -o "$TMP/a64_1.o" "$TMP/main64.o"
"$LDX" -melf64-x86-64 -r -o "$TMP/a64_2.o" "$TMP/main64.o"
"$LDX" -m amd64 -r -o "$TMP/a64_3.o" "$TMP/main64.o"
"$LDX" -m elf_i386 -r -o "$TMP/a32_1.o" "$TMP/main32.o"
"$LDX" -melf32-i386 -r -o "$TMP/a32_2.o" "$TMP/main32.o"
"$LDX" -m i386 -r -o "$TMP/a32_3.o" "$TMP/main32.o"

if "$LDX" -m notreal -r -o "$TMP/bad_emu.o" "$TMP/main64.o" >"$TMP/bad_emu.err" 2>&1; then
	echo "FAIL: unsupported -m emulation unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "unsupported emulation 'notreal' for -m" "$TMP/bad_emu.err"; then
	echo "FAIL: missing unsupported emulation diagnostic" >&2
	cat "$TMP/bad_emu.err" >&2
	exit 1
fi

if "$LDX" -m32 -m64 -r -o "$TMP/bad_conflict1.o" "$TMP/main64.o" >"$TMP/bad_conflict1.err" 2>&1; then
	echo "FAIL: conflicting -m32/-m64 unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "conflicting target mode options" "$TMP/bad_conflict1.err"; then
	echo "FAIL: missing conflict diagnostic for -m32/-m64" >&2
	cat "$TMP/bad_conflict1.err" >&2
	exit 1
fi

if "$LDX" -m elf_i386 -m64 -r -o "$TMP/bad_conflict2.o" "$TMP/main64.o" >"$TMP/bad_conflict2.err" 2>&1; then
	echo "FAIL: conflicting -m/-m64 unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "conflicting target mode options" "$TMP/bad_conflict2.err"; then
	echo "FAIL: missing conflict diagnostic for -m/-m64" >&2
	cat "$TMP/bad_conflict2.err" >&2
	exit 1
fi

if "$LDX" -m -r -o "$TMP/bad_missing.o" "$TMP/main64.o" >"$TMP/bad_missing.err" 2>&1; then
	echo "FAIL: missing -m argument unexpectedly succeeded" >&2
	exit 1
fi

echo "ok: ld strict -m/-m32/-m64 parser"
