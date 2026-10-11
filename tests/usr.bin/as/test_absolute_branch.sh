#!/bin/sh
# A branch to a number (AS-T-137).
#
# `jmp 0x1234` is a jump to address 0x1234.  Where the section it is in
# will be is the linker's to say, so the distance cannot be written
# here: the object has a PC-relative relocation that names no symbol,
# and the linker subtracts the place from the address.  It was encoded
# as the distance from the instruction with the section at address 0 and
# no relocation, which is right for a section loaded at 0 and for no
# other.
#
# Each case is the bytes of .text and its relocations, which are GNU
# as's; GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is MODE SOURCE BYTES RELOCATIONS: `|` in SOURCE is a newline; a
# relocation is type@offset, then `:symbol` if it names one, then
# `+addend` where the addend is in the relocation.
is() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    # With no symbol readelf leaves two columns out, and the addend of a
    # RELA relocation is then the fourth.
    relocs=$(readelf -rW t.o | awk '/ R_/ {
                 if (NF <= 4) printf "%s@%x%s\n", $3, strtonum("0x" $1), (NF == 4 ? "+" $4 : "")
                 else printf "%s@%x:%s\n", $3, strtonum("0x" $1), $5
             }' | sort | tr '\n' ' ' | sed 's/ $//')
    [ "$bytes" = "$3" ] || { echo "FAIL --$1 [$2]: bytes $bytes, not $3"; fail=1; }
    [ "$relocs" = "$4" ] || { echo "FAIL --$1 [$2]: relocations [$relocs], not [$4]"; fail=1; }
}

is 32 'jmp 0x1234'      e930120000     'R_386_PC32@1'
is 32 'call 0x1000'     e8fc0f0000     'R_386_PC32@1'
is 32 'je 0x1000'       0f84fc0f0000   'R_386_PC32@2'
is 32 'nop|jmp 0x1234'  90e930120000   'R_386_PC32@2'
is 32 'jmp 0'           e9fcffffff     'R_386_PC32@1'
is 32 'jmp 0x1000+0x234' e930120000    'R_386_PC32@1'
is 64 'jmp 0x1234'      e900000000     'R_X86_64_PC32@1+1230'
is 64 'call 0x1000'     e800000000     'R_X86_64_PC32@1+ffc'
is 64 'je 0x100+8'      0f8400000000   'R_X86_64_PC32@2+104'
is 64 'jmp 0x1234|call 0x1000' e900000000e800000000 'R_X86_64_PC32@1+1230 R_X86_64_PC32@6+ffc'
# The instructions with a short form only.
is 32 'loop 0x10'       e20f           'R_386_PC8@1'
is 32 'jecxz 5'         e304           'R_386_PC8@1'
is 32 'jcxz 0x10'       67e30f         'R_386_PC8@2'
is 64 'jecxz 0x10'      67e300         'R_X86_64_PC8@2+f'
is 64 'loop 0x20'       e200           'R_X86_64_PC8@1+1f'

# What is not a branch to a number is as it was.
is 32 'jmp *0x1234'     ff2534120000   ''
is 32 '1: jmp 1b'       ebfe           ''
is 32 'jmp ext'         e9fcffffff     'R_386_PC32@1:ext'

[ "$fail" -eq 0 ] && echo "ok: branches to an address ($cases cases)"
exit "$fail"
