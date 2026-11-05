#!/bin/sh
# ld -r with a linker script puts each output section's input sections in
# the order of the script's input descriptions, and those a SORT or one of
# its relatives takes in the order it says, as GNU ld does: the order is
# not a final link's alone.
#
# ld -r merged them input by input, in the order they came, whatever the
# script said.  Linux links each module with -r and scripts/module.lds,
# whose __ksymtab is *(SORT(___ksymtab+*)): the kernel searches that table
# by halving, so in modpost's order a module's exports were not found and
# every module using one failed to load ("Unknown symbol pps_event").
#
# GNU ld is the judge: at x86-64 and i386, the symbols of each output
# section by offset, and its relocations (offset, type, symbol; in no
# order: GNU ld's go by offset, this ld's input by input).
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${GNU_LD:-ld} --version 2>/dev/null | grep -q 'GNU ld'; then
    echo "skip: GNU ld is the judge"
    exit 0
fi
if ! ${GNU_AS:-as} --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as makes the objects"
    exit 0
fi
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
# a table of entries in no order, each pointing at its function; sections
# the script names in the other order than they come
for o in a b; do
    cat > $o.s <<EOF
	.text
	.globl f_$o
f_$o:	ret
	.section ___t+${o}_pear,"a"
t_${o}_pear:	.long f_$o
	.section ___t+${o}_apple,"a"
t_${o}_apple:	.long f_$o
	.section ___t+${o}_zebra,"a"
t_${o}_zebra:	.long f_$o
	.section ___t+${o}_kiwi,"a"
t_${o}_kiwi:	.long f_$o
	.section .two,"a"
two_$o:	.long f_$o
	.section .one,"a"
one_$o:	.long f_$o
EOF
done
cat > script.lds <<'EOF'
SECTIONS {
    .tab 0 : ALIGN(4) { *(SORT(___t+*)) }
    .both 0 : { *(.one) *(.two) }
}
EOF

# for each output section, its symbols by offset and its relocations
summary() {
    for s in .tab .both; do
        n=$(readelf -SW "$1" | sed -n "s/^ *\[ *\([0-9]*\)\] $s .*/\1/p")
        echo "$s"
        readelf -sW "$1" | awk -v n="$n" '$7 == n && $4 != "SECTION" { print $2, $8 }' | sort
        readelf -rW "$1" | awk -v s="$s" '
            /^Relocation section/ { on = ($3 == "'"'"'.rela" s "'"'"'" || $3 == "'"'"'.rel" s "'"'"'"); next }
            on && /^[0-9a-f]+ / { print $1, $3, $5 }' | sort
    done
}

for emu in elf_x86_64 elf_i386; do
    case $emu in
    elf_x86_64) asflag=--64 ;;
    *) asflag=--32 ;;
    esac
    ${GNU_AS:-as} $asflag -o a.o a.s && ${GNU_AS:-as} $asflag -o b.o b.s || { echo "FAIL: GNU as"; exit 1; }
    ${GNU_LD:-ld} -m $emu -r -T script.lds -o gnu.o a.o b.o 2>gnu.err || {
        echo "FAIL: GNU ld: $emu"; cat gnu.err; exit 1; }
    if ! ./ld -m $emu -r -T script.lds -o our.o a.o b.o 2>our.err; then
        echo "FAIL -m $emu: not linked:"; cat our.err; fail=1; continue
    fi
    summary gnu.o > gnu.txt
    summary our.o > our.txt
    if ! grep -q t_a_apple gnu.txt; then
        echo "FAIL -m $emu: the summary found no symbols"; cat gnu.txt; fail=1; continue
    fi
    if ! cmp -s gnu.txt our.txt; then
        echo "FAIL -m $emu: sections' order differs from GNU ld's (GNU first):"
        diff gnu.txt our.txt
        fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
