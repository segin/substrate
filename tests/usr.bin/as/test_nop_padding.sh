#!/bin/sh
# What an alignment pads with between instructions (AS-T-251).
#
# GNU as pads code with as few instructions as there are: one NOP of the
# odd length, then the longest NOP of the mode over and over -- 8 bytes
# with a 32-bit object's NOPs, 11 with a 64-bit object's, 5 in 16-bit
# code.  Past two of them (seven with the 64-bit ones) it jumps over the
# padding first.  Behind data, where there is no instruction to follow
# on from, a NOP of one byte comes before the rest.  A fill of 0x90 asks
# for the same; any other fill is that byte, and a section that holds no
# code is padded with zeros.
#
# This assembler wrote 0x90 for each byte.  The bytes here are GNU as
# 2.46's, and GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# The bytes of the sections the cases write, a run of 0xf4 as `~`.
bytes() {
    for s in .text .foo .text.hot .data; do
        objcopy -O binary -j $s t.o /dev/stdout 2> /dev/null | od -An -v -tx1 | tr -d ' \n' |
            sed -E 's/(f4){8,}/~/g'
    done
}

# is MODE SOURCE BYTES: `|` in SOURCE is a newline
is() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1; return
    fi
    got=$(bytes)
    [ "$got" = "$3" ] || { echo "FAIL --$1 [$2]: $got"; echo "  wanted: $3"; fail=1; }
}

# sum MODE SOURCE MD5: the same, of bytes too many to write out
sum() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1; return
    fi
    got=$(echo "$(bytes)" | md5sum | cut -c1-32)
    [ "$got" = "$3" ] || { echo "FAIL --$1 [$2]: $(bytes | cut -c1-120)"; fail=1; }
}

# Each length from 1 to 15.
is 32 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c3c3c3c3c390c3
is 32 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c3c3c3c36690c3
is 32 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c3c3c38d7600c3
is 32 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c3c38d742600c3
is 32 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c32e8d742600c3
is 32 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c38db600000000c3
is 32 'ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c38db42600000000c3
is 32 'ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c32e8db42600000000c3
is 32 'ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3902e8db42600000000c3
is 32 'ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c366902e8db42600000000c3
is 32 'ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c38d76002e8db42600000000c3
is 32 'ret|ret|ret|ret|.p2align 4|ret' c3c3c3c38d7426002e8db42600000000c3
is 32 'ret|ret|ret|.p2align 4|ret' c3c3c32e8d7426002e8db42600000000c3
is 32 'ret|ret|.p2align 4|ret' c3c38db6000000002e8db42600000000c3
is 32 'ret|.p2align 4|ret' c38db426000000002e8db42600000000c3
is 64 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c3c3c3c3c390c3
is 64 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c3c3c3c36690c3
is 64 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c3c3c30f1f00c3
is 64 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c3c30f1f4000c3
is 64 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3c30f1f440000c3
is 64 'ret|ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c3c3660f1f440000c3
is 64 'ret|ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c3c30f1f8000000000c3
is 64 'ret|ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3c30f1f840000000000c3
is 64 'ret|ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3c3660f1f840000000000c3
is 64 'ret|ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c3c3662e0f1f840000000000c3
is 64 'ret|ret|ret|ret|ret|.p2align 4|ret' c3c3c3c3c366662e0f1f840000000000c3
is 64 'ret|ret|ret|ret|.p2align 4|ret' c3c3c3c39066662e0f1f840000000000c3
is 64 'ret|ret|ret|.p2align 4|ret' c3c3c3669066662e0f1f840000000000c3
is 64 'ret|ret|.p2align 4|ret' c3c30f1f0066662e0f1f840000000000c3
is 64 'ret|.p2align 4|ret' c30f1f400066662e0f1f840000000000c3

