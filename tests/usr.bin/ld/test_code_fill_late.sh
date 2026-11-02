#!/bin/sh
# The gaps in an output section that becomes code only when a later input
# section goes in are filled as code's.
#
# GNU ld fills the padding between the input sections of an output section
# by what the output section is in the end: no-ops in code, zeros
# elsewhere.  Linux's x86-64 vmlinux.lds puts the kexec code,
# *(.text..relocate_kernel), into .data after all its data, so .data is
# code, and the padding between its data sections holds no-ops.  The
# linker filled each gap as the section was when the gap was made, and
# those before the code were zeros.
#
# GNU ld is the judge: .data's bytes, x86-64 and i386, with a gap before
# the code and one after it.
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

cat > a.s <<'EOF'
.text
.globl _start
_start: ret
.data
.byte 1, 2, 3
.section .text..relocate_kernel,"ax"
.byte 0xc3
EOF
cat > b.s <<'EOF'
.data
.p2align 4
.byte 4
.section .text..relocate_kernel,"ax"
.p2align 3
.byte 0xcc
EOF
cat > s.lds <<'EOF'
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x400000;
    .text : { *(.text) } :text
    . = ALIGN(0x1000);
    .data : { *(.data) *(.text..relocate_kernel) } :data
}
EOF

for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    for f in a b; do
        ${GNU_AS:-as} --$m -mx86-used-note=no -o $f.o $f.s || { echo "FAIL: as"; exit 1; }
    done
    rm -f gnu our
    ${GNU_LD:-ld} -m $emu --no-warn-rwx-segments -T s.lds -o gnu a.o b.o || { echo "FAIL: GNU ld did not link"; exit 1; }
    if ! ./ld -m $emu --no-warn-rwx-segments -T s.lds -o our a.o b.o; then
        echo "FAIL: $emu: not linked"; fail=1; continue
    fi
    want=$(section gnu .data)
    got=$(section our .data)
    if [ "$got" != "$want" ]; then
        echo "FAIL: $emu: .data is $got, GNU ld's $want"; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_code_fill_late: ok"
fi
exit $fail
