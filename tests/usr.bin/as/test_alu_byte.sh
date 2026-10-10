#!/bin/sh
# The byte forms of the eight arithmetic instructions (AS-T-038).
#
# Each encoder chose a byte opcode by a ladder of names that had add, or,
# and, sub and xor in it and ended in cmp's: adcb and sbbb were assembled
# as cmpb.  Arithmetic on more than a word, written with byte carries,
# compared where it should have added, and said nothing.
#
# The bytes are GNU as's.  Register-to-register forms are left out: the
# 64-bit encoder writes them with the other of the two opcodes that mean
# the same.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# enc MODE INSTRUCTION BYTES
enc() {
    printf '\t.text\n\t%s\n' "$2" > t.s
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 $2: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$3" ] || { echo "FAIL --$1 $2: $got, not $3"; fail=1; }
}

# Opcode of `OPb %cl, (mem)`, of `OPb (mem), %dl`, and the /digit of
# `OPb $imm, (mem)`: the eight, in the order the processor numbers them.
n=0
for op in add or adc sbb and sub xor cmp; do
    store=$(printf '%02x' $((n * 8)))
    load=$(printf '%02x' $((n * 8 + 2)))
    modrm=$(printf '%02x' $((n * 8)))
    enc 32 "${op}b %cl, (%eax)"        "${store}08"
    enc 32 "${op}b (%eax), %dl"        "${load}10"
    enc 32 "${op}b \$1, (%eax)"        "80${modrm}01"
    # (cmp writes nothing and takes no lock.)
    [ "$op" = cmp ] || enc 32 "lock ${op}b %cl, (%ebx)" "f0${store}0b"
    enc 64 "${op}b %cl, (%rax)"        "${store}08"
    enc 64 "${op}b (%rax), %dl"        "${load}10"
    enc 64 "${op}b \$1, (%rax)"        "80${modrm}01"
    n=$((n + 1))
done

# And between registers, by what the processor makes of the bytes.
if command -v objdump > /dev/null; then
    for m in 32 64; do
        for op in adc sbb; do
            printf '\t%sb %%al, %%bl\n' "$op" > t.s
            "$AS" --$m -o t.o t.s 2> /dev/null
            objdump -d t.o | grep -q "$op *%al,%bl" ||
                { echo "FAIL --$m ${op}b %al, %bl: $(objdump -d t.o | tail -1)"; fail=1; }
        done
    done
fi

[ "$fail" -eq 0 ] && echo "ok: byte arithmetic"
exit "$fail"
