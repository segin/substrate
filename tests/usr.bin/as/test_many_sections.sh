#!/bin/sh
# An object of more than 65,279 sections (AS-T-302).
#
# 66,000 functions, each in a section of its own with a global symbol:
# the object is written with ELF's extended numbering -- e_shnum 0 and
# the count in the null section's size, e_shstrndx SHN_XINDEX and the
# index in its link, and SHT_SYMTAB_SHNDX for the symbols of sections
# numbered past 0xffff.  The library gives the 256 numbers SHN_LORESERVE
# to SHN_HIRESERVE empty sections of SHT_NULL, so that no section is
# numbered as a reserved index is: each symbol's section is GNU as's but
# for those 256, and the object links.  (Past 65,279 it was refused:
# "invalid ELF format".)
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
N=66000
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

command -v readelf > /dev/null 2>&1 || { echo "SKIP: no readelf"; exit 0; }

awk -v n=$N 'BEGIN {
    for (i = 0; i < n; i++) {
        print ".section .text.f" i ",\"ax\",@progbits"; print ".globl f" i; print "f" i ": ret"
    }
}' > many.s

fail=0
for mode in 32 64; do
    if ! "$AS" --$mode -o many.o many.s 2> err; then
        echo "FAIL --$mode: not assembled: $(head -1 err)"
        fail=1
        continue
    fi
    hdr=$(readelf -h many.o 2>&1)
    # The header's count is 0 and the real one the null section's; the
    # names' section is SHN_XINDEX there, and the last section.
    total=$(echo "$hdr" | sed -n 's/.*Number of section headers: *0 (\([0-9]*\)).*/\1/p')
    if [ -z "$total" ] || [ "$total" -le $((N + 256)) ]; then
        echo "FAIL --$mode: the count of sections is not 0 in the header and past $((N + 256)) in the null section"
        fail=1
    fi
    echo "$hdr" | grep -q "Section header string table index: *65535 ($((total - 1)))" || {
        echo "FAIL --$mode: the names' section is not SHN_XINDEX and the null section's link"
        fail=1
    }
    readelf -SW many.o | grep -q 'SYMTAB SECTION INDICES\|SYMTAB_SHNDX' || {
        echo "FAIL --$mode: no SHT_SYMTAB_SHNDX"
        fail=1
    }
    # Each function's section is f0's and the function's number, and 256
    # more past the reserved indices, which no section is numbered as.
    readelf -sW many.o | awk -v mode=$mode '
        $8 ~ /^f[0-9]+$/ { sec[substr($8, 2) + 0] = $7 + 0 }
        END {
            base = sec[0]
            for (i = 0; i < 66000; i++) {
                want = base + i
                if (want >= 65280) want += 256
                if (sec[i] != want) { printf "FAIL --%s: f%d in %d, not %d\n", mode, i, sec[i], want; exit 1 }
            }
        }' || fail=1
    if command -v ld > /dev/null 2>&1; then
        emu=$([ $mode = 32 ] && echo elf_i386 || echo elf_x86_64)
        if ld -m $emu -e f0 -o many.out many.o 2> err; then
            a=$(nm many.out | awk '$3 == "f0" { print $1 }')
            b=$(nm many.out | awk '$3 == "f65999" { print $1 }')
            [ $((0x$b - 0x$a)) -eq $((N - 1)) ] || {
                echo "FAIL --$mode: linked, f65999 is not f0 and $((N - 1)) bytes"
                fail=1
            }
        else
            echo "FAIL --$mode: the host's ld does not link it: $(head -1 err)"
            fail=1
        fi
    fi
done

[ $fail -eq 0 ] && echo "ok: an object of $N sections, in both modes"
exit $fail
