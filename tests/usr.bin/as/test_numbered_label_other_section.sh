#!/bin/sh
# A jump to a numbered label in another section.
#
# Linux's static_cpu_has() is `jmp 6f` in .text, with `6:` in
# .altinstr_aux (by .pushsection), and from there `jnz` back to labels
# in .text.  The jump to 6f was written in its long form with a
# relocation, rightly, but counted as two bytes when the labels were
# placed: every label after it in .text was three bytes short, and the
# jumps back went into the middle of an instruction -- the kernel
# faulted in early boot.  A named label (.Lx) was right.
#
# What is compared is the addend of the jump back to .Lyes, a label at
# .text+12 (8 in the REL field), which is GNU as's; GNU as passes this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
for target in '6f|6:' '.Lx|.Lx:'; do
    ref=${target%%|*}
    def=${target#*|}
    printf '\t.text\nf:\n\tmovl $1, %%eax\n\tjmp %s\n\tnop\n\tnop\n.Lyes:\n\tret\n\t.pushsection .aux,"ax"\n%s\n\ttestb $32, 0\n\tjnz .Lyes\n\t.popsection\n\tret\n' "$ref" "$def" > t.s
    if ! "$AS" --32 -o t.o t.s 2> err; then
        echo "FAIL [$ref]: $(tail -1 err)"; fail=1; continue
    fi
    # the jnz is at .aux+7; its 32-bit field at +9
    field=$(objcopy -O binary -j .aux t.o /dev/stdout | od -An -v -tx1 -j 9 -N 4 | tr -d ' \n')
    [ "$field" = "08000000" ] ||
        { echo "FAIL [$ref]: the jump back to .Lyes holds $field, not 08000000 (.Lyes at .text+12)"; fail=1; }
done

# Linux 7.3's alternative padding, `.skip -(((775f-774f)-(772b-771b)) > 0)
# * ...`, two bytes here, the replacement in .altinstr_replacement being
# that much longer than the original.  Placing the labels after it, seen
# from another section, counted it as nothing: the expression came
# re-spaced from the parser, and only the kernel's old numbering
# (740..744) was looked for.
printf '\t.text\n771:\n\tjmp 6f\n772:\n\t.skip -(((775f-774f)-(772b-771b)) > 0) * ((775f-774f)-(772b-771b)),0x90\n773:\n\t.pushsection .altinstr_replacement,"ax"\n774:\n\tjmp ext\n\tnop\n\tnop\n775:\n\t.popsection\n.Lyes:\n\tret\n\t.pushsection .aux,"ax"\n6:\n\ttestb $32, 0\n\tjnz .Lyes\n\t.popsection\n' > t.s
if ! "$AS" --32 -o t.o t.s 2> err; then
    echo "FAIL [alt pad]: $(tail -1 err)"; fail=1
else
    field=$(objcopy -O binary -j .aux t.o /dev/stdout | od -An -v -tx1 -j 9 -N 4 | tr -d ' \n')
    [ "$field" = "03000000" ] ||
        { echo "FAIL [alt pad]: the jump back to .Lyes holds $field, not 03000000 (.Lyes at .text+7)"; fail=1; }
fi

[ "$fail" -eq 0 ] && echo "ok: jumps to a numbered label in another section"
exit "$fail"
