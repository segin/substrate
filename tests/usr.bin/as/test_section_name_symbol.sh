#!/bin/sh
# A name no label has, of a section, is that section's start, as gas
# has it: `.long (from) - __ex_table` in __ex_table is the distance from
# the table's start to `from`, a PC-relative relocation.  The assembler
# took __ex_table for an undefined symbol and the difference for none
# it could write: Linux's vDSO extable (ASM_VDSO_EXTABLE_HANDLE in
# arch/x86/entry/vdso/vdso64/vsgx.S, the Arch config) did not assemble.
# The objects are linked with GNU ld and the linked bytes are those of
# GNU as's, --64 and --32.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

if ! command -v ld >/dev/null 2>&1 || ! command -v objcopy >/dev/null 2>&1 || ! as --version 2>/dev/null | grep -q GNU; then
    echo "test_section_name_symbol: skip (no GNU as, ld or objcopy)"
    exit 0
fi

cat > t.s <<'EOF'
	.text
	.globl _start
_start:	nop
foo:	nop
	nop
bar:	nop
	.pushsection __ex_table, "a"
	.long (foo) - __ex_table
	.long (bar) - __ex_table
	.long 7
	.long (_start) - __ex_table
	.popsection
	.data
	.long __ex_table
	.long __ex_table + 4
EOF

fail=0
for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    if ! "$AS" --$m -o c.o t.s > out 2>&1; then
        echo "FAIL test_section_name_symbol --$m: $(head -1 out)"
        fail=1
        continue
    fi
    as --$m -o g.o t.s || { fail=1; continue; }
    for o in c g; do
        ld -m $emu -Ttext=0x1000 -Tdata=0x3000 --section-start=__ex_table=0x8000 -o $o.x $o.o || fail=1
        objcopy -O binary -j __ex_table $o.x $o.ex
        objcopy -O binary -j .data $o.x $o.data
    done
    for s in ex data; do
        if ! cmp -s g.$s c.$s; then
            echo "FAIL test_section_name_symbol --$m: $s is [$(od -An -tx1 c.$s | tr -d '\n')], GNU's [$(od -An -tx1 g.$s | tr -d '\n')]"
            fail=1
        fi
    done
done
[ $fail -eq 0 ] && echo "test_section_name_symbol: ok"
exit $fail
