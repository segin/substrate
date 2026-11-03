#!/bin/sh
# --eh-frame-hdr over an FDE whose CIE pointer lands on a "CIE" whose
# length runs past the end of .eh_frame.
#
# Filling .eh_frame_hdr walks the records forward from the start, testing
# each one's length, but an FDE's CIE pointer may land anywhere before it:
# in the middle of a record, or in the FDE's own bytes.  The length found
# there was taken as the bound for reading the CIE's version, augmentation
# string and fields, without being tested against what is left of the
# section, so those reads ran on past its end.
#
# Here the FDE's CIE pointer is 1, so its "CIE" starts 3 bytes into the
# FDE: that CIE's length is 0x100, its id is the zero bytes that follow,
# its version is 1, and its augmentation string is the rest of the FDE,
# with no NUL before the end of the section.  The linker is built with
# AddressSanitizer, which reports the read past the end.  GNU ld is the
# judge: it links such an object ("error in ...(.eh_frame); no
# .eh_frame_hdr table will be created"), with no search table, and the
# link must do the same.
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
if ! ${CC:-cc} -O0 -g -w -fsanitize=address -fno-omit-frame-pointer -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c 2>/dev/null; then
    echo "SKIP: the linker does not build for the host with AddressSanitizer"
    exit 0
fi

cd "$work" || exit 1
# whether OBJ's .eh_frame_hdr has a search table: none without the
# section, or with its table encoding DW_EH_PE_omit
table() {
    enc=$(readelf -x .eh_frame_hdr "$1" 2>/dev/null | awk '/^ *0x/ { print substr($2, 7, 2); exit }')
    if [ -z "$enc" ] || [ "$enc" = ff ]; then echo none; else echo table; fi
}

# A good CIE and its FDE first, then the FDE whose CIE is its own bytes.
cat > bad.s <<EOF
	.text
	.globl _start
_start:
	ret
	.section .eh_frame,"a",@progbits
	.long 12
	.long 0
	.byte 1, 0, 1, 0x7c, 8, 0, 0, 0
	.long 16
	.long 20
	.long 0, 0, 0
	.long 32
	.long 1
	.byte 0, 0, 0, 1
	.fill 24, 1, 0x41
EOF
${CC:-cc} -m32 -c -o bad.o bad.s || { echo "SKIP: no 32-bit assembler"; exit 0; }
"${GNU_LD:-ld}" -m elf_i386 --eh-frame-hdr -o gnu bad.o > gnu.said 2>&1 || {
    echo "FAIL: GNU ld would not link the object: $(head -1 gnu.said)"; exit 1; }
if ! ASAN_OPTIONS=detect_leaks=0 timeout 60 ./ld -m elf_i386 --eh-frame-hdr -o ours bad.o > said 2>&1; then
    echo "FAIL: the link failed: $(grep -m1 ERROR said || head -1 said)"; fail=1
elif [ "$(table ours)" != "$(table gnu)" ]; then
    echo "FAIL: .eh_frame_hdr table: $(table ours), GNU ld's: $(table gnu)"; fail=1
fi

[ $fail -eq 0 ] && echo "PASS: test_eh_frame_cie_length"
exit $fail
