#!/bin/sh
# What a section is, and what may be put in it.
#
# - An instruction is assembled into the section it is written in,
#   whatever the section's flags.  It went by the flag: in a section not
#   marked as code the instruction was left out without a word, so
#   `call f` in .init, opened by name as crti.S opens it, was not there.
# - A section opened by name alone is what its name means: .init and
#   .fini are code, .tdata and .tbss thread-local, .init_array an array
#   the linker runs, .note a note; any other is not allocated.  Only
#   .text, .data, .bss and .rodata were known and everything else was
#   "a".
# - The flags `e`, `R`, `o`, `?` and a number, and the types of the
#   arrays, were passed over.  A letter that is no flag is refused.
# - A section of no bits holds zeros: `.long 5` in .bss was dropped and
#   the section said to be four bytes long.
#
# The flags, types and bytes are GNU as 2.46's.  `|` is a new line.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# k MODES SECTION TYPE FLAGS BYTES SOURCE.  FLAGS and BYTES are `-` for
# none; TYPE `refused` for a source that is not assembled.
k() {
    for mode in $1; do
        cases=$((cases + 1))
        printf '%s\n' "$6" | tr '|' '\n' > t.s
        rm -f t.o
        if ! "$AS" "-$mode" -o t.o t.s > out 2> err; then
            [ "$3" = refused ] || { echo "FAIL $mode [$6]: $(head -1 err)"; fail=1; }
            continue
        fi
        if [ "$3" = refused ]; then
            echo "FAIL $mode [$6]: assembled"; fail=1; continue
        fi
        got=$(readelf -SW t.o | sed 's/^ *\[ *[0-9]*\] //' |
              awk -v s="$2" '$1 == s { f = "-"; for (i = 7; i <= NF; i++) if ($i ~ /^[A-Zxop]+$/) { f = $i; break }
                                       print $2, f; exit }')
        bytes=$(objcopy -O binary --only-section="$2" t.o /dev/stdout 2>/dev/null | od -An -v -tx1 | tr -d ' \n')
        got="$got ${bytes:--}"
        [ "$got" = "$3 $4 $5" ] || { echo "FAIL $mode [$6]: $2 is $got, and GNU as: $3 $4 $5"; fail=1; }
    done
}

# Instructions where the section is not marked as code.
k "32 64" .init   PROGBITS AX c3         '.section .init|ret'
k "32 64" .fini   PROGBITS AX 90c3       '.section .fini|nop|ret'
k "32 64" .foo    PROGBITS A  90c3       '.section .foo,"a"|nop|ret'
k "32 64" .foo    PROGBITS WA 9001000000 '.section .foo,"aw"|nop|.long 1'
k "32 64" .data   PROGBITS WA 019002     '.data|.byte 1|nop|.byte 2'
k "32 64" .rodata PROGBITS A  c3         '.section .rodata|ret'

# And the relocation of one is there with it.
for mode in 32 64; do
    cases=$((cases + 1))
    printf '.section .init\ncall f\nret\n' > r.s
    if "$AS" "-$mode" -o r.o r.s 2> err; then
        readelf -rW r.o | grep -q ' f\( \|$\)' || { echo "FAIL $mode: no relocation for call f in .init"; fail=1; }
        readelf -SW r.o | grep -q ' \.rela\{0,1\}\.init ' || { echo "FAIL $mode: no relocation section for .init"; fail=1; }
    else
        echo "FAIL $mode call f in .init: $(head -1 err)"; fail=1
    fi
done

