#!/bin/sh
set -eu

# Reqs: LD-U-007, LD-U-006

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-i386-suite-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/caller_start.s" <<'SRC'
.globl _start
_start:
	mov target, %eax
	ret
SRC

cat > "$TMP/caller_dyn.s" <<'SRC'
.globl f
f:
	mov target, %eax
	ret
SRC

cat > "$TMP/def.s" <<'SRC'
.globl target
.section .data
target:
	.long 0x12345678
SRC

as --32 -o "$TMP/caller_start.o" "$TMP/caller_start.s"
as --32 -o "$TMP/caller_dyn.o" "$TMP/caller_dyn.s"
as --32 -o "$TMP/def.o" "$TMP/def.s"
ar rcs "$TMP/libprov.a" "$TMP/def.o"
ld -m elf_i386 -shared -o "$TMP/libprov.so" "$TMP/def.o"

# Direct object resolution path.
"$LDX" -m32 -static -e _start -o "$TMP/direct.out" "$TMP/caller_start.o" "$TMP/def.o"
if ! readelf -Ws "$TMP/direct.out" | awk '$NF=="target"{ if ($7=="UND") exit 1; found=1 } END{ exit found?0:1 }'; then
	echo "FAIL: i386 direct link did not resolve symbol 'target'" >&2
	readelf -Ws "$TMP/direct.out" >&2 || true
	exit 1
fi

# Archive extraction resolution path.
"$LDX" -m32 -static -e _start -L"$TMP" -o "$TMP/archive.out" "$TMP/caller_start.o" -lprov
if ! readelf -Ws "$TMP/archive.out" | awk '$NF=="target"{ if ($7=="UND") exit 1; found=1 } END{ exit found?0:1 }'; then
	echo "FAIL: i386 archive link did not resolve symbol 'target'" >&2
	readelf -Ws "$TMP/archive.out" >&2 || true
	exit 1
fi

# Shared-provider resolution path.
"$LDX" -m32 -shared -L"$TMP" -o "$TMP/shared.out" "$TMP/caller_dyn.o" -lprov
if ! readelf -d "$TMP/shared.out" 2>/dev/null | grep -q "Shared library: \\[libprov.so\\]"; then
	echo "FAIL: i386 shared link missing DT_NEEDED for libprov.so" >&2
	readelf -d "$TMP/shared.out" >&2 || true
	exit 1
fi

echo "ok: i386 relocation suite passes for direct + archive + shared paths"
