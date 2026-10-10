#!/bin/sh
# A branch across an alignment reaches its label (AS-T-222).
#
# To choose between the short and the near form of a jump the assembler
# measures what lies between the jump and its label, by assembling each
# statement there into a scratch buffer and taking its length.  An
# alignment computed its padding from the length of the buffer it was
# writing to -- the scratch one, empty -- and so measured nothing, every
# time.  The jump was then aimed short by the padding: into the padding.
# gcc puts `.p2align 4,,10` before loops and branches over them, so this
# was in the path of anything compiled at -O2.
#
# The bytes are GNU as's.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# head WHAT SOURCE BYTES: .text begins with BYTES, in both modes.
head_is() {
    printf "$2" > t.s
    for m in 32 64; do
        if ! "$AS" --$m -o t.o t.s 2> err; then
            echo "FAIL --$m $1: $(head -1 err | sed 's/.*: //')"; fail=1; continue
        fi
        objcopy -O binary -j .text t.o t.bin
        got=$(od -An -v -tx1 t.bin | tr -d ' \n' | cut -c1-${#3})
        [ "$got" = "$3" ] || { echo "FAIL --$m $1: begins $got, not $3"; fail=1; }
    done
}

# tail WHAT SOURCE BYTES: .text ends with BYTES.
tail_is() {
    printf "$2" > t.s
    for m in 32 64; do
        if ! "$AS" --$m -o t.o t.s 2> err; then
            echo "FAIL --$m $1: $(head -1 err | sed 's/.*: //')"; fail=1; continue
        fi
        objcopy -O binary -j .text t.o t.bin
        got=$(od -An -v -tx1 t.bin | tr -d ' \n' | tail -c ${#3})
        [ "$got" = "$3" ] || { echo "FAIL --$m $1: ends $got, not $3"; fail=1; }
    done
}

# The label is at 0x80, after 120 bytes and 6 of padding: eb 7e.  It was
# eb 78, into the int3 fill.
head_is "forward over a fill" \
    '.text\njmp 1f\n.skip 120,0x90\n.p2align 4,0xcc\n1: ret\n'    eb7e
head_is "forward over padding" \
    '.text\nnop\njmp 1f\nnop\n.p2align 3\n1: ret\n'               90eb05
# No padding, the maximum being exceeded: the jump is over the nop alone.
head_is "an alignment that pads nothing" \
    '.text\nnop\nje 1f\n.p2align 4,,10\nnop\n1: ret\n'            90740190c3
head_is "padding allowed by its maximum" \
    '.text\nnop\njne 2f\nnop\n.p2align 4,,15\n2: nop\n'           90750d
tail_is "backward over padding" \
    '.text\n1: ret\nnop\n.p2align 4\njmp 1b\n'                    ebee

# A difference of labels taken from another section sees the padding too.
printf '.text\na: .byte 1\n.p2align 4\nb: .byte 2\n.data\n.long b-a\n' > t.s
"$AS" --32 -o t.o t.s 2> err || { echo "FAIL label difference: $(head -1 err)"; fail=1; }
objcopy -O binary -j .data t.o t.bin
[ "$(od -An -v -tx1 t.bin | tr -d ' \n')" = 10000000 ] ||
    { echo "FAIL label difference: $(od -An -v -tx1 t.bin | tr -d ' \n'), not 16"; fail=1; }

# A jump table.  Measuring a statement must not need the value of what it
# holds: `.long .L1` is four bytes wherever .L1 is.  Measured by being
# assembled, it asked where .L1 was, which measured the branch before the
# table, which measured the table -- and the assembler died of a stack
# overflow on what gcc writes for any switch.
cat > t.s <<'EOF'
	.text
	cmpl $1, %eax
	ja .L1
	jmp *.L3(,%eax,4)
	.section .rodata
	.align 4
.L3:
	.long .L1
	.long .L2
	.text
.L2:	nop
.L1:	ret
EOF
if ! "$AS" --32 -o t.o t.s 2> err; then
    echo "FAIL jump table: exit $? $(head -1 err)"; fail=1
else
    objcopy -O binary -j .text t.o text.bin
    objcopy -O binary -j .rodata t.o table.bin
    # Each entry is the offset in .text of its label: .L1 is the ret, .L2
    # the nop before it.
    first=$(od -An -v -tu4 -N4 table.bin | tr -d ' ')
    second=$(od -An -v -tu4 -j4 -N4 table.bin | tr -d ' ')
    [ "$(od -An -v -tx1 -j"$first" -N1 text.bin | tr -d ' ')" = c3 ] ||
        { echo "FAIL jump table: the first entry, $first, is not the ret"; fail=1; }
    [ "$(od -An -v -tx1 -j"$second" -N1 text.bin | tr -d ' ')" = 90 ] ||
        { echo "FAIL jump table: the second entry, $second, is not the nop"; fail=1; }
    [ "$(readelf -rW t.o | grep -c ' R_386_32 ')" = 3 ] ||
        { echo "FAIL jump table: not three R_386_32 relocations"; fail=1; }
fi

[ "$fail" -eq 0 ] && echo "ok: branches across alignments"
exit "$fail"
