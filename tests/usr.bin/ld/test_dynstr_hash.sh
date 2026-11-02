#!/bin/sh
# .dynstr and .gnu.hash, as GNU ld makes them.
#
# The linker appended every string to .dynstr as it came (libc.so.6 nine
# times in our cc, and "close" though "fclose" ends with it): 954 bytes
# where GNU ld's are 835.  Its .gnu.hash had one bucket and hashed every
# symbol, the undefined ones too: 376 bytes where GNU ld's are 48.  GNU ld
# keeps a string once and finds one that ends another at that one's end;
# it puts the undefined symbols first in .dynsym, hashes only the defined
# ones, grouped by bucket, with bfd's number of buckets and Bloom filter.
#
# GNU ld is the judge: a shared object exporting 40 functions whose names
# end alike, and a PIE that imports some of them and libc's, have
# .dynstr and .gnu.hash of GNU ld's sizes, .gnu.hash with GNU ld's header,
# and the program finds every function through them.
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
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
mkdir -p bin && ln -sf "$work/ld" bin/ld
{
    i=0
    while [ $i -lt 20 ]; do
        echo "int close$i(void) { return $i; }"
        echo "int fclose$i(void) { return $i + 100; }"
        i=$((i + 1))
    done
} > lib.c
{
    echo '#include <stdio.h>'
    i=0
    while [ $i -lt 20 ]; do
        echo "extern int close$i(void); extern int fclose$i(void);"
        i=$((i + 1))
    done
    echo 'int main(void) { int s = 0;'
    i=0
    while [ $i -lt 20 ]; do
        echo "    s += close$i() + fclose$i();"
        i=$((i + 1))
    done
    echo '    printf("%d\n", s); return 0; }'
} > prog.c

# the size of SEC in OBJ, and .gnu.hash's header
size_of() {
    readelf -SW "$1" | sed -n 's/^ *\[ *[0-9]*\] //p' | awk -v s="$2" '$1 == s { print $5 }'
}
gnu_hash_header() {
    rm -f gh.bin
    objcopy --dump-section .gnu.hash=gh.bin "$1" scratch.o 2>/dev/null && od -An -tu4 -N16 gh.bin | tr -s ' '
}
compare() {     # WHAT GNU OURS
    for sec in .dynstr .gnu.hash .hash; do
        if [ "$(size_of "$2" $sec)" != "$(size_of "$3" $sec)" ]; then
            echo "FAIL: $1: $sec is $(size_of "$3" $sec) bytes, GNU ld's $(size_of "$2" $sec)"; fail=1
        fi
    done
    if [ "$(gnu_hash_header "$2")" != "$(gnu_hash_header "$3")" ]; then
        echo "FAIL: $1: .gnu.hash header [$(gnu_hash_header "$3")], GNU ld's [$(gnu_hash_header "$2")]"; fail=1
    fi
}

${GCC:-gcc} -O2 -fPIC -c -o lib.o lib.c || exit 1
# (both hash tables: the SysV one's buckets are bfd's number too)
${GCC:-gcc} -shared -Wl,--hash-style=both -o libgnu.so lib.o || exit 1
if ! ${GCC:-gcc} -B"$work/bin/" -shared -Wl,--hash-style=both -o libours.so lib.o > said 2>&1; then
    echo "FAIL: ld -shared: $(head -1 said)"; exit 1
fi
compare "a shared object" libgnu.so libours.so

${GCC:-gcc} -O2 -fPIE -c -o prog.o prog.c || exit 1
cp libgnu.so libt.so
${GCC:-gcc} -pie -o gnu.out prog.o -L. -lt || exit 1
if ! ${GCC:-gcc} -B"$work/bin/" -pie -o ours.out prog.o -L. -lt > said 2>&1; then
    echo "FAIL: ld -pie: $(head -1 said)"; exit 1
fi
for sec in .dynstr .gnu.hash; do
    if [ "$(size_of gnu.out $sec)" != "$(size_of ours.out $sec)" ]; then
        echo "FAIL: a PIE: $sec is $(size_of ours.out $sec) bytes, GNU ld's $(size_of gnu.out $sec)"; fail=1
    fi
done

# the functions are found through our shared object's .gnu.hash.  (The
# linker brands what it makes ELFOSABI_SUBSTRATE, which the host's
# dynamic linker refuses in a shared object: the byte is put back to 0
# for the host to load it.)
want=$(LD_LIBRARY_PATH=. ./gnu.out)
cp libours.so libt.so
printf '\000' | dd of=libt.so bs=1 seek=7 conv=notrunc 2>/dev/null
have=$(LD_LIBRARY_PATH=. ./gnu.out)
if [ -z "$want" ] || [ "$have" != "$want" ]; then
    echo "FAIL: through our shared object the program prints [$have], [$want] through GNU ld's"; fail=1
fi
have=$(LD_LIBRARY_PATH=. ./ours.out)
if [ "$have" != "$want" ]; then
    echo "FAIL: our PIE prints [$have], GNU ld's [$want]"; fail=1
fi

if [ $fail -eq 0 ]; then
    echo "ok   test_dynstr_hash"
fi
exit $fail
