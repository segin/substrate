#!/bin/sh
# A section flagged "o" (SHF_LINK_ORDER) is ordered by the section of
# the symbol after its type, its sh_link, and is one section of each
# such symbol, as gas has it: gcc's __patchable_function_entries, one
# for each function, each ordered by its function's section.  The
# assembler kept one section of the name, with sh_link 0, so a linker
# could not drop a function's entry with the function.  Each section
# here is as gas's -- its flags and the section it is ordered by -- and
# linked with GNU ld --gc-sections, which drops the entries of the
# functions it drops, the entries are those of GNU as's, --64 and --32.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

if ! command -v ld >/dev/null 2>&1 || ! command -v objcopy >/dev/null 2>&1 || ! command -v readelf >/dev/null 2>&1 ||
    ! as --version 2>/dev/null | grep -q GNU; then
    echo "test_section_link_order: skip (no GNU as, ld, objcopy or readelf)"
    exit 0
fi

for m in 64 32; do
    if [ $m = 64 ]; then word=.quad; else word=.long; fi
    {
        printf '\t.globl _start\n'
        for f in _start used dead init; do
            case $f in
            dead) printf '\t.section .text.dead,"ax",@progbits\n' ;;
            init) printf '\t.section .init.text,"ax",@progbits\n' ;;
            *) printf '\t.section .text.%s,"ax",@progbits\n' "$f" ;;
            esac
            printf '\t.section __patchable_function_entries,"awo",@progbits,.LPFE_%s\n' "$f"
            printf '\t.align %d\n\t%s .LPFE_%s\n\t.previous\n' $((m / 8)) $word "$f"
            printf '.LPFE_%s:\n\tnop\n\tnop\n%s:\n' "$f" "$f"
            if [ $f = _start ]; then printf '\tcall used\n\tcall init\n'; fi
            printf '\tret\n'
        done
    } > t.s
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    if ! "$AS" --$m -o c.o t.s > out 2>&1; then
        echo "FAIL test_section_link_order --$m: $(head -1 out)"
        fail=1
        continue
    fi
    as --$m -o g.o t.s || { fail=1; continue; }
    # each such section: its flags and the name of the section it is ordered by
    for o in c g; do
        readelf -SW $o.o | sed -n 's/^ *\[ *\([0-9]*\)\] /\1 /p' >$o.secs
        awk 'NR == FNR { name[$1] = $2; next }
            $2 == "__patchable_function_entries" { print $8, name[$9] }' $o.secs $o.secs | sort >$o.links
    done
    if ! cmp -s g.links c.links || [ "$(wc -l < c.links)" -ne 4 ]; then
        echo "FAIL test_section_link_order --$m: [$(tr '\n' ';' < c.links)], GNU's [$(tr '\n' ';' < g.links)]"
        fail=1
        continue
    fi
    for o in c g; do
        ld -m $emu --gc-sections -e _start -Ttext=0x1000 -o $o.x $o.o || { fail=1; continue 2; }
        objcopy -O binary -j __patchable_function_entries $o.x $o.pfe
    done
    if ! cmp -s g.pfe c.pfe || [ ! -s c.pfe ]; then
        echo "FAIL test_section_link_order --$m: linked entries [$(od -An -tx1 c.pfe | tr -d '\n')], GNU's [$(od -An -tx1 g.pfe | tr -d '\n')]"
        fail=1
    fi
done
[ "${fail:-0}" -eq 0 ] && echo "test_section_link_order: ok"
exit "${fail:-0}"
