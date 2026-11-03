#!/bin/sh
# --eh-frame-hdr over an .eh_frame record whose length runs past the end.
#
# The walks of .eh_frame (counting its FDEs, then filling .eh_frame_hdr)
# tested a record's length as `off + 4 + len > size`.  With a 32-bit
# size_t, as the linker has on Substrate i386, a length near 4 GiB wraps
# that sum below the section's size: 0xfffffffc passed the test and
# moved the walk on by nothing, so the link never finished; 0xfffffffd
# and 0xfffffffe moved it on by one or two bytes, into the middle of the
# record.  Such a length comes straight from an input's .eh_frame.
#
# The linker is built for the host 32-bit here, so its size_t is 32 bits.
# GNU ld is the judge: it links such an object ("error in ...(.eh_frame);
# no .eh_frame_hdr table will be created"), with no search table, and the
# link must do the same, in time -- with the bad record first, and after
# a good one.
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
if ! ${CC:-cc} -m32 -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c 2>/dev/null; then
    echo "SKIP: the linker does not build for the host 32-bit"
    exit 0
fi

cd "$work" || exit 1
# whether OBJ's .eh_frame_hdr has a search table: none without the
# section, or with its table encoding DW_EH_PE_omit
table() {
    enc=$(readelf -x .eh_frame_hdr "$1" 2>/dev/null | awk '/^ *0x/ { print substr($2, 7, 2); exit }')
    if [ -z "$enc" ] || [ "$enc" = ff ]; then echo none; else echo table; fi
}

for case in 0xfffffffc 0xfffffffd 0xfffffffe cie,0xfffffffc cie,0xfffffffd cie,0xfffffffe; do
    len=${case#cie,}
    cie=
    [ "$len" != "$case" ] && cie='.long 8, 0, 0'
    cat > bad.s <<EOF
	.text
	.globl _start
_start:
	ret
	.section .eh_frame,"a",@progbits
	$cie
	.long $len
	.long 0
	.long 0
EOF
    ${CC:-cc} -m32 -c -o bad.o bad.s || { echo "SKIP: no 32-bit assembler"; exit 0; }
    "${GNU_LD:-ld}" -m elf_i386 --eh-frame-hdr -o gnu bad.o > gnu.said 2>&1 || {
        echo "FAIL: GNU ld would not link a length of $case: $(head -1 gnu.said)"; fail=1; continue; }
    if ! timeout 20 ./ld -m elf_i386 --eh-frame-hdr -o ours bad.o > said 2>&1; then
        echo "FAIL: a length of $case: the link did not finish, or failed: $(head -1 said)"; fail=1; continue
    fi
    if [ "$(table ours)" != "$(table gnu)" ]; then
        echo "FAIL: a length of $case: .eh_frame_hdr table: $(table ours), GNU ld's: $(table gnu)"; fail=1
    fi
done

[ $fail -eq 0 ] && echo "PASS: test_eh_frame_length"
exit $fail
