#!/bin/sh
# An x86-64 program's thread-local variable that is further from the thread
# pointer than a 32-bit field can say.
#
# Local-exec code (R_X86_64_TPOFF32) and initial-exec code the linker
# rewrites to local-exec (R_X86_64_GOTTPOFF) both end with the distance in
# a sign-extended 32-bit field.  The linker wrote the low 32 bits of
# whatever the distance was, so a thread-local block over 2 GiB linked
# without a word and the code reached the wrong place.  GNU ld refuses
# TPOFF32 with "relocation truncated to fit"; the linker must refuse both,
# and still link a block that just fits.
#
# The objects are assembled by the host's as, and GNU ld is the judge of
# the TPOFF32 case where it is installed.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1

# MODEL SIZE: a program reaching x, the first of SIZE bytes of .tbss, so
# that x is SIZE back from the thread pointer.
mkobj() {
    case $1 in
    le) insn='movq %fs:x@tpoff, %rax' ;;
    ie) insn='movq x@gottpoff(%rip), %rax' ;;
    esac
    printf '\t.text\n\t.globl _start\n_start:\t%s\n\thlt\n' "$insn" > "$1$2.s"
    printf '\t.section .tbss,"awT",@nobits\n\t.globl x\nx:\t.skip %s\n' "$2" >> "$1$2.s"
    as --64 -o "$1$2.o" "$1$2.s"
}

if ! mkobj le 0x7ffff000 2> err; then
    echo "skip: the host's as does not assemble --64"; exit 0
fi
mkobj ie 0x7ffff000
mkobj le 0x90000000
mkobj ie 0x90000000

for model in le ie; do
    # Just fits: the field is -0x7ffff000.
    if ! ./ld -m elf_x86_64 -o fits$model $model"0x7ffff000.o" 2> err; then
        echo "FAIL $model: a block of 0x7ffff000 bytes does not link: $(head -1 err)"; fail=1
    else
        got=$(objdump -d --insn-width=16 -j .text fits$model | grep -c '00 10 00 80')
        if [ "$got" -ge 1 ]; then
            echo "ok   $model: a block of 0x7ffff000 bytes puts x at -0x7ffff000"
        else
            echo "FAIL $model: x is not at -0x7ffff000 in the code"; fail=1
        fi
    fi
    # Does not: -0x90000000 is not a sign-extended 32-bit value.
    if ./ld -m elf_x86_64 -o far$model $model"0x90000000.o" 2> err; then
        echo "FAIL $model: a block of 0x90000000 bytes linked, the distance cut to 32 bits"; fail=1
    elif grep -q 'truncated to fit' err; then
        echo "ok   $model: a block of 0x90000000 bytes is refused: $(head -1 err)"
    else
        echo "FAIL $model: refused, but not for the distance: $(head -1 err)"; fail=1
    fi
done

# GNU ld, where there is one, agrees about TPOFF32.
gld=${GNU_LD:-ld}
if $gld -v 2> /dev/null | grep -q 'GNU ld'; then
    if $gld -m elf_x86_64 -o gfits le0x7ffff000.o 2> /dev/null; then
        echo "ok   GNU ld links the block that fits"
    else
        echo "FAIL GNU ld does not link the block that fits"; fail=1
    fi
    if $gld -m elf_x86_64 -o gfar le0x90000000.o 2> err; then
        echo "FAIL GNU ld links the block that does not fit"; fail=1
    elif grep -q 'truncated to fit' err; then
        echo "ok   GNU ld refuses the block that does not fit"
    else
        echo "FAIL GNU ld refuses it for another reason: $(head -1 err)"; fail=1
    fi
fi

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
