#!/bin/sh
# A linker script's SORT and its relatives: SORT and SORT_BY_NAME,
# SORT_BY_ALIGNMENT, the two nested either way, SORT_BY_INIT_PRIORITY and
# SORT_NONE, around the patterns of an input section description.
#
# GNU ld puts the sections such a pattern takes in that order: by name; the
# most aligned first; the outer of two nested first and the inner breaking
# its ties; by the priority the name ends in; and ties in input order.
# Linux's vmlinux.lds has KEEP(*(SORT(___ksymtab+*))), whose table the
# kernel searches by halving, and *(SORT_BY_ALIGNMENT(.data..percpu..hot.*))
# followed by ASSERT(__per_cpu_hot_end - __per_cpu_hot_start <= 64): in
# input order the padding between those sections broke the assertion.  The
# linker read the sorts and dropped them.
#
# GNU ld is the judge: .data and the symbols of executables linked with
# such scripts, x86-64 and i386.
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

# name, then the output sections after .text
cat > cases <<'EOF'
sort|.data 0x500000 : { *(SORT(.s.*)) *(.data) }
by-name|.data 0x500000 : { *(SORT_BY_NAME(.s.*)) *(.data) }
by-alignment|.data 0x500000 : { start = .; *(SORT_BY_ALIGNMENT(.s.*)) end = .; *(.data) }
name-alignment|.data 0x500000 : { *(SORT_BY_NAME(SORT_BY_ALIGNMENT(.s.*))) *(.data) }
alignment-name|.data 0x500000 : { *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.s.*))) *(.data) }
none|.data 0x500000 : { *(SORT_NONE(.s.*)) *(.data) }
keep|.data 0x500000 : { KEEP(*(SORT(.s.*))) *(.data) }
two-patterns|.data 0x500000 : { *(SORT(.s.b*) SORT(.s.a*)) *(.s.*) *(.data) }
init-priority|.init_array 0x500000 : { *(SORT_BY_INIT_PRIORITY(.init_array.*)) } .data 0x501000 : { *(.s.*) *(.data) }
EOF

# in each file, sections of several names and alignments, in no order
cat > a.s <<'EOF'
.section .s.c,"aw"
.globl c1
c1: .byte 0xc1
.section .s.a,"aw"
.p2align 3
.globl a1
a1: .byte 0xa1
.section .s.b,"aw"
.p2align 1
.globl b1
b1: .byte 0xb1
.section .s.bb,"aw"
.p2align 3
.globl bb1
bb1: .byte 0xbb
.section .init_array.00200,"aw"
.byte 0x20
.section .init_array.00100,"aw"
.byte 0x10
.data
.byte 0xd1
.text
.globl _start
_start: ret
EOF
cat > b.s <<'EOF'
.section .s.b,"aw"
.p2align 3
.globl b2
b2: .byte 0xb2
.section .s.a,"aw"
.globl a2
a2: .byte 0xa2
.section .s.c,"aw"
.p2align 2
.globl c2
c2: .byte 0xc2
.section .init_array.00150,"aw"
.byte 0x15
.data
.byte 0xd2
EOF

for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    for f in a b; do
        ${GNU_AS:-as} --$m -mx86-used-note=no -o $f.o $f.s || { echo "FAIL: as"; exit 1; }
    done
    while IFS='|' read -r name body; do
        printf 'SECTIONS { . = 0x400000; .text : { *(.text) } %s }\n' "$body" > s.lds
        rm -f gnu our
        ${GNU_LD:-ld} -m $emu --no-warn-rwx-segments -T s.lds -o gnu a.o b.o ||
            { echo "FAIL: GNU ld did not link $name"; exit 1; }
        if ! ./ld -m $emu --no-warn-rwx-segments -T s.lds -o our a.o b.o; then
            echo "FAIL: $emu $name: not linked"; fail=1; continue
        fi
        for sec in .data .init_array; do
            want=$(section gnu $sec)
            got=$(section our $sec)
            if [ "$got" != "$want" ]; then
                echo "FAIL: $emu $name: $sec is $got, GNU ld's $want"; fail=1
            fi
        done
        nm gnu | sort > gnu.sym
        nm our | sort > our.sym
        if ! cmp -s gnu.sym our.sym; then
            echo "FAIL: $emu $name: the symbols are not GNU ld's:"; diff gnu.sym our.sym; fail=1
        fi
    done < cases
done

if [ $fail = 0 ]; then
    echo "test_script_sort: ok"
fi
exit $fail
