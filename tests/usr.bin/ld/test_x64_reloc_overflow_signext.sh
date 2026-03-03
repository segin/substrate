#!/bin/sh
set -eu

# Reqs: LD-E-006, LD-U-006

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
GNU_LD=${GNU_LD:-$(command -v ld || true)}
TMP=${TMPDIR:-/tmp}/ldx86-x64-reloc-range-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

if [ -z "$GNU_LD" ]; then
	echo "FAIL: GNU ld not found in PATH" >&2
	exit 1
fi

cat > "$TMP/rel32s.s" <<'SRC'
.globl _start
_start:
	mov $target, %rax
	ret
SRC

cat > "$TMP/rel32.s" <<'SRC'
.globl _start
_start:
	mov $target, %eax
	ret
SRC

as --64 -o "$TMP/rel32s.o" "$TMP/rel32s.s"
as --64 -o "$TMP/rel32.o" "$TMP/rel32.s"

# R_X86_64_32S must reject values outside signed 32-bit range.
if "$GNU_LD" -m elf_x86_64 -static -e _start --defsym target=0x80000000 -o "$TMP/gnu_32s_fail" "$TMP/rel32s.o" \
	>"$TMP/gnu_32s_fail.err" 2>&1; then
	echo "FAIL: GNU ld unexpectedly accepted out-of-range R_X86_64_32S value" >&2
	exit 1
fi
if "$LDX" -m64 -static -e _start --defsym target=0x80000000 -o "$TMP/our_32s_fail" "$TMP/rel32s.o" \
	>"$TMP/our_32s_fail.err" 2>&1; then
	echo "FAIL: our ld unexpectedly accepted out-of-range R_X86_64_32S value" >&2
	exit 1
fi

# R_X86_64_32 accepts full unsigned 32-bit range including 0x80000000.
"$GNU_LD" -m elf_x86_64 -static -e _start --defsym target=0x80000000 -o "$TMP/gnu_32_pass" "$TMP/rel32.o"
"$LDX" -m64 -static -e _start --defsym target=0x80000000 -o "$TMP/our_32_pass" "$TMP/rel32.o"

# R_X86_64_32 must reject 33-bit values.
if "$GNU_LD" -m elf_x86_64 -static -e _start --defsym target=0x100000000 -o "$TMP/gnu_32_fail" "$TMP/rel32.o" \
	>"$TMP/gnu_32_fail.err" 2>&1; then
	echo "FAIL: GNU ld unexpectedly accepted out-of-range R_X86_64_32 value" >&2
	exit 1
fi
if "$LDX" -m64 -static -e _start --defsym target=0x100000000 -o "$TMP/our_32_fail" "$TMP/rel32.o" \
	>"$TMP/our_32_fail.err" 2>&1; then
	echo "FAIL: our ld unexpectedly accepted out-of-range R_X86_64_32 value" >&2
	exit 1
fi

echo "ok: x86-64 relocation signed/unsigned range behavior matches GNU ld"
