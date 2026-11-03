#!/bin/sh
# --eh-frame-hdr over an FDE too short to hold its initial location.
#
# Filling .eh_frame_hdr reads each FDE's initial location from the bytes
# after its CIE pointer, and tested only that the FDE's length covered the
# CIE pointer.  An FDE of length 4 that is the last record of .eh_frame
# then had its initial location read from past the end of the section, and
# a shorter-than-needed one not last had it read from the next record.
#
# Here a good CIE and its FDE are followed by an FDE whose length is 4:
# the CIE pointer and nothing more, at the very end of .eh_frame.  The
# linker is built with AddressSanitizer, which reports the read past the
# end.  GNU ld is the judge: it links such an object ("error in
# ...(.eh_frame); no .eh_frame_hdr table will be created"), with no search
# table, and the link must do the same.
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

# A good CIE and its FDE, then an FDE that is only its CIE pointer.
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
	.long 4
	.long 40
EOF
${CC:-cc} -m32 -c -o bad.o bad.s || { echo "SKIP: no 32-bit assembler"; exit 0; }
"${GNU_LD:-ld}" -m elf_i386 --eh-frame-hdr -o gnu bad.o > gnu.said 2>&1 || {
    echo "FAIL: GNU ld would not link the object: $(head -1 gnu.said)"; exit 1; }
if ! ASAN_OPTIONS=detect_leaks=0 timeout 60 ./ld -m elf_i386 --eh-frame-hdr -o ours bad.o > said 2>&1; then
    echo "FAIL: the link failed: $(grep -m1 ERROR said || head -1 said)"; fail=1
elif [ "$(table ours)" != "$(table gnu)" ]; then
    echo "FAIL: .eh_frame_hdr table: $(table ours), GNU ld's: $(table gnu)"; fail=1
fi

[ $fail -eq 0 ] && echo "PASS: test_eh_frame_fde_short"
exit $fail
