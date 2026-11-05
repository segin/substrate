#!/bin/sh
# Input sections ordered by others (SHF_LINK_ORDER), gcc's
# __patchable_function_entries, as GNU ld puts them out: left to their own
# name -- with no script, or orphans of one -- they make one output section
# for each section they are ordered by, its sh_link that one; a script
# statement that places them, under their name or another, gathers them
# into one, its sh_link the first one's.
#
# A statement keeping the name kept them apart, three sections of one name
# in each Linux module (module.lds has `__patchable_function_entries : {
# *(__patchable_function_entries) }`), and the kernel refused to load the
# module ("cannot create duplicate filename .../sections/
# __patchable_function_entries").  One renaming them gathered them with an
# sh_link of 0.
#
# GNU ld is the judge: at x86-64 and i386, linked -r with no script, a
# statement of the same name and one of another, each output section's
# name, size, flags and sh_link section, and its relocations, are GNU ld's.
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
# two objects, each with functions in three sections and an entry for each
for o in a b; do
    cat > $o.s <<EOF
	.text
${o}1:	nop
	ret
	.section .init.text,"ax",@progbits
${o}2:	nop
	ret
	.section .exit.text,"ax",@progbits
${o}3:	nop
	ret
	.section __patchable_function_entries,"awo",@progbits,${o}1
	.quad ${o}1
	.section __patchable_function_entries,"awo",@progbits,${o}2
	.quad ${o}2
	.section __patchable_function_entries,"awo",@progbits,${o}3
	.quad ${o}3
EOF
done
printf 'SECTIONS {\n  __patchable_function_entries 0 : { *(__patchable_function_entries) }\n}\n' > same.lds
printf 'SECTIONS {\n  __pfe 0 : { *(__patchable_function_entries) }\n}\n' > rename.lds

# each output section ordered by another: name, size, flags, the section it is ordered by; then its relocations
ordered() {
    readelf -SW "$1" | sed -n 's/^ *\[ *\([0-9]*\)\] /\1 /p' | awk '
        { idx = $1; name[idx] = $2; line[idx] = $0 }
        END {
            for (i in line) {
                n = split(line[i], f, " ")
                # NR Name Type Addr Off Size ES Flg Lk Inf Al
                if (f[n - 3] ~ /L/) print f[2], f[6], f[n - 3], name[f[n - 2]]
            }
        }' | sort
    readelf -rW "$1" | awk '/^Relocation section/ { s = $3 } /^[0-9a-f]+ / && s ~ /pfe|patchable/ { print s, $1, $3, $5, $6, $7 }'
}

for emu in elf_x86_64 elf_i386; do
    case $emu in
    elf_x86_64) asflag=--64 ;;
    *) asflag=--32; sed -i 's/\.quad/.long/' a.s b.s ;;
    esac
    ${GNU_AS:-as} $asflag -o a.o a.s && ${GNU_AS:-as} $asflag -o b.o b.s || { echo "FAIL: GNU as"; exit 1; }
    for s in none same rename; do
        opt=""
        [ $s != none ] && opt="-T $s.lds"
        ${GNU_LD:-ld} -m $emu -r $opt -o gnu.o a.o b.o 2>gnu.err || { echo "FAIL: GNU ld: $emu $s"; cat gnu.err; exit 1; }
        if ! ./ld -m $emu -r $opt -o our.o a.o b.o 2>our.err; then
            echo "FAIL -m $emu ($s): not linked:"; cat our.err; fail=1; continue
        fi
        ordered gnu.o > gnu.txt
        ordered our.o > our.txt
        if ! cmp -s gnu.txt our.txt; then
            echo "FAIL -m $emu ($s): sections ordered by others differ from GNU ld's (GNU first):"
            diff gnu.txt our.txt
            fail=1
        fi
    done
done

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
