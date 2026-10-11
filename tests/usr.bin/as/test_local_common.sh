#!/bin/sh
# A common symbol that is local is storage in .bss (AS-T-275).
#
# `.lcomm name, size`, and `.comm name, size[, alignment]` of a name a
# `.local` above it names -- which is how a C compiler writes
#
#     static int counter;
#
# -- are a local object in .bss with its size and alignment.  This
# assembler made the first a local symbol of the section COMMON, which
# no linker gives a place, and the second a global common symbol: two
# files with a `static` of one name had one variable between them.
#
# The places, sizes and bindings here are GNU as's, and GNU as passes
# this; so are the refusals.  The last case links two such objects and
# runs the program.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# symbols: "name value size type bind section", by name, of the names given
symbols() {
    readelf -sSW t.o | awk -v want=" $1 " '
        /^ *\[ *[0-9]+\]/ { sub(/^ *\[ */, ""); sub(/\]/, ""); sec[$1] = $2; next }
        $4 == "OBJECT" || $4 == "FUNC" || $4 == "NOTYPE" {
            if (index(want, " " $8 " ")) {
                s = ($7 in sec) ? sec[$7] : $7
                printf "%s %d %d %s %s %s\n", $8, strtonum("0x" $2), $3, $4, $5, s
            }
        }' | sort
}

# placed WHAT NAMES SOURCE EXPECTED, in both modes
placed() {
    for m in 32 64; do
        cases=$((cases + 1))
        printf "$3" > t.s
        rm -f t.o
        if ! "$AS" --$m -o t.o t.s > out 2>&1; then
            echo "FAIL $1 (--$m): $(tail -1 out)"; fail=1
            continue
        fi
        got=$(symbols "$2" | tr '\n' ';')
        if [ "$got" != "$4" ]; then
            echo "FAIL $1 (--$m): $got"; echo "  wanted: $4"; fail=1
        fi
    done
}

# refused WHAT SOURCE
refused() {
    cases=$((cases + 1))
    printf "$2" > t.s
    if "$AS" --64 -o t.o t.s > out 2>&1; then
        echo "FAIL $1: assembled"; fail=1
    fi
}

placed "a static without an initialiser" "x" \
    '\t.local x\n\t.comm x,16,8\n' \
    'x 0 16 OBJECT LOCAL .bss;'
placed ".lcomm" "y" \
    '\t.lcomm y,5\n' \
    'y 0 5 OBJECT LOCAL .bss;'
placed "each behind the last, at its alignment" "x y w q" \
    '\t.local x\n\t.comm x,16,8\n\t.lcomm y,5\n\t.lcomm w,32\n\t.local q\n\t.comm q,3\n' \
    'q 56 3 OBJECT LOCAL .bss;w 24 32 OBJECT LOCAL .bss;x 0 16 OBJECT LOCAL .bss;y 16 5 OBJECT LOCAL .bss;'
placed "behind what the source puts in .bss, wherever that is written" "x v u" \
    '\t.local x\n\t.comm x,16,8\n\t.bss\n\t.byte 0\n\t.lcomm v,1\n\t.lcomm u,9\n\t.byte 0\n' \
    'u 32 9 OBJECT LOCAL .bss;v 24 1 OBJECT LOCAL .bss;x 8 16 OBJECT LOCAL .bss;'
placed "the alignment a .lcomm has of its size" "l1 l2 l3 l4 l7 l8 l9" \
    '\t.lcomm l1,1\n\t.lcomm l2,2\n\t.lcomm l3,3\n\t.lcomm l4,4\n\t.lcomm l7,7\n\t.lcomm l8,8\n\t.lcomm l9,9\n' \
    'l1 0 1 OBJECT LOCAL .bss;l2 2 2 OBJECT LOCAL .bss;l3 4 3 OBJECT LOCAL .bss;l4 8 4 OBJECT LOCAL .bss;l7 12 7 OBJECT LOCAL .bss;l8 24 8 OBJECT LOCAL .bss;l9 32 9 OBJECT LOCAL .bss;'
placed "a local .comm with no alignment has none" "a b c" \
    '\t.local a\n\t.local b\n\t.local c\n\t.comm a,1\n\t.comm b,8\n\t.comm c,4\n' \
    'a 0 1 OBJECT LOCAL .bss;b 1 8 OBJECT LOCAL .bss;c 9 4 OBJECT LOCAL .bss;'
placed "several names of one .local" "a b" \
    '\t.local a, b\n\t.comm a,4,4\n\t.comm b,4,4\n' \
    'a 0 4 OBJECT LOCAL .bss;b 4 4 OBJECT LOCAL .bss;'
placed "a size that is a symbol set above" "x y" \
    '\t.set n, 12\n\t.lcomm x,n\n\t.local y\n\t.comm y,n*2,4\n' \
    'x 0 12 OBJECT LOCAL .bss;y 12 24 OBJECT LOCAL .bss;'