# More than one NOP; and past the most there are, a jump over them:
# of two bytes while that reaches, and of five beyond.
is 32 'ret|.skip 239,0xf4|.balign 256|ret' c3~908db426000000002e8db42600000000c3
is 32 'ret|.skip 232,0xf4|.balign 256|ret' c3~908db6000000002e8db426000000002e8db42600000000c3
is 32 'ret|.skip 231,0xf4|.balign 256|ret' c3~908db426000000002e8db426000000002e8db42600000000c3
is 32 'ret|.skip 224,0xf4|.balign 256|ret' c3~90eb1c8d7426002e8db426000000002e8db426000000002e8db42600000000c3
is 64 'ret|.skip 239,0xf4|.balign 256|ret' c3~900f1f400066662e0f1f840000000000c3
is 64 'ret|.skip 232,0xf4|.balign 256|ret' c3~9066662e0f1f84000000000066662e0f1f840000000000c3
is 64 'ret|.skip 231,0xf4|.balign 256|ret' c3~909066662e0f1f84000000000066662e0f1f840000000000c3
is 64 'ret|.skip 224,0xf4|.balign 256|ret' c3~900f1f84000000000066662e0f1f84000000000066662e0f1f840000000000c3
is 64 'ret|.skip 223,0xf4|.balign 256|ret' c3~90660f1f84000000000066662e0f1f84000000000066662e0f1f840000000000c3
sum 32 'ret|.skip 155,0xf4|.balign 256|ret' a9576da309037f538b868cd2eb56cd93
sum 32 'ret|.skip 128,0xf4|.balign 256|ret' c3ce0fcfa79b124fa1921388c0c738d7
sum 32 'ret|.skip 127,0xf4|.balign 256|ret' 893a788b9496017a46153d7b2cb04102
sum 32 'ret|.skip 126,0xf4|.balign 256|ret' 5bc941f4702c17aff08de9f81d975483
sum 32 'ret|.skip 125,0xf4|.balign 256|ret' bcd971371835fa438e0dfb0cbbdd0290
sum 32 'ret|.skip 124,0xf4|.balign 256|ret' da979d376520a293a2b2f864e51fd042
sum 32 'ret|.skip 55,0xf4|.balign 256|ret' 7b2c104a4add7cdc8fdf0e959f3d9e01
sum 64 'ret|.skip 155,0xf4|.balign 256|ret' ea0bc51c9762669e72838e19281d45df
sum 64 'ret|.skip 128,0xf4|.balign 256|ret' ee75a2caaf930f991157017cad6519ea
sum 64 'ret|.skip 127,0xf4|.balign 256|ret' ae82aaff633dd31c5187671f00ce8387
sum 64 'ret|.skip 126,0xf4|.balign 256|ret' 0243642670c9cb950e4ebe6d25a520c1
sum 64 'ret|.skip 125,0xf4|.balign 256|ret' ce1aa9af0b8c25ad4f0ac239a188d10a
sum 64 'ret|.skip 124,0xf4|.balign 256|ret' 480725b1f6be5f1c4148c011447d21e6
sum 64 'ret|.skip 55,0xf4|.balign 256|ret' d114d29625f463b8f76de8bb51ddeb70

# NOPs are what 0x90 asks for too; another byte is that byte.
is 32 'ret|.p2align 3,0x90|ret' c38db42600000000c3
is 32 'ret|.balign 16,0x90|ret' c38db426000000002e8db42600000000c3
is 32 'ret|.p2align 3,0xcc|ret' c3ccccccccccccccc3
is 32 'ret|.p2align 3,0|ret' c300000000000000c3
is 32 'ret|.p2align 4,,15|ret' c38db426000000002e8db42600000000c3
is 32 'ret|.p2align 4,,14|ret' c3c3
is 32 'ret|.p2align 4,0x90,15|ret' c38db426000000002e8db42600000000c3
is 64 'ret|.p2align 3,0x90|ret' c30f1f8000000000c3
is 64 'ret|.balign 16,0x90|ret' c30f1f400066662e0f1f840000000000c3
is 64 'ret|.p2align 3,0xcc|ret' c3ccccccccccccccc3
is 64 'ret|.p2align 3,0|ret' c300000000000000c3
is 64 'ret|.p2align 4,,15|ret' c30f1f400066662e0f1f840000000000c3
is 64 'ret|.p2align 4,,14|ret' c3c3
is 64 'ret|.p2align 4,0x90,15|ret' c30f1f400066662e0f1f840000000000c3

# Where the section holds code, whatever it is called; and nowhere else.
is 32 '.section .foo,"ax"|ret|.p2align 3|ret' c38db42600000000c3
is 32 '.section .text.hot,"ax",@progbits|ret|.p2align 3|ret' c38db42600000000c3
is 32 '.section .foo,"a"|.byte 1|.p2align 3|.byte 2' 010000000000000002
is 32 '.data|.byte 1|.p2align 3|.byte 2' 010000000000000002
is 64 '.section .foo,"ax"|ret|.p2align 3|ret' c30f1f8000000000c3
is 64 '.section .text.hot,"ax",@progbits|ret|.p2align 3|ret' c30f1f8000000000c3
is 64 '.section .foo,"a"|.byte 1|.p2align 3|.byte 2' 010000000000000002
is 64 '.data|.byte 1|.p2align 3|.byte 2' 010000000000000002

