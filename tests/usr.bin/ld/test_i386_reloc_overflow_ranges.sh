#!/bin/sh
set -eu

# Reqs: LD-E-006, LD-U-006

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
GNU_LD=${GNU_LD:-$(command -v ld || true)}
TMP=${TMPDIR:-/tmp}/ldx86-i386-reloc-range-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

if [ -z "$GNU_LD" ]; then
	echo "FAIL: GNU ld not found in PATH" >&2
	exit 1
fi

cat > "$TMP/rel8.s" <<'SRC'
.globl _start
_start:
	nop
.section .data
v8:
	.byte target
SRC

cat > "$TMP/rel16.s" <<'SRC'
.globl _start
_start:
	nop
.section .data
v16:
	.word target
SRC

cat > "$TMP/rel32.s" <<'SRC'
.globl _start
_start:
	nop
.section .data
v32:
	.long target
SRC

as --32 -o "$TMP/rel8.o" "$TMP/rel8.s"
as --32 -o "$TMP/rel16.o" "$TMP/rel16.s"
as --32 -o "$TMP/rel32.o" "$TMP/rel32.s"

# In-range values must succeed on both linkers.
"$GNU_LD" -m elf_i386 -static -e _start --defsym target=0xff -o "$TMP/gnu_8_ok" "$TMP/rel8.o"
"$LDX" -m32 -static -e _start --defsym target=0xff -o "$TMP/our_8_ok" "$TMP/rel8.o"

"$GNU_LD" -m elf_i386 -static -e _start --defsym target=0xffff -o "$TMP/gnu_16_ok" "$TMP/rel16.o"
"$LDX" -m32 -static -e _start --defsym target=0xffff -o "$TMP/our_16_ok" "$TMP/rel16.o"

"$GNU_LD" -m elf_i386 -static -e _start --defsym target=0xffffffff -o "$TMP/gnu_32_ok" "$TMP/rel32.o"
"$LDX" -m32 -static -e _start --defsym target=0xffffffff -o "$TMP/our_32_ok" "$TMP/rel32.o"

# Out-of-range values for 8/16-bit relocations must fail on both linkers.
if "$GNU_LD" -m elf_i386 -static -e _start --defsym target=0x100 -o "$TMP/gnu_8_bad" "$TMP/rel8.o" \
	>"$TMP/gnu_8_bad.err" 2>&1; then
	echo "FAIL: GNU ld unexpectedly accepted out-of-range R_386_8 value" >&2
	exit 1
fi
if "$LDX" -m32 -static -e _start --defsym target=0x100 -o "$TMP/our_8_bad" "$TMP/rel8.o" \
	>"$TMP/our_8_bad.err" 2>&1; then
	echo "FAIL: our ld unexpectedly accepted out-of-range R_386_8 value" >&2
	exit 1
fi

if "$GNU_LD" -m elf_i386 -static -e _start --defsym target=0x10000 -o "$TMP/gnu_16_bad" "$TMP/rel16.o" \
	>"$TMP/gnu_16_bad.err" 2>&1; then
	echo "FAIL: GNU ld unexpectedly accepted out-of-range R_386_16 value" >&2
	exit 1
fi
if "$LDX" -m32 -static -e _start --defsym target=0x10000 -o "$TMP/our_16_bad" "$TMP/rel16.o" \
	>"$TMP/our_16_bad.err" 2>&1; then
	echo "FAIL: our ld unexpectedly accepted out-of-range R_386_16 value" >&2
	exit 1
fi

# R_386_32 follows 32-bit wrap semantics under GNU ld for high absolute values.
"$GNU_LD" -m elf_i386 -static -e _start --defsym target=0x100000000 -o "$TMP/gnu_32_wrap" "$TMP/rel32.o"
"$LDX" -m32 -static -e _start --defsym target=0x100000000 -o "$TMP/our_32_wrap" "$TMP/rel32.o"
gnu_32_wrap=$(readelf -x .data "$TMP/gnu_32_wrap" | awk '/^  0x/{print $2; exit}')
our_32_wrap=$(readelf -x .data "$TMP/our_32_wrap" | awk '/^  0x/{print $2; exit}')
if [ "$gnu_32_wrap" != "$our_32_wrap" ]; then
	echo "FAIL: R_386_32 wrap behavior diverges from GNU ld" >&2
	echo "  gnu .data: $gnu_32_wrap" >&2
	echo "  our .data: $our_32_wrap" >&2
	exit 1
fi

echo "ok: i386 relocation 8/16/32-bit range behavior matches GNU ld"
