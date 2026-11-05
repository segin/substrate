#!/bin/sh
# A RIP-relative operand naming a numeric label of another section is
# that label's address, which the linker puts in.
#
# `lea 2f(%rip), %r10` is a distance where 2: is in the instruction's
# own section, and the assembler works it out; where 2: is in another
# section it had no relocation at all, and the operand was the next
# instruction.  Linux's x86-64 WARN() takes the address of its
# __bug_table entry so, `lea (2f)(%rip)` in .text with 2: after a
# .pushsection __bug_table: the bug pointer pointed into the code, and
# the first WARN at boot faulted reading its format string from what it
# took for the entry.
#
# GNU as is the judge: the relocations and the bytes, forward and
# backward, with something added, and the same section's labels (which
# stay distances, with no relocation).
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

bytes_of() { objcopy -O binary -j "$2" "$1" /dev/stdout | od -An -v -tx1 | tr -d ' \n'; }
# the relocations of a section: offset, type and target, in order (a
# section symbol named by its section, as readelf prints it)
relocs_of() { readelf -rW "$1" | awk -v s=".rela$2" '$0 ~ "section .\\" s "." {x=1; next} /^Relocation/ {x=0} x && /R_X86/ {print $1, $3, $5, $6, $7}'; }

for form in "2f(%rip)" "(2f)(%rip)" "(2f+4)(%rip)" "1b(%rip)" "(1b-2)(%rip)" "3f(%rip)"; do
    cat > t.s <<EOF
	.pushsection __bug_table,"aw"
1:	.long 0
	.popsection
	.text
f:	lea $form, %r10
	movq $form, %rax
	nop
3:	ret
	.pushsection __bug_table,"aw"
2:	.long 1b - .
	.long 3b - .
	.popsection
EOF
    if ! "$GAS" --64 -o g.o t.s 2> err; then
        echo "FAIL $form: GNU as refused it: $(head -1 err)"; fail=1; continue
    fi
    if ! "$AS" --64 -o o.o t.s 2> err; then
        echo "FAIL $form: $(head -1 err | sed 's/.*: //')"; fail=1; continue
    fi
    for s in .text __bug_table; do
        if [ "$(bytes_of o.o $s)" != "$(bytes_of g.o $s)" ]; then
            echo "FAIL $form, $s: $(bytes_of o.o $s), GNU as $(bytes_of g.o $s)"; fail=1
        fi
        if [ "$(relocs_of o.o $s)" != "$(relocs_of g.o $s)" ]; then
            echo "FAIL $form, $s relocations: [$(relocs_of o.o $s | tr '\n' ';')], GNU as [$(relocs_of g.o $s | tr '\n' ';')]"
            fail=1
        fi
    done
done

if [ $fail = 0 ]; then
    echo "test_rip_numeric_label_other_section: ok"
fi
exit $fail
