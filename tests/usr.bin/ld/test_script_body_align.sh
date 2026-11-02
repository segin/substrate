#!/bin/sh
# `. = ALIGN(n)` between two input section descriptions of an output
# section aligns the address there, and does not make the section more
# aligned.
#
# GNU ld works ALIGN out on the location counter's address, and the output
# section's alignment is its input sections' greatest.  The linker made the
# output section as aligned as any ALIGN in its body asked, so that it
# could work the gap out on offsets: a section given an address that is
# not so aligned, or that follows one, was laid out differently, and
# Linux's .text, which has `. = ALIGN((1 << 21))` in its body, was 2 MiB
# aligned, which the output writer refuses as more than any section may be.
#
# GNU ld is the judge: the addresses, sizes, alignments and bytes of the
# sections and the symbols, x86-64 and i386.
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
# the bytes of section $2 of $1
section() {
    objcopy -O binary --only-section="$2" "$1" "$1$2.bin" 2>/dev/null && od -An -tx1 -v "$1$2.bin" | tr -d ' \n'
}
# the address, size and alignment of section $2 of $1
place() {
    readelf -SW "$1" | awk -v s="$2" '{ sub(/^ *\[ *[0-9]+\] */, ""); if ($1 == s) print $3, $5, $NF }'
}

# name, then what SECTIONS says
cat > cases <<'EOF'
unaligned|. = 0x400000; .text : { *(.text) } .data 0x500004 : { a.o(.data) . = ALIGN(16); mid = .; b.o(.data) }
after|. = 0x400000; .text : { a.o(.text) . = ALIGN(32); b.o(.text) } = 0x90 .data : { a.o(.data) . = ALIGN(64); b.o(.data) }
huge|. = 0x400000; .text : { _text = .; . = ALIGN(0x200000); a.o(.text) . = ALIGN(16); b.o(.text) } .data : { *(.data) }
EOF

printf '.text\n.globl _start\n_start: .byte 0xc3, 0xc3, 0xc3\n.data\n.byte 1, 2, 3\n' > a.s
printf '.text\n.globl b\nb: .byte 0xbb\n.data\n.globl bd\nbd: .byte 4\n' > b.s
for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    for f in a b; do
        ${GNU_AS:-as} --$m -mx86-used-note=no -o $f.o $f.s || { echo "FAIL: as"; exit 1; }
    done
    while IFS='|' read -r name body; do
        printf 'SECTIONS { %s }\n' "$body" > s.lds
        rm -f gnu our
        ${GNU_LD:-ld} -m $emu --no-warn-rwx-segments -T s.lds -o gnu a.o b.o ||
            { echo "FAIL: GNU ld did not link $name"; exit 1; }
        if ! ./ld -m $emu --no-warn-rwx-segments -T s.lds -o our a.o b.o; then
            echo "FAIL: $emu $name: not linked"; fail=1; continue
        fi
        for sec in .text .data; do
            if [ "$(place our $sec)" != "$(place gnu $sec)" ]; then
                echo "FAIL: $emu $name: $sec is at, as long and as aligned as $(place our $sec), GNU ld's $(place gnu $sec)"
                fail=1
            fi
            if [ "$(section our $sec)" != "$(section gnu $sec)" ]; then
                echo "FAIL: $emu $name: $sec's bytes are not GNU ld's"; fail=1
            fi
        done
        nm gnu | sort > gnu.sym
        nm our | sort > our.sym
        if ! cmp -s gnu.sym our.sym; then
            echo "FAIL: $emu $name: the symbols are not GNU ld's:"; diff gnu.sym our.sym; fail=1
        fi
    done < cases
done

if [ $fail = 0 ]; then
    echo "test_script_body_align: ok"
fi
exit $fail
