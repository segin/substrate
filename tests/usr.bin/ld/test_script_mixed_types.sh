#!/bin/sh
# Input sections of different types in one output section, as GNU ld
# makes it: the output is of the type of the first input that goes in,
# empty or not, except that zeros (NOBITS) and then contents make
# PROGBITS, the zeros written out; an array of pointers to call
# (INIT_ARRAY, FINI_ARRAY, PREINIT_ARRAY) has a word for its entry size,
# whatever its inputs say; and .note.GNU-stack says nothing of the type.
#
# The linker refused every mix but zeros with PROGBITS ("section type
# mismatch during merge"): *(.data) *(.note.x), *(.bss) *(.init_array)
# and the like failed the link.
#
# GNU ld is the judge: for each mix, at x86-64 and i386, linked (and with
# -r, where the mix allows it; see below), the output section's type,
# size, entry size and flags are GNU ld's, and the linked bytes are.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${GNU_LD:-ld} --version 2>/dev/null | grep -q 'GNU ld'; then
    echo "skip: GNU ld is the judge"
    exit 0
fi
if ! ${GNU_AS:-as} --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as makes the objects"
    exit 0
fi
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
# every kind of section, each with contents, and an empty one of each
cat > a.s <<'EOF'
	.globl _start
	.text
_start:	ret
	.data
	.long 0x11111111, 0x11111111
	.section .note.x,"a",@note
	.long 4, 4, 1
	.ascii "GNU\0"
	.long 0x22222222
	.bss
	.zero 16
	.section .init_array,"aw",@init_array
	.long 0x33333333, 0x33333333
	.section .fini_array,"aw",@fini_array
	.long 0x55555555, 0x55555555
	.section .pre,"aw",@preinit_array
	.long 0x44444444, 0x44444444
	.section .tbss,"awT",@nobits
	.zero 8
	.section .edata,"aw",@progbits
	.section .ebss,"aw",@nobits
	.section .enote,"a",@note
	.section .einit,"aw",@init_array
EOF
# an empty .note.GNU-stack, which *(.note*) takes before the notes
printf '\t.section .note.GNU-stack,"",@progbits\n' > s.s

# name|what the output section takes|"-r" where -r is checked too: under -r
# the linker does not yet put a script's input descriptions in their order
# (GNU ld does), but merges an object's sections in an order of its own,
# so only the mixes in that order are its to get right
cat > mixes <<'EOF'
dn|*(.data) *(.note.x)|-r
nd|*(.note.x) *(.data)|
ia|*(.init_array) *(.data)|
ai|*(.data) *(.init_array)|-r
fi|*(.fini_array) *(.init_array)|
pi|*(.pre) *(.init_array)|
bn|*(.bss) *(.note.x)|
nb|*(.note.x) *(.bss)|
bi|*(.bss) *(.init_array)|-r
db|*(.data) *(.bss)|-r
bd|*(.bss) *(.data)|
tb|*(.tbss) *(.bss)|
dt|*(.data) *(.tbss)|-r
dnb|*(.data) *(.note.x) *(.bss)|-r
bdn|*(.bss) *(.data) *(.note.x)|
Edn|*(.edata) *(.note.x)|
Ebn|*(.ebss) *(.note.x)|
End|*(.enote) *(.data)|
Eid|*(.einit) *(.data)|
Ebd|*(.ebss) *(.data)|
Enb|*(.enote) *(.bss)|
Edi|*(.edata) *(.init_array)|
sn|*(.note.GNU-stack) *(.note.x)|-r
EOF

# .out's type, size, entry size and flags in $1
shdr() {
    readelf -SW "$1" | sed -n 's/^ *\[ *[0-9]*\] \.out  *//p' |
        awk '{ print $1, $4, $5, $6 }'
}

for arch in 64 32; do
    if [ $arch = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    ${GNU_AS:-as} --$arch -o a.o a.s && ${GNU_AS:-as} --$arch -o s.o s.s || { echo "FAIL: as --$arch"; exit 1; }
    while IFS='|' read -r name what rel; do
        printf 'SECTIONS { . = 0x400000; .text : { *(.text) } .out : { %s } }\n' "$what" > $name.lds
        for r in "" $rel; do
            tag="$name -m $emu $r"
            ${GNU_LD:-ld} -m $emu $r -T $name.lds -o gnu s.o a.o 2>gnu.err ||
                { echo "FAIL: GNU ld: $tag"; cat gnu.err; exit 1; }
            if ! ./ld -m $emu $r -T $name.lds -o our s.o a.o 2>our.err; then
                echo "FAIL $tag ($what): not linked:"; cat our.err; fail=1; continue
            fi
            if [ "$(shdr our)" != "$(shdr gnu)" ]; then
                echo "FAIL $tag ($what): .out is [$(shdr our)], GNU ld's [$(shdr gnu)] (type size entsize flags)"
                fail=1
            fi
            if [ -z "$r" ]; then
                objcopy -O binary -j .out gnu gnu.bin && objcopy -O binary -j .out our our.bin ||
                    { echo "FAIL $tag: objcopy"; fail=1; continue; }
                if ! cmp -s gnu.bin our.bin; then
                    echo "FAIL $tag ($what): the bytes of .out are not GNU ld's"; fail=1
                fi
            fi
        done
    done < mixes
    # and what a PT_NOTE holds is a NOTE, whatever its first input: the
    # vDSO's .note begins with the empty PROGBITS .note.GNU-stack
    for what in '*(.data) *(.note.x)' '*(.note.GNU-stack) *(.note.x)' '*(.edata) *(.note.x) *(.bss)'; do
        cat > pn.lds <<EOF
PHDRS { text PT_LOAD FILEHDR PHDRS; note PT_NOTE; }
SECTIONS { . = 0x400000 + SIZEOF_HEADERS; .text : { *(.text) } :text .out : { $what } :text :note }
EOF
        ${GNU_LD:-ld} -m $emu -T pn.lds -o gnu s.o a.o 2>gnu.err || { echo "FAIL: GNU ld: PT_NOTE $what"; cat gnu.err; exit 1; }
        if ! ./ld -m $emu -T pn.lds -o our s.o a.o 2>our.err; then
            echo "FAIL PT_NOTE -m $emu ($what): not linked:"; cat our.err; fail=1; continue
        fi
        if [ "$(shdr our)" != "$(shdr gnu)" ]; then
            echo "FAIL PT_NOTE -m $emu ($what): .out is [$(shdr our)], GNU ld's [$(shdr gnu)]"; fail=1
        fi
    done
done

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
