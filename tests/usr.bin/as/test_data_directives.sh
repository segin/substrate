#!/bin/sh
# The data directives that are other ones by another name, or that work
# their bytes out: .dc.*, .ds.*, .dcb.*, .octa, .uleb128 and .sleb128,
# .single, .tfloat, .string16 and its fellows.
#
# None was known.  A directive that nothing knew was passed over in
# silence, so each put nothing in the section and what followed it was
# at the wrong address.
#
# .uleb128 and .sleb128 of the distance between two labels are the
# lengths in a compiler's exception table: how many bytes one takes
# depends on the distance, and the distance on how many bytes the others
# take.
#
# The bytes are GNU as 2.46's.  `|` in a source is a new line.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# bytes_of MODE SOURCE: the bytes of .data, or `refused`.
bytes_of() {
    { echo .data; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if "$AS" "-$1" -o t.o t.s > out 2> err; then
        objcopy -O binary --only-section=.data t.o data.bin 2>/dev/null
        od -An -v -tx1 data.bin | tr -d ' \n'
    else
        printf refused
    fi
}

# d MODES BYTES SOURCE
d() {
    for mode in $1; do
        cases=$((cases + 1))
        got=$(bytes_of "$mode" "$3")
        if [ "$got" != "$2" ]; then
            echo "FAIL $mode [$3]: $got, and GNU as: $2 $(head -1 err 2>/dev/null)"
            fail=1
        fi
    done
}

# p MODES PREFIX LENGTH SOURCE: .data begins with PREFIX and is LENGTH
# bytes long.
p() {
    for mode in $1; do
        cases=$((cases + 1))
        got=$(bytes_of "$mode" "$4")
        case "$got" in
        "$2"*) [ "$((${#got} / 2))" = "$3" ] || { echo "FAIL $mode [$4]: $((${#got} / 2)) bytes, not $3"; fail=1; } ;;
        *) echo "FAIL $mode [$4]: begins $(printf %s "$got" | cut -c1-24), not $2"; fail=1 ;;
        esac
    done
}

# .dc: a constant of the size its suffix names.  .dc.a is an address.
d  "32 64" 0102 '.dc.b 1,2'
d  "32 64" 01000200 '.dc.w 1,2'
d  "32 64" 01000200 '.dc 1,2'
d  "32 64" 01000000 '.dc.l 1'
d  "32"    01000000 '.dc.a 1'
d  "64"    0100000000000000 '.dc.a 1'
d  "32"    00000000 '.dc.a sym'
d  "64"    0000000000000000 '.dc.a sym'
d  "32 64" 0000c03f '.dc.s 1.5'
d  "32 64" 000000000000f83f '.dc.d 1.5'
d  "32 64" 00000000000000c0ff3f '.dc.x 1.5'
d  "32 64" 010002 '.dc.b 1,,2'

# .ds: that many of the size, zero or a value.
d  "32 64" 000000 '.ds.b 3'
d  "32 64" 000000000000 '.ds.w 3'
d  "32 64" 000000000000 '.ds 3'
d  "32 64" 0000000000000000 '.ds.l 2'
d  "32 64" 00000000 '.ds.s 1'
d  "32 64" 0000000000000000 '.ds.d 1'
d  "32 64" 00000000000000000000 '.ds.x 1'
d  "32 64" 00000000000000000000 '.ds.p 1'
d  "32 64" 0707 '.ds.b 2,7'
d  "32 64" 34123412 '.ds.w 2,0x1234'
d  "32 64" 4433221144332211 '.ds.l 2,0x11223344'
d  "32 64" 05000000000000000000 '.ds.x 1,5'
d  "32 64" '' '.ds.b 0'

# .dcb: that many of the constant.
d  "32 64" 070707 '.dcb.b 3,7'
d  "32 64" 34123412 '.dcb.w 2,0x1234'
d  "32 64" 34123412 '.dcb 2,0x1234'
d  "32 64" 4433221144332211 '.dcb.l 2,0x11223344'
d  "32 64" 0000 '.dcb.b 2'
d  "32 64" 000000000000 '.dcb.w 3'
d  "32 64" 0000c03f0000c03f '.dcb.s 2,1.5'
d  "32 64" 000000000000f83f '.dcb.d 1,1.5'
d  "32 64" 00000000000000c0ff3f00000000000000c0ff3f '.dcb.x 2,1.5'

# .octa: sixteen bytes, of a number that may need all of them.
d  "32 64" 01000000000000000000000000000000 '.octa 1'
d  "32 64" ffffffffffffffffffffffffffffffff '.octa -1'
d  "32 64" 03000000000000000000000000000000 '.octa 1+2'
d  "32 64" 00ffeeddccbbaa998877665544332211 '.octa 0x112233445566778899aabbccddeeff00'
d  "32 64" ffffffffffffffffffffffffffffffff '.octa 340282366920938463463374607431768211455'
d  "32 64" 0000000000000000ffffffffffffffff '.octa -0x10000000000000000'
d  "32 64" 0000000000000000010000000000000005000000000000000000000000000000 '.octa 18446744073709551616, 5'
d  "32 64" 050000000000000000000000000000000f000000000000000000000000000000 '.octa 0b101, 017'
# More than 128 bits are cut off, as GNU as cuts them.
d  "32 64" ffffffffffffffffffffffffffffffff '.octa 0x1ffffffffffffffffffffffffffffffffff'

