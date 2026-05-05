#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-short-local-temp-branch-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/short_local_temp_rel64.s" <<'SRC'
.text
.globl short_local_temp_rel64
.type short_local_temp_rel64,@function
short_local_temp_rel64:
    cmpq $0, %rax
    jne .Ltarget
    xorq %rax, %rax
.Ltarget:
    ret
.size short_local_temp_rel64, .-short_local_temp_rel64
SRC

"$AS" -64 -o "$TMP/short_local_temp_rel64.o" "$TMP/short_local_temp_rel64.s"
readelf --wide -r "$TMP/short_local_temp_rel64.o" > "$TMP/short_local_temp_rel64.relocs"
grep -q 'R_X86_64_PC8' "$TMP/short_local_temp_rel64.relocs"
grep -q '.Ltarget - 1' "$TMP/short_local_temp_rel64.relocs"

echo "ok: x86_64 short local temp branch"
