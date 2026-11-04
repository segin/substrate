#!/bin/sh
# What a symbol assignment or ASSERT in an output section's body works
# out: "." and ABSOLUTE(.), ALIGN(exp) and ALIGN(exp, align), ADDR and
# SIZEOF, and what the symbols they set are taken as in later statements
# and later sections.
#
# Linux's arch/x86/boot/setup.ld has
#     .signature : {
#         setup_sig = .;
#         LONG(0x5a5aaa55)
#         setup_size = ALIGN(ABSOLUTE(.), 4096);
#         setup_sects = ABSOLUTE(setup_size / 512);
#         ASSERT(setup_sects >= 5, "The setup must be at least 5 sectors in size");
#         ...
#     }
# GNU ld's rules for a body ("Expression Section" in its manual): "." is
# an offset in the section, an absolute symbol is read as a number, and a
# number assigned is an offset in the section; ALIGN(exp, align) aligns
# an offset in a section as an offset.  The linker took any symbol set in
# a body as an address, put a symbol whose value was absolute in whatever
# section held its address, and aligned addresses, so setup_size went in
# .bss, setup_sects came out an address, and the ASSERT failed.
#
# GNU ld is the judge: each symbol's value and section, or that it is
# absolute, and that the links GNU ld makes are made.
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
# the script's symbols of $1: name, value, and the section's name or ABS
symbols() {
    readelf -SW "$1" | awk '/^ *\[ *[0-9]+\]/ { sub(/^ *\[ */, ""); sub(/\]/, ""); print $1, $2 }' > "$1.secs"
    readelf -sW "$1" | awk 'NR > 3 && $5 == "GLOBAL" { print $8, $2, $7 }' |
        awk 'NR == FNR { sec[$1] = $2; next } { print $1, $2, ($3 == "ABS" ? "ABS" : sec[$3]) }' "$1.secs" - |
        grep -v '^_start ' | sort
}

# what stands before .sig, its body, what stands after it
cat > cases <<'EOF'
|setup_sig = .; setup_size = ALIGN(ABSOLUTE(.), 4096); setup_sects = ABSOLUTE(setup_size / 512); ASSERT(setup_sects >= 5, "small"); ASSERT(setup_sects <= 64, "big");|
|x = ABSOLUTE(.); y = x; ASSERT(y >= 5, "small");|
|x = ALIGN(ABSOLUTE(.), 4096); y = x / 512;|
|x = .; y = ABSOLUTE(.); z = ALIGN(8); w = ALIGN(., 16); v = ALIGN(ABSOLUTE(.), 16); u = ALIGN(x, 32);|
|x = ADDR(.data); y = SIZEOF(.data); z = ADDR(.data) + 4; w = 10; u = . - ADDR(.sig); t = SIZEOF(.data) / 2;|
|x = ABSOLUTE(.); y = x / 2;|a = x; b = y; c = x + 1; d = ABSOLUTE(y); e = y + 0;
|x = .;|a = x; b = x - ADDR(.sig); c = x + 1;
|x = ABSOLUTE(.); ASSERT(x == 0x138d, "absolute"); ASSERT(. == 4, "dot"); ASSERT(x > 0x1000, "number");|
|ASSERT(ABSOLUTE(.) == 0x138d, "a"); ASSERT(. - ADDR(.sig) == 4, "b");|x = .; ASSERT(x == 0x138d, "c") . = ASSERT(. == 0x138d, "d");
|start = .; abs = ABSOLUTE(start); off = abs - ADDR(.sig); half = ABSOLUTE(off) / 2;|after = half; size = ALIGN(ABSOLUTE(abs), 0x100) - abs;
. = 0x2000;|a = .; b = ABSOLUTE(.) / 0x100;|c = . + 12; .tail : { d = ABSOLUTE(c) - 1; e = c - 1; }
EOF

printf '.section .data\n.fill 5000,1,0\n.bss\n.zero 4\n.globl _start\n.text\n_start: ret\n' > a.s
${GNU_AS:-as} --32 -mx86-used-note=no -o a.o a.s || { echo "FAIL: as"; exit 1; }
n=0
while IFS='|' read -r pre body post; do
    n=$((n + 1))
    printf 'SECTIONS { . = 0; .text : { *(.text) } .data : { *(.data) } %s .sig : { LONG(0x5a5aaa55) %s } %s }\n' \
        "$pre" "$body" "$post" > s.lds
    rm -f gnu our
    ${GNU_LD:-ld} -m elf_i386 --no-warn-rwx-segments -T s.lds -o gnu a.o 2>gnu.err ||
        { echo "FAIL: GNU ld did not link case $n:"; cat gnu.err; cat s.lds; exit 1; }
    if ! ./ld -m elf_i386 --no-warn-rwx-segments -T s.lds -o our a.o 2>our.err; then
        echo "FAIL: case $n ($body): not linked:"; cat our.err; fail=1; continue
    fi
    symbols gnu > gnu.sym
    symbols our > our.sym
    if ! cmp -s gnu.sym our.sym; then
        echo "FAIL: case $n ($pre | $body | $post): the symbols are not GNU ld's:"; diff gnu.sym our.sym; fail=1
    fi
done < cases

# an ASSERT GNU ld fails, this must fail too
printf 'SECTIONS { . = 0; .text : { *(.text) } .data : { *(.data) } .sig : { LONG(0) x = ABSOLUTE(.); y = ABSOLUTE(x / 512); ASSERT(y >= 0x100, "too small"); } }\n' > f.lds
if ${GNU_LD:-ld} -m elf_i386 --no-warn-rwx-segments -T f.lds -o gnu a.o 2>/dev/null; then
    echo "FAIL: GNU ld linked what fails its ASSERT"; exit 1
fi
if ./ld -m elf_i386 --no-warn-rwx-segments -T f.lds -o our a.o 2>err; then
    echo "FAIL: an ASSERT that fails was linked"; fail=1
elif ! grep -q 'too small' err; then
    echo "FAIL: the ASSERT that fails was not said:"; cat err; fail=1
fi

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
