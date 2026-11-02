#!/bin/sh
# An indirect branch may name its segment before the star.
#
# GNU as takes `ljmp %cs:*(%eax)` for `ljmp *%cs:(%eax)`, and Linux's
# arch/x86/hyperv/hv_trampoline.S writes it so; the assembler refused it
# ("invalid operand '%cs:* ( %eax )'").
#
# GNU as is the judge: far and near jumps and calls, through memory with
# a displacement, a base, an index and a symbol, under each segment, are
# GNU as's bytes and relocations, --32 and --64.
# Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
GAS=${AS_GNU:-as}
if ! "$GAS" --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as is the judge"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

text_of() { objcopy -O binary -j .text "$1" /dev/stdout | od -An -v -tx1 | tr -d ' \n'; }
relocs_of() { readelf -rW "$1" | awk '/R_/ { print $1, $3, $5 }'; }

# like MODE LINE: ours assembles LINE as GNU as does
like() {
    printf '\t.globl sym\n\t.text\n\t%s\n' "$2" > t.s
    if ! "$GAS" "--$1" -o g.o t.s 2> err; then
        echo "FAIL --$1 '$2': GNU as refused it: $(head -1 err)"; fail=1; return
    fi
    if ! "$AS" "--$1" -o o.o t.s 2> err; then
        echo "FAIL --$1 '$2': $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    if [ "$(text_of o.o)" != "$(text_of g.o)" ] || [ "$(relocs_of o.o)" != "$(relocs_of g.o)" ]; then
        echo "FAIL --$1 '$2': $(text_of o.o), GNU as $(text_of g.o)"; fail=1
    fi
}

for seg in cs ds es fs gs ss; do
    like 32 "ljmp %$seg:*(%eax)"
    like 32 "lcall %$seg:*4(%ebx)"
    like 32 "jmp %$seg:*(%eax)"
    like 32 "call %$seg:*8(%ebx,%esi,4)"
    like 32 "jmp %$seg:*sym"
    like 64 "ljmp %$seg:*(%rax)"
    like 64 "call %$seg:*16(%rbx)"
    like 64 "jmp %$seg:*sym(%rip)"
done

if [ $fail = 0 ]; then
    echo "test_segment_before_star: ok"
fi
exit $fail