# The NOPs are the object's, not the mode's; 16-bit code has its own.
is 32 '.code32|ret|.p2align 3|ret' c38db42600000000c3
is 64 '.code32|ret|.p2align 3|ret' c30f1f8000000000c3
is 32 '.code16|ret|.p2align 4|ret' c3eb0d8d74002e8db400002e8db40000c3
is 64 '.code16|ret|.p2align 4|ret' c3eb0d8d74002e8db400002e8db40000c3
is 32 '.code16|ret|ret|ret|.p2align 5|ret' c3c3c3eb1b89f62e8db400002e8db400002e8db400002e8db400002e8db40000c3
is 64 '.code16|ret|ret|ret|.p2align 5|ret' c3c3c3eb1b89f62e8db400002e8db400002e8db400002e8db400002e8db40000c3
sum 32 '.code16|ret|.skip 55,0xf4|.balign 256|ret' 6c80594799bf98013830b439ad5c59a4
sum 64 '.code16|ret|.skip 55,0xf4|.balign 256|ret' 6c80594799bf98013830b439ad5c59a4

# Behind data, one NOP of a byte comes first; a label or an alignment
# between changes nothing, an instruction does.
is 32 'ret|.byte 1|.p2align 4|ret' c301902e8d7426002e8db42600000000c3
is 32 'ret|.long 1|.p2align 4|ret' c3010000009066902e8db42600000000c3
is 32 'ret|.word 1|.p2align 4|ret' c30100908d7426002e8db42600000000c3
is 32 'ret|.string "a"|.p2align 4|ret' c36100908d7426002e8db42600000000c3
is 32 'ret|l: |.p2align 4|ret' c38db426000000002e8db42600000000c3
is 32 '.byte 1|ret|.p2align 4|ret' 01c38db6000000002e8db42600000000c3
is 32 'ret|.byte 1|l:|.p2align 4|ret' c301902e8d7426002e8db42600000000c3
is 32 'ret|.zero 1|.p2align 4|ret' c300902e8d7426002e8db42600000000c3
is 32 'ret|.org 2|.p2align 4|ret' c3008db6000000002e8db42600000000c3
is 32 'ret|.byte 1|.p2align 1|.p2align 4|ret' c301902e8d7426002e8db42600000000c3
is 32 'ret|.byte 1|.section .x|.previous|.p2align 4|ret' c301902e8d7426002e8db42600000000c3
is 32 'ret|.byte 1|.p2align 2|.p2align 3|.p2align 4|ret' c3019090908d7600908db42600000000c3
is 32 '.byte 1|.p2align 4|ret' 01908db6000000002e8db42600000000c3
is 64 'ret|.byte 1|.p2align 4|ret' c30190669066662e0f1f840000000000c3
is 64 'ret|.long 1|.p2align 4|ret' c30100000090662e0f1f840000000000c3
is 64 'ret|.word 1|.p2align 4|ret' c30100909066662e0f1f840000000000c3
is 64 'ret|.string "a"|.p2align 4|ret' c36100909066662e0f1f840000000000c3
is 64 'ret|l: |.p2align 4|ret' c30f1f400066662e0f1f840000000000c3
is 64 '.byte 1|ret|.p2align 4|ret' 01c30f1f0066662e0f1f840000000000c3
is 64 'ret|.byte 1|l:|.p2align 4|ret' c30190669066662e0f1f840000000000c3
is 64 'ret|.zero 1|.p2align 4|ret' c30090669066662e0f1f840000000000c3
is 64 'ret|.org 2|.p2align 4|ret' c3000f1f0066662e0f1f840000000000c3
is 64 'ret|.byte 1|.p2align 1|.p2align 4|ret' c30190669066662e0f1f840000000000c3
is 64 'ret|.byte 1|.section .x|.previous|.p2align 4|ret' c30190669066662e0f1f840000000000c3
is 64 'ret|.byte 1|.p2align 2|.p2align 3|.p2align 4|ret' c3019090900f1f00900f1f8000000000c3
is 64 '.byte 1|.p2align 4|ret' 01900f1f0066662e0f1f840000000000c3

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
