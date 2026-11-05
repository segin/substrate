#!/bin/sh
# --force-group-allocation, and a script's FORCE_GROUP_ALLOCATION: a -r
# link places the members of section groups as a final link does, as GNU
# ld does -- the groups gone, one of each signature's members kept, and no
# longer marked SHF_GROUP.
#
# The option was accepted and ignored ("unsupported option ignored"), on
# every link of a Linux module, and the script command was read and
# ignored: the members kept SHF_GROUP with no group to be in.
#
# GNU ld is the judge: at x86-64 and i386, linked -r with the option and
# with the command, each output section's name, type, size and flags, and
# the groups, are GNU ld's.
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
# two objects, each with the same COMDAT group of code and data
for o in a b; do
    cat > $o.s <<EOF
	.text
	.globl $o
$o:	ret
	.section .text.inl,"axG",@progbits,inl,comdat
	.globl inl
	.weak inl
inl:	nop
	ret
	.section .data.inl,"awG",@progbits,inl,comdat
	.long 7
EOF
done
printf 'FORCE_GROUP_ALLOCATION\n' > force.lds

# each section but the symbol and string tables: name, type, size, flags; then the groups
shdrs() {
    readelf -SW "$1" | sed -n 's/^ *\[ *[0-9]*\] //p' | awk '$1 !~ /^\.(symtab|strtab|shstrtab)$/ && $2 != "NULL" {
        n = split($0, f, " "); print f[1], f[2], f[5], (f[n - 3] ~ /^[A-Z]+$/ ? f[n - 3] : "-") }' | sort
    readelf -gW "$1" | sed -n 's/^COMDAT group section \[ *[0-9]*\] \(.*\)/\1/p; s/^ *\[ *[0-9]*\] *\(.*\)/  \1/p'
}

for emu in elf_x86_64 elf_i386; do
    case $emu in
    elf_x86_64) asflag=--64 ;;
    *) asflag=--32 ;;
    esac
    ${GNU_AS:-as} $asflag -o a.o a.s && ${GNU_AS:-as} $asflag -o b.o b.s || { echo "FAIL: GNU as"; exit 1; }
    for how in option script; do
        opt=--force-group-allocation
        [ $how = script ] && opt="-T force.lds"
        ${GNU_LD:-ld} -m $emu -r $opt -o gnu.o a.o b.o 2>gnu.err || { echo "FAIL: GNU ld: $emu $how"; cat gnu.err; exit 1; }
        if ! ./ld -m $emu -r $opt -o our.o a.o b.o 2>our.err; then
            echo "FAIL -m $emu ($how): not linked:"; cat our.err; fail=1; continue
        fi
        if [ -s our.err ]; then
            echo "FAIL -m $emu ($how): said:"; cat our.err; fail=1
        fi
        shdrs gnu.o > gnu.txt
        shdrs our.o > our.txt
        if ! cmp -s gnu.txt our.txt; then
            echo "FAIL -m $emu ($how): sections differ from GNU ld's (GNU first):"
            diff gnu.txt our.txt
            fail=1
        fi
    done
done

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
