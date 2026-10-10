#!/bin/sh
# The long NOP is 0F 1F /0 (AS-T-029).
#
# In 32-bit mode nopw and nopl were assembled with 0F 1D, which is not a
# NOP: a prefetch hint on AMD processors and reserved on Intel's.  These
# are the instructions compilers and assemblers pad code with.  In 64-bit
# mode `nop` with an operand was 0F 18 /0, which is prefetchnta.
#
# The bytes are GNU as's.  The forms with a displacement of zero written
# out are left to AS-T-186: GNU drops the displacement and this assembler
# keeps a byte for it, which is a NOP all the same.  Run by run-suite.sh,
# which sets $AS.
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

enc 32 'nop'                         90
enc 32 'nopw (%eax)'                 660f1f00
enc 32 'nopl (%eax)'                 0f1f00
enc 32 'nopl 4(%eax)'                0f1f4004
enc 32 'nopl 0x100(%eax,%eax,2)'     0f1f844000010000
enc 32 'nopw 0x100(%eax,%eax,2)'     660f1f844000010000
enc 32 'nopw %cs:4(%eax,%eax,1)'     2e660f1f440004
enc 32 'nopw %ax'                    660f1fc0

enc 64 'nop'                         90
enc 64 'nopw (%rax)'                 660f1f00
enc 64 'nopl (%rax)'                 0f1f00
enc 64 'nopl 4(%rax)'                0f1f4004
enc 64 'nopl 0x100(%rax,%rax,2)'     0f1f844000010000
enc 64 'nopw 0x100(%rax,%rax,2)'     660f1f844000010000
enc 64 'nopw %cs:4(%rax,%rax,1)'     2e660f1f440004
enc 64 'nop (%rax)'                  0f1f00

# However the displacement is encoded, the padding forms are long NOPs.
for m in 32 64; do
    r=eax; [ "$m" = 64 ] && r=rax
    for insn in "nopl 0x0(%$r,%$r,1)" "nopw 0x0(%$r,%$r,1)" "nopl 0(%$r)"; do
        printf '\t.text\n\t%s\n' "$insn" > t.s
        "$AS" --$m -o t.o t.s 2> err || { echo "FAIL --$m $insn: $(head -1 err)"; fail=1; continue; }
        objcopy -O binary -j .text t.o t.bin
        case "$(od -An -v -tx1 t.bin | tr -d ' \n')" in
        0f1f*|660f1f*) ;;
        *) echo "FAIL --$m $insn: $(od -An -v -tx1 t.bin | tr -d ' \n')"; fail=1 ;;
        esac
    done
done

[ "$fail" -eq 0 ] && echo "ok: long nop"
exit "$fail"
