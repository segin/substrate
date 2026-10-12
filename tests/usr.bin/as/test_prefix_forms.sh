#!/bin/sh
# The size prefixes by their names, and what they are written before.
#
# `data16` before a jump or a call to a label the assembler knew was
# left out and nothing said; before one to a symbol of another object
# the distance had four bytes behind a prefix that says two, and so had
# `callw sym` in 64-bit code.  `addr16 loop` was `loop`, which counts in
# another register.  `addr16 movss sym, %xmm0` had the prefix before
# the ModRM byte of a 32-bit address, which behind it is `(%di)`, and
# most emitters left the prefix out; `fldl sym(%eip)` was `fldl
# sym(%rip)`.  In 16-bit code `cmpxchg %eax, (%bx)`, `btl $1, sym`,
# `bsf (%si), %eax` and `shld` had no operand-size prefix and were of
# 16 bits, and `crc32w` had one and read 32.  `addr32` in 32-bit code,
# a jump to a label with a size letter, `es` before an instruction of
# 64-bit code, `stosb %bl, %es:(%edi)` and `movsxd %ax, %bx` were
# assembled, and GNU as refuses each.
#
# prefix_forms.txt holds what GNU as 2.47 makes of each statement: the
# bytes of .text, and the type, place and symbol of each relocation; or
# `refused`.  (`;` in a statement is a new line.)  GNU as passes this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

# extreg_forms.txt is the same of %r8 to %r15 where an instruction asks
# for one register by name.  %r9 passed for %rcx, %r8 for the
# accumulator and %r10w for %dx, their low three bits being the same:
# `shrdq $7, %r8, %r9` -- which a compiler writes -- was refused, its
# destination taken for the count, and `in %r10w, %eax`, `lods
# %ds:(%rsi), %r8` and `movabs sym, %r8` were assembled as something
# else.
#
# rep_forms.txt is the same of the repeat and lock prefixes, and of
# jumps to labels the source exports.  `rep bsf`, which a compiler
# writes for a count of trailing zeros, was `bsf` in 32-bit code: the
# emitter of it and a dozen others dropped the prefix and said nothing,
# and before the others of them it is refused now.  A jump to a global
# label of its own section goes where the label is, in the short form
# if it reaches -- it was the long form and a relocation -- and a call
# of one is still the linker's.  The last two are jumps across an
# alignment, which are short where GNU as's are.
cat "$here/prefix_forms.txt" "$here/extreg_forms.txt" "$here/rep_forms.txt" > forms.txt

fail=0
cases=0
while IFS='	' read -r mode want src; do
    cases=$((cases + 1))
    printf '%s\n' "$src" | tr ';' '\n' > t.s
    if "$AS" --"$mode" -o t.o t.s 2> /dev/null; then
        objcopy -O binary --only-section=.text t.o t.bin
        got=$(od -An -v -tx1 t.bin | tr -d ' \n')
        got=$got$(readelf -rW t.o | awk '/R_/{printf "/%s@%s:%s", $3, $1, $5 $6 $7}' | sed 's/@0*\([0-9a-f]\)/@\1/g')
    else
        got=refused
    fi
    rm -f t.o t.bin
    if [ "$got" != "$want" ]; then
        echo "FAIL --$mode [$src]: $got, and GNU as: $want"
        fail=1
    fi
done < forms.txt

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
