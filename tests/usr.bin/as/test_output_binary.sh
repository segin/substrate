#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-binary-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

hex_of() {
    od -An -tx1 -v "$1" | tr -d ' \n'
}

cat > "$TMP/layout.s" <<'SRC'
.text
.byte 0x90
.align 4
.byte 0xcc
.rodata
.ascii "AB"
.data
.long 0x11223344
.bss
.zero 3
SRC

"$AS" -64 -O binary -o "$TMP/layout.bin" "$TMP/layout.s"
[ "$(hex_of "$TMP/layout.bin")" = "90000000cc0000004142000044332211000000" ]

cat > "$TMP/org.s" <<'SRC'
.text
.byte 0xaa
.org 4
.byte 0xbb
SRC

"$AS" -32 -Obinary -o "$TMP/org.bin" "$TMP/org.s"
[ "$(hex_of "$TMP/org.bin")" = "aa000000bb" ]

cat > "$TMP/reloc_fail.s" <<'SRC'
.text
start:
    mov $extsym, %eax
    ret
SRC

if "$AS" -32 -O binary -o "$TMP/reloc_fail.bin" "$TMP/reloc_fail.s" >"$TMP/reloc.out" 2>"$TMP/reloc.err"; then
    echo "expected relocation-bearing binary assembly to fail"
    exit 1
fi
grep -qi "unresolved relocation" "$TMP/reloc.err"

cat > "$TMP/metadata_fail.s" <<'SRC'
.text
.type start,@function
start:
    ret
SRC

if "$AS" -64 -O binary -o "$TMP/metadata_fail.bin" "$TMP/metadata_fail.s" >"$TMP/meta.out" 2>"$TMP/meta.err"; then
    echo "expected ELF metadata directive to fail in binary mode"
    exit 1
fi
grep -qi "metadata directive" "$TMP/meta.err"

cat > "$TMP/boot.s" <<'SRC'
.text
.org 510
.byte 0x55, 0xaa
SRC

"$AS" -32 -O binary -o "$TMP/boot.bin" "$TMP/boot.s"
[ "$(wc -c < "$TMP/boot.bin" | tr -d ' ')" = "512" ]
[ "$(od -An -tx1 -j510 -N2 "$TMP/boot.bin" | tr -d ' \n')" = "55aa" ]

echo "ok: -O binary output mode"
