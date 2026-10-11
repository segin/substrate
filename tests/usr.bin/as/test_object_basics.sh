#!/bin/sh
# Five things an object says of its source (AS-T-255, 274, 277, 278,
# 292).
#
# - `.comm name, size, alignment` is a common symbol whose size is its
#   size and whose value is its alignment.  The size was put for the
#   value and nothing for the size: the linker gave `.comm buf,64,32` no
#   room at all, at an alignment of 64.
# - `.file "name"` is a symbol of type FILE, first in the symbol table;
#   `.ident "text"` is a string in .comment.  Both were taken and
#   nothing made of them.
# - `.zero`, `.skip`, `.space` and `.fill` of more than a mebibyte were
#   refused as "unreasonably large".
# - An object has no .eh_frame_hdr: that is the linker's to make.
#
# What is checked is what GNU as writes, and GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# symbols SOURCE WANT: each symbol of the given type or section as
# name=value/size/type/section, by name; `|` in SOURCE is a newline
symbols() {
    for m in 32 64; do
        cases=$((cases + 1))
        printf '%s\n' "$1" | tr '|' '\n' > t.s
        rm -f t.o
        if ! "$AS" --$m -o t.o t.s 2> err; then
            echo "FAIL --$m [$1]: $(tail -1 err)"; fail=1; continue
        fi
        got=$(readelf -sW t.o | awk '$7 == "COM" || $4 == "FILE" {
                  printf "%s=%d/%d/%s/%s\n", $8, strtonum("0x" $2), $3, $4, $7 }' | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')
        [ "$got" = "$2" ] || { echo "FAIL --$m [$1]: $got"; echo "  wanted: $2"; fail=1; }
    done
}

# Common symbols: the size, and the alignment written or implied.
symbols '.comm bufa,64,32' 'bufa=32/64/OBJECT/COM'
symbols '.comm c1,1|.comm c2,2|.comm c3,3|.comm c4,4|.comm c5,5|.comm c8,8|.comm c9,9|.comm c16,16|.comm c17,17|.comm c100,100' \
    'c100=16/100/OBJECT/COM c16=16/16/OBJECT/COM c17=16/17/OBJECT/COM c1=1/1/OBJECT/COM c2=2/2/OBJECT/COM c3=4/3/OBJECT/COM c4=4/4/OBJECT/COM c5=8/5/OBJECT/COM c8=8/8/OBJECT/COM c9=16/9/OBJECT/COM'
symbols '.comm a1,4,1|.comm a3,4,3|.comm t,0' 'a1=1/4/OBJECT/COM a3=3/4/OBJECT/COM t=1/0/OBJECT/COM'

# Two objects with one common symbol each have one object between them,
# of the larger size, at the larger alignment.
if command -v ld > /dev/null 2>&1; then
    cases=$((cases + 1))
    printf '\t.comm shared,24,8\n\t.text\n\t.globl _start\n_start:\tmovl shared, %%eax\n' > a.s
    printf '\t.comm shared,64,32\n\t.text\n\t.globl other\nother:\tmovl shared, %%eax\n' > b.s
    if "$AS" --32 -o a.o a.s 2> err && "$AS" --32 -o b.o b.s 2>> err && ld -m elf_i386 -o prog a.o b.o 2>> err; then
        got=$(readelf -sW prog | awk '$8 == "shared" { printf "%d %s %d", $3, $4, strtonum("0x" $2) % 32 }')
        [ "$got" = "64 OBJECT 0" ] || { echo "FAIL linked: shared is [$got]"; fail=1; }
    else
        echo "FAIL linked: $(tail -1 err)"; fail=1
    fi
fi

# The files a source names, each a symbol, before every other.
symbols '.file "hello.c"|.text|f: nop' 'hello.c=0/0/FILE/ABS'
symbols '.text|f: nop|.file "a.c"|.file "b.c"|.globl g|g: nop' 'a.c=0/0/FILE/ABS b.c=0/0/FILE/ABS'
symbols '.file "dir/sub/x.c"' 'dir/sub/x.c=0/0/FILE/ABS'
for m in 32 64; do
    cases=$((cases + 1))
    printf '\t.text\naaa:\tnop\n\t.file "z.c"\nbbb:\tnop\n' > t.s
    "$AS" --$m -o t.o t.s 2> err
    got=$(readelf -sW t.o | awk '$1 == "1:" { printf "%s %s", $4, $8 }')
    [ "$got" = "FILE z.c" ] || { echo "FAIL --$m: the first symbol is [$got]"; fail=1; }
