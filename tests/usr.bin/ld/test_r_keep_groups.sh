#!/bin/sh
# ld -r keeps the inputs' section groups, as GNU ld does without
# --force-group-allocation: each group is one of the output's, under its
# signature, its members sections of their own still marked SHF_GROUP --
# which no section of another group or of none joins -- with their
# relocation sections in the group too; a COMDAT group seen before is left
# out with its members, and groups that are not COMDAT are each kept.  The
# signature is the symbol of its name.
#
# ld left the groups out and their members marked SHF_GROUP with no group
# to be in; members of one name went into one section with the same name's
# non-members; and the writer named each group by a new undefined symbol
# even where the object defined one of the signature's name.
#
# GNU ld is the judge: at x86-64 and i386, the groups (kind, signature,
# members), each section's name, type, size and flags, and each signature's
# symbol, are GNU ld's.
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
# both: a COMDAT group of code (calling out), data and a table ordered by the code; a group that is not
# COMDAT; a: a section of a member's name in no group
for o in a b; do
    cat > $o.s <<EOF
	.text
	.globl $o
$o:	call inl
	ret
	.section .text.inl,"axG",@progbits,inl,comdat
	.globl inl
	.weak inl
	.type inl,@function
inl:	call ext
	ret
	.section .data.inl,"awG",@progbits,inl,comdat
	.globl inl_data
	.weak inl_data
inl_data:
	.long 7
	.section __patchable_function_entries,"awoG",@progbits,inl,inl,comdat
	.dc.a inl
	.section .text.ng,"axG",@progbits,ng
	.globl ng_$o
ng_$o:	ret
EOF
done
cat >> a.s <<'EOF'
	.section .text.inl,"ax",@progbits
plain:	ret
EOF

# groups: kind, signature, member names; sections: count, name, type, size, flags; signatures' symbols
summary() {
    # (a member to a line: the order of a group's members is not GNU ld's, nor need be)
    readelf -gW "$1" | awk '
        /group section/ { g = ($1 == "COMDAT" ? "COMDAT " : "group ") $(NF - 3); n++; next }
        /^ *\[ *[0-9]+\]/ { print g, $NF }
        END { print n, "groups" }' | sort
    readelf -SW "$1" | sed -n 's/^ *\[ *[0-9]*\] //p' | awk '$2 != "NULL" && $1 !~ /^\.(symtab|strtab|shstrtab)$/ {
        n = split($0, f, " "); print f[1], f[2], f[5], (f[n - 3] ~ /^[A-Z]+$/ ? f[n - 3] : "-") }' | sort | uniq -c
    for sym in inl ng; do
        readelf -sW "$1" | awk -v s=$sym '$8 == s { print s, $4, $5, ($7 == "UND" ? "UND" : "defined") }' | sort
    done
}

for emu in elf_x86_64 elf_i386; do
    case $emu in
    elf_x86_64) asflag=--64 ;;
    *) asflag=--32 ;;
    esac
    ${GNU_AS:-as} $asflag -o a.o a.s && ${GNU_AS:-as} $asflag -o b.o b.s || { echo "FAIL: GNU as"; exit 1; }
    ${GNU_LD:-ld} -m $emu -r -o gnu.o a.o b.o 2>gnu.err || { echo "FAIL: GNU ld: $emu"; cat gnu.err; exit 1; }
    if ! ./ld -m $emu -r -o our.o a.o b.o 2>our.err; then
        echo "FAIL -m $emu: not linked:"; cat our.err; fail=1; continue
    fi
    summary gnu.o > gnu.txt
    summary our.o > our.txt
    if ! cmp -s gnu.txt our.txt; then
        echo "FAIL -m $emu: groups, sections or signatures differ from GNU ld's (GNU first):"
        diff gnu.txt our.txt
        fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
