#!/bin/sh
# The numbers of .cfi_* directives are constant expressions, as GNU as
# reads them.  Linux's 32-bit vDSO writes
#     .cfi_def_cfa esp, IA32_SIGFRAME_sigcontext - 4
# with a number from asm-offsets for the name, and this assembler took
# only a literal ("a register and an offset").  The .eh_frame made must
# be GNU as's; GNU as is the judge here, so this test needs it (or
# skips).
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
GAS=${GAS:-as}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

if ! printf 'nop\n' | "$GAS" --32 -o /dev/null - 2> /dev/null; then
    echo "skip: no GNU as ($GAS)"
    exit 0
fi

fail=0
cases=0

for d in '.cfi_def_cfa esp, 40 - 4' '.cfi_def_cfa_offset 3*4' '.cfi_offset ebx, -2 * 4' \
    '.cfi_adjust_cfa_offset -4' '.cfi_def_cfa esp, 0x10'; do
    cases=$((cases + 1))
    printf 'f:\n\t.cfi_startproc simple\n\t%s\n\tnop\n\t.cfi_endproc\n' "$d" > t.s
    if ! "$AS" --32 -o m.o t.s 2> err; then
        echo "FAIL [$d]: $(tail -1 err)"; fail=1; continue
    fi
    "$GAS" --32 -o g.o t.s
    objcopy -O binary -j .eh_frame m.o m.b
    objcopy -O binary -j .eh_frame g.o g.b
    cmp -s m.b g.b || { echo "FAIL [$d]: .eh_frame differs from GNU as's"; fail=1; }
done

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