# LEB128 of numbers.
d  "32 64" 007f8001ff7f808001 '.uleb128 0, 127, 128, 16383, 16384'
d  "32 64" 80808080808080808001 '.uleb128 0x8000000000000000'
d  "32 64" ffffffffffffffffff01 '.uleb128 0xffffffffffffffff'
d  "32 64" ffffffffffffffffff01 '.uleb128 -1'
d  "32 64" a382bcefbcb5e2b3c5c604 '.uleb128 0x123456789abcdef0123'
d  "32 64" 003fc0007f40bf7fff7eff008001 '.sleb128 0, 63, 64, -1, -64, -65, -129, 127, 128'
d  "32 64" ffffffffffffffffff00 '.sleb128 0x7fffffffffffffff'
d  "32 64" 8080808080808080807f '.sleb128 -0x8000000000000000'
d  "32 64" 80808080808080808001 '.sleb128 0x8000000000000000'

# LEB128 of the distance between two labels.
d  "32 64" 010203037d 'a: .byte 1,2,3|b: .uleb128 b-a|.sleb128 a-b'
d  "32 64" 0104ac0200000000 '.uleb128 1, b-a, 300|a: .long 0|b:'
d  "32 64" 03 '.uleb128 b-a|.text|a: nop|nop|nop|b: ret'
d  "32 64" 0116150000000000000000000000000000000000000000 '.byte 1|.uleb128 c-., c-.|.fill 20|c:'
# The directive lies between its labels: 127 bytes with one of its own,
# and with one more the distance is 129 and takes two.
p  "32 64" 7f0000000000000000000000 128 'a: .uleb128 b-a|.fill 126|b: .byte 9'
p  "32 64" 810100000000000000000000 130 'a: .uleb128 b-a|.fill 127|b: .byte 9'
p  "32 64" 430000000000000000000000 62  'a: .sleb128 a-b|.fill 60|b: .byte 1'
p  "32 64" be7f00000000000000000000 67  'a: .sleb128 a-b|.fill 64|b: .byte 1'
# A length that spans other lengths, each of which is two bytes.
p  "32 64" aa037c7e0000000000000000 427 \
   's: .uleb128 e-s|.uleb128 x1-s, x2-s|.fill 120|x1: .uleb128 e-x1|x2: .fill 300|e: .byte 0xee'
d  "32 64" refused '.uleb128 undefined_thing'

# A compiler's exception table: the length of the call-site table, and
# in it distances in the code, one of them over 127.
cases=$((cases + 1))
cat > lsda.s <<'EOF'
	.text
f:
.LFB0:
	nop
.LEHB0:
	call g
.LEHE0:
	.fill 300, 1, 0x90
.LEHB1:
	call g
.LEHE1:
	ret
.L5:
	nop
	.section .gcc_except_table,"a",@progbits
	.byte 0xff
	.byte 0xff
	.byte 0x1
	.uleb128 .LLSDACSE0-.LLSDACSB0
.LLSDACSB0:
	.uleb128 .LEHB0-.LFB0
	.uleb128 .LEHE0-.LEHB0
	.uleb128 .L5-.LFB0
	.uleb128 0
	.uleb128 .LEHB1-.LFB0
	.uleb128 .LEHE1-.LEHB1
	.uleb128 0
	.uleb128 0
.LLSDACSE0:
EOF
if "$AS" -32 -o lsda.o lsda.s 2> err; then
    objcopy -O binary --only-section=.gcc_except_table lsda.o lsda.bin
    got=$(od -An -v -tx1 lsda.bin | tr -d ' \n')
    [ "$got" = ffff010a0105b80200b202050000 ] || { echo "FAIL exception table: $got"; fail=1; }
else
    echo "FAIL exception table: $(head -1 err)"; fail=1
fi

# .single is .float.  .tfloat is the x87 format of ten bytes, rounded to
# nearest from the decimal.
d  "32 64" 0000c03f000000c0 '.single 1.5, -2'
d  "32 64" 00000000000000c0ff3f '.tfloat 1.5'
d  "32 64" 00000000000000000000 '.tfloat 0'
d  "32 64" cdccccccccccccccfb3f00000000a0d424f221c0 '.tfloat 0.1, -3.25e10'
d  "32 64" cdf67e5ca9b22cd7fe7f '.tfloat 1e4932'
d  "32 64" 0000000000000080ff3f0000000000000080ffbf35c26821a2da0fc90040 '.tfloat 1, -1, 3.14159265358979323846'
d  "32 64" 628e27630600000000008cb928be522e0cff4d40 '.tfloat 1e-4940, 6.02214076e23'
d  "32 64" d9ce4f19fbb4e4fdcc3b77f0b961fb8774c75f40 '.tfloat 4.9e-324, 123456789012345678901234567890'

