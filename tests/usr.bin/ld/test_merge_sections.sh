#!/bin/sh
# SHF_MERGE sections: strings and constants each kept once.
#
# The linker laid every input's .rodata.str1.*, .rodata.cst* and
# .comment end to end: our cc's .rodata was 125752 bytes where GNU ld's
# is 113528, and its .comment 0x604 bytes of one "GCC: ..." string again
# and again.  GNU ld keeps each string or constant once in a program or
# shared object, and a string that ends another is found at that one's
# end; under -r it merges nothing.
#
# GNU ld is the judge: the merged sections hold GNU ld's bytes, the
# program prints what GNU ld's does (its strings reached through the
# merged copies), its debugging strings are where its .debug_info says,
# and under -r the sections are the size GNU ld's are.
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
# strings and constants in two files: some the same, one that ends
# another, strings gcc puts in .rodata.str1.8, and constants of .cst8 and
# .cst16; a program that writes them out with its own system calls
cat > a.c <<'EOF'
extern const char *b_strings[];
extern double b_scale(double);
static long out(const char *s) {
    long n = 0, r;
    while (s[n]) n++;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(1L), "D"(1L), "S"(s), "d"(n) : "rcx", "r11", "memory");
    return r;
}
/* (global, in .data: .rodata holds only what is merged, which has no addresses in it) */
const char *a_strings[] ={ "hello, world\n", "same\n", "a much longer string to be aligned\n" };
long a_main(void) {
    int i;
    for (i = 0; i < 3; i++) out(a_strings[i]);
    for (i = 0; b_strings[i]; i++) out(b_strings[i]);
    return b_scale(2.5) == 5.0 && b_scale(1.25) == 2.5;
}
void _start(void) {
    long ok = a_main();
    __asm__ volatile ("syscall" : : "a"(60L), "D"(ok ? 0L : 1L));
    for (;;) {}
}
EOF
cat > b.c <<'EOF'
const char *b_strings[] = { "world\n", "same\n", "a much longer string to be aligned\n", "only in b\n", 0 };
double b_scale(double x) { return x * 2.0; }
EOF
for f in a b; do
    ${GCC:-gcc} -O2 -g -fno-pic -fno-builtin -ffreestanding -fno-tree-loop-distribute-patterns \
        -fno-asynchronous-unwind-tables -fno-stack-protector -c -o $f.o $f.c || exit 1
done
if ! readelf -SW a.o | grep -q 'rodata.str1'; then
    echo "skip: gcc made no merged strings"; exit 0
fi

"${GNU_LD:-ld}" -o gnu a.o b.o || exit 1
if ! ./ld -m elf_x86_64 -o ours a.o b.o > said 2>&1; then
    echo "FAIL: ld: $(head -1 said)"; exit 1
fi

# The contents of section SEC of OBJ, in hex.
contents() {
    rm -f sec.bin
    objcopy --dump-section "$2"=sec.bin "$1" scratch.o 2>/dev/null && od -An -v -tx1 sec.bin | tr -d ' \n'
}
for sec in .rodata .comment; do
    if [ "$(contents gnu $sec)" != "$(contents ours $sec)" ]; then
        echo "FAIL: $sec is $(contents ours $sec | wc -c) hex digits, GNU ld's $(contents gnu $sec | wc -c), or differs"
        fail=1
    fi
done
./gnu > want; gst=$?
./ours > have; ost=$?
if [ $gst -ne $ost ] || ! cmp -s want have; then
    echo "FAIL: the program prints [$(tr '\n' '|' < have)] status $ost, GNU ld's [$(tr '\n' '|' < want)] status $gst"
    fail=1
fi
# the debugging strings: what .debug_info names, where it says
if command -v objdump > /dev/null; then
    objdump --dwarf=info gnu 2>/dev/null | grep -E 'DW_AT_name' | sed 's/.*: //' | sort > want
    objdump --dwarf=info ours 2>/dev/null | grep -E 'DW_AT_name' | sed 's/.*: //' | sort > have
    if [ ! -s want ] || ! cmp -s want have; then
        echo "FAIL: the names .debug_info gives are not GNU ld's"; diff want have | head -5; fail=1
    fi
fi

# -r: nothing merged
"${GNU_LD:-ld}" -r -o gnu.o a.o b.o || exit 1
./ld -m elf_x86_64 -r -o ours.o a.o b.o || { echo "FAIL: ld -r"; exit 1; }
for sec in .rodata.str1.1 .rodata.str1.8 .comment; do
    g=$(readelf -SW gnu.o | sed -n 's/^ *\[ *[0-9]*\] //p' | awk -v s="$sec" '$1 == s { print $5 }')
    o=$(readelf -SW ours.o | sed -n 's/^ *\[ *[0-9]*\] //p' | awk -v s="$sec" '$1 == s { print $5 }')
    if [ "$g" != "$o" ]; then
        echo "FAIL: ld -r: $sec is $o bytes, GNU ld's $g"; fail=1
    fi
done

if [ $fail -eq 0 ]; then
    echo "ok   test_merge_sections"
fi
exit $fail
