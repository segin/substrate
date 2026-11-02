#!/bin/sh
# A linker script's fills: an output section's `=fill` and FILL().
#
# GNU ld fills every gap in an output section -- the padding an input
# section's alignment leaves, and the room `. = ALIGN(16)` or `. += n`
# makes -- with the fill in effect where the gap is: the last FILL()
# before it in the section's body, or the section's `=fill`, and with
# neither, no-ops in code and zeros elsewhere.  A fill written as a hex
# number is that string of bytes, as long as it is (0x123 is 01 23); any
# other expression is 4 bytes, big-endian.  Each gap starts the pattern
# over.  Linux's vmlinux.lds gives its .text `:text = 0xcccccccc`.  The
# linker read the fills and dropped them, filling with zeros, and the
# room a statement made in code was zeros, not no-ops.
#
# GNU ld is the judge: .text and .data of executables linked with such
# scripts, x86-64 and i386, are GNU ld's byte for byte.
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

# name, then the script: .text and .data, each `{ body } fill`
cat > cases <<'EOF'
assign-align-fill|.text : { a.o(.text) . = . + 5; b.o(.text) FILL(0x5566) c.o(.text) . = ALIGN(16); } = 0x11223344 .data : { *(.data) }
five-bytes|.text : { a.o(.text) . = . + 6; b.o(.text) c.o(.text) . += 3; } = 0x123456789a .data : { *(.data) }
expression|.text : { a.o(.text) . = . + 6; b.o(.text) c.o(.text) } = 144+1 .data : { *(.data) }
decimal|.text : { a.o(.text) . += 7; b.o(.text) c.o(.text) } = 256 .data : { *(.data) }
odd-digits|.text : { a.o(.text) . = . + 5; b.o(.text) c.o(.text) } = 0x123 .data : { *(.data) }
leading-zero|.text : { a.o(.text) . += 7; b.o(.text) c.o(.text) } = 0x0090 .data : { *(.data) }
parenthesized|.text : { a.o(.text) . = . + 5; b.o(.text) c.o(.text) } = (0x1234) .data : { *(.data) }
fill-positional|.text : { a.o(.text) . += 2; FILL(0x77) . += 2; b.o(.text) . += 2; FILL(0x88) . += 2; c.o(.text) } = 0x11 .data : { *(.data) }
fill-only|.text : { FILL(0x90) a.o(.text) b.o(.text) c.o(.text) . = ALIGN(32); } .data : { *(.data) }
no-fill-code|.text : { a.o(.text) . = . + 5; b.o(.text) c.o(.text) . = ALIGN(16); } .data : { a.o(.data) . += 3; b.o(.data) c.o(.data) . = ALIGN(16); }
data-fill|.text : { *(.text) } .data : { a.o(.data) . += 3; b.o(.data) FILL(0xee) c.o(.data) . = ALIGN(16); } = 0xab
one-pattern|.text : { *(.text) . = ALIGN(64); } = 0xcccccccc .data : { *(.data) . = ALIGN(16); } = 0x0102
EOF

printf '.text\n.globl _start\n_start: .byte 0xaa,0xaa,0xaa\n.data\n.byte 1\n' > a.s
printf '.text\n.p2align 3\nb: .byte 0xbb\n.data\n.p2align 3\n.byte 2\n' > b.s
printf '.text\n.byte 0xcc\n.data\n.byte 3\n' > c.s
for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    for f in a b c; do
        ${GNU_AS:-as} --$m -mx86-used-note=no -o $f.o $f.s || { echo "FAIL: as"; exit 1; }
    done
    while IFS='|' read -r name body; do
        printf 'SECTIONS { . = 0x400000; %s }\n' "$body" > s.lds
        # Linux's form: the fill after the program header
        printf 'PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }\nSECTIONS { . = 0x400000; %s }\n' \
            "$(printf '%s' "$body" | sed 's/^\(.text : {[^}]*}\)/\1 :text/; s/\(.data : {[^}]*}\)/\1 :data/')" > p.lds
        for s in s p; do
            rm -f gnu our
            ${GNU_LD:-ld} -m $emu --no-warn-rwx-segments -T $s.lds -o gnu a.o b.o c.o ||
                { echo "FAIL: GNU ld did not link $name"; exit 1; }
            if ! ./ld -m $emu --no-warn-rwx-segments -T $s.lds -o our a.o b.o c.o; then
                echo "FAIL: $emu $name ($s.lds): not linked"; fail=1; continue
            fi
            for sec in .text .data; do
                want=$(section gnu $sec)
                got=$(section our $sec)
                if [ "$got" != "$want" ]; then
                    echo "FAIL: $emu $name ($s.lds): $sec is $got, GNU ld's $want"; fail=1
                fi
            done
        done
    done < cases
done

if [ $fail = 0 ]; then
    echo "test_script_fill: ok"
fi
exit $fail
