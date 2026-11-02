#!/bin/sh
# An output section that is both writable and executable.
#
# Linux's vmlinux.lds puts the x86-64 kexec code, *(.text..relocate_kernel),
# into .data, which so becomes writable and executable.  GNU ld links it:
# what it says of such a thing is a warning about the segment, which
# --no-warn-rwx-segments (the kernel's flag) turns off.  The linker refused
# the link ("W^X policy violation"), whatever the options said.
#
# GNU ld is the judge: the link succeeds, the section's flags and bytes are
# GNU ld's, and so is whether anything is said, x86-64 and i386 (with
# PHDRS saying the segment is not executable, as the kernel's do, there is
# no segment to warn of).
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
# the bytes of section $2 of $1
section() {
    objcopy -O binary --only-section="$2" "$1" "$1$2.bin" 2>/dev/null && od -An -tx1 -v "$1$2.bin" | tr -d ' \n'
}
# the flags of section $2 of $1
flags() {
    readelf -SW "$1" | awk -v s="$2" '{ sub(/^ *\[ *[0-9]+\] */, ""); if ($1 == s) print $8 }'
}

cat > a.s <<'EOF'
.text
.globl _start
_start: ret
.section .text..relocate_kernel,"ax"
.globl relocate_kernel
relocate_kernel: nop; ret
.data
.byte 1, 2, 3
EOF
# as the kernel has it: its program headers, and .data where a page begins
cat > s.lds <<'EOF'
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x400000;
    .text : { *(.text) } :text
    . = ALIGN(0x1000);
    .data : { *(.data) . = ALIGN(16); *(.text..relocate_kernel) } :data
}
EOF

for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    ${GNU_AS:-as} --$m -mx86-used-note=no -o a.o a.s || { echo "FAIL: as"; exit 1; }
    for opt in --no-warn-rwx-segments --warn-rwx-segments --error-rwx-segments; do
        rm -f gnu our
        ${GNU_LD:-ld} -m $emu $opt -T s.lds -o gnu a.o 2>gnu.err ||
            { echo "FAIL: GNU ld did not link"; exit 1; }
        if ! ./ld -m $emu $opt -T s.lds -o our a.o 2>our.err; then
            echo "FAIL: $emu $opt: not linked:"; cat our.err; fail=1; continue
        fi
        if [ "$(flags our .data)" != "$(flags gnu .data)" ]; then
            echo "FAIL: $emu $opt: .data's flags are $(flags our .data), GNU ld's $(flags gnu .data)"; fail=1
        fi
        if [ "$(section our .data)" != "$(section gnu .data)" ]; then
            echo "FAIL: $emu $opt: .data is $(section our .data), GNU ld's $(section gnu .data)"; fail=1
        fi
        if { [ -s gnu.err ] && [ ! -s our.err ]; } || { [ ! -s gnu.err ] && [ -s our.err ]; }; then
            echo "FAIL: $emu $opt: said '$(cat our.err)', GNU ld '$(cat gnu.err)'"; fail=1
        fi
    done
done

if [ $fail = 0 ]; then
    echo "test_writable_code_section: ok"
fi
exit $fail
