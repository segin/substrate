#!/bin/sh
# A merged string section whose laid-out size does not fit in size_t.
#
# The pieces of SHF_MERGE input sections are laid out one after the other
# in a 64-bit offset, each rounded up to its alignment, and the block that
# holds them was then allocated with that size cast to size_t.  With a
# 32-bit size_t, as the linker has on Substrate i386, a size over 4 GiB
# was cut to its low bits, so the block was small while the pieces were
# still copied to their full offsets, far past its end, and the link died
# of a segmentation fault.  The first string of each input section keeps
# the section's alignment, which may be as much as 1 MiB, so 4100 such
# sections of one short string each, in an object of a few hundred KiB,
# are enough.
#
# The linker is built for the host 32-bit here, so its size_t is 32 bits,
# and must fail the link with a message, not be killed by a signal.  GNU
# ld is no judge here: a 64-bit GNU ld 2.47 "succeeds" with a 4 GiB file
# whose .rodata segment wraps the address space and whose section headers
# are blank, and running it would write that file.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${CC:-cc} -m32 -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c 2>/dev/null; then
    echo "SKIP: the linker does not build for the host 32-bit"
    exit 0
fi

cd "$work" || exit 1

# 4100 mergeable string sections of one output section, each aligned to
# 1 MiB and holding a string no other holds.
{
    i=0
    while [ $i -lt 4100 ]; do
        echo "	.section .rodata.str1.1,\"aMS\",@progbits,1,unique,$i"
        echo '	.p2align 20'
        echo "	.asciz \"s$i\""
        i=$((i + 1))
    done
    echo '	.text'
    echo '	.globl _start'
    echo '_start:'
    echo '	ret'
} > big.s
${CC:-cc} -m32 -c -o big.o big.s || { echo "SKIP: no 32-bit assembler"; exit 0; }

timeout 120 ./ld -m elf_i386 -o out big.o > said 2>&1
rc=$?
if [ $rc -eq 124 ] || [ $rc -gt 128 ]; then
    echo "FAIL: the link did not finish cleanly (status $rc)"; fail=1
elif [ $rc -eq 0 ]; then
    echo "FAIL: ld made a 32-bit link whose merged .rodata is over 4 GiB"; fail=1
elif [ ! -s said ]; then
    echo "FAIL: ld failed with no message"; fail=1
fi

[ $fail -eq 0 ] && echo "PASS: test_merge_size"
exit $fail