# Sections opened by name.
k "32 64" .init              PROGBITS      AX  - '.section .init'
k "32 64" .fini              PROGBITS      AX  - '.section .fini'
k "32 64" .plt               PROGBITS      AX  - '.section .plt'
k "32 64" .text.foo          PROGBITS      AX  - '.section .text.foo'
k "32 64" .text.unlikely     PROGBITS      AX  - '.section .text.unlikely'
k "32 64" .data.foo          PROGBITS      WA  - '.section .data.foo'
k "32 64" .data1             PROGBITS      WA  - '.section .data1'
k "32 64" .data.rel.ro       PROGBITS      WA  - '.section .data.rel.ro'
k "32 64" .data.rel.ro.local PROGBITS      WA  - '.section .data.rel.ro.local'
k "32 64" .got               PROGBITS      WA  - '.section .got'
k "32 64" .persistent        PROGBITS      WA  - '.section .persistent'
k "32 64" .bss.foo           NOBITS        WA  - '.section .bss.foo'
k "32 64" .noinit            NOBITS        WA  - '.section .noinit'
k "32 64" .gnu.linkonce.b.x  NOBITS        WA  - '.section .gnu.linkonce.b.x'
k "32 64" .rodata            PROGBITS      A   - '.section .rodata'
k "32 64" .rodata.str1.1     PROGBITS      A   - '.section .rodata.str1.1'
k "32 64" .rodata1           PROGBITS      A   - '.section .rodata1'
k "32 64" .tdata             PROGBITS      WAT - '.section .tdata'
k "32 64" .tdata.foo         PROGBITS      WAT - '.section .tdata.foo'
k "32 64" .tbss              NOBITS        WAT - '.section .tbss'
k "32 64" .tbss.foo          NOBITS        WAT - '.section .tbss.foo'
k "32 64" .init_array        INIT_ARRAY    WA  - '.section .init_array'
k "32 64" .init_array.100    INIT_ARRAY    WA  - '.section .init_array.100'
k "32 64" .fini_array        FINI_ARRAY    WA  - '.section .fini_array'
k "32 64" .fini_array.5      FINI_ARRAY    WA  - '.section .fini_array.5'
k "32 64" .preinit_array     PREINIT_ARRAY WA  - '.section .preinit_array'
k "32 64" .note              NOTE          -   - '.section .note'
k "32 64" .note.foo          NOTE          -   - '.section .note.foo'
k "32 64" .note.GNU-stack    NOTE          -   - '.section .note.GNU-stack'
# What a name does not make allocated.
k "32 64" .foo               PROGBITS      -   - '.section .foo'
k "32 64" .comment           PROGBITS      -   - '.section .comment'
k "32 64" .debug_info        PROGBITS      -   - '.section .debug_info'
k "32 64" .debug_line        PROGBITS      -   - '.section .debug_line'
k "32 64" .eh_frame          PROGBITS      -   - '.section .eh_frame'
k "32 64" .eh_frame_hdr      PROGBITS      -   - '.section .eh_frame_hdr'
k "32 64" .gcc_except_table  PROGBITS      -   - '.section .gcc_except_table'
k "32 64" .ctors             PROGBITS      -   - '.section .ctors'
k "32 64" .dtors             PROGBITS      -   - '.section .dtors'
k "32 64" .gnu.linkonce.t.x  PROGBITS      -   - '.section .gnu.linkonce.t.x'
k "32 64" .gnu.linkonce.d.x  PROGBITS      -   - '.section .gnu.linkonce.d.x'
k "32 64" .gnu.linkonce.r.x  PROGBITS      -   - '.section .gnu.linkonce.r.x'
# A name is a default: flags that are written are the flags, and the
# type is still the name's.
k "32 64" .init       PROGBITS   WA  - '.section .init,"aw"'
k "32 64" .init_array INIT_ARRAY WA  - '.section .init_array,"aw"'
k "32 64" .eh_frame   PROGBITS   A   - '.section .eh_frame,"a",@progbits'
k "32 64" .tbss       NOBITS     WAT - '.section .tbss,"awT",@nobits'

