#!/bin/sh
# The local symbols a link leaves in its output.
#
# A program or shared object kept every input's section symbols: our cc,
# linked from gcc's objects, had 6228 of them.  GNU ld makes none in a
# program or shared object (only under --emit-relocs, whose relocations
# may name them), and keeps them under -r.
#
# GNU ld is the judge: for each command line, the section symbols of the
# output are there or not as in GNU ld's, and the output still runs.
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
# a local of every kind: a named one and a temporary (.L), in a section
# of strings (SHF_MERGE), in code and in data
cat > a.s <<'EOF'
	.section .rodata.str1.1,"aMS",@progbits,1
mstr:	.string "x"
.Lmstr:	.string "y"
	.text
	.globl _start
_start:	lea mstr(%rip), %rax
	lea .Lmstr(%rip), %rax
ltext:	mov $60, %eax
	xor %edi, %edi
.Ltext:	syscall
	.data
ldata:	.long 1
.Ldata:	.long 2
EOF
${AS_GNU:-as} --64 -L -o a.o a.s || exit 1
# relocations against sections, as gcc's objects are full of: the
# assembler makes a reference to a local of a section one to the section
cat > s.s <<'EOF'
	.text
	.globl sfunc
sfunc:	lea sdata(%rip), %rax
	ret
	.data
sdata:	.long 1
	.section .rodata
	.quad .text + 4
EOF
${AS_GNU:-as} --64 -o s.o s.s || exit 1
if [ "$(readelf -sW s.o | awk '$4 == "SECTION"' | wc -l)" -eq 0 ]; then
    echo "FAIL: the assembler made no section symbols for s.o"; exit 1
fi

# How many section symbols OBJ has.
sections() {
    readelf -sW "$1" | awk '$4 == "SECTION"' | wc -l
}

# check ARGS...: GNU ld and this one agree on whether there are any
check() {
    "${GNU_LD:-ld}" "$@" -o gnu.out a.o s.o 2>/dev/null || { echo "skip: GNU ld $*"; return; }
    if ! ./ld -m elf_x86_64 "$@" -o ours.out a.o s.o > said 2>&1; then
        echo "FAIL: ld $*: $(head -1 said)"; fail=1; return
    fi
    g=$(sections gnu.out); o=$(sections ours.out)
    if [ "$g" -eq 0 ] && [ "$o" -ne 0 ]; then
        echo "FAIL: ld $*: $o section symbols, GNU ld's none"; fail=1
    elif [ "$g" -ne 0 ] && [ "$o" -eq 0 ]; then
        echo "FAIL: ld $*: no section symbols, GNU ld's $g"; fail=1
    fi
}

check
check -pie
check -shared

# Under --emit-relocs a relocation against a section keeps the symbol it
# names: none of the output's relocations is left naming nothing.
if ! ./ld -m elf_x86_64 --emit-relocs -e sfunc -o emit.out s.o > said 2>&1; then
    echo "FAIL: ld --emit-relocs: $(head -1 said)"; fail=1
else
    n=$(readelf -rW emit.out | grep -c 'R_X86_64_')
    named=$(readelf -rW emit.out | awk '/R_X86_64_/ && NF >= 6' | wc -l)
    if [ "$n" -ne 2 ] || [ "$named" -ne 2 ]; then
        echo "FAIL: ld --emit-relocs: $n relocations, $named of them naming a symbol, not 2 and 2"
        readelf -rW emit.out | sed 's/^/    /'
        fail=1
    fi
fi

./ld -m elf_x86_64 -o prog a.o s.o 2>/dev/null && ./prog
if [ $? -ne 0 ]; then
    echo "FAIL: the program does not run"; fail=1
fi

if [ $fail -eq 0 ]; then
    echo "ok   test_local_symbols"
fi
exit $fail