# Strings of wider characters, each ended by a zero of the same width.
d  "32 64" 616200 '.string8 "ab"'
d  "32 64" 610062000000 '.string16 "ab"'
d  "32 64" 61000a00000062000000 '.string16 "a\n", "b"'
d  "32 64" 6100000000000000 '.string32 "a"'
d  "32 64" 61000000000000000000000000000000 '.string64 "a"'

# Alignments that pad with a pattern of two bytes or of four.  The
# padding must be a whole number of patterns; without a pattern it is
# zeros; the third argument is the most to pad, as for the others.
d  "32 64" 010134123412341202 '.byte 1,1|.balignw 8,0x1234|.byte 2'
d  "32 64" 010134123412341202 '.byte 1,1|.p2alignw 3,0x1234|.byte 2'
d  "32 64" 010101014433221102 '.byte 1,1,1,1|.balignl 8,0x11223344|.byte 2'
d  "32 64" 010101014433221102 '.byte 1,1,1,1|.p2alignl 3,0x11223344|.byte 2'
d  "32 64" 010156345634563402 '.byte 1,1|.balignw 8,0x123456|.byte 2'
d  "32 64" 010100000000000002 '.byte 1,1|.balignw 8|.byte 2'
d  "32 64" 010100000000000002 '.byte 1,1|.balignl 8|.byte 2'
d  "32 64" 010100000000000002 '.byte 1,1|.p2alignw 3,,6|.byte 2'
d  "32 64" 010102 '.byte 1,1|.balignw 8,0x1234,4|.byte 2'
d  "32 64" 010134123412341202 '.byte 1,1|.balignw 8,0x1234,6|.byte 2'
d  "32 64" 0101010102 '.byte 1,1,1,1|.balignl 16,0x11223344,8|.byte 2'
d  "32 64" 0101010144332211443322114433221102 '.byte 1,1,1,1|.balignl 16,0x11223344,12|.byte 2'
d  "32 64" refused '.byte 1|.balignw 8,0x1234|.byte 2'
d  "32 64" refused '.byte 1,1|.balignl 8,0x11223344|.byte 2'
d  "32 64" refused '.byte 1|.p2alignl 3,0x11223344|.byte 2'
# And the plain ones are as they were.
d  "32 64" 01ababab02 '.byte 1|.balign 4,0xab|.byte 2'
d  "32 64" 013434343434343402 '.byte 1|.balign 8,0x1234|.byte 2'

# .struct: what follows is laid out from an offset and assembled into
# nothing; its labels are the offsets.  It was passed over, and the
# fields went into the section as data.
d  "32 64" 00000000040000000c000000 '.struct 0|a: .struct a+4|b: .struct b+8|c:|.data|.long a, b, c'
d  "32 64" 0000000004000000050000000d000000 '.struct 0|f1: .long 0|f2: .byte 0|f3: .quad 0|sz:|.data|.long f1, f2, f3, sz'
d  "32 64" 101416 '.struct 16|x: .skip 4|y: .space 2|z:|.text|movl $z, %eax|.data|.byte x, y, z'
d  "32 64" 00080c '.struct 0|a: .long 0|.balign 8|b: .byte 0|.p2align 2|c:|.data|.byte a,b,c'
d  "32 64" 00061416 '.struct 0|a: .fill 3, 2, 0|b: .org 20|c: .dc.w 0|d:|.data|.byte a,b,c,d'
d  "32 64" 0004 '.struct|a: .long 0|b:|.data|.byte a,b'
d  "32 64" 0c10 '.set N, 12|.struct N|a: .long 0|b:|.data|.byte a, b'
d  "32 64" 08000000 '.globl a|.struct 8|a: .long 0|.data|.long a'
# It ends at the next directive that names a section, and .previous and
# .popsection come back as from any other.
d  "32 64" 090408 '.data|.byte 9|.struct 4|a: .long 0|b:|.previous|.byte a, b'
d  "32 64" 00046465 '.struct 0|a: .long 0|b:|.section .data|.byte a,b|.struct 100|c: .byte 0|d:|.data|.byte c, d'
d  "32 64" 07000408 '.struct 0|a: .long 0|.pushsection .data|.byte 7|.popsection|b: .long 0|c:|.data|.byte a,b,c'
# Room is reserved and nothing is stored.
d  "32 64" refused '.struct 0|a: .long 5|b:|.data|.byte b'
d  "32 64" refused '.struct 0|p: .ascii "abc"|q:|.data|.byte q'

# The labels of a .struct are absolute symbols, and global if said to be.
cases=$((cases + 1))
printf '.globl a\n.struct 8\na: .long 0\nb:\n.data\n.long a\n' > abs.s
if "$AS" -32 -o abs.o abs.s 2> err; then
    got=$(nm abs.o | tr '\n' ' ')
    [ "$got" = "00000008 A a 0000000c a b " ] || { echo "FAIL .struct symbols: $got"; fail=1; }
else
    echo "FAIL .struct symbols: $(head -1 err)"; fail=1
fi

[ "$fail" -eq 0 ] && echo "ok: data directives ($cases cases)"
exit "$fail"
