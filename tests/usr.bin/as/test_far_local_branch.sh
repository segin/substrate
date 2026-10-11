#!/bin/sh
# A branch to a local label of another section (AS-T-136).
#
# The branch code took a local label -- a numeric one, `1:`, or a
# compiler's, `.L5` -- to be in the branch's own section.  When it was
# not:
#
#   - `jmp 1b` was left out of the object, and the assembler exited 0;
#   - `call 1b`, `jne 1b` were branches to offset 0 of their own section;
#   - `jmp .Lx` failed with "failed to emit relocations";
#   - `jne .Lx` was the two-byte form with an 8-bit relocation, which
#     reaches 127 bytes and is asked to reach another section.
#
# gcc writes the last two for every function with a cold part: `je
# .Lcold` from .text to .text.unlikely.
#
# How far such a label is is not known until the sections are placed, so
# the branch is the long form with a PC-relative relocation against the
# label's section.  Each case is the bytes of .text and its relocations,
# which are GNU as's; GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is MODE SOURCE BYTES RELOCATIONS: `|` in SOURCE is a newline; the
# relocations are type@offset:symbol+addend, sorted.
is() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    relocs=$(readelf -rW t.o | awk '/ R_/ { printf "%s@%x:%s%s\n", $3, strtonum("0x" $1), $5, $6 $7 }' |
             sort | tr '\n' ' ' | sed 's/ $//')
    [ "$bytes" = "$3" ] || { echo "FAIL --$1 [$2]: bytes $bytes, not $3"; fail=1; }
    [ "$relocs" = "$4" ] || { echo "FAIL --$1 [$2]: relocations [$relocs], not [$4]"; fail=1; }
}

# A numeric label, behind and ahead.
is 32 '.data|1: .long 0|.text|jmp 1b'   e9fcffffff   'R_386_PC32@1:.data'
is 32 '.data|1: .long 0|.text|call 1b'  e8fcffffff   'R_386_PC32@1:.data'
is 32 '.data|1: .long 0|.text|jne 1b'   0f85fcffffff 'R_386_PC32@2:.data'
is 32 '.text|jmp 1f|.data|1: .long 0'   e9fcffffff   'R_386_PC32@1:.data'
is 32 '.text|jne 1f|nop|.data|.long 5|1: .long 0' 0f850000000090 'R_386_PC32@2:.data'
is 64 '.data|1: .long 0|.text|jmp 1b'   e900000000   'R_X86_64_PC32@1:.data-4'
is 64 '.data|.long 7|1: .long 0|.text|jne 1b|call 1b' 0f8500000000e800000000 \
    'R_X86_64_PC32@2:.data+0 R_X86_64_PC32@7:.data+0'
is 64 '.text|jmp 1f|jne 1f|call 1f|.section .rodata|.long 1|1: .long 0' e9000000000f8500000000e800000000 \
    'R_X86_64_PC32@1:.rodata+0 R_X86_64_PC32@7:.rodata+0 R_X86_64_PC32@c:.rodata+0'

# A compiler's label.
is 32 '.data|.Lx: .long 0|.text|jmp .Lx'  e9fcffffff   'R_386_PC32@1:.data'
is 32 '.data|.Lx: .long 0|.text|jne .Lx'  0f85fcffffff 'R_386_PC32@2:.data'
is 32 '.data|.Lx: .long 0|.text|call .Lx' e8fcffffff   'R_386_PC32@1:.data'
is 32 '.section .text.a,"ax"|.La: ret|.text|jmp .La|jne .La|call .La' e9fcffffff0f85fcffffffe8fcffffff \
    'R_386_PC32@1:.text.a R_386_PC32@7:.text.a R_386_PC32@c:.text.a'
is 32 '.pushsection .fix,"ax"|.Lf: ret|.popsection|jmp .Lf|jne .Lf' e9fcffffff0f85fcffffff \
    'R_386_PC32@1:.fix R_386_PC32@7:.fix'
# What gcc writes.
is 64 '.section .text.unlikely,"ax"|.Lcold: ud2|.text|f: test %eax,%eax|je .Lcold|ret' 85c00f8400000000c3 \
    'R_X86_64_PC32@4:.text.unlikely-4'

# With something added.
is 32 '.data|1: .long 0|.text|jmp 1b+4' e900000000 'R_386_PC32@1:.data'
is 32 '.data|.Lx: .long 0|.text|jne .Lx+8|call .Lx-4' 0f8504000000e8f8ffffff \
    'R_386_PC32@2:.data R_386_PC32@7:.data'

# The instructions that have a short form only keep it, with the 8-bit
# relocation that GNU as gives them too.
is 32 '.data|1: .long 0|.text|loop 1b'  e2ff 'R_386_PC8@1:.data'
is 32 '.data|1: .long 0|.text|jecxz 1b' e3ff 'R_386_PC8@1:.data'

# And a label of the branch's own section is as near as it was: the
# nearest `1:` behind is the one in .text, whatever .data has.
is 32 '.text|1: nop|jmp 1b|jne 1b|.data|1: .long 0|.text|jmp 1b' 90ebfd75fbe9fcffffff 'R_386_PC32@6:.data'
is 32 '.data|1: .long 0|.text|1: nop|jmp 1b' 90ebfd ''
is 32 '.text|.subsection 1|1: ret|.subsection 0|jmp 1b' eb00c3 ''
is 64 '.Lh: nop|jne .Lh|jmp .Lh' 9075fdebfb ''

[ "$fail" -eq 0 ] && echo "ok: branches to a local label of another section ($cases cases)"
exit "$fail"
