#!/bin/sh
# A linker script's `. = expr` in an output section's body, with expr an
# address: a symbol from an input section already placed, in this output
# section or an earlier one, and any operator on it.
#
# Linux's vmlinux.lds has, in .text,
#     *(.text..__x86.rethunk_untrain)
#     ...
#     . = srso_alias_untrain_ret | (1 << 2) | (1 << 8) | (1 << 14) | (1 << 20);
#     *(.text..__x86.rethunk_safe)
# GNU ld works such an assignment out with the symbol's final address and
# puts what follows there, filling the gap as it fills any other (no-ops
# in code, or the section's =fill or FILL()); it refuses only a counter
# moved backwards.  The linker understood only ALIGN and adding a constant
# between two input section descriptions, and refused the rest: the merge
# places input sections before anything has an address.  Now the link is
# done over with the addresses the first pass came to.
#
# GNU ld is the judge: the bytes, addresses and sizes of .text and .data
# and the addresses of the symbols, x86-64 and i386.
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
# the address and size of section $2 of $1
place() {
    readelf -SW "$1" | awk -v s="$2" '{ sub(/^ *\[ *[0-9]+\] */, ""); if ($1 == s) print $3, $5 }'
}
# the symbols of $1 and their addresses
symbols() {
    nm "$1" | awk '{ print $3, $1 }' | sort
}

# name, then what SECTIONS says
cat > cases <<'EOF'
vmlinux|. = 0x400000; .text : { _text = .; . = ALIGN(0x1000); *(.text.untrain) . = ALIGN(16); estart = .; *(.text.entry) eend = .; . = srso | (1 << 2) | (1 << 8); *(.text.safe) . = ALIGN(0x1000); *(.text) } = 0xcccccccc _etext = .; .data : { *(.data) }
no-fill|. = 0x400000; .text : { *(.text.untrain) *(.text.entry) . = srso | 0x40; *(.text.safe) *(.text) } .data : { *(.data) }
fill|. = 0x400000; .text : { *(.text.untrain) FILL(0x1122) *(.text.entry) . = srso + 0x30; *(.text.safe) *(.text) } = 0x90 .data : { *(.data) }
address|. = 0x400010; .text : { a.o(.text) . = (_start & ~0xff) + 0x300; b.o(.text) *(.text.*) } .data : { *(.data) }
symbol-set|. = 0x400000; .text : { *(.text.untrain) here = . + 8; *(.text.entry) . = here + 0x21; *(.text.safe) *(.text) } .data : { *(.data) }
data|. = 0x400000; .text : { *(.text*) } _etext = .; .data : { a.o(.data) . = dmark + 0x20; b.o(.data) . = MAX(., _etext + 0x80); *(.data.*) } = 0xab
EOF

cat > a.s <<'EOF'
.section .text.untrain,"ax"
.byte 0xaa
.globl srso
srso: .byte 0xab,0xac
.section .text.entry,"ax"
.byte 0xe1,0xe2,0xe3
.section .text.safe,"ax"
.globl safe
safe: .byte 0x5a
.text
.globl _start
_start: .byte 0xc3
.data
.globl dmark
dmark: .byte 1,2,3
EOF
cat > b.s <<'EOF'
.text
.p2align 2
.globl b
b: .byte 0xbb
.data
.globl bdata
bdata: .byte 4
.section .data.more,"aw"
.globl more
more: .byte 5
EOF

for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    for f in a b; do
        ${GNU_AS:-as} --$m -mx86-used-note=no -o $f.o $f.s || { echo "FAIL: as"; exit 1; }
    done
    while IFS='|' read -r name body; do
        printf 'SECTIONS { %s }\n' "$body" > s.lds
        rm -f gnu our
        ${GNU_LD:-ld} -m $emu --no-warn-rwx-segments -T s.lds -o gnu a.o b.o ||
            { echo "FAIL: GNU ld did not link $name"; exit 1; }
        if ! ./ld -m $emu --no-warn-rwx-segments -T s.lds -o our a.o b.o; then
            echo "FAIL: $emu $name: not linked"; fail=1; continue
        fi
        for sec in .text .data; do
            want=$(place gnu $sec)
            got=$(place our $sec)
            if [ "$got" != "$want" ]; then
                echo "FAIL: $emu $name: $sec is at and as long as $got, GNU ld's $want"; fail=1
            fi
            want=$(section gnu $sec)
            got=$(section our $sec)
            if [ "$got" != "$want" ]; then
                echo "FAIL: $emu $name: $sec is $got, GNU ld's $want"; fail=1
            fi
        done
        symbols gnu > gnu.sym
        symbols our > our.sym
        if ! cmp -s gnu.sym our.sym; then
            echo "FAIL: $emu $name: the symbols are not GNU ld's:"; diff gnu.sym our.sym; fail=1
        fi
    done < cases

    # the counter moved backwards: GNU ld refuses it, and so must this
    printf 'SECTIONS { . = 0x400000; .text : { *(.text.untrain) *(.text.entry) . = srso; *(.text.safe) *(.text) } }\n' > back.lds
    if ${GNU_LD:-ld} -m $emu --no-warn-rwx-segments -T back.lds -o gnu a.o b.o 2>/dev/null; then
        echo "FAIL: GNU ld linked a counter moved backwards"; exit 1
    fi
    if ./ld -m $emu --no-warn-rwx-segments -T back.lds -o our a.o b.o 2>err; then
        echo "FAIL: $emu: a counter moved backwards was linked"; fail=1
    elif ! grep -q 'backwards' err; then
        echo "FAIL: $emu: a counter moved backwards was refused for another reason:"; cat err; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_script_dot_expr: ok"
fi
exit $fail