placed "a size of nothing" "x y" \
    '\t.lcomm x,0\n\t.lcomm y,4\n' \
    'x 0 0 OBJECT LOCAL .bss;y 0 4 OBJECT LOCAL .bss;'
placed "made global after" "x" \
    '\t.lcomm x,4\n\t.globl x\n' \
    'x 0 4 OBJECT GLOBAL .bss;'
placed "made weak after" "x" \
    '\t.lcomm x,4\n\t.weak x\n' \
    'x 0 4 OBJECT WEAK .bss;'
placed "a size said after" "x" \
    '\t.lcomm x,4\n\t.size x,9\n' \
    'x 0 9 OBJECT LOCAL .bss;'
placed "the section the source was in is the one it stays in" "x f g" \
    '\t.text\nf:\tnop\n\t.lcomm x,4\ng:\tnop\n' \
    'f 0 0 NOTYPE LOCAL .text;g 1 0 NOTYPE LOCAL .text;x 0 4 OBJECT LOCAL .bss;'
placed "a label on the directive's line" "x f" \
    '\t.text\n\tnop\nf:\t.lcomm x,4\n\tnop\n' \
    'f 1 0 NOTYPE LOCAL .text;x 0 4 OBJECT LOCAL .bss;'

# A .local that comes after makes nothing local: the symbol is common.
for m in 32 64; do
    cases=$((cases + 1))
    printf '\t.comm x,4\n\t.local x\n' > t.s
    "$AS" --$m -o t.o t.s > out 2>&1
    readelf -sW t.o | grep -q 'GLOBAL.* COM x$' || { echo "FAIL .local after .comm (--$m)"; fail=1; }
done

# The section is as wide as its widest object's alignment.
cases=$((cases + 1))
printf '\t.local x\n\t.comm x,16,32\n' > t.s
"$AS" --64 -o t.o t.s > out 2>&1
readelf -SW t.o | awk '$2 == ".bss" || $3 == ".bss" { print $NF }' | grep -q '^32$' ||
    { echo "FAIL the alignment of .bss"; fail=1; }

# An instruction that names one reaches it.
for m in 32 64; do
    cases=$((cases + 1))
    printf '\t.text\n\tmovl x, %%eax\n\tmovl y, %%eax\n\t.lcomm x,8\n\t.lcomm y,8\n' > t.s
    "$AS" --$m -o t.o t.s > out 2>&1
    ld -m elf_$([ $m = 32 ] && echo i386 || echo x86_64) -Ttext=0x1000 -Tbss=0x2000 -e 0 -o t t.o 2> out
    got=$(objdump -d t | awk '/mov/ { print $NF }' | sed 's/.*mov *//' | tr '\n' ' ')
    [ "$got" = "0x2000,%eax 0x2008,%eax " ] || { echo "FAIL reached (--$m): $got $(cat out)"; fail=1; }
done

refused "a .lcomm that is there twice" '\t.lcomm x,4\n\t.lcomm x,4\n'
refused "a label of the name" 'x:\t.long 0\n\t.lcomm x,4\n'
refused "a label of the name, after" '\t.lcomm x,4\n\t.data\nx:\t.long 0\n'
refused "no size" '\t.lcomm x\n'
refused "a size that is no number" '\t.lcomm x,undefined_thing\n'
refused "a third argument to .lcomm" '\t.lcomm x,4,4\n'
refused "an alignment that is no power of two" '\t.local x\n\t.comm x,4,3\n'
refused "an alignment that is not there" '\t.local x\n\t.comm x,4,\n'

# A size below nothing is said and the directive is passed over.
cases=$((cases + 1))
printf '\tnop\n\t.lcomm x,-1\n' > t.s
rm -f t.o
"$AS" --64 -o t.o t.s > out 2>&1
[ -f t.o ] && grep -q '^t\.s:2: Warning: .*-1' out || { echo "FAIL a size of -1: $(tr '\n' '|' < out)"; fail=1; }

# Two files that each have a `static` of one name have one each.
if command -v cc > /dev/null 2>&1; then
    cases=$((cases + 1))
    cat > a.s <<'EOF'
	.text
	.globl	main
main:
	movl	$7, counter(%rip)
	call	other
	movl	counter(%rip), %eax
	subl	$7, %eax
	ret
	.local	counter
	.comm	counter,4,4
	.section	.note.GNU-stack,"",@progbits
EOF
    cat > b.s <<'EOF'
	.text
	.globl	other
other:
	movl	$9, counter(%rip)
	ret
	.local	counter
	.comm	counter,4,4
	.section	.note.GNU-stack,"",@progbits
EOF
    if "$AS" --64 -o a.o a.s > out 2>&1 && "$AS" --64 -o b.o b.s >> out 2>&1 &&
       cc -no-pie -o prog a.o b.o >> out 2>&1; then
        ./prog || { echo "FAIL two statics of one name are one variable"; fail=1; }
    else
        echo "FAIL the program was not built: $(tail -1 out)"; fail=1
    fi
fi

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
