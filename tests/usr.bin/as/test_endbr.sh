#!/bin/sh
# endbr32 and endbr64 (AS-T-085).
#
# gcc writes one at the head of every function where it is built with
# -fcf-protection on by default, as Ubuntu's and Fedora's are; an
# assembler without them assembles nothing such a compiler makes.  Either
# is valid in either mode.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

for mode in --32 --64; do
    for insn in endbr32:f30f1efb endbr64:f30f1efa; do
        printf '\t.text\n\t%s\n' "${insn%%:*}" > t.s
        if ! "$AS" "$mode" -o t.o t.s 2> err; then
            echo "FAIL $mode ${insn%%:*}: $(head -1 err | sed 's/.*: //')"; fail=1; continue
        fi
        objcopy -O binary -j .text t.o t.bin
        got=$(od -An -v -tx1 t.bin | tr -d ' \n')
        [ "$got" = "${insn##*:}" ] || { echo "FAIL $mode ${insn%%:*}: $got"; fail=1; }
    done
done

# notrack, the other thing such a compiler writes: the byte 3E before an
# indirect jmp or call, which says the target need not be an endbr.  It
# goes before the jump through every jump table.
for pair in '32:notrack jmp *%eax:3effe0' '32:notrack call *%eax:3effd0' \
            '32:notrack jmp *tab(,%eax,4):3eff248500000000' \
            '64:notrack jmp *%rax:3effe0' '64:notrack call *%rax:3effd0' \
            '64:notrack jmp *(%rax):3eff20' '64:jmp *%rax:ffe0'; do
    mode=${pair%%:*}; rest=${pair#*:}; insn=${rest%%:*}; want=${rest##*:}
    printf '\t.text\n\t%s\n' "$insn" > t.s
    if ! "$AS" "--$mode" -o t.o t.s 2> err; then
        echo "FAIL --$mode $insn: $(head -1 err | sed 's/.*: //')"; fail=1; continue
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$want" ] || { echo "FAIL --$mode $insn: $got, not $want"; fail=1; }
done

# With an operand it is not an instruction.
printf '\tendbr64 %%eax\n' > t.s
if "$AS" --64 -o t.o t.s 2> /dev/null; then echo "FAIL: endbr64 %eax assembled"; fail=1; fi

# What the compiler writes with the protection asked for.
if command -v gcc > /dev/null; then
    printf 'int f(int x) { return x + 1; }\n' > c.c
    for m in 32 64; do
        if gcc -m$m -O2 -fcf-protection=full -S -o c.s c.c 2> /dev/null; then
            grep -q endbr c.s || continue
            "$AS" --$m -o c.o c.s 2> err ||
                { echo "FAIL gcc -m$m -fcf-protection: $(head -1 err | sed 's/.*: //')"; fail=1; }
        fi
    done
fi

[ "$fail" -eq 0 ] && echo "ok: endbr"
exit "$fail"