# The letters of the flags, and a number.
k "32 64" .foo PROGBITS X   - '.section .foo,"x"'
k "32 64" .foo PROGBITS AT  - '.section .foo,"aT"'
k "32 64" .foo NOBITS   WAT - '.section .foo,"awT",@nobits'
k "32 64" .foo PROGBITS AE  - '.section .foo,"ae"'
k "32 64" .foo PROGBITS E   - '.section .foo,"e"'
k "32 64" .foo PROGBITS A   - '.section .foo,"0x12"'
k "32 64" .foo PROGBITS A   - '.section .foo,"a0x10"'
k "32 64" .foo PROGBITS AMS - '.section .foo,"aMS",@progbits,1'
k "32 64" .foo PROGBITS AM  - '.section .foo,"aM",@progbits,8'
k "32 64" .foo PROGBITS AXG - '.section .foo,"axG",@progbits,grp,comdat'
# `?`: of the group of the section the directive is written in.
k "32 64" .bar PROGBITS AG  - '.section .foo,"axG",@progbits,grp,comdat|.section .bar,"a?"'
# `o`: ordered by the section of a symbol, which is not a group's name.
k "32 64" .bar PROGBITS AL  - '.text|sym:|.section .bar,"ao",@progbits,sym'
k "32 64" .foo refused  -   - '.section .foo,"d"'
k "32 64" .foo refused  -   - '.section .foo,"az"'
k "32"    .foo refused  -   - '.section .foo,"l"'

# The types.
k "32 64" .foo INIT_ARRAY    WA - '.section .foo,"aw",@init_array'
k "32 64" .foo FINI_ARRAY    WA - '.section .foo,"aw",@fini_array'
k "32 64" .foo PREINIT_ARRAY WA - '.section .foo,"aw",@preinit_array'
k "32 64" .foo NOTE          A  - '.section .foo,"a",@note'
k "32 64" .foo PROGBITS      A  - '.section .foo,"a",%progbits'
k "32 64" .foo PROGBITS      A  - '.section .foo,"a","progbits"'
k "32"    .foo PROGBITS      A  - '.section .foo,"a",@unwind'
k "64"    .foo X86_64_UNWIND A  - '.section .foo,"a",@unwind'
k "64"    .foo X86_64_UNWIND A  - '.section .foo,"a",@0x70000001'

# A section is what it was first said to be.
k "32 64" .foo PROGBITS WA - '.section .foo,"aw"|.section .foo'
k "32 64" .foo refused  -  - '.section .foo,"aw"|.section .foo,"a"'

# A section of no bits has a size and no contents.
k "32 64" .bss  NOBITS  WA - '.bss|.zero 8'
k "32 64" .bss  NOBITS  WA - '.bss|.skip 8'
k "32 64" .bss  NOBITS  WA - '.bss|.long 0'
k "32 64" .bss  NOBITS  WA - '.bss|.fill 4,1,0'
k "32 64" .bss  NOBITS  WA - '.bss|.balign 8|x: .skip 4'
k "32 64" .bss  refused -  - '.bss|.long 5'
k "32 64" .bss  refused -  - '.bss|.byte 0,0,1'
k "32 64" .bss  refused -  - '.bss|.ascii "a"'
k "32 64" .bss  refused -  - '.bss|.fill 4,1,7'
k "32 64" .tbss refused -  - '.section .tbss|.long 1'
k "32 64" .foo  refused -  - '.section .foo,"aw",@nobits|.long 3'
for mode in 32 64; do
    cases=$((cases + 1))
    printf '.bss\n.zero 8\n.skip 8\nx: .long 0\n' > z.s
    if "$AS" "-$mode" -o z.o z.s 2> err; then
        got=$(readelf -SW z.o | sed 's/^ *\[ *[0-9]*\] //' | awk '$1 == ".bss" { print $5 }')
        [ "$got" = 000014 ] || { echo "FAIL $mode: .bss of 20 bytes has size $got"; fail=1; }
    else
        echo "FAIL $mode .bss size: $(head -1 err)"; fail=1
    fi
done

[ "$fail" -eq 0 ] && echo "ok: section kinds ($cases cases)"
exit "$fail"
