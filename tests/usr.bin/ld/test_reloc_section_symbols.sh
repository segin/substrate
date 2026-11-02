#!/bin/sh
# A relocation against an input section, under -r and --emit-relocs.
#
# The link plan wrote such a relocation against a local of its own,
# __elfobj_local_N_N_N, at the input section's place in the output
# section.  GNU ld writes it against the output section's symbol, the
# input section's place added to the addend (for REL, i386's, in the
# relocation's place in the section).
#
# GNU ld is the judge: ld -r of objects with relocations against their
# sections, x86-64 (RELA) and i386 (REL), names the symbols GNU ld's
# does, with its addends; linked into a program by GNU ld, its code and
# data are GNU ld's -r output's, byte for byte; under --emit-relocs the
# relocations name section symbols; and no symbol of the linker's own
# making is left.
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
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
# the assembler makes a reference to a local of a section one to the section
cat > a.s <<'EOF'
	.text
	.globl _start
_start:	mov adata, %eax
	mov adata + 4, %eax
	call alocal
	ret
alocal:	ret
	.data
	.long 0
adata:	.long 1, 2
	.long alocal
EOF
cat > b.s <<'EOF'
	.text
	.globl bfunc
bfunc:	mov bdata, %eax
	ret
	.data
	.long 0, 0, 0
bdata:	.long 3
	.long bfunc + 1
EOF

# Every relocation of OBJ: the section it is in, its place, type, the
# symbol it names (a section's, by the section's name) and the addend.
relocs() {
    readelf -rW "$1" | awk '
        /^Relocation section/ { sec = $3; next }
        /R_(X86_64|386)_/ { print sec, $1, $3, $5, $6 $7 }'
}
# The bytes of section SEC of OBJ.
bytes() {
    rm -f sec.bin
    objcopy --dump-section "$2"=sec.bin "$1" scratch.o 2>/dev/null && od -An -v -tx1 sec.bin | tr -d ' \n'
}

for b in 64 32; do
    if [ $b = 64 ]; then em=elf_x86_64; else em=elf_i386; fi
    ${AS_GNU:-as} --$b -o a.o a.s || exit 1
    ${AS_GNU:-as} --$b -o b.o b.s || exit 1

    "${GNU_LD:-ld}" -m $em -r -o gnu.o a.o b.o || exit 1
    if ! ./ld -m $em -r -o ours.o a.o b.o > said 2>&1; then
        echo "FAIL: ld -m $em -r: $(head -1 said)"; fail=1; continue
    fi
    relocs gnu.o > want
    relocs ours.o > have
    if ! cmp -s want have; then
        echo "FAIL: ld -m $em -r: the relocations differ from GNU ld's:"
        diff want have | sed 's/^/    /'
        fail=1
    fi
    for sec in .text .data; do
        if [ "$(bytes gnu.o $sec)" != "$(bytes ours.o $sec)" ]; then
            echo "FAIL: ld -m $em -r: $sec differs from GNU ld's (the addends in it, for REL)"; fail=1
        fi
    done
    # what the -r output means: a program of it, by GNU ld
    "${GNU_LD:-ld}" -m $em -o gnu.out gnu.o || exit 1
    "${GNU_LD:-ld}" -m $em -o ours.out ours.o || { echo "FAIL: GNU ld cannot link ld -r's output"; fail=1; continue; }
    for sec in .text .data; do
        if [ "$(bytes gnu.out $sec)" != "$(bytes ours.out $sec)" ]; then
            echo "FAIL: ld -m $em -r: linked, $sec is not GNU ld's"; fail=1
        fi
    done

    # --emit-relocs: the relocations against sections name section symbols
    if ! ./ld -m $em --emit-relocs -o emit.out a.o b.o > said 2>&1; then
        echo "FAIL: ld -m $em --emit-relocs: $(head -1 said)"; fail=1; continue
    fi
    if [ "$(relocs emit.out | awk '$4 == ".data"' | wc -l)" -eq 0 ]; then
        echo "FAIL: ld -m $em --emit-relocs: no relocation names .data's section symbol"; fail=1
    fi
    for o in ours.o emit.out; do
        if n=$(readelf -sW $o | grep -c ' __elfobj_'); [ "$n" -ne 0 ]; then
            echo "FAIL: ld -m $em: $n symbols of the linker's making in $o"; fail=1
        fi
    done
done

if [ $fail -eq 0 ]; then
    echo "ok   test_reloc_section_symbols"
fi
exit $fail
