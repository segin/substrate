#!/bin/sh
# Intel syntax: every instruction with operands of every kind.
#
# An instruction written the Intel way was handed to each emitter with
# a flag, to be read backwards.  Those that did not read the flag took
# the first operand for the source and sized the instruction by
# nothing: `shl rax, 4`, `cmove r15, rax`, `imul rcx, rdx`, `bt cx, dx`,
# `cmpxchg [rax], rdx`, `movzx cx, dl` and `lea cx, [rax]` were
# instructions of 32 bits, `shl BYTE PTR [rax], 1` shifted four bytes,
# `mov QWORD PTR [rax], 1` stored four, `fld QWORD PTR [rax]` loaded a
# float, and nothing was said of any.  Of 25 common mnemonics with
# operands of every kind, a quarter of what GNU as assembles had other
# bytes here, and as many more were refused; four times as many
# statements were assembled that GNU as refuses.
#
# And what a compiler writes with -masm=intel was not assembled at all:
# a name in quotes with a relocation operator on it (`call
# "free"@PLT`), `OFFSET FLAT:sym`, an address with its symbol before
# the brackets (`.LC0[rip]`, `"x"@GOTOFF[ebx+8]`, which lost the symbol
# and said nothing), a jump through brackets of its own (`jmp [QWORD
# PTR .L4[0+rax*8]]`), and a quoted name that spells a register
# (`"sp"[rip]`).  `fsubp st(1), st` was AT&T's `fsubp`, which is the
# other subtraction.
#
# An Intel statement is made the AT&T statement it is where it is read
# (as_parser.c), and there is one reading of an instruction.
#
# intel_forms.txt holds what GNU as 2.47 makes of each statement: its
# bytes, or `refused`.  intel_reloc_forms.txt is the same with the
# type, place and symbol of each relocation, for the forms of a
# compiler.  (`;` in a statement is a new line.)  GNU as passes this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# The forms with relocations, one statement to an object.
while IFS='	' read -r mode want src; do
    cases=$((cases + 1))
    { echo '.intel_syntax noprefix'; printf '%s\n' "$src" | tr ';' '\n'; } > t.s
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
done < "$here/intel_reloc_forms.txt"

# The rest, all that GNU as takes in one object, by GNU's lengths.
for mode in 64 32; do
    echo '.intel_syntax noprefix' > ok.s
    echo '.intel_syntax noprefix' > head.s
    : > ok.hex
    : > bad.s
    awk -F'\t' -v m="$mode" '$1 == m && $2 != "refused" { print $3 >> "ok.s"; print $2 >> "ok.hex" }
                             $1 == m && $2 == "refused" { print $3 >> "bad.s" }' "$here/intel_forms.txt"
    n=$(wc -l < ok.hex)
    cases=$((cases + n))
    if ! "$AS" --$mode -o ok.o ok.s 2> err; then
        echo "FAIL --$mode: refused, of $n forms GNU as takes:"
        sed 's/^[^:]*:\([0-9]*\): *\(Error: \)\{0,1\}/\1 /' err | head -20 | while read -r ln why; do
            echo "  [$(sed -n "${ln}p" ok.s)] $why"
        done
        fail=1
    else
        objcopy -O binary --only-section=.text ok.o ok.bin
        od -An -v -tx1 ok.bin | tr -d ' \n' > got.hex
        if ! tr -d '\n' < ok.hex | cmp -s - got.hex; then
            # The first line whose bytes are not GNU's, by GNU's lengths.
            awk 'NR == FNR { got = $0; next }
                 { n = length($0); if (substr(got, at + 1, n) != $0) { print FNR "\t" substr(got, at + 1, n) "\t" $0; exit } at += n }' \
                got.hex ok.hex > first
            IFS='	' read -r ln got want < first
            echo "FAIL --$mode [$(sed -n "$((ln + 1))p" ok.s)]: $got, and GNU as: $want"
            fail=1
        fi
    fi
    # What GNU refuses, ninety lines at a time: each is refused by its
    # own line, which is one further down for the line of the syntax.
    n=$(wc -l < bad.s)
    cases=$((cases + n))
    split -l 90 bad.s badpart.
    for part in badpart.*; do
        [ -f "$part" ] || continue
        cat head.s "$part" > bad1.s
        "$AS" --$mode -o bad.o bad1.s 2> err
        sed -n 's/^[^:]*:\([0-9]*\):.*/\1/p' err | sort -un > refused
        awk 'NR == FNR { r[$1 - 1] = 1; next } !(FNR in r) { print }' refused "$part" > taken
        if [ -s taken ]; then
            echo "FAIL --$mode: assembled, and GNU as refuses:"
            head -20 taken | sed 's/^/  /'
            fail=1
        fi
    done
    rm -f ok.s ok.hex bad.s badpart.* ok.o bad.o bad1.s
done

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