done
# (A `.file` with a number is of the line table, and is no symbol.)
symbols '.file "a.c"|.file 1 "x.c"|.text|nop' 'a.c=0/0/FILE/ABS'

# What the source was made by.
comment() {
    cases=$((cases + 1))
    printf '%s\n' "$1" | tr '|' '\n' > t.s
    rm -f t.o
    if ! "$AS" --64 -o t.o t.s 2> err; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    got=$(readelf -x .comment t.o 2> /dev/null | awk '/^  0x/ { for (i = 2; i <= 5; i++) if ($i ~ /^[0-9a-f]+$/) printf "%s", $i }')
    [ "$got" = "$2" ] || { echo "FAIL [$1]: .comment is $got, not $2"; fail=1; }
    if [ -n "$2" ]; then
        readelf -SW t.o | grep -q ' \.comment  *PROGBITS .* 01  *MS ' ||
            { echo "FAIL [$1]: .comment is not a section of strings to merge"; fail=1; }
    fi
}
comment '.ident "GCC: (GNU) 16.1.0"' 004743433a2028474e55292031362e312e3000
comment '.ident "one"|.text|nop|.ident "two"' 006f6e650074776f00
comment '.ident "a\tb\"c"' 00610962226300
comment '.text|nop' ''

# More than a mebibyte, where there are bytes for it and where there
# are none.
for m in 32 64; do
    cases=$((cases + 1))
    printf '\t.data\n\t.zero 0x200000\n\t.byte 7\n\t.fill 0x80000,4,0x11223344\n\t.bss\n\t.zero 0x4000000\n\t.skip 0x100000\n\t.section .foo,"aw"\n\t.space 0x180000,0x5a\n' > t.s
    rm -f t.o
    if ! "$AS" --$m -o t.o t.s 2> err; then
        echo "FAIL --$m large fills: $(tail -1 err)"; fail=1; continue
    fi
    got=$(readelf -SW t.o | awk '{ for (i = 1; i <= NF; i++) if ($i == ".data" || $i == ".bss" || $i == ".foo")
                                       printf "%s=%d ", $i, strtonum("0x" $(i + 4)) }')
    [ "$got" = ".data=4194305 .bss=68157440 .foo=1572864 " ] || { echo "FAIL --$m large fills: $got"; fail=1; }
    objcopy -O binary -j .data t.o data.bin
    objcopy -O binary -j .foo t.o foo.bin
    tail=$(od -An -v -tx1 -j 2097152 -N 5 data.bin | tr -d ' \n')
    [ "$tail" = 0744332211 ] || { echo "FAIL --$m large fills: .data has $tail at 2 MiB"; fail=1; }
    foo=$(od -An -v -tx1 -j 1572860 foo.bin | tr -d ' \n')
    [ "$foo" = 5a5a5a5a ] || { echo "FAIL --$m large fills: .foo ends $foo"; fail=1; }
done

# And 64 MiB of it where there are bytes.
cases=$((cases + 1))
printf '\t.data\n\t.zero 0x4000000\n\t.byte 9\n' > t.s
rm -f t.o
if ! "$AS" --64 -o t.o t.s 2> err; then
    echo "FAIL 64 MiB in .data: $(tail -1 err)"; fail=1
else
    got=$(readelf -SW t.o | awk '{ for (i = 1; i <= NF; i++) if ($i == ".data") print strtonum("0x" $(i + 4)) }')
    [ "$got" = 67108865 ] || { echo "FAIL 64 MiB in .data: .data is $got bytes"; fail=1; }
fi
rm -f t.o

# No .eh_frame_hdr in an object.
for m in 32 64; do
    cases=$((cases + 1))
    printf '\t.text\n\t.cfi_startproc\nf:\tret\n\t.cfi_endproc\n' > t.s
    "$AS" --$m -o t.o t.s 2> err
    if readelf -SW t.o | grep -q 'eh_frame_hdr'; then
        echo "FAIL --$m: an .eh_frame_hdr in a relocatable object"; fail=1
    fi
done

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
