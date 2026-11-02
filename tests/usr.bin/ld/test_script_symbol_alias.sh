#!/bin/sh
# A symbol a script sets from another symbol of the program: its section
# and its type.
#
# Linux's vmlinux.lds ends with aliases set outside SECTIONS,
#     PROVIDE(sev_es_terminate = __pi_sev_es_terminate);
# which GNU ld puts in the section of the symbol they name, giving them its
# type: sev_es_terminate is a function in .text ('T' to nm, so in
# System.map and the kernel's kallsyms).  The linker made every symbol set
# outside SECTIONS absolute ('A'), and every symbol a script sets untyped.
# GNU ld gives a symbol the type of the one symbol its expression names,
# if it names one and no other (`sym = func + 1` is a function too).
#
# GNU ld is the judge: each script symbol's value, type, binding and
# section, x86-64 and i386.
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
# the script's symbols in $1: name, value, type, binding, and the name of
# their section (or ABS)
symbols() {
    readelf -SW "$1" | awk '{ sub(/^ *\[ */, ""); sub(/\] */, " "); if ($1 ~ /^[0-9]+$/) print $1, $2 }' > "$1.secs"
    readelf -sW "$1" | awk -v secs="$1.secs" '
        BEGIN { while ((getline l < secs) > 0) { split(l, f, " "); name[f[1]] = f[2] } }
        $8 ~ /^(top|in|uses)[0-9]/ { print $8, $2, $4, $5, ($7 in name) ? name[$7] : $7 }' | sort
}

cat > a.s <<'EOF'
.text
.globl _start
.type _start, @function
_start: ret
.globl f
.type f, @function
f: ret
.size f, 1
.data
.globl o
.type o, @object
o: .long 1
.size o, 4
EOF
cat > s.lds <<'EOF'
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
top1 = f;
SECTIONS {
    . = 0x400000;
    .text : { *(.text) in1 = f; } :text
    in2 = f;
    in3 = f + 1;
    in4 = o;
    . = ALIGN(0x1000);
    .data : { *(.data) } :data
}
PROVIDE(top2 = f);
top3 = f + 1;
top4 = o;
top5 = 0x1234;
top6 = o - f;
top7 = ADDR(.data);
EOF
printf '.text\n.globl uses1\nuses1: call top2\n' > b.s

for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    for f in a b; do
        ${GNU_AS:-as} --$m -mx86-used-note=no -o $f.o $f.s || { echo "FAIL: as"; exit 1; }
    done
    rm -f gnu our
    ${GNU_LD:-ld} -m $emu -T s.lds -o gnu a.o b.o || { echo "FAIL: GNU ld did not link"; exit 1; }
    if ! ./ld -m $emu -T s.lds -o our a.o b.o; then
        echo "FAIL: $emu: not linked"; fail=1; continue
    fi
    symbols gnu > gnu.sym
    symbols our > our.sym
    if ! cmp -s gnu.sym our.sym; then
        echo "FAIL: $emu: the script's symbols are not GNU ld's:"; diff gnu.sym our.sym; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_script_symbol_alias: ok"
fi
exit $fail
