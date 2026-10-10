#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS=${AS:-"$ROOT/usr.bin/as/as"}
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
# A branch to a label of its own section is resolved by the assembler: the
# short form, its distance in place, and no relocation.  (This test once
# wanted an R_X86_64_PC8 against .Ltarget, which is what the assembler
# wrote before it sized such branches itself.)
readelf --wide -r "$TMP/short_local_temp_rel64.o" > "$TMP/short_local_temp_rel64.relocs"
if grep -q 'R_X86_64_' "$TMP/short_local_temp_rel64.relocs"; then
    echo "a relocation for a branch within the section"
    exit 1
fi
objcopy -O binary -j .text "$TMP/short_local_temp_rel64.o" "$TMP/short_local_temp_rel64.bin"
# cmpq (4 bytes), then 75 03: over the three bytes of the xorq, to the ret.
case "$(od -An -tx1 "$TMP/short_local_temp_rel64.bin" | tr -d ' \n')" in
4883f8007503??????c3) ;;
*) echo "the branch is not the short form over three bytes"; exit 1 ;;
esac

echo "ok: x86_64 short local temp branch"
