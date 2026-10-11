#!/bin/sh
# A branch to a label of its own section, and the relocation of one that
# is not (AS-T-187, AS-T-206, AS-T-207, AS-T-209).
#
# Only a compiler's `.L` labels and numeric labels were places the
# assembler knew.  A branch to any other label -- `jmp l` / `l: ret`,
# a call of a static function -- was the long form with a relocation
# for the linker to work out a distance that was known here: five bytes
# and a relocation for two bytes and none, on every such branch of every
# hand-written source.
#
# A label that is the source's own, named by no .globl or .weak, is now
# as near as a .L label: the distance is resolved and the form is the
# short one where it reaches.
#
# And for what is left to the linker the relocation is GNU as's: on
# i386 R_386_PC32 for a call, a jump or a conditional jump (a call had
# R_386_PLT32, which is `call f@PLT`); on x86-64 R_X86_64_PLT32 for all
# three of a bare symbol (a jump had R_X86_64_PC32) and R_X86_64_PC32
# where something is added to the symbol.
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

# is MODE SOURCE BYTES RELOCATIONS: `|` in SOURCE is a newline; `Zn` in
# BYTES is n bytes of zero; the relocations are type@offset:symbol+addend,
# sorted.
is() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1; return
    fi
    want=$(printf '%s' "$3" | awk '{ while (match($0, /Z[0-9]+/)) { n = substr($0, RSTART + 1, RLENGTH - 1) + 0; z = "";
               for (i = 0; i < n; i++) z = z "00"; $0 = substr($0, 1, RSTART - 1) z substr($0, RSTART + RLENGTH) } print }')
    bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    relocs=$(readelf -rW t.o | awk '/ R_/ { printf "%s@%x:%s%s\n", $3, strtonum("0x" $1), $5, $6 $7 }' |
             sort | tr '\n' ' ' | sed 's/ $//')
    [ "$bytes" = "$want" ] || { echo "FAIL --$1 [$2]: bytes $(echo "$bytes" | cut -c1-80), not $(echo "$want" | cut -c1-80)"; fail=1; }
    [ "$relocs" = "$4" ] || { echo "FAIL --$1 [$2]: relocations [$relocs], not [$4]"; fail=1; }
}

# The source's own label, ahead and behind.
is 32 'jmp l|l: ret' eb00c3 ''
is 64 'jmp l|l: ret' eb00c3 ''
is 32 'l: nop|jmp l|jne l|call l' 90ebfd75fbe8f6ffffff ''
is 64 'l: nop|jmp l|jne l|call l' 90ebfd75fbe8f6ffffff ''
is 32 'a: nop|b: nop|jmp a|jmp b|call a|loop b' 9090ebfcebfbe8f5ffffffe2f4 ''
is 64 'f: ret|call f|jmp f' c3e8faffffffebf8 ''
is 64 '.type sf,@function|sf: ret|.size sf,.-sf|main: call sf|jmp sf' c3e8faffffffebf8 ''
# Short where it reaches, and long where it does not.
is 64 'l: .skip 125|jmp l|jne l|jmp l' Z125eb810f857bffffffe976ffffff ''
is 32 'jmp l|jne l|.skip 100|l: ret' eb667564Z100c3 ''
is 64 'jmp l|.skip 200|l: ret' e9c8000000Z200c3 ''

# A symbol that is not the source's own is the linker's.
is 32 'call ext|jmp ext|jne ext' e8fcffffffe9fcffffff0f85fcffffff \
    'R_386_PC32@1:ext R_386_PC32@6:ext R_386_PC32@c:ext'
is 64 'call ext|jmp ext|jne ext' e800000000e9000000000f8500000000 \
    'R_X86_64_PLT32@1:ext-4 R_X86_64_PLT32@6:ext-4 R_X86_64_PLT32@c:ext-4'
is 64 'jmp ext+4|jne ext-2' e9000000000f8500000000 'R_X86_64_PC32@1:ext+0 R_X86_64_PC32@7:ext-6'
is 32 'call ext@PLT|jmp ext@PLT' e8fcffffffe9fcffffff 'R_386_PLT32@1:ext R_386_PLT32@6:ext'
is 64 'call ext@PLT' e800000000 'R_X86_64_PLT32@1:ext-4'
# A .L label that is not defined is another file's symbol too: a
# conditional jump to one was the two-byte form with an 8-bit relocation.
is 32 'je .Lundefined|jmp .Lu2|call .Lu3' 0f84fcffffffe9fcffffffe8fcffffff \
    'R_386_PC32@2:.Lundefined R_386_PC32@7:.Lu2 R_386_PC32@c:.Lu3'
is 64 'je .Lundefined|jmp .Lu2|call .Lu3' 0f8400000000e900000000e800000000 \
    'R_X86_64_PLT32@2:.Lundefined-4 R_X86_64_PLT32@7:.Lu2-4 R_X86_64_PLT32@c:.Lu3-4'
is 32 'nop|x: je .Lundefined|jmp x' 900f84fcffffffebf8 'R_386_PC32@3:.Lundefined'
# One that another object may replace is not the source's own, defined
# here or not.
is 32 '.weak w|w: nop|jmp w|call w' 90e9fcffffffe8fcffffff 'R_386_PC32@2:w R_386_PC32@7:w'
is 64 '.weak w|w: nop|jmp w|call w' 90e900000000e800000000 'R_X86_64_PLT32@2:w-4 R_X86_64_PLT32@7:w-4'
is 64 '.globl g|g: ret|call g' c3e800000000 'R_X86_64_PLT32@2:g-4'
is 32 '.globl g|g: ret|call g' c3e8fcffffff 'R_386_PC32@2:g'

[ "$fail" -eq 0 ] && echo "ok: branches to a label of the source's own ($cases cases)"
exit "$fail"
